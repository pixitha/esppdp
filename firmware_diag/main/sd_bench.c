#include "sd_bench.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_bench";

/* Match the verified ESP32-S3 DevKitC / Adafruit SDIO breakout wiring. */
#define SD_CLK_GPIO GPIO_NUM_14
#define SD_CMD_GPIO GPIO_NUM_13
#define SD_D0_GPIO  GPIO_NUM_4
#define SD_D1_GPIO  GPIO_NUM_6
#define SD_D2_GPIO  GPIO_NUM_15
#define SD_D3_GPIO  GPIO_NUM_5
#define SD_FREQ_KHZ 20000

#define BENCH_BYTES (8U * 1024U * 1024U)
#define BENCH_CHUNK (16U * 1024U)
#define BENCH_FREE_MARGIN (1U * 1024U * 1024U)

static uint8_t pattern_byte(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return (uint8_t)*state;
}

static void fill_pattern(uint8_t *buffer, size_t length, uint32_t *state)
{
    for (size_t i = 0; i < length; ++i) buffer[i] = pattern_byte(state);
}

static unsigned long long kib_per_second(int64_t elapsed_us)
{
    if (elapsed_us <= 0) elapsed_us = 1;
    return (unsigned long long)((uint64_t)(BENCH_BYTES / 1024U) * 1000000ULL /
                                (uint64_t)elapsed_us);
}

bool sd_bench_run(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SD_FREQ_KHZ;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = SD_CLK_GPIO;
    slot.cmd = SD_CMD_GPIO;
    slot.d0 = SD_D0_GPIO;
    slot.d1 = SD_D1_GPIO;
    slot.d2 = SD_D2_GPIO;
    slot.d3 = SD_D3_GPIO;

    ESP_LOGI(TAG, "mount 4-bit SDMMC at %d kHz: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d",
             SD_FREQ_KHZ, SD_CLK_GPIO, SD_CMD_GPIO, SD_D0_GPIO,
             SD_D1_GPIO, SD_D2_GPIO, SD_D3_GPIO);
    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot,
                                            &mount_config, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: SD mount: %s (card is never formatted)", esp_err_to_name(err));
        return false;
    }
    sdmmc_card_print_info(stdout, card);

    bool ok = false;
    bool created = false;
    FILE *file = NULL;
    char path[32] = {0};
    uint8_t *expected = NULL;
    uint8_t *actual = NULL;
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    err = esp_vfs_fat_info("/sdcard", &total_bytes, &free_bytes);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: SD capacity query: %s", esp_err_to_name(err));
        goto cleanup;
    }
    ESP_LOGI(TAG, "card FAT: %llu MiB total, %llu MiB free",
             (unsigned long long)(total_bytes / (1024U * 1024U)),
             (unsigned long long)(free_bytes / (1024U * 1024U)));
    if (free_bytes < (uint64_t)BENCH_BYTES + BENCH_FREE_MARGIN) {
        ESP_LOGE(TAG, "FAIL: need at least %u MiB free for scratch benchmark",
                 (BENCH_BYTES + BENCH_FREE_MARGIN) / (1024U * 1024U));
        goto cleanup;
    }

    expected = heap_caps_malloc(BENCH_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    actual = heap_caps_malloc(BENCH_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!expected || !actual) {
        ESP_LOGE(TAG, "FAIL: benchmark buffer allocation");
        goto cleanup;
    }

    int fd = -1;
    for (int attempt = 0; attempt < 8; ++attempt) {
        snprintf(path, sizeof path, "/sdcard/B%07lX.TMP",
                 (unsigned long)(esp_random() & 0x0fffffff));
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) {
        ESP_LOGE(TAG, "FAIL: exclusive scratch-file create: errno %d", errno);
        goto cleanup;
    }
    created = true;
    file = fdopen(fd, "wb");
    if (!file) {
        ESP_LOGE(TAG, "FAIL: scratch-file stream: errno %d", errno);
        close(fd);
        goto cleanup;
    }

    ESP_LOGI(TAG, "benchmark: %u MiB scratch file %s; only this file may be removed",
             BENCH_BYTES / (1024U * 1024U), path);
    uint32_t pattern = 0x13579bdf;
    int64_t write_start = esp_timer_get_time();
    for (size_t offset = 0; offset < BENCH_BYTES; offset += BENCH_CHUNK) {
        fill_pattern(expected, BENCH_CHUNK, &pattern);
        size_t written = fwrite(expected, 1, BENCH_CHUNK, file);
        if (written != BENCH_CHUNK) {
            ESP_LOGE(TAG, "FAIL: write at %u: %u/%u bytes, ferror %d errno %d",
                     (unsigned)offset, (unsigned)written, BENCH_CHUNK,
                     ferror(file), errno);
            goto cleanup;
        }
        if (((offset + BENCH_CHUNK) % (1024U * 1024U)) == 0) vTaskDelay(1);
    }
    if (fclose(file) != 0) {
        file = NULL;
        ESP_LOGE(TAG, "FAIL: write close: errno %d", errno);
        goto cleanup;
    }
    file = NULL;
    int64_t write_us = esp_timer_get_time() - write_start;

    file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "FAIL: read open: errno %d", errno);
        goto cleanup;
    }
    pattern = 0x13579bdf;
    int64_t read_start = esp_timer_get_time();
    for (size_t offset = 0; offset < BENCH_BYTES; offset += BENCH_CHUNK) {
        size_t amount = fread(actual, 1, BENCH_CHUNK, file);
        if (amount != BENCH_CHUNK) {
            ESP_LOGE(TAG, "FAIL: read at %u: %u/%u bytes, ferror %d errno %d",
                     (unsigned)offset, (unsigned)amount, BENCH_CHUNK,
                     ferror(file), errno);
            goto cleanup;
        }
        fill_pattern(expected, BENCH_CHUNK, &pattern);
        if (memcmp(expected, actual, BENCH_CHUNK) != 0) {
            ESP_LOGE(TAG, "FAIL: data mismatch at offset %u", (unsigned)offset);
            goto cleanup;
        }
        if (((offset + BENCH_CHUNK) % (1024U * 1024U)) == 0) vTaskDelay(1);
    }
    if (fclose(file) != 0) {
        file = NULL;
        ESP_LOGE(TAG, "FAIL: read close: errno %d", errno);
        goto cleanup;
    }
    file = NULL;
    int64_t read_us = esp_timer_get_time() - read_start;

    ESP_LOGI(TAG, "PASS: %u MiB verified; write %llu KiB/s (%lld ms), read %llu KiB/s (%lld ms)",
             BENCH_BYTES / (1024U * 1024U),
             kib_per_second(write_us), (long long)(write_us / 1000),
             kib_per_second(read_us), (long long)(read_us / 1000));
    ok = true;

cleanup:
    if (file) fclose(file);
    free(expected);
    free(actual);
    if (created) {
        if (ok) {
            if (remove(path) != 0) {
                ESP_LOGE(TAG, "FAIL: could not remove own scratch file %s: errno %d", path, errno);
                ok = false;
            } else {
                ESP_LOGI(TAG, "removed scratch file %s", path);
            }
        } else {
            ESP_LOGW(TAG, "scratch file retained after failure for inspection: %s", path);
        }
    }
    err = esp_vfs_fat_sdcard_unmount("/sdcard", card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: SD unmount: %s", esp_err_to_name(err));
        ok = false;
    }
    return ok;
}
