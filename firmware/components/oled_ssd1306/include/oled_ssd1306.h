#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#define OLED_SSD1306_WIDTH 128
#define OLED_SSD1306_HEIGHT 64

typedef struct {
    i2c_port_num_t port;
    int sda_gpio;
    int scl_gpio;
    uint8_t address;
    uint32_t clock_hz;
} oled_ssd1306_config_t;

esp_err_t oled_ssd1306_init(const oled_ssd1306_config_t *config);
/* Return the configured address once the OLED driver initialized successfully. */
bool oled_ssd1306_get_address(uint8_t *address);
/* Probe standard 7-bit I2C addresses on the initialized display bus. */
esp_err_t oled_ssd1306_scan_i2c(uint8_t *addresses, size_t capacity,
                                size_t *found_count, uint8_t *failed_address);
esp_err_t oled_ssd1306_render_lines(const char *const lines[], size_t line_count);
esp_err_t oled_ssd1306_test_pattern(void);
esp_err_t oled_ssd1306_clear(void);
void oled_ssd1306_console_clear(void);
void oled_ssd1306_console_putc(char c);
