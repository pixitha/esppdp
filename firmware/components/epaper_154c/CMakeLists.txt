idf_component_register(
    SRCS "epaper_154c.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_spi esp_timer
)
