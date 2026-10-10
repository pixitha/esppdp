/* Host-only checks for the embedded breakpoint table; no simulator process. */
#include "sim_defs.h"
#include <assert.h>

uint32 sim_brk_dflt = SWMASK('E');
uint32 sim_brk_types = SWMASK('E') | SWMASK('P') | SWMASK('R') |
                       SWMASK('S') | SWMASK('W') | SWMASK('X');
static double instruction_time;

double sim_gtime(void)
{
    return instruction_time;
}

int main(void)
{
    assert(sim_brk_init() == SCPE_OK);
    assert(sim_brk_set(01234, 0, 0, NULL) == SCPE_OK);
    assert(sim_brk_summ == SWMASK('E'));
    assert(sim_brk_fnd(01234) != NULL);
    assert(sim_brk_test(01234, SWMASK('E')) == SWMASK('E'));
    assert(sim_brk_test(01234, SWMASK('E')) == 0); /* resume at same PC */
    instruction_time = 1;
    assert(sim_brk_test(01234, SWMASK('E')) == SWMASK('E'));
    sim_brk_npc(0);
    assert(sim_brk_test(01234, SWMASK('E')) == SWMASK('E'));

    assert(sim_brk_set(01234, SWMASK('W'), 0, NULL) == SCPE_OK);
    assert(sim_brk_summ == (SWMASK('E') | SWMASK('W')));
    assert(sim_brk_test(01234, SWMASK('W')) == SWMASK('W'));
    assert(sim_brk_clr(01234, SWMASK('E')) == SCPE_OK);
    assert(sim_brk_summ == SWMASK('W'));
    assert(sim_brk_test(01234, SWMASK('E')) == 0);

    assert(sim_brk_set(02000, SWMASK('E'), 2, NULL) == SCPE_OK);
    assert(sim_brk_test(02000, SWMASK('E')) == 0);
    instruction_time = 2;
    assert(sim_brk_test(02000, SWMASK('E')) == SWMASK('E'));
    assert(sim_brk_clrall(0) == SCPE_OK);
    assert(sim_brk_summ == 0);
    assert(sim_brk_fnd(01234) == NULL);
    assert(sim_brk_fnd(02000) == NULL);
    assert(sim_brk_init() == SCPE_OK);
    return 0;
}
