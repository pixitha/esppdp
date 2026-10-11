/* Exercise breakpoint counts against the real embedded event-queue clock. */
#include "sim_defs.h"
#include <assert.h>

extern int32 noqueue_time;
extern double sim_time, sim_rtime;
extern UNIT *sim_clock_queue;

uint32 sim_brk_dflt = SWMASK('E');
uint32 sim_brk_types = SWMASK('E');

int main(void)
{
    UNIT event = {0};

    assert(sim_brk_init() == SCPE_OK);
    assert(sim_brk_set(01000, SWMASK('E'), 2, NULL) == SCPE_OK);

    sim_time = sim_rtime = 0;
    sim_clock_queue = QUEUE_LIST_END;
    noqueue_time = sim_interval = 100;
    assert(sim_brk_test(01000, SWMASK('E')) == 0);
    assert(sim_brk_test(01000, SWMASK('E')) == 0); /* same instruction */
    sim_interval--;
    assert(sim_brk_test(01000, SWMASK('E')) == SWMASK('E'));
    assert(sim_gtime() == 1);

    /* The same clock must account for elapsed instructions with an event. */
    event.next = QUEUE_LIST_END;
    event.time = sim_interval = 20;
    sim_clock_queue = &event;
    sim_interval--;
    assert(sim_gtime() == 2);
    assert(event.time == 19);

    sim_clock_queue = QUEUE_LIST_END;
    assert(sim_brk_init() == SCPE_OK);
    return 0;
}
