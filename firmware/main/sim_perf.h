#ifndef SIM_PERF_H_
#define SIM_PERF_H_

#include <stddef.h>
#include <stdint.h>

void sim_perf_note_tto(uint32_t elapsed_us);
void sim_perf_note_disk_read(size_t bytes, uint32_t elapsed_us);
void sim_perf_note_disk_write(size_t bytes, uint32_t elapsed_us);
void sim_perf_note_wait(uint32_t elapsed_us);
void sim_perf_note_event(uint32_t elapsed_us);
void sim_perf_note_pc(uint16_t pc, uint16_t ir);
void sim_perf_checkpoint(uint32_t guest_instructions, uint16_t pc);

#endif
