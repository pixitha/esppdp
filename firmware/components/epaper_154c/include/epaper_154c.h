#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#define EPAPER_154C_WIDTH 200
#define EPAPER_154C_HEIGHT 200
#define EPAPER_154C_PLANE_SIZE ((EPAPER_154C_WIDTH * EPAPER_154C_HEIGHT) / 8)

/* Suggested ESP32-S3 wiring: MOSI=11, SCLK=12, CS=10, DC=9, RST=8, BUSY=7. */

typedef struct {
    spi_host_device_t host;
    gpio_num_t mosi;
    gpio_num_t sclk;
    gpio_num_t cs;
    gpio_num_t dc;
    gpio_num_t rst;
    gpio_num_t busy;
    int clock_hz;
    spi_dma_chan_t dma_chan;
} epaper_154c_config_t;

esp_err_t epaper_154c_init(const epaper_154c_config_t *config);
esp_err_t epaper_154c_clear(void);
esp_err_t epaper_154c_refresh(const uint8_t *black, const uint8_t *color);
esp_err_t epaper_154c_sleep(void);
bool epaper_154c_is_initialized(void);
