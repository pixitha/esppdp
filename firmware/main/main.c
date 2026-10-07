//Main code for pdp11 emulator thing
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

/*
 * ----------------------------------------------------------------------------
 * Added SPI connected SDcards and more generalized hw definition approach.
 * SvenMb
 * ----------------------------------------------------------------------------
 */


#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_random.h"

// load hardware specific definitions, use 'idf.py menuconfig' to set it 

#if CONFIG_ESPPDP_HW_WROVER_KIT
#include "hw_wrover.h"
#elif CONFIG_ESPPDP_HW_2432S028
#include "hw_2432S028.h"
#elif CONFIG_ESPPDP_HW_FINAL
#include "hw_final.h"
#elif CONFIG_ESPPDP_HW_S3_DEVKITC
#include "hw_s3_devkitc.h"
#else
#error ESPPDP Hardware not configured, use 'idf.py menuconfig' to choose
#endif

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "ie15lcd.h"
#include "nvs_flash.h"
#include "bthid.h"
#include "esp_vfs_fat.h"
#ifdef SD_MMC
  #include "driver/sdmmc_host.h"
#endif // SD_MMC
#ifdef SD_SPI
#include "driver/sdspi_host.h"
#endif // SD_SPI
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#include "wifi_if.h"
#include "status_led.h"
#include "wifid.h"
#include "wifid_iface.h"
#include "esp_timer.h"
#include "usb_keyboard.h"
#include "boot_menu.h"
#include "sim_hang_probe.h"
#include "epaper_154c.h"
#include "oled_ssd1306.h"
#if CONFIG_ESPPDP_SD_CARD_DIAG
#include "psa/crypto.h"
#endif

#define TAG "main"

#if CONFIG_ESPPDP_SD_CARD_DIAG
enum { SD_DIAG_SIZE = 1024 * 1024, SD_DIAG_CHUNK = 4096 };

static uint32_t sd_diag_next_byte(uint32_t *state)
{
	*state ^= *state << 13;
	*state ^= *state >> 17;
	*state ^= *state << 5;
	return *state & 0xff;
}

static void sd_card_diagnostic(void)
{
	char path[32];
	static uint8_t expected[SD_DIAG_CHUNK];
	static uint8_t actual[SD_DIAG_CHUNK];
	int fd = -1;
	for (int attempt = 0; attempt < 8; ++attempt) {
		snprintf(path, sizeof path, "/sdcard/T%07lX.TST",
			(unsigned long)(esp_random() & 0x0fffffff));
		fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (fd >= 0 || errno != EEXIST) break;
	}
	if (fd < 0) {
		ESP_LOGE(TAG, "SD diagnostic: exclusive scratch-file create failed: errno %d", errno);
		return;
	}
	FILE *file = fdopen(fd, "wb");
	if (!file) {
		ESP_LOGE(TAG, "SD diagnostic: scratch-file stream failed: errno %d", errno);
		close(fd);
		return;
	}
	uint32_t pattern = 0x13579bdf;
	int64_t write_start = esp_timer_get_time();
	for (size_t offset = 0; offset < SD_DIAG_SIZE; offset += sizeof expected) {
		for (size_t i = 0; i < sizeof expected; ++i)
			expected[i] = (uint8_t)sd_diag_next_byte(&pattern);
		if (fwrite(expected, 1, sizeof expected, file) != sizeof expected) {
			ESP_LOGE(TAG, "SD diagnostic: write failed at offset %u: errno %d",
				(unsigned)offset, errno);
			fclose(file);
			return;
		}
	}
	int close_result = fclose(file);
	int64_t write_us = esp_timer_get_time() - write_start;
	if (close_result != 0) {
		ESP_LOGE(TAG, "SD diagnostic: write close failed: errno %d", errno);
		return;
	}
	file = fopen(path, "rb");
	if (!file) {
		ESP_LOGE(TAG, "SD diagnostic: scratch-file read open failed: errno %d", errno);
		return;
	}
	pattern = 0x13579bdf;
	int64_t read_start = esp_timer_get_time();
	for (size_t offset = 0; offset < SD_DIAG_SIZE; offset += sizeof actual) {
		if (fread(actual, 1, sizeof actual, file) != sizeof actual) {
			ESP_LOGE(TAG, "SD diagnostic: read failed at offset %u: errno %d",
				(unsigned)offset, errno);
			fclose(file);
			return;
		}
		for (size_t i = 0; i < sizeof expected; ++i)
			expected[i] = (uint8_t)sd_diag_next_byte(&pattern);
		if (memcmp(expected, actual, sizeof actual) != 0) {
			ESP_LOGE(TAG, "SD diagnostic: readback mismatch at offset %u",
				(unsigned)offset);
			fclose(file);
			return;
		}
	}
	close_result = fclose(file);
	int64_t read_us = esp_timer_get_time() - read_start;
	if (close_result != 0) {
		ESP_LOGE(TAG, "SD diagnostic: read close failed: errno %d", errno);
		return;
	}
	ESP_LOGI(TAG, "SD diagnostic PASS: %u-byte scratch file %s; write %lld KiB/s, read %lld KiB/s",
		(unsigned)SD_DIAG_SIZE, path,
		(long long)((int64_t)SD_DIAG_SIZE * 1000000 / write_us / 1024),
		(long long)((int64_t)SD_DIAG_SIZE * 1000000 / read_us / 1024));
	if (remove(path) != 0)
		ESP_LOGW(TAG, "SD diagnostic: unable to remove scratch file %s: errno %d", path, errno);
}

