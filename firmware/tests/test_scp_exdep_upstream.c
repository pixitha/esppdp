/* Host-only checks for the selected historical SIMH SCP primitives. */
#include "sim_defs.h"
#include <assert.h>
#include <stdint.h>

extern t_addr sim_last_addr;
extern t_value sim_last_val;
extern void put_rval(const REG *reg, uint32 index, t_value value);

const REG *sim_PC;
const t_value width_mask[] = {
    0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095,
    8191, 16383, 32767, 65535, 131071, 262143, 524287, 1048575,
    2097151, 4194303, 8388607, 16777215, 33554431, 67108863,
    134217727, 268435455, 536870911, 1073741823, 2147483647,
    UINT32_MAX
};
const size_t size_map[] = { 1, 1, 2, 4, 4 };
static int pc_checks;
void sim_brk_npc(uint32 count) { (void)count; ++pc_checks; }

int main(void)
{
    t_addr low, high;
    const char *end;
    uint16 words[] = { 012345, 023456, 034567, 045670 };
    uint32 pc_value = 01000;
    REG array = { .name = "WORDS", .loc = words, .radix = 8,
                  .width = 16, .depth = 4, .obj_size = sizeof words };
    REG pc = { .name = "PC", .loc = &pc_value, .radix = 8,
               .width = 16, .depth = 1, .obj_size = sizeof pc_value };

    end = get_range(NULL, "1000-1010", &low, &high, 8, 0177777, 0);
    assert(end && !*end && low == 01000 && high == 01010);
    end = get_range(NULL, "1000/10", &low, &high, 8, 0177777, 0);
    assert(end && !*end && low == 01000 && high == 01007);
    end = get_range(NULL, "0-3]", &low, &high, 10, 3, ']');
    assert(end && !*end && low == 0 && high == 3);
    assert(!get_range(NULL, "1010-1000", &low, &high, 8, 0177777, 0));
    assert(!get_range(NULL, "1000/0", &low, &high, 8, 0177777, 0));
    assert(!get_range(NULL, "37777777777/2", &low, &high, 8,
                      UINT32_MAX, 0));
    assert(!get_range(NULL, "1000-", &low, &high, 8, 0177777, 0));
    assert(!get_range(NULL, "-1", &low, &high, 8, 0177777, 0));
    sim_last_val = 01234;
    end = get_range(NULL, "$", &low, &high, 8, 0177777, 0);
    assert(end && !*end && low == 01234 && high == 01234);
    sim_last_addr = 01400;
    end = get_range(NULL, ".", &low, &high, 8, 0177777, 0);
    assert(end && !*end && low == 01400 && high == 01400);

    assert(get_rval(&array, 2) == 034567);
    put_rval(&array, 1, 076543);
    assert(words[0] == 012345 && words[1] == 076543 && words[2] == 034567);
    sim_PC = &pc;
    put_rval(&pc, 0, 01234);
    assert(pc_value == 01234 && pc_checks == 1);
    return 0;
}
