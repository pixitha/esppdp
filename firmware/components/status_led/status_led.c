#include "sdkconfig.h"
#include "status_led.h"

#if CONFIG_ESPPDP_HW_S3_DEVKITC
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "esp_log.h"

/* v1.1 uses GPIO38; override this for the initial GPIO48 board. */
#ifndef ESPPDP_RGB_LED_GPIO
#define ESPPDP_RGB_LED_GPIO 38
#endif

static led_strip_handle_t strip;
static const char *TAG = "status_led";

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!strip) return;
    led_strip_set_pixel(strip, 0, r, g, b);
    led_strip_refresh(strip);
}

void status_led_init(void)
{
    led_strip_config_t cfg = {
        .strip_gpio_num = ESPPDP_RGB_LED_GPIO,
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
        ESP_LOGW(TAG, "RGB LED unavailable: %s", esp_err_to_name(err));
        strip = NULL;
        return;
    }
    set_rgb(0, 0, 0);
}

void status_led_read(void)  { set_rgb(0, 8, 0); }
void status_led_write(void) { set_rgb(12, 0, 0); }
void status_led_error(void) { set_rgb(0, 0, 12); }

#else
void status_led_init(void) {}
void status_led_read(void) {}
void status_led_write(void) {}
void status_led_error(void) {}
#endif
