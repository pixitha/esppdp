#include "oled_ssd1306.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "pixelfont_5x5_thin.h"

#define TAG "oled_ssd1306"

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static uint8_t s_buffer[OLED_SSD1306_WIDTH * OLED_SSD1306_HEIGHT / 8];
static uint8_t s_console_x;
static uint8_t s_console_y;
static uint8_t s_device_address;
static bool s_ready;

static esp_err_t transmit(const uint8_t *data, size_t length)
{
    return i2c_master_transmit(s_device, data, length, 1000);
}

static esp_err_t command(uint8_t value)
{
    const uint8_t packet[] = {0x00, value};
    return transmit(packet, sizeof(packet));
}

static esp_err_t command_list(const uint8_t *values, size_t count)
{
    uint8_t packet[17];
    while (count != 0) {
        size_t chunk = count > 16 ? 16 : count;
        packet[0] = 0x00;
        memcpy(&packet[1], values, chunk);
        ESP_RETURN_ON_ERROR(transmit(packet, chunk + 1), TAG, "command transfer failed");
        values += chunk;
        count -= chunk;
    }
    return ESP_OK;
}

static esp_err_t write_buffer(void)
{
    uint8_t packet[1 + 16];
    for (size_t offset = 0; offset < sizeof(s_buffer); offset += 16) {
        packet[0] = 0x40;
        memcpy(&packet[1], &s_buffer[offset], 16);
        ESP_RETURN_ON_ERROR(transmit(packet, sizeof(packet)), TAG, "display data transfer failed");
    }
    return ESP_OK;
}

static void glyph(char c, uint8_t out[6])
{
    memcpy(out, pixelfont_5x5_thin_ascii[(unsigned char)c - 0x20], 6);
}

static esp_err_t flush_console(void)
{
    ESP_RETURN_ON_ERROR(command(0x21), TAG, "column setup failed");
    ESP_RETURN_ON_ERROR(command(0x00), TAG, "column start failed");
    ESP_RETURN_ON_ERROR(command(0x7F), TAG, "column end failed");
    ESP_RETURN_ON_ERROR(command(0x22), TAG, "page setup failed");
    ESP_RETURN_ON_ERROR(command(0x00), TAG, "page start failed");
    ESP_RETURN_ON_ERROR(command(0x07), TAG, "page end failed");
    return write_buffer();
}

static void scroll_console_if_needed(void)
{
    if (s_console_y < 8) return;
    memmove(s_buffer, s_buffer + OLED_SSD1306_WIDTH,
            sizeof(s_buffer) - OLED_SSD1306_WIDTH);
    memset(s_buffer + sizeof(s_buffer) - OLED_SSD1306_WIDTH, 0,
           OLED_SSD1306_WIDTH);
    s_console_y = 7;
}

esp_err_t oled_ssd1306_init(const oled_ssd1306_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "configuration is required");
    if (s_ready) return ESP_OK;
    i2c_master_bus_config_t bus_config = {
        .i2c_port = config->port,
        .sda_io_num = config->sda_gpio,
        .scl_io_num = config->scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    uint8_t address = config->address;
    if (address == 0) {
        if (i2c_master_probe(s_bus, 0x3C, 100) == ESP_OK) address = 0x3C;
        else if (i2c_master_probe(s_bus, 0x3D, 100) == ESP_OK) address = 0x3D;
        else {
            ESP_LOGE(TAG, "No OLED ACK at 0x3C or 0x3D");
            return ESP_ERR_NOT_FOUND;
        }
    }
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = config->clock_hz ? config->clock_hz : 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &device_config, &s_device), TAG, "OLED device setup failed");
    static const uint8_t init[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
        0xAF,
    };
    ESP_RETURN_ON_ERROR(command_list(init, sizeof(init)), TAG, "OLED initialization failed");
    s_device_address = device_config.device_address;
    s_ready = true;
    s_console_x = 0;
    s_console_y = 0;
    memset(s_buffer, 0, sizeof(s_buffer));
    ESP_LOGI(TAG, "SSD1306 OLED ready at I2C address 0x%02x", device_config.device_address);
    return ESP_OK;
}

bool oled_ssd1306_get_address(uint8_t *address)
{
    if (!s_ready || !address) return false;
    *address = s_device_address;
    return true;
}

