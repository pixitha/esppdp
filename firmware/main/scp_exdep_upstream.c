/* SIMH SCP range and register primitives, from simh/simh 8bbec614
   (the pre-REG-layout-change revision matching this ESP fork).

   Copyright (c) 1993-2016, Robert M Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR
   OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
   ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
   OTHER DEALINGS IN THE SOFTWARE.

   ESP adaptations: make last address/value visible to the separate command
   reader, retain the fork's const REG interface, use libc ctype where this
   fork lacks sim_is* helpers, reject numeric overflow in strtotv, and use
   the field's actual width for REG_UNIT access (as newer upstream does).
   The older REG_UNIT primitive assumes every field is uint32, but RQ's
   CPKT/PKTQ are uint16 and can be unaligned on the ESP32. The
   optional VM address parser remains
   NULL until its upstream PDP-11 hook is restored. Command output limits and
   target validation remain in scp_console.c, not in these primitives. */

#include "sim_defs.h"
#include <ctype.h>
#include <limits.h>
#include <string.h>

extern const t_value width_mask[];
extern const size_t size_map[];
extern const REG *sim_PC;

t_value sim_last_val;
t_addr sim_last_addr;
t_addr (*sim_vm_parse_addr)(DEVICE *dptr, CONST char *cptr,
                            CONST char **tptr) = NULL;

#define SZ_R(rp) \
    (size_map[((rp)->width + (rp)->offset + CHAR_BIT - 1) / CHAR_BIT])

/* Upstream strtotv, with an ESP-side overflow guard. */
t_value strtotv(CONST char *inptr, CONST char **endptr, uint32 radix)
{
    t_value val = 0;
    uint32 c, digit;
    t_bool nodigits = TRUE;

    if (endptr) *endptr = inptr;
    if (((radix < 2) || (radix > 36)) && (radix != 0)) return 0;
    while (isspace((unsigned char)*inptr)) ++inptr;
    if (((radix == 0) || (radix == 16)) &&
        (inptr[0] == '0') && (inptr[1] == 'x' || inptr[1] == 'X')) {
        radix = 16;
        inptr += 2;
    }
    if (((radix == 0) || (radix == 2)) &&
        (inptr[0] == '0') && (inptr[1] == 'b' || inptr[1] == 'B')) {
        radix = 2;
        inptr += 2;
    }
    if ((radix == 0) && (*inptr == '0')) radix = 8;
    if (radix == 0) radix = 10;
    for (c = (unsigned char)*inptr; isalnum(c);
         c = (unsigned char)*++inptr) {
        c = toupper(c);
        digit = isdigit(c) ? c - (uint32)'0' : c + 10 - (uint32)'A';
        if (digit >= radix || val > (T_VALUE_MAX - digit) / radix)
            return 0;
        val = val * radix + digit;
        nodigits = FALSE;
    }
    if (nodigits) return 0;
    if (endptr) *endptr = inptr;
    return val;
}

/* Upstream get_range, with the compatible pre-2022 REG/SCP state. */
CONST char *get_range(DEVICE *dptr, CONST char *cptr, t_addr *lo, t_addr *hi,
                      uint32 rdx, t_addr max, char term)
{
    CONST char *tptr;

    if (max && strncmp(cptr, "ALL", strlen("ALL")) == 0) {
        tptr = cptr + strlen("ALL");
        *lo = 0;
        *hi = max;
    } else {
        if ((strncmp(cptr, ".", strlen(".")) == 0) &&
            ((cptr[1] == '\0') || (cptr[1] == '-') ||
             (cptr[1] == ':') || (cptr[1] == '/'))) {
            tptr = cptr + strlen(".");
            *lo = *hi = sim_last_addr;
        } else {
            if (strncmp(cptr, "$", strlen("$")) == 0) {
                tptr = cptr + strlen("$");
                *hi = *lo = (t_addr)sim_last_val;
            } else {
                if (dptr && sim_vm_parse_addr)
                    *lo = sim_vm_parse_addr(dptr, cptr, &tptr);
                else *lo = (t_addr)strtotv(cptr, &tptr, rdx);
                if (cptr == tptr) return NULL;
            }
        }
        if ((*tptr == '-') || (*tptr == ':')) {
            cptr = tptr + 1;
            if (dptr && sim_vm_parse_addr)
                *hi = sim_vm_parse_addr(dptr, cptr, &tptr);
            else *hi = (t_addr)strtotv(cptr, &tptr, rdx);
            if ((cptr == tptr) || (*lo > *hi)) return NULL;
        } else if (*tptr == '/') {
            cptr = tptr + 1;
            *hi = (t_addr)strtotv(cptr, &tptr, rdx);
            if ((cptr == tptr) || (*hi == 0)) return NULL;
            if (*hi - 1 > (t_addr)-1 - *lo) return NULL;
            *hi = *lo + *hi - 1;
        } else *hi = *lo;
    }
    sim_last_addr = *hi;
    if (term && (*tptr++ != term)) return NULL;
    return tptr;
}

