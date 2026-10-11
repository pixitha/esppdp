/* Host-only safety checks for the bounded ESP SCP exdep adapter. */
#include "scp_exdep_adapter.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern t_addr sim_last_addr;
extern t_value sim_last_val;

const REG *sim_PC;
int32 sim_switches;
int32 sim_emax = 4;
static t_value eval_words[4];
t_value *sim_eval = eval_words;
static unsigned symbolic_prints;
const t_value width_mask[] = {
    0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095,
    8191, 16383, 32767, 65535, 131071, 262143, 524287, 1048575,
    2097151, 4194303, 8388607, 16777215, 33554431, 67108863,
    134217727, 268435455, 536870911, 1073741823, 2147483647,
    UINT32_MAX
};
const size_t size_map[] = { 1, 1, 2, 4, 4 };
void sim_brk_npc(uint32 count) { (void)count; }
uint32 scp_exdep_radix(uint32 base) { return base; }
t_value get_uint(const char *text, uint32 radix, t_value max, t_stat *status)
{
    char *end;
    unsigned long value = strtoul(text, &end, radix);
    *status = (!*text || *end || value > max) ? SCPE_ARG : SCPE_OK;
    return (t_value)value;
}
t_stat parse_sym(const char *text, t_addr address, UNIT *unit,
                 t_value *value, int32 switches)
{
    (void)address;
    (void)unit;
    (void)switches;
    if (!strcmp(text, "SYM")) {
        value[0] = 0123;
        return -1;
    }
    if (!strcmp(text, "LONG")) {
        value[0] = 0123;
        value[1] = 0456;
        return -3;
    }
    return SCPE_ARG;
}
t_stat fprint_sym(FILE *out, t_addr address, t_value *value,
                  UNIT *unit, int32 switches)
{
    (void)value;
    (void)unit;
    if (!(switches & SWMASK('M'))) return SCPE_ARG;
    ++symbolic_prints;
    fputs("NOP", out);
    return address == 0 ? -3 : -1;
}
t_stat fprint_val(FILE *out, t_value value, uint32 radix, uint32 width,
                  uint32 format)
{
    (void)width;
    (void)format;
    return fprintf(out, radix == 10 ? "%u" : "%o", (unsigned)value) < 0 ?
           SCPE_IOERR : SCPE_OK;
}
t_stat get_aval(t_addr address, DEVICE *dev, UNIT *unit)
{
    return dev->examine ? dev->examine(&sim_eval[0], address, unit, sim_switches)
                        : SCPE_NOFNC;
}

static uint16 memory[128];
static unsigned writes;
static t_stat examine(t_value *value, t_addr address, UNIT *unit, int32 sw)
{
    (void)unit;
    (void)sw;
    if (address & 1 || address / 2 >= 128) return SCPE_NXM;
    *value = memory[address / 2];
    return SCPE_OK;
}
static t_stat deposit(t_value value, t_addr address, UNIT *unit, int32 sw)
{
    (void)unit;
    (void)sw;
    if (address & 1 || address / 2 >= 128) return SCPE_NXM;
    memory[address / 2] = (uint16)value;
    ++writes;
    return SCPE_OK;
}

