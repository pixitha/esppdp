// Hardware definition for Espressif ESP32-S3-DevKitC-1 N16R8.
// External 3.3 V SDMMC breakout: CLK=14, CMD=13, D0=4, D1=6, D2=15, D3=5.
// The DevKitC itself has no onboard LCD or SD socket.

#define ESP32_S3_DEVKITC
#define NO_DISPLAY
/* This board's addressable RGB LED is wired to GPIO48. */
#define ESPPDP_RGB_LED_GPIO 48

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320
#define DISPLAY_CHW 4
#define DISPLAY_CHH 10
#define DISPLAY_BCKL GPIO_NUM_NC
#define DISPLAY_BCKL_ON 1
#define DISPLAY_BCKL_OFF 0
#define DISPLAY_INVERT 0
#define DISPLAY_ROTATE 1

#define DISPLAY_SPI
#define DISPLAY_SPI_HOST SPI2_HOST
#define DISPLAY_SPI_DMA SPI_DMA_CH_AUTO
#define DISPLAY_SPI_MODE 0
#define DISPLAY_SPI_MISO GPIO_NUM_NC
#define DISPLAY_SPI_MOSI 11
#define DISPLAY_SPI_SCLK 12
#define DISPLAY_SPI_CS 10
#define DISPLAY_SPI_DC 9
#define DISPLAY_SPI_RST GPIO_NUM_NC
#define DISPLAY_SPI_HZ 24000000

#define SD_MMC
#define SD_MMC_CLK GPIO_NUM_14
#define SD_MMC_CMD GPIO_NUM_13
#define SD_MMC_D0 GPIO_NUM_4
#define SD_MMC_D1 GPIO_NUM_6
#define SD_MMC_D2 GPIO_NUM_15
#define SD_MMC_D3 GPIO_NUM_5
#define SD_MMC_WIDTH 4
#define SD_MMC_FREQ_KHZ 20000
