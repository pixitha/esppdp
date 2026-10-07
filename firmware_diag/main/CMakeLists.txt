idf_component_register(SRCS "main.c" "sd_bench.c" INCLUDE_DIRS "."
                       REQUIRES led_strip spi_flash esp_psram heap freertos log oled_ssd1306
                                fatfs sdmmc esp_driver_sdmmc esp_timer)