/* Upstream register reads for this fork's REG layout. */
static uint32 get_unit_rval(const REG *rptr, uint32 idx)
{
    const uint8 *loc = (const uint8 *)rptr->loc + idx * sizeof(UNIT);
    uint32 val = 0;
    if (rptr->obj_size == sizeof(uint8)) {
        uint8 word;
        memcpy(&word, loc, sizeof(word));
        val = word;
    } else if (rptr->obj_size == sizeof(uint16)) {
        uint16 word;
        memcpy(&word, loc, sizeof(word));
        val = word;
    } else if (rptr->obj_size == sizeof(uint32)) {
        memcpy(&val, loc, sizeof(val));
    }
    return val;
}

static void put_unit_rval(const REG *rptr, uint32 idx, uint32 val)
{
    uint8 *loc = (uint8 *)rptr->loc + idx * sizeof(UNIT);
    if (rptr->obj_size == sizeof(uint8)) {
        uint8 word = (uint8)val;
        memcpy(loc, &word, sizeof(word));
    } else if (rptr->obj_size == sizeof(uint16)) {
        uint16 word = (uint16)val;
        memcpy(loc, &word, sizeof(word));
    } else if (rptr->obj_size == sizeof(uint32)) {
        memcpy(loc, &val, sizeof(val));
    }
}

t_value get_rval(const REG *rptr, uint32 idx)
{
    size_t sz = SZ_R(rptr);
    t_value val;
    uint32 *ptr;

    if ((rptr->depth > 1) && (rptr->flags & REG_CIRC)) {
        idx += rptr->qptr;
        if (idx >= rptr->depth) idx -= rptr->depth;
    }
    if (rptr->flags & REG_UNIT) {
        val = get_unit_rval(rptr, idx);
    } else if ((rptr->depth > 1) && (rptr->flags & REG_STRUCT)) {
        ptr = (uint32 *)(((size_t)rptr->loc) + (idx * rptr->str_size));
        val = *ptr;
    } else if (((rptr->depth > 1) || (rptr->flags & REG_FIT)) &&
               (sz == sizeof(uint8)))
        val = *(((uint8 *)rptr->loc) + idx);
    else if (((rptr->depth > 1) || (rptr->flags & REG_FIT)) &&
             (sz == sizeof(uint16)))
        val = *(((uint16 *)rptr->loc) + idx);
    else val = *(((uint32 *)rptr->loc) + idx);
    return (val >> rptr->offset) & width_mask[rptr->width];
}

#define PUT_RVAL(sz,rp,id,v,m) \
    *(((sz *) (rp)->loc) + (id)) = \
        (sz)((*(((sz *) (rp)->loc) + (id)) & \
        ~((m) << (rp)->offset)) | ((v) << (rp)->offset))

void put_rval_pcchk(const REG *rptr, uint32 idx, t_value val, t_bool pc_chk)
{
    size_t sz;
    t_value mask;
    uint32 *ptr;

    if (pc_chk && (rptr == sim_PC)) sim_brk_npc(0);
    sz = SZ_R(rptr);
    mask = width_mask[rptr->width];
    if ((rptr->depth > 1) && (rptr->flags & REG_CIRC)) {
        idx += rptr->qptr;
        if (idx >= rptr->depth) idx -= rptr->depth;
    }
    if (rptr->flags & REG_UNIT) {
        uint32 current = get_unit_rval(rptr, idx);
        current = (current & ~(((uint32)mask) << rptr->offset)) |
                  (((uint32)val) << rptr->offset);
        put_unit_rval(rptr, idx, current);
    } else if ((rptr->depth > 1) && (rptr->flags & REG_STRUCT)) {
        ptr = (uint32 *)(((size_t)rptr->loc) + (idx * rptr->str_size));
        *ptr = (*ptr & ~(((uint32)mask) << rptr->offset)) |
               (((uint32)val) << rptr->offset);
    } else if (((rptr->depth > 1) || (rptr->flags & REG_FIT)) &&
               (sz == sizeof(uint8)))
        PUT_RVAL(uint8, rptr, idx, (uint32)val, (uint32)mask);
    else if (((rptr->depth > 1) || (rptr->flags & REG_FIT)) &&
             (sz == sizeof(uint16)))
        PUT_RVAL(uint16, rptr, idx, (uint32)val, (uint32)mask);
    else PUT_RVAL(uint32, rptr, idx, val, mask);
}

void put_rval(const REG *rptr, uint32 idx, t_value val)
{
    put_rval_pcchk(rptr, idx, val, TRUE);
}
