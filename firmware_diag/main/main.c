#include <stdint.h>
#include <stdlib.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "oled_ssd1306.h"
#include "sd_bench.h"

static const char *TAG = "flashdiag";
#define FLASH_BYTES (16U * 1024U * 1024U)
#define CHUNK_BYTES (16U * 1024U)

static bool s_oled_ready;

static void screen_text(const char *text)
{
    if (!s_oled_ready) return;
    for (const char *p = text; *p != '\0'; ++p) oled_ssd1306_console_putc(*p);
}

static void screen_line(const char *text)
{
    screen_text(text);
    screen_text("\n");
}

static void screen_init(void)
{
    const oled_ssd1306_config_t config = {
        .port = I2C_NUM_0, .sda_gpio = 18, .scl_gpio = 17,
        .address = 0, .clock_hz = 400000,
    };
    esp_err_t err = oled_ssd1306_init(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OLED unavailable; continuing serial-only: %s", esp_err_to_name(err));
        return;
    }
    s_oled_ready = true;
    oled_ssd1306_console_clear();
    screen_line("ESP32-S3 DIAG");
    screen_line("OLED READY");
}

static void led_probe_pin(int gpio, const char *label, uint8_t r, uint8_t g, uint8_t b)
{
    ESP_LOGI(TAG, "LED probe init %s GPIO%d", label, gpio);
    led_strip_handle_t strip = NULL;
    led_strip_config_t cfg = {
        .strip_gpio_num = gpio,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = { .with_dma = false },
    };
    esp_err_t err = led_strip_new_rmt_device(&cfg, &rmt, &strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED probe %s GPIO%d: init failed: %s", label, gpio, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "LED probe START %s GPIO%d: color %02x%02x%02x for 3 seconds",
             label, gpio, (unsigned)r, (unsigned)g, (unsigned)b);
    err = led_strip_set_pixel(strip, 0, r, g, b);
    if (err != ESP_OK) ESP_LOGE(TAG, "LED probe %s GPIO%d: set pixel failed: %s", label, gpio, esp_err_to_name(err));
    err = led_strip_refresh(strip);
    if (err != ESP_OK) ESP_LOGE(TAG, "LED probe %s GPIO%d: refresh failed: %s", label, gpio, esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(3000));
    err = led_strip_clear(strip);
    if (err != ESP_OK) ESP_LOGE(TAG, "LED probe %s GPIO%d: clear failed: %s", label, gpio, esp_err_to_name(err));
    err = led_strip_refresh(strip);
    if (err != ESP_OK) ESP_LOGE(TAG, "LED probe %s GPIO%d: clear refresh failed: %s", label, gpio, esp_err_to_name(err));
    err = led_strip_del(strip);
    if (err != ESP_OK) ESP_LOGE(TAG, "LED probe %s GPIO%d: delete failed: %s", label, gpio, esp_err_to_name(err));
    ESP_LOGI(TAG, "LED probe END %s GPIO%d", label, gpio);
    vTaskDelay(pdMS_TO_TICKS(500));
}

static void led_probe(void)
{
    ESP_LOGI(TAG, "LED probe: discrete power/TX/RX indicators are not tested; testing confirmed addressable LED GPIO48");
    led_probe_pin(48, "confirmed-board", 32, 0, 0);
    ESP_LOGI(TAG, "LED probe complete: GPIO48 is the addressable LED data pin");
}

