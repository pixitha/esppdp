/* Bounded ESP adaptation of SIMH 8bbec614 exdep_reg_loop and
   exdep_addr_loop. The iteration, get_rval/put_rval, get_aval, fprint_sym,
   and parse_sym behavior follow upstream; host-file output, search,
   interactive modes, and raw-media deposits are not supplied here. */

#include "scp_exdep_adapter.h"
#include <limits.h>
#include <stdint.h>

extern const t_value width_mask[];
extern t_addr sim_last_addr;
extern t_value sim_last_val;
extern t_value *sim_eval;
extern int32 sim_switches;
extern int32 sim_emax;

t_stat scp_exdep_range(DEVICE *dev, CONST char *text, uint32 radix,
                       t_addr max, char terminator, t_addr *low, t_addr *high)
{
    t_addr previous = sim_last_addr;
    CONST char *end;
    if (!text || !low || !high) return SCPE_IERR;
    end = get_range(dev, text, low, high, radix, max, terminator);
    if (!end || *end || *low > *high || *high > max) {
        sim_last_addr = previous;
        return SCPE_ARG;
    }
    return SCPE_OK;
}

static t_stat register_preflight(DEVICE *dev, const REG *reg, uint32 index,
                                 t_bool deposit, t_value value)
{
    size_t access;
    if (!reg || !reg->loc || !reg->depth || !reg->width ||
        reg->width > 32 || reg->offset >= 32 ||
        reg->width > 32 - reg->offset ||
        (reg->flags & REG_STRUCT))
        return SCPE_NOFNC;
    if (index >= reg->depth) return SCPE_SUB;
    if ((reg->flags & REG_CIRC) && reg->qptr >= reg->depth)
        return SCPE_NOFNC;
    if (reg->flags & REG_UNIT) {
        uintptr_t base, loc;
        if (!dev || !dev->units || reg->depth > dev->numunits ||
            (reg->obj_size != sizeof(uint8) &&
             reg->obj_size != sizeof(uint16) &&
             reg->obj_size != sizeof(uint32)) ||
            reg->width + reg->offset > reg->obj_size * CHAR_BIT)
            return SCPE_NOFNC;
        base = (uintptr_t)dev->units;
        loc = (uintptr_t)reg->loc;
        if (loc < base || loc - base > sizeof(UNIT) - reg->obj_size)
            return SCPE_NOFNC;
        if (deposit && (dev->units[index].flags & UNIT_RO)) return SCPE_RO;
    } else {
        access = (reg->depth > 1 || (reg->flags & REG_FIT)) &&
                 reg->width + reg->offset <= 8 ? 1 :
                 (reg->depth > 1 || (reg->flags & REG_FIT)) &&
                 reg->width + reg->offset <= 16 ? 2 : 4;
        if (reg->obj_size / access < reg->depth) return SCPE_NOFNC;
    }
    if (deposit && (reg->flags & REG_RO)) return SCPE_RO;
    if (deposit && value > width_mask[reg->width]) return SCPE_ARG;
    if (deposit && (reg->flags & REG_NZ) && !value) return SCPE_ARG;
    return SCPE_OK;
}

t_stat scp_exdep_register(const REG *reg, uint32 index, t_bool deposit,
                          t_value *value)
{
    t_stat status;
    if (!value) return SCPE_IERR;
    status = register_preflight(NULL, reg, index, deposit, *value);
    if (status != SCPE_OK) return status;
    if (deposit) put_rval(reg, index, *value);
    else *value = get_rval(reg, index);
    return SCPE_OK;
}

/* Upstream's register loop expands unindexed arrays and contiguous REG
   descriptors. Preflight the complete bounded selection before deposits so
   a late read-only or malformed register cannot cause a partial update. */
t_stat scp_exdep_reg_loop(FILE *out, DEVICE *dev,
                          const REG *first, const REG *last,
                          uint32 lows, uint32 highs, t_bool deposit,
                          t_value value, t_bool skip_hidden)
{
    const REG *reg;
    uint32 limit, index;
    uint64_t count = 0;
    uint32 radix;
    t_stat status;

    if (!out || !first || !last || first > last) return SCPE_IERR;
    for (reg = first; reg <= last; ++reg) {
        if (skip_hidden && (reg->flags & REG_HIDDEN)) continue;
        if (!reg->depth) return SCPE_NOFNC;
        limit = highs == UINT32_MAX ? reg->depth - 1 : highs;
        if (lows > limit || limit >= reg->depth) return SCPE_SUB;
        count += (uint64_t)limit - lows + 1;
        if (count > SCP_EXDEP_MAX_VALUES) return SCPE_ARG;
        for (index = lows; index <= limit; ++index) {
            status = register_preflight(dev, reg, index, deposit, value);
            if (status != SCPE_OK) return status;
        }
    }
    if (!count) return SCPE_NXREG;
    for (reg = first; reg <= last; ++reg) {
        if (skip_hidden && (reg->flags & REG_HIDDEN)) continue;
        limit = highs == UINT32_MAX ? reg->depth - 1 : highs;
        radix = scp_exdep_radix(reg->radix);
        for (index = lows; index <= limit; ++index) {
            t_value current = value;
            status = register_preflight(dev, reg, index, deposit, current);
            if (status != SCPE_OK) return status;
            if (deposit) put_rval(reg, index, current);
            else current = get_rval(reg, index);
            sim_last_val = current;
            if (!deposit) {
                if (reg->depth > 1) fprintf(out, "%s[%u]: ", reg->name, index);
                else fprintf(out, "%s: ", reg->name);
                fprint_val(out, current, radix, reg->width, reg->flags & REG_FMT);
                fputc('\n', out);
            }
        }
    }
    return SCPE_OK;
}

