#ifndef SIM_HANG_PROBE_H_
#define SIM_HANG_PROBE_H_

#include "sdkconfig.h"
#include <stdint.h>

#if CONFIG_ESPPDP_HANG_PROBE
void sim_hang_probe_start(void);
void sim_hang_probe_instruction(uint32_t count, uint16_t pc);
void sim_hang_probe_event_enter(uintptr_t action);
void sim_hang_probe_event_exit(void);
void sim_hang_probe_input(void);
void sim_hang_probe_tti_deliver(void);
void sim_hang_probe_tti_read(void);
void sim_hang_probe_output(void);
#else
#define sim_hang_probe_start() ((void)0)
#define sim_hang_probe_instruction(count, pc) ((void)0)
#define sim_hang_probe_event_enter(action) ((void)0)
#define sim_hang_probe_event_exit() ((void)0)
#define sim_hang_probe_input() ((void)0)
#define sim_hang_probe_tti_deliver() ((void)0)
#define sim_hang_probe_tti_read() ((void)0)
#define sim_hang_probe_output() ((void)0)
#endif

#endif
