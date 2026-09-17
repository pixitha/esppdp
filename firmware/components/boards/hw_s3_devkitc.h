// Hardware definition for Espressif ESP32-S3-DevKitC-1 N16R8.
// The DevKitC has no onboard LCD or SD socket; these pins are reserved for
// the external IE15 display and SPI microSD wiring used by the Fuzzball port.

#define ESP32_S3_DEVKITC

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

#define SD_SPI
#define SD_SPI_HOST SPI3_HOST
#define SD_SPI_DMA SPI_DMA_CH_AUTO
#define SD_SPI_CS 5
#define SD_SPI_MOSI 13
#define SD_SPI_SCLK 14
#define SD_SPI_MISO 4