static void sd_bos_image_probe(void)
{
	static const char path[] = "/sdcard/BOS6.IMG";
	static const char expected_sha256[] =
		"ef33a6cd1f8c4f35ade69e933f295d5675f2c2a5b269a13cfbf3fe82d11b911b";
	enum { EXPECTED_SIZE = 159334400, HASH_CHUNK = 4096 };
	static uint8_t buffer[HASH_CHUNK];
	struct stat info;
	if (stat(path, &info) != 0) {
		if (errno == ENOENT)
			ESP_LOGI(TAG, "BOS image probe pending: %s is not on the card", path);
		else
			ESP_LOGE(TAG, "BOS image probe: stat failed: errno %d", errno);
		return;
	}
	if (info.st_size != EXPECTED_SIZE) {
		ESP_LOGE(TAG, "BOS image probe: size %lld, expected %d bytes",
			(long long)info.st_size, EXPECTED_SIZE);
		return;
	}
	FILE *file = fopen(path, "rb");
	if (!file) {
		ESP_LOGE(TAG, "BOS image probe: read open failed: errno %d", errno);
		return;
	}
	psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
	psa_status_t crypto_status = psa_hash_setup(&hash, PSA_ALG_SHA_256);
	if (crypto_status != PSA_SUCCESS) {
		ESP_LOGE(TAG, "BOS image probe: SHA-256 setup failed: %d", (int)crypto_status);
		fclose(file);
		return;
	}
	ESP_LOGI(TAG, "BOS image probe: hashing %d bytes from %s (read-only)",
		EXPECTED_SIZE, path);
	int64_t start_us = esp_timer_get_time();
	size_t total = 0;
	while (total < EXPECTED_SIZE) {
		size_t amount = fread(buffer, 1, sizeof buffer, file);
		if (amount == 0) {
			ESP_LOGE(TAG, "BOS image probe: read stopped at %u: ferror %d errno %d",
				(unsigned)total, ferror(file), errno);
			psa_hash_abort(&hash);
			fclose(file);
			return;
		}
		crypto_status = psa_hash_update(&hash, buffer, amount);
		if (crypto_status != PSA_SUCCESS) {
			ESP_LOGE(TAG, "BOS image probe: SHA-256 update failed: %d", (int)crypto_status);
			psa_hash_abort(&hash);
			fclose(file);
			return;
		}
		total += amount;
		if (total % (1024 * 1024) == 0)
			vTaskDelay(1);
		if (total % (32 * 1024 * 1024) == 0)
			ESP_LOGI(TAG, "BOS image probe: %u of %d bytes", (unsigned)total, EXPECTED_SIZE);
	}
	if (fclose(file) != 0) {
		ESP_LOGE(TAG, "BOS image probe: close failed: errno %d", errno);
		psa_hash_abort(&hash);
		return;
	}
	uint8_t digest[32];
	size_t digest_length = 0;
	crypto_status = psa_hash_finish(&hash, digest, sizeof digest, &digest_length);
	if (crypto_status != PSA_SUCCESS || digest_length != sizeof digest) {
		ESP_LOGE(TAG, "BOS image probe: SHA-256 finish failed: %d", (int)crypto_status);
		psa_hash_abort(&hash);
		return;
	}
	char digest_hex[sizeof digest * 2 + 1];
	for (size_t i = 0; i < sizeof digest; ++i)
		snprintf(&digest_hex[i * 2], 3, "%02x", digest[i]);
	if (strcmp(digest_hex, expected_sha256) != 0) {
		ESP_LOGE(TAG, "BOS image probe: SHA-256 mismatch: %s", digest_hex);
		return;
	}
	ESP_LOGI(TAG, "BOS image probe PASS: size and SHA-256 match in %lld ms; no guest boot or image writes",
		(long long)((esp_timer_get_time() - start_us) / 1000));
}
#endif