int main(void)
{
    const char *number_end;
    t_addr low, high;
    t_value value = 1;
    uint16 words[4] = { 1, 2, 3, 4 };
    uint32 scalar = 5;
    REG regs[] = {
        { .name = "WORDS", .loc = words, .radix = 8, .width = 16,
          .depth = 4, .obj_size = sizeof words },
        { .name = "RO", .loc = &scalar, .radix = 8, .width = 16,
          .depth = 1, .obj_size = sizeof scalar, .flags = REG_RO }
    };
    UNIT unit = { .flags = UNIT_FIX, .capac = sizeof memory };
    UNIT unit_array[2] = { { .u3 = 3 }, { .u3 = 4 } };
    DEVICE dev = { .name = "TEST", .units = &unit, .numunits = 1,
                   .aradix = 8, .awidth = 16, .aincr = 2,
                   .dradix = 8, .dwidth = 16,
                   .examine = examine, .deposit = deposit };
    FILE *out = tmpfile();
    assert(out);

    assert(strtotv("FFFFFFFF", &number_end, 16) == UINT32_MAX);
    assert(*number_end == '\0');
    assert(strtotv("100000000", &number_end, 16) == 0);
    assert(number_end && *number_end == '1');

    sim_last_addr = 020;
    assert(scp_exdep_range(&dev, "1000/10", 8, 0177777, 0,
                            &low, &high) == SCPE_OK);
    assert(low == 01000 && high == 01007);
    sim_last_addr = 020;
    assert(scp_exdep_range(&dev, "1000/0", 8, 0177777, 0,
                            &low, &high) == SCPE_ARG);
    assert(sim_last_addr == 020);
    assert(scp_exdep_range(&dev, "177777/2", 8, 0177777, 0,
                            &low, &high) == SCPE_ARG);
    assert(sim_last_addr == 020);

    assert(scp_exdep_register(&regs[0], 4, FALSE, &value) == SCPE_SUB);
    assert(scp_exdep_register(&regs[1], 0, TRUE, &value) == SCPE_RO);
    assert(scalar == 5);
    regs[0].obj_size = 2;
    assert(scp_exdep_register(&regs[0], 0, FALSE, &value) == SCPE_NOFNC);
    regs[0].obj_size = sizeof words;
    regs[0].flags = REG_NZ;
    value = 0;
    assert(scp_exdep_register(&regs[0], 0, TRUE, &value) == SCPE_ARG);
    assert(words[0] == 1);
    regs[0].flags = 0;

    assert(scp_exdep_reg_loop(out, NULL, regs, &regs[1], 0, UINT32_MAX,
                              TRUE, 7, FALSE) == SCPE_RO);
    assert(words[0] == 1 && words[3] == 4 && scalar == 5);
    regs[1].flags = 0;
    regs[1].width = 3;
    assert(scp_exdep_reg_loop(out, NULL, regs, &regs[1], 0, UINT32_MAX,
                              TRUE, 8, FALSE) == SCPE_ARG);
    assert(words[0] == 1 && words[3] == 4 && scalar == 5);
    regs[1].width = 16;
    regs[1].flags = REG_RO;
    assert(scp_exdep_reg_loop(out, NULL, regs, regs, 1, 3,
                              TRUE, 7, FALSE) == SCPE_OK);
    assert(words[0] == 1 && words[1] == 7 && words[3] == 7);
    regs[0].depth = 65;
    assert(scp_exdep_reg_loop(out, NULL, regs, regs, 0, UINT32_MAX,
                              TRUE, 8, FALSE) == SCPE_ARG);
    assert(words[0] == 1);
    regs[0].depth = 4;
    regs[0].flags = REG_CIRC;
    regs[0].qptr = 2;
    value = 0;
    assert(scp_exdep_register(&regs[0], 0, FALSE, &value) == SCPE_OK);
    assert(value == 7); /* physical slot 2 */
    regs[0].qptr = 4;
    assert(scp_exdep_register(&regs[0], 0, FALSE, &value) == SCPE_NOFNC);
    regs[0].flags = 0;

    REG unit_reg = { .name = "U3", .loc = &unit_array[0].u3,
                     .radix = 8, .width = 16, .depth = 2,
                     .obj_size = sizeof unit_array[0].u3,
                     .flags = REG_UNIT };
    DEVICE unit_dev = { .name = "UNITS", .units = unit_array, .numunits = 2 };
    assert(scp_exdep_reg_loop(out, &unit_dev, &unit_reg, &unit_reg,
                              0, 1, FALSE, 0, FALSE) == SCPE_OK);
    unit_array[1].flags = UNIT_RO;
    assert(scp_exdep_reg_loop(out, &unit_dev, &unit_reg, &unit_reg,
                              0, 1, TRUE, 5, FALSE) == SCPE_RO);
    assert(unit_array[0].u3 == 3 && unit_array[1].u3 == 4);
    unit_array[1].flags = 0;
    assert(scp_exdep_reg_loop(out, &unit_dev, &unit_reg, &unit_reg,
                              0, 1, TRUE, 5, FALSE) == SCPE_OK);
    assert(unit_array[0].u3 == 5 && unit_array[1].u3 == 5);

    /* RQ's CPKT uses UNIT.us9, a uint16 immediately before UNIT.us10.
       The old upstream uint32 cast both rejected it and could touch us10. */
    REG packet_reg = { .name = "CPKT", .loc = &unit_array[0].us9,
                       .radix = 10, .width = 5, .depth = 2,
                       .obj_size = sizeof unit_array[0].us9,
                       .flags = REG_UNIT };
    unit_array[0].us9 = 3;
    unit_array[0].us10 = 0xBEEF;
    unit_array[1].us9 = 4;
    unit_array[1].us10 = 0xCAFE;
    assert(scp_exdep_reg_loop(out, &unit_dev, &packet_reg, &packet_reg,
                              0, 1, FALSE, 0, FALSE) == SCPE_OK);
    assert(get_rval(&packet_reg, 0) == 3 && get_rval(&packet_reg, 1) == 4);
    assert(scp_exdep_reg_loop(out, &unit_dev, &packet_reg, &packet_reg,
                              0, 1, TRUE, 5, FALSE) == SCPE_OK);
    assert(unit_array[0].us9 == 5 && unit_array[1].us9 == 5);
    assert(unit_array[0].us10 == 0xBEEF && unit_array[1].us10 == 0xCAFE);
    packet_reg.width = 17;
    assert(scp_exdep_reg_loop(out, &unit_dev, &packet_reg, &packet_reg,
                              0, 1, FALSE, 0, FALSE) == SCPE_NOFNC);

    unit.flags |= UNIT_RO;
    assert(scp_exdep_addr_loop(out, &dev, &unit, 0, 0, TRUE, "11") == SCPE_RO);
    assert(writes == 0 && memory[0] == 0);
    unit.flags &= ~UNIT_RO;
    assert(scp_exdep_addr_loop(out, &dev, &unit, 1, 2, TRUE, "11") == SCPE_ARG);
    assert(scp_exdep_addr_loop(out, &dev, &unit, 0, 128, TRUE, "11") == SCPE_ARG);
    assert(writes == 0);
    assert(scp_exdep_addr_loop(out, &dev, &unit, 0, 126, FALSE, NULL) == SCPE_OK);
    sim_switches = SWMASK('M');
    assert(scp_exdep_addr_loop(out, &dev, &unit, 0, 4, FALSE, NULL) == SCPE_OK);
    assert(symbolic_prints == 2);
    sim_switches = 0;
    assert(scp_exdep_addr_loop(out, &dev, &unit, 0, 4, TRUE, "11") == SCPE_OK);
    assert(writes == 3 && memory[0] == 9 && memory[2] == 9);
    assert(scp_exdep_addr_loop(out, &dev, &unit, 6, 6, TRUE, "SYM") == SCPE_OK);
    assert(memory[3] == 0123);
    assert(scp_exdep_addr_loop(out, &dev, &unit, 8, 8, TRUE, "LONG") == SCPE_OK);
    assert(memory[4] == 0123 && memory[5] == 0456);
    unit.capac = 10;
    assert(scp_exdep_addr_loop(out, &dev, &unit, 8, 8, TRUE, "LONG") == SCPE_NXM);
    unit.capac = sizeof memory;
    dev.flags = DEV_DIS;
    assert(scp_exdep_addr_loop(out, &dev, &unit, 0, 0, TRUE, "3") == SCPE_UDIS);
    dev.flags = 0;
    unit.flags |= UNIT_DIS;
    assert(scp_exdep_addr_loop(out, &dev, &unit, 0, 0, FALSE, NULL) == SCPE_UDIS);
    fclose(out);
    return 0;
}
