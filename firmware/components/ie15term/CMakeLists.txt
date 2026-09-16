
idf_component_register(SRCS "ie15lcd.c"
                    REQUIRES esp_driver_spi esp_driver_gpio esp_driver_uart esp_ringbuf vfs
                    INCLUDE_DIRS "." "../boards"
                    EMBED_FILES chargen.bin)
