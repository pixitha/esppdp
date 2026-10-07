#pragma once

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
esp_err_t oled_ssd1306_test_pattern(void);
esp_err_t oled_ssd1306_clear(void);
void oled_ssd1306_console_clear(void);
void oled_ssd1306_console_putc(char c);