esp_err_t oled_ssd1306_scan_i2c(uint8_t *addresses, size_t capacity,
                                size_t *found_count, uint8_t *failed_address)
{
    ESP_RETURN_ON_FALSE(s_bus != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "I2C bus is not initialized");
    ESP_RETURN_ON_FALSE(found_count != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "found count is required");
    ESP_RETURN_ON_FALSE(addresses != NULL || capacity == 0, ESP_ERR_INVALID_ARG,
                        TAG, "address buffer is required");
    *found_count = 0;
    if (failed_address) *failed_address = 0xff;

    /* Skip reserved 7-bit ranges; probes send only address + START/STOP. */
    for (uint16_t address = 0x08; address <= 0x77; ++address) {
        esp_err_t err = i2c_master_probe(s_bus, address, 20);
        if (err == ESP_ERR_NOT_FOUND) continue;
        if (err != ESP_OK) {
            if (failed_address) *failed_address = (uint8_t)address;
            return err;
        }
        if (*found_count >= capacity) return ESP_ERR_INVALID_SIZE;
        addresses[(*found_count)++] = (uint8_t)address;
    }
    return ESP_OK;
}

esp_err_t oled_ssd1306_render_lines(const char *const lines[], size_t line_count)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "OLED is not initialized");
    ESP_RETURN_ON_FALSE(lines != NULL || line_count == 0,
                        ESP_ERR_INVALID_ARG, TAG, "line array is required");

    memset(s_buffer, 0, sizeof(s_buffer));
    s_console_x = 0;
    s_console_y = 0;
    const size_t rows = line_count < OLED_SSD1306_HEIGHT / 8
        ? line_count : OLED_SSD1306_HEIGHT / 8;
    const size_t columns = OLED_SSD1306_WIDTH / 6;
    for (size_t row = 0; row < rows; ++row) {
        if (!lines[row]) continue;
        for (size_t column = 0; column < columns && lines[row][column]; ++column) {
            unsigned char ch = (unsigned char)lines[row][column];
            if (ch < 0x20 || ch > 0x7e) ch = '?';
            uint8_t columns_data[6];
            glyph((char)ch, columns_data);
            memcpy(&s_buffer[row * OLED_SSD1306_WIDTH + column * 6],
                   columns_data, sizeof columns_data);
        }
    }
    return flush_console();
}

esp_err_t oled_ssd1306_clear(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
    ESP_RETURN_ON_ERROR(command(0x21), TAG, "column setup failed");
    ESP_RETURN_ON_ERROR(command(0x00), TAG, "column start failed");
    ESP_RETURN_ON_ERROR(command(0x7F), TAG, "column end failed");
    ESP_RETURN_ON_ERROR(command(0x22), TAG, "page setup failed");
    ESP_RETURN_ON_ERROR(command(0x00), TAG, "page start failed");
    ESP_RETURN_ON_ERROR(command(0x07), TAG, "page end failed");
    return write_buffer();
}

void oled_ssd1306_console_clear(void)
{
    if (!s_ready) return;
    memset(s_buffer, 0, sizeof(s_buffer));
    s_console_x = 0;
    s_console_y = 0;
    (void)flush_console();
}

void oled_ssd1306_console_putc(char c)
{
    if (!s_ready) return;
    if (c == '\r') return;
    if (c == '\n') {
        s_console_x = 0;
        s_console_y++;
        scroll_console_if_needed();
    } else if (c == '\b') {
        if (s_console_x >= 6) s_console_x -= 6;
    } else if (c >= 0x20 && c <= 0x7E) {
        if (s_console_x + 6 > OLED_SSD1306_WIDTH) {
            s_console_x = 0;
            s_console_y++;
            scroll_console_if_needed();
        }
        uint8_t columns[6];
        glyph(c, columns);
        /*
         * PixelFonts is rendered into the existing 6x8 terminal cell. The
         * font's glyph data occupies up to six columns and one SSD1306 page.
         */
        const size_t page = s_console_y;
        for (uint8_t x = 0; x < 6; ++x) {
            s_buffer[page * OLED_SSD1306_WIDTH + s_console_x + x] = columns[x];
        }
        s_console_x += 6;
    }
    (void)flush_console();
}

esp_err_t oled_ssd1306_test_pattern(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
    for (uint16_t y = 0; y < OLED_SSD1306_HEIGHT; ++y) {
        for (uint16_t x = 0; x < OLED_SSD1306_WIDTH; ++x) {
            if (x < 2 || x >= OLED_SSD1306_WIDTH - 2 || y < 2 || y >= OLED_SSD1306_HEIGHT - 2 ||
                ((x / 8 + y / 8) & 1) == 0) {
                s_buffer[(y / 8) * OLED_SSD1306_WIDTH + x] |= (uint8_t)(1U << (y & 7));
            }
        }
    }
    ESP_RETURN_ON_ERROR(command(0x21), TAG, "column setup failed");
    ESP_RETURN_ON_ERROR(command(0x00), TAG, "column start failed");
    ESP_RETURN_ON_ERROR(command(0x7F), TAG, "column end failed");
    ESP_RETURN_ON_ERROR(command(0x22), TAG, "page setup failed");
    ESP_RETURN_ON_ERROR(command(0x00), TAG, "page start failed");
    ESP_RETURN_ON_ERROR(command(0x07), TAG, "page end failed");
    return write_buffer();
}