#if CONFIG_ESPPDP_EPAPER_TEST
static void epaper_diagnostic(void)
{
	static uint8_t black[EPAPER_154C_PLANE_SIZE];
	static uint8_t color[EPAPER_154C_PLANE_SIZE];
	memset(black, 0xFF, sizeof(black));
	memset(color, 0xFF, sizeof(color));
	/* Black border and red horizontal bands make bit order/wiring visible. */
	for (uint16_t y = 0; y < EPAPER_154C_HEIGHT; ++y) {
		for (uint16_t x = 0; x < EPAPER_154C_WIDTH; ++x) {
			size_t index = (size_t)y * (EPAPER_154C_WIDTH / 8) + x / 8;
			uint8_t mask = (uint8_t)(0x80U >> (x & 7));
			if (x < 4 || x >= EPAPER_154C_WIDTH - 4 || y < 4 || y >= EPAPER_154C_HEIGHT - 4)
				black[index] &= (uint8_t)~mask;
			if ((y >= 72 && y < 88) || (y >= 112 && y < 128))
				color[index] &= (uint8_t)~mask;
		}
	}
	const epaper_154c_config_t config = {
		.host = SPI2_HOST,
		.mosi = GPIO_NUM_11,
		.sclk = GPIO_NUM_12,
		.cs = GPIO_NUM_10,
		.dc = GPIO_NUM_9,
		.rst = GPIO_NUM_8,
		.busy = GPIO_NUM_7,
		.clock_hz = 20000000,
		.dma_chan = SPI_DMA_CH_AUTO,
	};
	esp_err_t err = epaper_154c_init(&config);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "e-paper init failed: %s", esp_err_to_name(err));
		return;
	}
	err = epaper_154c_refresh(black, color);
	if (err != ESP_OK)
		ESP_LOGE(TAG, "e-paper diagnostic refresh failed: %s", esp_err_to_name(err));
	else
		ESP_LOGI(TAG, "e-paper diagnostic image refreshed");
}
#endif

#if CONFIG_ESPPDP_OLED_TEST
static void oled_diagnostic(void)
{
	const oled_ssd1306_config_t config = {
		.port = I2C_NUM_0,
		.sda_gpio = GPIO_NUM_18,
		.scl_gpio = GPIO_NUM_17,
		.address = 0,
		.clock_hz = 400000,
	};
	esp_err_t err = oled_ssd1306_init(&config);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "OLED init failed: %s", esp_err_to_name(err));
		return;
	}
	oled_ssd1306_console_clear();
	const char *ready = "OLED READY\n";
	for (const char *p = ready; *p; ++p) oled_ssd1306_console_putc(*p);
	ESP_LOGI(TAG, "OLED console initialized");
}
#endif

int main(int argc, char **argv);

#ifdef ESP_PLATFORM
static void simh_task(void *arg)
{
	(void)arg;
	char *args[] = {"simh", NULL};
	main(1, args);
	/* main() is intended to run forever; keep the task safe if it returns. */
	vTaskDelete(NULL);
}
#endif

#if CONFIG_HEAP_TRACING_STANDALONE
#include "esp_heap_trace.h"
#define NUM_RECORDS 100
static heap_trace_record_t trace_record[NUM_RECORDS]; // This buffer must be in internal RAM
#endif

