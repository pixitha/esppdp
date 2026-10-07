#include "sdkconfig.h"

#if defined(ESP_PLATFORM) && CONFIG_ESPPDP_PERF_TRACE

#include "sim_perf.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "sim_perf";
static uint64_t tto_chars;
static uint64_t tto_blocked_us;
static uint64_t disk_reads;
static uint64_t disk_read_bytes;
static uint64_t disk_read_busy_us;
static uint64_t disk_writes;
static uint64_t disk_write_bytes;
static uint64_t disk_write_busy_us;
static uint64_t wait_us;
/* Event service includes any synchronous disk or terminal work it invokes. */
static uint64_t event_us;
static uint64_t event_calls;
typedef struct {
    uint16_t pc;
    uint16_t ir;
    uint32_t samples;
} pc_sample_t;
static pc_sample_t pc_samples[64];
static uint32_t pc_samples_other;
static int64_t checkpoint_us;
static uint32_t checkpoint_guest_instructions;

void sim_perf_note_tto(uint32_t elapsed_us)
{
    ++tto_chars;
    tto_blocked_us += elapsed_us;
}

void sim_perf_note_disk_read(size_t bytes, uint32_t elapsed_us)
{
    ++disk_reads;
    disk_read_bytes += bytes;
    disk_read_busy_us += elapsed_us;
}

void sim_perf_note_disk_write(size_t bytes, uint32_t elapsed_us)
{
    ++disk_writes;
    disk_write_bytes += bytes;
    disk_write_busy_us += elapsed_us;
}

void sim_perf_note_wait(uint32_t elapsed_us)
{
    wait_us += elapsed_us;
}

void sim_perf_note_event(uint32_t elapsed_us)
{
    ++event_calls;
    event_us += elapsed_us;
}

void sim_perf_note_pc(uint16_t pc, uint16_t ir)
{
    /* Called for one of every 4096 executed guest instructions. */
    for (size_t i = 0; i < sizeof(pc_samples) / sizeof(pc_samples[0]); ++i) {
        if (pc_samples[i].samples == 0 ||
            (pc_samples[i].pc == pc && pc_samples[i].ir == ir)) {
            pc_samples[i].pc = pc;
            pc_samples[i].ir = ir;
            ++pc_samples[i].samples;
            return;
        }
    }
    ++pc_samples_other;
}

void sim_perf_checkpoint(uint32_t guest_instructions, uint16_t pc)
{
    int64_t now = esp_timer_get_time();
    if (checkpoint_us == 0) {
        checkpoint_us = now;
        checkpoint_guest_instructions = guest_instructions;
        return;
    }
    int64_t elapsed_us = now - checkpoint_us;
    if (elapsed_us < 5000000)
        return;

    uint32_t guest_instruction_delta = guest_instructions - checkpoint_guest_instructions;
    double guest_ips = (double)guest_instruction_delta * 1000000.0 / (double)elapsed_us;
    ESP_LOGI(TAG,
        "SIMH perf: guest %.0f instr/s; WAIT %llu ms; event %llu calls/%llu ms; disk R %llu/%llu KiB/%llu ms, W %llu/%llu KiB/%llu ms; TTO %llu chars/%llu ms blocked; PC=%06o",
        guest_ips,
        (unsigned long long)(wait_us / 1000),
        (unsigned long long)event_calls,
        (unsigned long long)(event_us / 1000),
        (unsigned long long)disk_reads,
        (unsigned long long)(disk_read_bytes / 1024),
        (unsigned long long)(disk_read_busy_us / 1000),
        (unsigned long long)disk_writes,
        (unsigned long long)(disk_write_bytes / 1024),
        (unsigned long long)(disk_write_busy_us / 1000),
        (unsigned long long)tto_chars,
        (unsigned long long)(tto_blocked_us / 1000),
        (unsigned)pc);

    pc_sample_t top[4] = {0};
    for (size_t i = 0; i < sizeof(pc_samples) / sizeof(pc_samples[0]); ++i) {
        pc_sample_t sample = pc_samples[i];
        for (size_t rank = 0; sample.samples && rank < 4; ++rank) {
            if (sample.samples > top[rank].samples) {
                pc_sample_t displaced = top[rank];
                top[rank] = sample;
                sample = displaced;
            }
        }
        pc_samples[i].samples = 0;
    }
    ESP_LOGI(TAG,
        "PC samples: %06o/%06o=%lu %06o/%06o=%lu %06o/%06o=%lu %06o/%06o=%lu other=%lu",
        top[0].pc, top[0].ir, (unsigned long)top[0].samples,
        top[1].pc, top[1].ir, (unsigned long)top[1].samples,
        top[2].pc, top[2].ir, (unsigned long)top[2].samples,
        top[3].pc, top[3].ir, (unsigned long)top[3].samples,
        (unsigned long)pc_samples_other);

    disk_reads = 0;
    disk_read_bytes = 0;
    disk_read_busy_us = 0;
    disk_writes = 0;
    disk_write_bytes = 0;
    disk_write_busy_us = 0;
    wait_us = 0;
    event_us = 0;
    event_calls = 0;
    pc_samples_other = 0;
    tto_chars = 0;
    tto_blocked_us = 0;
    checkpoint_us = now;
    checkpoint_guest_instructions = guest_instructions;
}

#else

void sim_perf_note_tto(uint32_t elapsed_us) { (void)elapsed_us; }
void sim_perf_note_disk_read(size_t bytes, uint32_t elapsed_us)
{
    (void)bytes;
    (void)elapsed_us;
}
void sim_perf_note_disk_write(size_t bytes, uint32_t elapsed_us)
{
    (void)bytes;
    (void)elapsed_us;
}
void sim_perf_note_wait(uint32_t elapsed_us) { (void)elapsed_us; }
void sim_perf_note_event(uint32_t elapsed_us) { (void)elapsed_us; }
void sim_perf_note_pc(uint16_t pc, uint16_t ir) { (void)pc; (void)ir; }
void sim_perf_checkpoint(uint32_t guest_instructions, uint16_t pc)
{
    (void)guest_instructions;
    (void)pc;
}

#endif
