# Reuse the validated SSD1306 implementation from the main ESP32 firmware.
set(OLED_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../firmware/components/oled_ssd1306")
idf_component_register(
    SRCS "${OLED_COMPONENT_DIR}/oled_ssd1306.c"
    INCLUDE_DIRS "${OLED_COMPONENT_DIR}/include"
    REQUIRES esp_driver_i2c
)