//ESP-IDF doesn't implement nanosleep, but SIMH needs it. We implement it here
//using an esp_timer. (Note this code is not re-entrant, do not try to sleep
//from multiple threads, if you need a generic nanosleep implementation look
//elsewhere!)

static esp_timer_handle_t nanosleep_timer;
static TaskHandle_t nanosleep_task = NULL;

void nanosleep_callback(void *arg) {
	xTaskNotifyGive(nanosleep_task);
}

void nanosleep_init() {
	const esp_timer_create_args_t args={
		.callback=nanosleep_callback,
		.arg=NULL,
		.dispatch_method=ESP_TIMER_TASK
	};
	esp_timer_create(&args, &nanosleep_timer);
}

//note: not reentrant!
int nanosleep(const struct timespec *req, struct timespec *rem) {
	//Note: We don't have signals; no need to write to rem
	nanosleep_task=xTaskGetCurrentTaskHandle();
	uint64_t wait_us=req->tv_nsec/1000UL+req->tv_sec*1000000UL;
	esp_timer_start_once(nanosleep_timer, wait_us);
	ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
	return 0;
}



void app_main(void) {
	esp_err_t ret;
	/* Start each monitor session with a clean terminal viewport.  This is
	 * harmless on UARTs that do not interpret ANSI and keeps idf_monitor/screen
	 * from leaving the previous boot's output above the new session. */
	fputs("\033[2J\033[H", stdout);
	fflush(stdout);
	status_led_init();
#if CONFIG_HEAP_TRACING_STANDALONE
	ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );
#endif

	//Initialize NVS
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	ie15_init();

#if CONFIG_ESPPDP_EPAPER_TEST
	epaper_diagnostic();
#endif
#if CONFIG_ESPPDP_OLED_TEST
	oled_diagnostic();
#endif
	//We cheat here: as the buffer in the IE15 emu is only 8 bytes, the following routine
	//will only finish after the initialization is complete.
	const char signon[]="Initializing emulator...\r\n";
	for (const char *p=signon; *p!=0; p++) ie15_sendchar(*p);

#if !defined(SD_NONE)
	//Initialize SD-card, if possible
	ESP_LOGI(TAG,"Initialize SD-card");
	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = false,
		.max_files = 2,
		.allocation_unit_size = 16 * 1024
	};
	sdmmc_card_t* card;
  #if defined(SD_MMC)
	ESP_LOGI(TAG,"Create SDMMC_HOST");
	sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
	#if CONFIG_ESPPDP_HW_S3_DEVKITC
	/* Route SDMMC explicitly on S3; the Adafruit breakout supplies pull-ups. */
	host.max_freq_khz = SD_MMC_FREQ_KHZ;
	slot_config.width = SD_MMC_WIDTH;
	slot_config.clk = SD_MMC_CLK;
	slot_config.cmd = SD_MMC_CMD;
	slot_config.d0 = SD_MMC_D0;
	slot_config.d1 = SD_MMC_D1;
	slot_config.d2 = SD_MMC_D2;
	slot_config.d3 = SD_MMC_D3;
	ESP_LOGI(TAG, "SDMMC: %d-bit, %d kHz; CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d",
		SD_MMC_WIDTH, SD_MMC_FREQ_KHZ, SD_MMC_CLK, SD_MMC_CMD,
		SD_MMC_D0, SD_MMC_D1, SD_MMC_D2, SD_MMC_D3);
	#else
	// GPIOs 15, 2, 4, 12, 13 should have external 10k pull-ups.
	// Internal pull-ups are not sufficient. However, enabling internal pull-ups
	// does make a difference some boards, so we do that here.
	gpio_set_pull_mode(SD_MMC_CMD, GPIO_PULLUP_ONLY);	// CMD, needed in 4- and 1- line modes
	gpio_set_pull_mode(SD_MMC_D0, GPIO_PULLUP_ONLY);	// D0, needed in 4- and 1-line modes
	gpio_set_pull_mode(SD_MMC_D1, GPIO_PULLUP_ONLY);	// D1, needed in 4-line mode only
	gpio_set_pull_mode(SD_MMC_D2, GPIO_PULLUP_ONLY);	// D2, needed in 4-line mode only
	gpio_set_pull_mode(SD_MMC_D3, GPIO_PULLUP_ONLY);	// D3, needed in 4- and 1-line modes
	#endif
	ESP_LOGI(TAG,"Initialize VFS via SDMMC\n");
	ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
  #elif defined(SD_SPI)
	ESP_LOGI(TAG,"Create SDSPI_HOST\n");
	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	host.slot = SD_SPI_HOST;
	spi_bus_config_t bus_cfg = {
        	.mosi_io_num = SD_SPI_MOSI,
        	.miso_io_num = SD_SPI_MISO,
        	.sclk_io_num = SD_SPI_SCLK,
        	.quadwp_io_num = -1,
        	.quadhd_io_num = -1,
        	.max_transfer_sz = 4000,
	};
	ESP_LOGI(TAG,"Initialize SPI_BUS\n");
	// ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
	ret = spi_bus_initialize(host.slot, &bus_cfg, SD_SPI_DMA);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG,"Failed to initialize bus.");
		return;
	}    
	sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
	slot_config.gpio_cs = SD_SPI_CS;
	slot_config.host_id = host.slot;

	ESP_LOGI(TAG,"Initialize VFS via SDSPI\n");
	//ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
	ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
  #else
    #error SD_MMC or SD_SPI or SD_NONE must be defined
