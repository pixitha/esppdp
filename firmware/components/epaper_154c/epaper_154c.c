/*
 * Native ESP-IDF adaptation of the GxEPD2_154c GDEW0154Z04 driver.
 *
 * Copyright (C) 2026 Kyle Duren and contributors.
 * The waveform tables and panel command sequence are derived from
 * GxEPD2 by Jean-Marc Zingg, which is licensed under the GNU GPL v3.
 * This file is therefore distributed under the GNU GPL v3 or later.
 */

#include "epaper_154c.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "epaper_154c"
#define DEFAULT_CLOCK_HZ 20000000
#define BUSY_TIMEOUT_MS 20000

static spi_device_handle_t s_device;
static epaper_154c_config_t s_config;
static bool s_initialized;
static bool s_power_on;

/* These waveform tables are from GxEPD2_154c for GDEW0154Z04/IL0376F. */
static const uint8_t s_lut_20_vcom0[] = {0x0E,0x14,0x01,0x0A,0x06,0x04,0x0A,0x0A,0x0F,0x03,0x03,0x0C,0x06,0x0A,0x00};
static const uint8_t s_lut_21_w[]     = {0x0E,0x14,0x01,0x0A,0x46,0x04,0x8A,0x4A,0x0F,0x83,0x43,0x0C,0x86,0x0A,0x04};
static const uint8_t s_lut_22_b[]     = {0x0E,0x14,0x01,0x8A,0x06,0x04,0x8A,0x4A,0x0F,0x83,0x43,0x0C,0x06,0x4A,0x04};
static const uint8_t s_lut_23_g1[]    = {0x8E,0x94,0x01,0x8A,0x06,0x04,0x8A,0x4A,0x0F,0x83,0x43,0x0C,0x06,0x0A,0x04};
static const uint8_t s_lut_24_g2[]    = {0x8E,0x94,0x01,0x8A,0x06,0x04,0x8A,0x4A,0x0F,0x83,0x43,0x0C,0x06,0x0A,0x04};
static const uint8_t s_lut_25_vcom1[] = {0x03,0x1D,0x01,0x01,0x08,0x23,0x37,0x37,0x01,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t s_lut_26_red0[]  = {0x83,0x5D,0x01,0x81,0x48,0x23,0x77,0x77,0x01,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t s_lut_27_red1[]  = {0x03,0x1D,0x01,0x01,0x08,0x23,0x37,0x37,0x01,0x00,0x00,0x00,0x00,0x00,0x00};

static esp_err_t transfer(bool data_mode, const uint8_t *buf, size_t len)
{
    gpio_set_level(s_config.dc, data_mode ? 1 : 0);
    spi_transaction_t transaction = {
        .length = len * 8,
        .tx_buffer = buf,
    };
    return spi_device_polling_transmit(s_device, &transaction);
}

static esp_err_t command(uint8_t value)
{
    return transfer(false, &value, 1);
}

static esp_err_t data(const uint8_t *buf, size_t len)
{
    while (len != 0) {
        size_t chunk = len > 1024 ? 1024 : len;
        ESP_RETURN_ON_ERROR(transfer(true, buf, chunk), TAG, "SPI data transfer failed");
        buf += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

static esp_err_t data_byte(uint8_t value)
{
    return data(&value, 1);
}

static esp_err_t wait_busy(const char *operation, uint32_t timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (gpio_get_level(s_config.busy) == 0) {
        if (esp_timer_get_time() >= deadline) {
            ESP_LOGE(TAG, "%s: BUSY timeout", operation);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

static esp_err_t power_on(void)
{
    if (!s_power_on) {
        ESP_RETURN_ON_ERROR(command(0x04), TAG, "power-on command failed");
        ESP_RETURN_ON_ERROR(wait_busy("power-on", 5000), TAG, "power-on timeout");
        s_power_on = true;
    }
    return ESP_OK;
}

static esp_err_t init_display(void)
{
    gpio_set_level(s_config.rst, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(s_config.rst, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "BUSY level before init command: %d", gpio_get_level(s_config.busy));
    ESP_RETURN_ON_ERROR(command(0x01), TAG, "power setting command failed");
    const uint8_t power[] = {0x07, 0x00, 0x08, 0x00};
    ESP_RETURN_ON_ERROR(data(power, sizeof(power)), TAG, "power setting failed");
    ESP_RETURN_ON_ERROR(command(0x06), TAG, "booster command failed");
    const uint8_t booster[] = {0x07, 0x07, 0x07};
    ESP_RETURN_ON_ERROR(data(booster, sizeof(booster)), TAG, "booster setting failed");
    ESP_RETURN_ON_ERROR(power_on(), TAG, "power-on failed");
    ESP_RETURN_ON_ERROR(command(0x00), TAG, "panel setting command failed");
    ESP_RETURN_ON_ERROR(data_byte(0xCF), TAG, "panel setting failed");
    ESP_RETURN_ON_ERROR(command(0x50), TAG, "VCOM command failed");
    ESP_RETURN_ON_ERROR(data_byte(0x37), TAG, "VCOM setting failed");
    ESP_RETURN_ON_ERROR(command(0x30), TAG, "PLL command failed");
    ESP_RETURN_ON_ERROR(data_byte(0x39), TAG, "PLL setting failed");
    ESP_RETURN_ON_ERROR(command(0x61), TAG, "resolution command failed");
    const uint8_t resolution[] = {0xC8, 0x00, 0xC8};
    ESP_RETURN_ON_ERROR(data(resolution, sizeof(resolution)), TAG, "resolution setting failed");
    ESP_RETURN_ON_ERROR(command(0x82), TAG, "VCOM DC command failed");
    return data_byte(0x0E);
}

static esp_err_t load_full_waveform(void)
{
    const struct { uint8_t command; const uint8_t *lut; size_t length; } tables[] = {
        {0x20, s_lut_20_vcom0, sizeof(s_lut_20_vcom0)},
        {0x21, s_lut_21_w, sizeof(s_lut_21_w)},
        {0x22, s_lut_22_b, sizeof(s_lut_22_b)},
        {0x23, s_lut_23_g1, sizeof(s_lut_23_g1)},
        {0x24, s_lut_24_g2, sizeof(s_lut_24_g2)},
        {0x25, s_lut_25_vcom1, sizeof(s_lut_25_vcom1)},
        {0x26, s_lut_26_red0, sizeof(s_lut_26_red0)},
        {0x27, s_lut_27_red1, sizeof(s_lut_27_red1)},
    };
    for (size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); ++i) {
        ESP_RETURN_ON_ERROR(command(tables[i].command), TAG, "LUT command failed");
        ESP_RETURN_ON_ERROR(data(tables[i].lut, tables[i].length), TAG, "LUT transfer failed");
    }
    return ESP_OK;
}

static esp_err_t update(void)
{
    ESP_RETURN_ON_ERROR(command(0x12), TAG, "refresh command failed");
    return wait_busy("refresh", BUSY_TIMEOUT_MS);
}

esp_err_t epaper_154c_init(const epaper_154c_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "configuration is required");
    ESP_RETURN_ON_FALSE(config->rst >= 0 && config->busy >= 0, ESP_ERR_INVALID_ARG, TAG, "RST and BUSY are required");
    s_config = *config;
    if (s_config.clock_hz <= 0) s_config.clock_hz = DEFAULT_CLOCK_HZ;
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << s_config.cs) | (1ULL << s_config.dc) | (1ULL << s_config.rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "control GPIO setup failed");
    gpio_config_t busy = {
        .pin_bit_mask = 1ULL << s_config.busy,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&busy), TAG, "BUSY GPIO setup failed");
    gpio_set_level(s_config.cs, 1);
    gpio_set_level(s_config.dc, 0);
    gpio_set_level(s_config.rst, 1);

    spi_bus_config_t bus = {
        .mosi_io_num = s_config.mosi,
        .miso_io_num = -1,
        .sclk_io_num = s_config.sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(s_config.host, &bus, s_config.dma_chan);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    spi_device_interface_config_t device = {
        .clock_speed_hz = s_config.clock_hz,
        .mode = 0,
        .spics_io_num = s_config.cs,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(s_config.host, &device, &s_device), TAG, "SPI device setup failed");
    s_initialized = false;
    s_power_on = false;
    ESP_LOGI(TAG, "GDEW0154Z04 200x200 driver ready at %d Hz", s_config.clock_hz);
    esp_err_t init_err = init_display();
    if (init_err == ESP_OK) s_initialized = true;
    return init_err;
}

esp_err_t epaper_154c_clear(void)
{
    static uint8_t white[EPAPER_154C_PLANE_SIZE];
    memset(white, 0xFF, sizeof(white));
    return epaper_154c_refresh(white, white);
}

esp_err_t epaper_154c_refresh(const uint8_t *black, const uint8_t *color)
{
    ESP_RETURN_ON_FALSE(s_initialized && black && color, ESP_ERR_INVALID_STATE, TAG, "driver or framebuffer unavailable");
    ESP_RETURN_ON_ERROR(load_full_waveform(), TAG, "waveform setup failed");
    ESP_RETURN_ON_ERROR(command(0x10), TAG, "black plane command failed");
    ESP_RETURN_ON_ERROR(data(black, EPAPER_154C_PLANE_SIZE), TAG, "black plane transfer failed");
    ESP_RETURN_ON_ERROR(command(0x13), TAG, "color plane command failed");
    ESP_RETURN_ON_ERROR(data(color, EPAPER_154C_PLANE_SIZE), TAG, "color plane transfer failed");
    return update();
}

esp_err_t epaper_154c_sleep(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "driver is not initialized");
    if (s_power_on) {
        ESP_RETURN_ON_ERROR(command(0x50), TAG, "power-off VCOM command failed");
        ESP_RETURN_ON_ERROR(data_byte(0x17), TAG, "power-off VCOM setting failed");
        ESP_RETURN_ON_ERROR(command(0x82), TAG, "power-off VCOM DC command failed");
        ESP_RETURN_ON_ERROR(data_byte(0x00), TAG, "power-off VCOM DC setting failed");
        ESP_RETURN_ON_ERROR(command(0x02), TAG, "power-off command failed");
        vTaskDelay(pdMS_TO_TICKS(1500));
        s_power_on = false;
    }
    ESP_RETURN_ON_ERROR(command(0x07), TAG, "hibernate command failed");
    return data_byte(0xA5);
}

bool epaper_154c_is_initialized(void)
{
    return s_initialized;
}