/* Upstream's address loop obtains data through get_aval, stepping by the
   selected device's aincr. ESP accepts only dptr->deposit here: raw attached
   media writes wait for the separate disk-safety work. */
t_stat scp_exdep_addr_loop(FILE *out, DEVICE *dev, UNIT *unit,
                           t_addr low, t_addr high, t_bool deposit,
                           CONST char *deposit_text)
{
    uint64_t count;
    t_addr address, max;
    t_stat status, sym_status;
    int32 saved_switches = sim_switches;
    uint32 radix;
    uint32 symbol_switches = SWMASK('A') | SWMASK('B') | SWMASK('C') |
                             SWMASK('M') | SWMASK('R');

    if (!out || !dev || !unit) return SCPE_IERR;
    if ((dev->flags & DEV_DIS) || (unit->flags & UNIT_DIS)) return SCPE_UDIS;
    if (!dev->awidth || dev->awidth > 32 || !dev->dwidth ||
        dev->dwidth > 32 || !dev->aincr) return SCPE_NOFNC;
    max = (t_addr)width_mask[dev->awidth];
    if (low > high || high > max ||
        ((low % dev->aincr) &&
         !(sim_switches & (SWMASK('A') | SWMASK('B'))))) return SCPE_ARG;
    count = (sim_switches & (SWMASK('A') | SWMASK('B'))) ?
            (uint64_t)high - low + 1 :
            (uint64_t)(high - low) / dev->aincr + 1;
    if (count > SCP_EXDEP_MAX_VALUES) return SCPE_ARG;
    if (deposit && (unit->flags & UNIT_RO)) return SCPE_RO;
    if (deposit && !dev->deposit) return SCPE_NOFNC;
    if (!dev->examine && (unit->flags & UNIT_FIX) && high >= unit->capac)
        return SCPE_NXM;
    if (deposit && (!deposit_text || !*deposit_text)) return SCPE_2FARG;
    if (!sim_eval || sim_emax <= 0) return SCPE_IERR;
    radix = scp_exdep_radix(dev->dradix);
    address = low;
    for (uint64_t n = 0; address <= high && n < SCP_EXDEP_MAX_VALUES; ++n) {
        uint32 step = dev->aincr;
        status = get_aval(address, dev, unit);
        sim_switches = saved_switches;
        if (status != SCPE_OK) return status;
        if (deposit) {
            int32 words;
            t_value numeric;
            sym_status = parse_sym(deposit_text, address, unit,
                                   sim_eval, sim_switches);
            sim_switches = saved_switches;
            if (sym_status > SCPE_OK) {
                numeric = get_uint(deposit_text, radix,
                                   width_mask[dev->dwidth], &status);
                if (status != SCPE_OK) return status;
                sim_eval[0] = numeric;
                words = 1;
            } else {
                step = (uint32)(1 - sym_status);
                words = (int32)((step + dev->aincr - 1) / dev->aincr);
            }
            if (words <= 0 || words > sim_emax ||
                (t_addr)(words - 1) > (max - address) / dev->aincr)
                return SCPE_ARG;
            for (int32 i = 0; i < words; ++i) {
                t_addr dest = address + (t_addr)i * dev->aincr;
                if ((unit->flags & UNIT_FIX) && dest >= unit->capac &&
                    (!dev->examine || words > 1)) return SCPE_NXM;
            }
            for (int32 i = 0; i < words; ++i) {
                t_addr dest = address + (t_addr)i * dev->aincr;
                status = dev->deposit(sim_eval[i] & width_mask[dev->dwidth],
                                      dest, unit, sim_switches);
                sim_switches = saved_switches;
                if (status != SCPE_OK) return status;
            }
            sim_last_val = sim_eval[0];
        } else {
            sim_last_val = sim_eval[0];
            fprint_val(out, address, dev->aradix, dev->awidth, PV_LEFT);
            fputs(":\t", out);
            if (sim_switches & symbol_switches) {
                sym_status = fprint_sym(out, address, sim_eval, unit,
                                        sim_switches);
                sim_switches = saved_switches;
                if (sym_status <= SCPE_OK)
                    step = (uint32)(1 - sym_status);
                else fprint_val(out, sim_eval[0], radix, dev->dwidth,
                                PV_RZRO);
            } else fprint_val(out, sim_eval[0], radix, dev->dwidth, PV_RZRO);
            fputc('\n', out);
        }
        if (!step || step > max - address) break;
        address += step;
    }
    return SCPE_OK;
}