static bool flash_walk(void)
{
    uint8_t *buf = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) { ESP_LOGE(TAG, "FAIL: flash buffer allocation"); return false; }
    uint32_t checksum = 0;
    for (uint32_t off = 0; off < FLASH_BYTES; off += CHUNK_BYTES) {
        esp_err_t err = esp_flash_read(esp_flash_default_chip, buf, off, CHUNK_BYTES);
        if (err != ESP_OK) { ESP_LOGE(TAG, "FAIL: flash read 0x%08lx: %s", (unsigned long)off, esp_err_to_name(err)); free(buf); return false; }
        for (size_t i = 0; i < CHUNK_BYTES; ++i) checksum = (checksum << 5) - checksum + buf[i];
        if ((off & 0xfffffU) == 0) {
            ESP_LOGI(TAG, "flash walk: 0x%08lx / 0x%08x", (unsigned long)off, FLASH_BYTES);
            char status[24];
            snprintf(status, sizeof(status), "FLASH %02lX/%02lX MB",
                     (unsigned long)(off / 0x100000U),
                     (unsigned long)(FLASH_BYTES / 0x100000U));
            screen_line(status);
        }
        vTaskDelay(1);
    }
    free((void *)buf);
    ESP_LOGI(TAG, "PASS: full flash read (0x%08x bytes), checksum 0x%08lx", FLASH_BYTES, (unsigned long)checksum);
    screen_line("FLASH PASS");
    return true;
}

/* March C-: the standard six-element sequence used by XTRAMTEST/March-U
 * style diagnostics.  The buffer is volatile so the compiler cannot fold the
 * reads and writes away. */
static bool march_c_minus(const char *name, uint32_t caps, size_t bytes)
{
    char status[24];
    snprintf(status, sizeof(status), "%s TEST", name);
    screen_line(status);
    volatile uint32_t *buf = heap_caps_malloc(bytes, caps | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "FAIL: %s allocation (%lu bytes)", name, (unsigned long)bytes);
        snprintf(status, sizeof(status), "%s FAIL", name);
        screen_line(status);
        return false;
    }
    size_t words = bytes / sizeof(*buf);
    bool ok = true;
    for (size_t i = 0; i < words; ++i) buf[i] = 0;
    for (size_t i = 0; i < words && ok; ++i) { if (buf[i] != 0) ok = false; buf[i] = 0xffffffff; }
    for (size_t i = 0; i < words && ok; ++i) { if (buf[i] != 0xffffffff) ok = false; buf[i] = 0; }
    for (size_t i = words; i-- > 0 && ok;) { if (buf[i] != 0) ok = false; buf[i] = 0xffffffff; }
    for (size_t i = words; i-- > 0 && ok;) { if (buf[i] != 0xffffffff) ok = false; buf[i] = 0; }
    for (size_t i = words; i-- > 0 && ok;) if (buf[i] != 0) ok = false;
    free((void *)buf);
    ESP_LOGI(TAG, "%s March C-: %s (%lu bytes)", name, ok ? "PASS" : "FAIL", (unsigned long)bytes);
    snprintf(status, sizeof(status), "%s %s", name, ok ? "PASS" : "FAIL");
    screen_line(status);
    return ok;
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    size_t psram = esp_psram_get_size();
    ESP_LOGI(TAG, "ESP32-S3 diagnostic ROM");
    screen_init();
    ESP_LOGI(TAG, "chip revision %d, cores %d, PSRAM %lu bytes", chip.revision, chip.cores, (unsigned long)psram);
    ESP_LOGI(TAG, "free internal heap: %lu, free PSRAM heap: %lu", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    led_probe();
    bool flash_ok = flash_walk();
    bool int_ok = march_c_minus("internal RAM", MALLOC_CAP_INTERNAL, 64 * 1024);
    size_t psram_test = psram ? heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) : 0;
    bool ps_ok = psram_test ? march_c_minus("PSRAM usable", MALLOC_CAP_SPIRAM, psram_test & ~3U) : false;
    if (psram && psram_test < psram) ESP_LOGW(TAG, "PSRAM reserved/unavailable: %lu of %lu bytes", (unsigned long)(psram - psram_test), (unsigned long)psram);
    screen_line("SD BENCH 4B/20M");
    bool sd_ok = sd_bench_run();
    screen_line(sd_ok ? "SD BENCH PASS" : "SD BENCH FAIL");
    bool all_ok = flash_ok && int_ok && ps_ok && sd_ok;
    ESP_LOGI(TAG, "RESULT: %s", all_ok ? "PASS" : "FAIL");
    screen_line(all_ok ? "RESULT PASS" : "RESULT FAIL");
    screen_line("SERIAL HAS DETAILS");
    while (true) vTaskDelay(pdMS_TO_TICKS(10000));
}
