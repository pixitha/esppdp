#include "sim_hang_probe.h"

#if CONFIG_ESPPDP_HANG_PROBE

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static uint32_t instruction_sample;
static uint32_t pc_sample;
static uint32_t event_enters;
static uint32_t event_exits;
static uintptr_t event_action;
static uint32_t input_bytes;
static uint32_t tti_deliveries;
static uint32_t tti_reads;
static uint32_t output_bytes;

void sim_hang_probe_instruction(uint32_t count, uint16_t pc)
{
    __atomic_store_n(&instruction_sample, count, __ATOMIC_RELAXED);
    __atomic_store_n(&pc_sample, pc, __ATOMIC_RELAXED);
}

void sim_hang_probe_event_enter(uintptr_t action)
{
    __atomic_store_n(&event_action, action, __ATOMIC_RELAXED);
    __atomic_fetch_add(&event_enters, 1, __ATOMIC_RELAXED);
}

void sim_hang_probe_event_exit(void)
{
    __atomic_fetch_add(&event_exits, 1, __ATOMIC_RELAXED);
}

void sim_hang_probe_input(void) { __atomic_fetch_add(&input_bytes, 1, __ATOMIC_RELAXED); }
void sim_hang_probe_tti_deliver(void) { __atomic_fetch_add(&tti_deliveries, 1, __ATOMIC_RELAXED); }
void sim_hang_probe_tti_read(void) { __atomic_fetch_add(&tti_reads, 1, __ATOMIC_RELAXED); }
void sim_hang_probe_output(void) { __atomic_fetch_add(&output_bytes, 1, __ATOMIC_RELAXED); }

static void sim_hang_probe_task(void *arg)
{
    uint32_t last_instructions = 0;
    uint32_t last_event_enters = 0;
    uint32_t last_event_exits = 0;
    uint32_t last_input = 0;
    uint32_t last_tti_deliveries = 0;
    uint32_t last_tti_reads = 0;
    uint32_t last_output = 0;
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        uint32_t instructions = __atomic_load_n(&instruction_sample, __ATOMIC_RELAXED);
        uint32_t enters = __atomic_load_n(&event_enters, __ATOMIC_RELAXED);
        uint32_t exits = __atomic_load_n(&event_exits, __ATOMIC_RELAXED);
        uint32_t input = __atomic_load_n(&input_bytes, __ATOMIC_RELAXED);
        uint32_t delivered = __atomic_load_n(&tti_deliveries, __ATOMIC_RELAXED);
        uint32_t read = __atomic_load_n(&tti_reads, __ATOMIC_RELAXED);
        uint32_t output = __atomic_load_n(&output_bytes, __ATOMIC_RELAXED);
        ESP_LOGI("sim_probe", "5s: instr +%lu pc=%06lo event +%lu/+%lu action=%p input +%lu TTI +%lu/+%lu TTO +%lu",
                 (unsigned long)(instructions - last_instructions),
                 (unsigned long)__atomic_load_n(&pc_sample, __ATOMIC_RELAXED),
                 (unsigned long)(enters - last_event_enters),
                 (unsigned long)(exits - last_event_exits),
                 (void *)__atomic_load_n(&event_action, __ATOMIC_RELAXED),
                 (unsigned long)(input - last_input),
                 (unsigned long)(delivered - last_tti_deliveries),
                 (unsigned long)(read - last_tti_reads),
                 (unsigned long)(output - last_output));
        last_instructions = instructions;
        last_event_enters = enters;
        last_event_exits = exits;
        last_input = input;
        last_tti_deliveries = delivered;
        last_tti_reads = read;
        last_output = output;
    }
}

void sim_hang_probe_start(void)
{
    if (xTaskCreatePinnedToCore(sim_hang_probe_task, "sim_probe", 3072, NULL, 2, NULL, 0) != pdPASS)
        ESP_LOGE("sim_probe", "Unable to start hang probe on CPU0");
}

#endif