#endif // defined(SD_MMC) // defined(SD_SPI)
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "SD-card: Failed to mount filesystem.");
		const char noflopstr[]="No SD card. Trying boot from built-in floppy.\r\n";
		for (const char *p=noflopstr; *p!=0; p++) ie15_sendchar(*p);
	} else {
		sdmmc_card_print_info(stdout, card);
	}
#if CONFIG_ESPPDP_SD_CARD_DIAG
	if (ret == ESP_OK) {
		sd_card_diagnostic();
		sd_bos_image_probe();
	}
	ESP_LOGI(TAG, "SD diagnostic complete; SIMH not started");
	return;
#endif
#else
	ESP_LOGI(TAG,"No SD-Card defined");
#endif // defined(SD_NONE)

#if !defined(SPIFFS_NONE)
	//Mount spiffs. This contains (a) floppy image(s).
	esp_vfs_spiffs_conf_t conf = {
		.base_path = "/spiffs",
		.partition_label = NULL,
		.max_files = 2,
		.format_if_mount_failed = false
	};
	// Use settings defined above to initialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is an all-in-one convenience function.
	ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
		}
		return;
	}
	{
		size_t total = 0;
		size_t used = 0;
		ret = esp_spiffs_info(conf.partition_label, &total, &used);
		if (ret == ESP_OK) {
			ESP_LOGI(TAG, "SPIFFS: %u KiB used of %u KiB (%u KiB free)",
				(unsigned)(used / 1024), (unsigned)(total / 1024),
				(unsigned)((total - used) / 1024));
		} else {
			ESP_LOGW(TAG, "Unable to read SPIFFS capacity: %s", esp_err_to_name(ret));
		}
	}
#else
	ESP_LOGI(TAG,"No SPIFFS defined");
#endif // defined(SPIFFS_NONE)
	
	bthid_start();
	usb_keyboard_start();
	boot_menu_select();
	if (boot_menu_choice() == NULL) {
		ESP_LOGE(TAG, "No bootable disk image is available");
		return;
	}

	nanosleep_init();
	sim_hang_probe_start();

#if CONFIG_HEAP_TRACING_STANDALONE
	ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );
	ESP_ERROR_CHECK( heap_trace_start(HEAP_TRACE_LEAKS) );
#endif
	
	/* Keep the SIMH CPU loop off CPU0, where ESP-IDF services, UART, and USB
	 * host work run.  SIMH deliberately occupies CPU1. */
	BaseType_t task_status = xTaskCreatePinnedToCore(
		simh_task, "simh", 24 * 1024, NULL, 5, NULL, 1);
	if (task_status != pdPASS) {
		ESP_LOGE(TAG, "Unable to start SIMH task");
		return;
	}
	ESP_LOGI(TAG, "SIMH task started on CPU1");
}
