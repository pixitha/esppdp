
idf_component_register(SRCS "ie15lcd.c"
                    REQUIRES esp_driver_spi esp_driver_gpio esp_driver_uart esp_ringbuf vfs oled_ssd1306
                    INCLUDE_DIRS "." "../boards"
                    EMBED_FILES chargen.bin)
