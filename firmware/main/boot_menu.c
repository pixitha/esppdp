#include "boot_menu.h"
#include "sd_upload_portal.h"
#include "sdkconfig.h"
#include "sim_defs.h"
#ifdef BIT
#undef BIT
#endif
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "oled_ssd1306.h"
#include "sim_term.h"
#include "usb_keyboard.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define BOOT_MAX_ENTRIES 9
#define BOOT_TIMEOUT_DEFAULT_S 10
#define BOOT_TIMEOUT_MIN_S 5
#define BOOT_TIMEOUT_MAX_S 30
#define CONSOLE_LINE_TIMEOUT_US (120LL * 1000 * 1000)
#define BOS6_RD54_IMAGE_SIZE 159334400
#define BOOT_ID_MAX 272
#define BOOT_LABEL_MAX 32
#define I2C_SCAN_ADDRESS_COUNT 112

#define ANSI_RESET "\033[0m"
#define ANSI_CYAN "\033[1;36m"
#define ANSI_YELLOW "\033[1;33m"
#define ANSI_GREEN "\033[1;32m"
#define ANSI_DIM "\033[2m"

typedef struct {
    boot_choice_t boot;
    char stable_id[BOOT_ID_MAX];
    char label[BOOT_LABEL_MAX];
} boot_entry_t;

typedef struct {
    unsigned timeout_s;
    bool auto_start;
    bool show_post;
    char default_id[BOOT_ID_MAX];
} boot_settings_t;

static boot_entry_t entries[BOOT_MAX_ENTRIES];
static size_t entry_count;
static boot_choice_t choice;
static char choice_stable_id[BOOT_ID_MAX];
static bool skip_line_feed;
static bool entry_limit_reached;
static bool invalid_bos6_image;
static size_t invalid_rk05_count;
static size_t other_sd_file_count;
static bool oled_menu_ready;

enum { MENU_KEY_NONE = -1, MENU_KEY_UP = 0x100, MENU_KEY_DOWN };

static int poll_menu_key(void);
static void show_hardware_inventory(void);
static const char *chip_model_name(esp_chip_model_t model);
#if CONFIG_ESPPDP_BOOT_OLED
static void oled_render_setup_screen(const boot_settings_t *settings,
                                     size_t selected_row, size_t default_option,
                                     unsigned page);
#endif

static bool regular_file(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

static bool rk05_name(const char *name)
{
    size_t length = strlen(name);
    return name[0] != '.' && length > 5 &&
        strcasecmp(name + length - 5, ".RK05") == 0;
}

static int compare_entries(const void *left, const void *right)
{
    const boot_entry_t *a = left;
    const boot_entry_t *b = right;
    return strcmp(a->boot.path, b->boot.path);
}

static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void make_entry_identity(boot_entry_t *entry)
{
    const char *path = entry->boot.path;
    const char *base = path_basename(path);
    const char *id = NULL;
    const char *label = base;

    switch (entry->boot.profile) {
    case BOOT_BOS6_RD54:
        id = "fuzzball-bos6-rd54";
        label = "Fuzzball / RT-11";
        break;
    case BOOT_RK05:
        if (strncmp(path, "/spiffs/", 8) == 0) {
            id = "builtin-unix-v6-rk05";
            label = "Unix V6";
        } else {
            snprintf(entry->stable_id, sizeof entry->stable_id,
                     "sd-rk05:%s", base);
        }
        break;
    case BOOT_RA92:
        id = "sd-rq-ra92";
        label = "RQ / RA92 disk";
        break;
    case BOOT_RX:
        id = "builtin-rx-floppy";
        label = "RX floppy";
        break;
    }

    if (id)
        snprintf(entry->stable_id, sizeof entry->stable_id, "%s", id);
    size_t out = 0;
    while (label[out] && out + 1 < sizeof entry->label) {
        unsigned char ch = (unsigned char)label[out];
        entry->label[out] = (ch >= 0x20 && ch < 0x7f && ch != '\033')
            ? (char)ch : '?';
        ++out;
    }
    entry->label[out] = '\0';
}

static bool add_entry(boot_profile_t profile, const char *path)
{
    if (!regular_file(path)) return false;
    if (entry_count == BOOT_MAX_ENTRIES) {
        entry_limit_reached = true;
        return false;
    }
    boot_entry_t *entry = &entries[entry_count];
    memset(entry, 0, sizeof *entry);
    entry->boot.profile = profile;
    int length = snprintf(entry->boot.path, sizeof entry->boot.path, "%s", path);
    if (length < 0 || (size_t)length >= sizeof entry->boot.path) return false;
    make_entry_identity(entry);
    ++entry_count;
    return true;
}

static void add_sd_images(void)
{
    size_t first_sd = entry_count;
    DIR *dir = opendir("/sdcard");
    if (!dir) return;
    struct dirent *item;
    while ((item = readdir(dir)) != NULL) {
        if (item->d_name[0] == '.') continue;
        char path[sizeof choice.path];
        int length = snprintf(path, sizeof path, "/sdcard/%s", item->d_name);
        if (length < 0 || (size_t)length >= sizeof path) {
            ++other_sd_file_count;
            continue;
        }
        if (rk05_name(item->d_name)) {
            if (!add_entry(BOOT_RK05, path)) {
                struct stat info;
                if (stat(path, &info) == 0 && S_ISREG(info.st_mode) &&
                    info.st_size > 0)
                    entry_limit_reached = true;
                else
                    ++invalid_rk05_count;
            }
        } else if (strcasecmp(item->d_name, "BOS6.IMG") != 0 &&
                   strcasecmp(item->d_name, "rq.dsk") != 0 &&
                   regular_file(path)) {
            ++other_sd_file_count;
        }
    }
    closedir(dir);
    qsort(entries + first_sd, entry_count - first_sd,
          sizeof entries[0], compare_entries);
}

static void add_bos6_image(void)
{
    struct stat info;
    const char *path = "/sdcard/BOS6.IMG";
    if (stat(path, &info) != 0) return;
    if (!S_ISREG(info.st_mode) || info.st_size != BOS6_RD54_IMAGE_SIZE) {
        invalid_bos6_image = true;
        return;
    }
    add_entry(BOOT_BOS6_RD54, path);
}

static void discover_entries(void)
{
    entry_count = 0;
    entry_limit_reached = false;
    invalid_bos6_image = false;
    invalid_rk05_count = 0;
    other_sd_file_count = 0;
#if CONFIG_ESPPDP_BOOT_UNIX_V6
    add_entry(BOOT_RK05, "/spiffs/Unix_V6.RK05");
#endif
    add_bos6_image();
    add_sd_images();
    /* Preserve the older port's known RQ and floppy boot profiles too. */
    add_entry(BOOT_RA92, "/sdcard/rq.dsk");
    add_entry(BOOT_RX, "/spiffs/floppy.dsk");
}

static bool upload_cancel_requested(void)
{
    t_stat input = sim_poll_kbd();
    return (input & SCPE_KFLAG) &&
        ((input & 0xff) == 'q' || (input & 0xff) == 'Q');
}

static bool read_console_line(const char *prompt, char *buffer,
                              size_t capacity, bool mask_input)
{
    printf("%s", prompt);
    fflush(stdout);
    size_t length = 0;
    size_t excess = 0;
    int64_t deadline = esp_timer_get_time() + CONSOLE_LINE_TIMEOUT_US;
    for (;;) {
        t_stat input = sim_poll_kbd();
        if (input & SCPE_KFLAG) {
            int ch = input & 0xff;
            if (skip_line_feed) {
                skip_line_feed = false;
                if (ch == '\n') continue;
            }
            if (ch == '\r' || ch == '\n') {
                if (ch == '\r') skip_line_feed = true;
                putchar('\n');
                if (excess) {
                    puts("Input is too long; no value was saved.");
                    return false;
                }
                buffer[length] = '\0';
                return true;
            }
            if (ch == 0x03 || ch == 0x1b) {
                puts("\nCancelled.");
                return false;
            }
            if (ch == '\b' || ch == 0x7f) {
                if (excess) {
                    --excess;
                    fputs("\b \b", stdout);
                } else if (length) {
                    --length;
                    fputs("\b \b", stdout);
                }
                fflush(stdout);
                continue;
            }
            if (ch >= 0x20 && ch != 0x7f) {
                if (length + 1 < capacity)
                    buffer[length++] = (char)ch;
                else
                    ++excess;
                putchar(mask_input ? '*' : ch);
                fflush(stdout);
            }
        } else if (esp_timer_get_time() >= deadline) {
            puts("\nTimed out; no value was saved.");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : 1);
    }
}

static void configure_wifi_serial(void)
{
    char ssid[33] = {0};
    char password[64] = {0};
    char answer[8] = {0};
    if (!read_console_line(
            "Wi-Fi SSID (1-32 bytes; blank clears saved Wi-Fi; Ctrl-C cancels): ",
            ssid, sizeof ssid, false))
        return;

    if (ssid[0] == '\0') {
        if (!read_console_line("Clear saved Wi-Fi credentials? [y/N]: ",
                               answer, sizeof answer, false))
            return;
        if (answer[0] != 'y' && answer[0] != 'Y') {
            puts("Saved Wi-Fi was not changed.");
            return;
        }
        esp_err_t result = sd_upload_portal_clear_wifi_credentials();
        if (result == ESP_OK)
            puts("Saved Wi-Fi cleared; W will use the setup access point.");
        else
            printf("Could not clear Wi-Fi settings: %s\n", esp_err_to_name(result));
        return;
    }

    if (!read_console_line(
            "Wi-Fi password (8-63 bytes, or blank for open network; masked): ",
            password, sizeof password, true)) {
        memset(password, 0, sizeof password);
        return;
    }
    esp_err_t result = sd_upload_portal_save_wifi_credentials(ssid, password);
    memset(password, 0, sizeof password);
    if (result == ESP_OK)
        printf("Wi-Fi settings saved for '%s'. Press W to start the portal.\n", ssid);
    else if (result == ESP_ERR_INVALID_ARG)
        puts("Invalid Wi-Fi settings; password must be empty or 8-63 bytes.");
    else
        printf("Could not save Wi-Fi settings: %s\n", esp_err_to_name(result));
}

static size_t default_index(void)
{
    nvs_handle_t handle;
    char last_id[BOOT_ID_MAX];
    size_t length = sizeof last_id;
    if (nvs_open("bootmenu", NVS_READONLY, &handle) != ESP_OK) return 0;
    esp_err_t result = nvs_get_str(handle, "last_id", last_id, &length);
    if (result == ESP_OK) {
        for (size_t i = 0; i < entry_count; ++i) {
            if (strcmp(entries[i].stable_id, last_id) == 0) {
                nvs_close(handle);
                return i;
            }
        }
    }

    /* Migrate the older path preference when no stable ID matches. */
    char last_path[sizeof choice.path];
    length = sizeof last_path;
    result = nvs_get_str(handle, "last_path", last_path, &length);
    nvs_close(handle);
    if (result != ESP_OK) return 0;
    for (size_t i = 0; i < entry_count; ++i)
        if (strcmp(entries[i].boot.path, last_path) == 0) return i;
    return 0;
}

static void load_settings(boot_settings_t *settings)
{
    settings->timeout_s = BOOT_TIMEOUT_DEFAULT_S;
    settings->auto_start = true;
    settings->show_post = true;
    settings->default_id[0] = '\0';
    nvs_handle_t handle;
    if (nvs_open("bootmenu", NVS_READONLY, &handle) != ESP_OK) return;

    uint32_t timeout_s;
    if (nvs_get_u32(handle, "timeout_s", &timeout_s) == ESP_OK &&
        timeout_s >= BOOT_TIMEOUT_MIN_S && timeout_s <= BOOT_TIMEOUT_MAX_S)
        settings->timeout_s = timeout_s;
    uint8_t auto_start;
    if (nvs_get_u8(handle, "auto_start", &auto_start) == ESP_OK &&
        auto_start <= 1)
        settings->auto_start = auto_start != 0;
    uint8_t show_post;
    if (nvs_get_u8(handle, "show_post", &show_post) == ESP_OK && show_post <= 1)
        settings->show_post = show_post != 0;
    size_t length = sizeof settings->default_id;
    (void)nvs_get_str(handle, "default_id", settings->default_id, &length);
    nvs_close(handle);
}

static size_t configured_default_index(const boot_settings_t *settings)
{
    if (settings->default_id[0]) {
        for (size_t i = 0; i < entry_count; ++i)
            if (strcmp(entries[i].stable_id, settings->default_id) == 0)
                return i;
    }
    return default_index();
}

static bool save_settings(const boot_settings_t *settings)
{
    nvs_handle_t handle;
    if (nvs_open("bootmenu", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result = nvs_set_u32(handle, "timeout_s", settings->timeout_s);
    if (result == ESP_OK)
        result = nvs_set_u8(handle, "auto_start", settings->auto_start ? 1 : 0);
    if (result == ESP_OK)
        result = nvs_set_u8(handle, "show_post", settings->show_post ? 1 : 0);
    if (result == ESP_OK && settings->default_id[0])
        result = nvs_set_str(handle, "default_id", settings->default_id);
    else if (result == ESP_OK) {
        result = nvs_erase_key(handle, "default_id");
        if (result == ESP_ERR_NVS_NOT_FOUND) result = ESP_OK;
    }
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
}

static bool configure_system_settings(size_t current_default)
{
    boot_settings_t settings;
    load_settings(&settings);
    if (!settings.default_id[0] && entry_count)
        snprintf(settings.default_id, sizeof settings.default_id, "%s",
                 entries[current_default].stable_id);

    static const unsigned timeout_options[] = {5, 10, 15, 30};
    static const char *const page_names[] = {"Main", "Boot", "Storage", "Hardware", "Info"};
    unsigned page = 0;
    size_t selected_row = 0;
    bool dirty = false;
    for (;;) {
        printf("\033[2J\033[H" ANSI_CYAN
               "+--------------------------------------------------------------------+\n"
               "|                    SYSTEM CONFIGURATION                           |\n"
               "+--------------------------------------------------------------------+\n" ANSI_RESET);
        printf("  1 Main   2 Boot   3 Storage   4 Hardware   5 Info\n");
        printf("  SECTION: %s\n\n", page_names[page]);
        size_t default_option = configured_default_index(&settings);
        size_t timeout_option = 0;
        while (timeout_option + 1 < sizeof timeout_options / sizeof timeout_options[0] &&
               timeout_options[timeout_option] != settings.timeout_s)
            ++timeout_option;
        if (page == 0) {
            printf("  %s%c Show POST:          %s%s\n",
                   selected_row == 0 ? ANSI_YELLOW : "",
                   selected_row == 0 ? '>' : ' ',
                   settings.show_post ? "Enabled" : "Disabled",
                   selected_row == 0 ? ANSI_RESET : "");
            printf("  %s%c Auto Start:         %s%s\n",
                   selected_row == 1 ? ANSI_YELLOW : "",
                   selected_row == 1 ? '>' : ' ',
                   settings.auto_start ? "Enabled" : "Disabled",
                   selected_row == 1 ? ANSI_RESET : "");
            printf("  %s%c Restore Defaults%s\n",
                   selected_row == 2 ? ANSI_YELLOW : "",
                   selected_row == 2 ? '>' : ' ',
                   selected_row == 2 ? ANSI_RESET : "");
            printf("  %s%c Save & Exit%s\n",
                   selected_row == 3 ? ANSI_YELLOW : "",
                   selected_row == 3 ? '>' : ' ',
                   selected_row == 3 ? ANSI_RESET : "");
        } else if (page == 1) {
            printf("  %s%c Default Boot:       %-32.32s%s\n",
                   selected_row == 0 ? ANSI_YELLOW : "",
                   selected_row == 0 ? '>' : ' ',
                   entry_count ? entries[default_option].label : "No boot entries",
                   selected_row == 0 ? ANSI_RESET : "");
            printf("  %s%c Boot Timeout:       %u seconds%s\n",
                   selected_row == 1 ? ANSI_YELLOW : "",
                   selected_row == 1 ? '>' : ' ', settings.timeout_s,
                   selected_row == 1 ? ANSI_RESET : "");
            printf("  %s%c Save & Exit%s\n",
                   selected_row == 2 ? ANSI_YELLOW : "",
                   selected_row == 2 ? '>' : ' ',
                   selected_row == 2 ? ANSI_RESET : "");
        } else if (page == 2) {
            uint64_t total = 0, free_bytes = 0;
            if (esp_vfs_fat_info("/sdcard", &total, &free_bytes) == ESP_OK)
                printf("  SD card:       mounted, %" PRIu64 " MiB free / %" PRIu64 " MiB\n",
                       free_bytes / (1024 * 1024), total / (1024 * 1024));
            else
                printf("  SD card:       not mounted\n");
            size_t total_spiffs = 0, used_spiffs = 0;
            if (esp_spiffs_info(NULL, &total_spiffs, &used_spiffs) == ESP_OK)
                printf("  SPIFFS:        %u KiB free / %u KiB\n",
                       (unsigned)((total_spiffs - used_spiffs) / 1024),
                       (unsigned)(total_spiffs / 1024));
            else
                printf("  SPIFFS:        unavailable\n");
            printf("  %s%c Upload files:       open local SD portal%s\n",
                   selected_row == 0 ? ANSI_YELLOW : "",
                   selected_row == 0 ? '>' : ' ',
                   selected_row == 0 ? ANSI_RESET : "");
        } else if (page == 3) {
            printf("  ESP32 and configured peripheral status\n");
            printf("  %s%c Open Hardware Diagnostics%s\n",
                   selected_row == 0 ? ANSI_YELLOW : "",
                   selected_row == 0 ? '>' : ' ',
                   selected_row == 0 ? ANSI_RESET : "");
            printf("  Probes are read-only; unconfigured pins are not scanned.\n");
        } else {
            esp_chip_info_t chip;
            esp_chip_info(&chip);
            printf("  Fuzzball System BIOS / ESP-IDF %s\n", esp_get_idf_version());
            printf("  Board processor: %s rev %u, %u core(s)\n",
                   chip_model_name(chip.model), chip.revision, chip.cores);
            printf("  Setup values are stored in NVS; guest media is not changed.\n");
        }
        printf("\n  Current saved settings: %s\n", dirty ? "modified" : "unchanged");
        printf("\n  1-5: section  Up/Down J/K: move  Enter: open/change\n");
        printf("  S: save & exit   Q: discard & exit   W/H: portal/diagnostics\n");
        fflush(stdout);
#if CONFIG_ESPPDP_BOOT_OLED
        oled_render_setup_screen(&settings, selected_row, default_option, page);
#endif

        int ch = MENU_KEY_NONE;
        while (ch == MENU_KEY_NONE) {
            ch = poll_menu_key();
            if (ch == MENU_KEY_NONE)
                vTaskDelay(pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : 1);
        }
        if (skip_line_feed) {
            skip_line_feed = false;
            if (ch == '\n') continue;
        }
        if (ch >= '1' && ch <= '5') {
            page = (unsigned)(ch - '1');
            selected_row = 0;
        } else if (ch == MENU_KEY_UP || ch == 'k' || ch == 'K')
            selected_row = selected_row ? selected_row - 1 :
                page == 0 ? 3 : page == 1 ? 2 : 0;
        else if (ch == MENU_KEY_DOWN || ch == 'j' || ch == 'J') {
            size_t rows = page == 0 ? 4 : page == 1 ? 3 : page == 2 || page == 3 ? 1 : 0;
            if (rows) selected_row = (selected_row + 1) % rows;
        }
        else if (ch == 'q' || ch == 'Q' || ch == 0x1b)
            return false;
        else if (ch == 's' || ch == 'S' ||
                 ((ch == '\r' || ch == '\n') &&
                  ((page == 0 && selected_row == 3) ||
                   (page == 1 && selected_row == 2)))) {
            if (ch == '\r') skip_line_feed = true;
            if (!save_settings(&settings)) {
                puts("\nCould not save settings to NVS. Press a key to return.");
                while (poll_menu_key() == MENU_KEY_NONE)
                    vTaskDelay(pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : 1);
                return false;
            }
            return true;
        } else if ((ch == 'w' || ch == 'W' || ch == '\r' || ch == '\n') && page == 2) {
            esp_err_t result = sd_upload_portal_run(upload_cancel_requested);
            if (result != ESP_OK)
                printf("SD upload portal could not start: %s\n", esp_err_to_name(result));
        } else if ((ch == 'h' || ch == 'H' || ch == '\r' || ch == '\n') && page == 3) {
            show_hardware_inventory();
        } else if ((ch == '\r' || ch == '\n') && page == 0 && selected_row == 0) {
            if (ch == '\r') skip_line_feed = true;
            settings.show_post = !settings.show_post;
            dirty = true;
        } else if ((ch == '\r' || ch == '\n') && page == 0 && selected_row == 1) {
            if (ch == '\r') skip_line_feed = true;
            settings.auto_start = !settings.auto_start;
            dirty = true;
        } else if ((ch == '\r' || ch == '\n') && page == 0 && selected_row == 2) {
            if (ch == '\r') skip_line_feed = true;
            settings.timeout_s = BOOT_TIMEOUT_DEFAULT_S;
            settings.auto_start = true;
            settings.show_post = true;
            settings.default_id[0] = '\0';
            dirty = true;
        } else if ((ch == '\r' || ch == '\n') && page == 1) {
            if (ch == '\r') skip_line_feed = true;
            if (selected_row == 0 && entry_count) {
                size_t next = (configured_default_index(&settings) + 1) % entry_count;
                snprintf(settings.default_id, sizeof settings.default_id, "%s",
                         entries[next].stable_id);
                dirty = true;
            } else if (selected_row == 1) {
                settings.timeout_s = timeout_options[(timeout_option + 1) %
                    (sizeof timeout_options / sizeof timeout_options[0])];
                dirty = true;
            }
        }
    }
}

static const char *profile_description(boot_profile_t profile)
{
    switch (profile) {
    case BOOT_RK05: return "PDP-11/40 | RK05";
    case BOOT_BOS6_RD54: return "PDP-11/73 | RD54";
    case BOOT_RA92: return "PDP-11 | RA92";
    case BOOT_RX: return "PDP-11 | RX floppy";
    }
    return "Unknown profile";
}

static const char *profile_memory_description(boot_profile_t profile)
{
    switch (profile) {
    case BOOT_RK05: return "256 KiB";
    case BOOT_BOS6_RD54: return "2 MiB";
    case BOOT_RA92: return "3.5 MiB";
    case BOOT_RX: return "512 KiB";
    }
    return "profile default";
}

static const char *chip_model_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32: return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C2: return "ESP32-C2";
    case CHIP_ESP32C3: return "ESP32-C3";
    case CHIP_ESP32C5: return "ESP32-C5";
    case CHIP_ESP32C6: return "ESP32-C6";
    case CHIP_ESP32C61: return "ESP32-C61";
    case CHIP_ESP32H2: return "ESP32-H2";
    case CHIP_ESP32H21: return "ESP32-H21";
    case CHIP_ESP32H4: return "ESP32-H4";
    case CHIP_ESP32P4: return "ESP32-P4";
    case CHIP_POSIX_LINUX: return "POSIX simulator";
    }
    return "ESP32 family";
}

static int show_post_screen(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint64_t sd_total = 0, sd_free = 0;
    bool sd_ready = esp_vfs_fat_info("/sdcard", &sd_total, &sd_free) == ESP_OK;
    printf("\033[2J\033[H" ANSI_CYAN
           "+--------------------------------------------------------------------+\n"
           "|                     FUZZBALL SYSTEM BIOS                          |\n"
           "|                     POWER-ON SELF TEST                            |\n"
           "+--------------------------------------------------------------------+\n" ANSI_RESET);
    printf("  ESP32       %s rev %u, %u core(s)\n",
           chip_model_name(chip.model), chip.revision, chip.cores);
    printf("  MEMORY      %u KiB heap free; PSRAM %s\n",
           (unsigned)(esp_get_free_heap_size() / 1024),
           esp_psram_is_initialized() ? "available" : "not detected");
    printf("  STORAGE     SD ");
    if (sd_ready)
        printf("mounted (%" PRIu64 "/%" PRIu64 " MiB free/total)",
               sd_free / (1024 * 1024), sd_total / (1024 * 1024));
    else
        printf("unavailable");
    size_t spiffs_total = 0, spiffs_used = 0;
    if (esp_spiffs_info(NULL, &spiffs_total, &spiffs_used) == ESP_OK)
        printf("; SPIFFS %u KiB free\n",
               (unsigned)((spiffs_total - spiffs_used) / 1024));
    else
        printf("; SPIFFS unavailable\n");
#if CONFIG_ESPPDP_BOOT_OLED
    printf("  DISPLAY     %s\n",
           oled_menu_ready ? "OLED driver initialized" : "serial console active");
#else
    printf("  DISPLAY     serial console active (OLED disabled)\n");
#endif
    printf("\n  Any key: skip POST   S: setup   Continuing in 2 seconds...\n");
    fflush(stdout);
#if CONFIG_ESPPDP_BOOT_OLED
    if (oled_menu_ready) {
        const char *lines[] = {
            "FUZZBALL BIOS", "POWER ON SELF TEST",
            chip_model_name(chip.model),
            esp_psram_is_initialized() ? "PSRAM PRESENT" : "NO PSRAM",
            sd_ready ? "SD CARD READY" : "SD UNAVAILABLE",
            "ANY KEY: SKIP", "S: SETUP", "AUTO CONTINUE 2S",
        };
        if (oled_ssd1306_render_lines(lines, 8) != ESP_OK)
            oled_menu_ready = false;
    }
#endif
    int64_t deadline = esp_timer_get_time() + 2000000;
    while (esp_timer_get_time() < deadline) {
        int ch = poll_menu_key();
        if (ch != MENU_KEY_NONE) {
            if (ch == '\r') skip_line_feed = true;
            return ch == 's' || ch == 'S' ? 'S' : ' ';
        }
        vTaskDelay(pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : 1);
    }
    return MENU_KEY_NONE;
}

#if CONFIG_ESPPDP_BOOT_OLED
static void oled_render_boot_screen(size_t selected, size_t remembered,
                                    bool auto_start, unsigned seconds_left)
{
    char storage_line[22];
    uint64_t total = 0, free_bytes = 0;
    if (esp_vfs_fat_info("/sdcard", &total, &free_bytes) == ESP_OK) {
        snprintf(storage_line, sizeof storage_line, "SD %u/%uMB free",
                 (unsigned)(free_bytes / (1024 * 1024)),
                 (unsigned)(total / (1024 * 1024)));
    } else {
        snprintf(storage_line, sizeof storage_line, "SD NOT MOUNTED");
    }

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    char lines[8][22] = {{0}};
    const char *view[8];
    for (size_t i = 0; i < 8; ++i) view[i] = lines[i];
    snprintf(lines[0], sizeof lines[0], "FUZZBALL SYSTEM BIOS");
    snprintf(lines[1], sizeof lines[1], "%s %uC %uMB PSRAM",
             chip_model_name(chip.model), (unsigned)chip.cores,
             (unsigned)(esp_psram_get_size() / (1024 * 1024)));
    snprintf(lines[2], sizeof lines[2], "%s", storage_line);
    if (auto_start)
        snprintf(lines[3], sizeof lines[3], "DEF %c/%c AUTO %02us",
                 (int)('0' + remembered + 1), (int)('0' + entry_count),
                 seconds_left % 100);
    else
        snprintf(lines[3], sizeof lines[3], "DEF %c/%c  PAUSED",
                 (int)('0' + (entry_count ? remembered + 1 : 0)),
                 (int)('0' + entry_count));

    if (entry_count) {
        size_t first = selected > 1 ? selected - 1 : 0;
        if (entry_count > 3 && first + 3 > entry_count) first = entry_count - 3;
        for (size_t row = 0; row < 3; ++row) {
            size_t index = first + row;
            if (index >= entry_count) break;
            snprintf(lines[4 + row], sizeof lines[4 + row], "%c%c %-16.16s",
                     index == selected ? '>' : ' ',
                     index == remembered ? '*' : ' ', entries[index].label);
        }
    } else {
        snprintf(lines[4], sizeof lines[4], "No boot images");
    }
    snprintf(lines[7], sizeof lines[7], "J/K MOVE H=HARDWARE");
    esp_err_t err = oled_ssd1306_render_lines(view, 8);
    if (err != ESP_OK) {
        ESP_LOGW("boot_menu", "OLED menu update failed: %s", esp_err_to_name(err));
        oled_menu_ready = false;
    }
}

static void oled_render_setup_screen(const boot_settings_t *settings,
                                     size_t selected_row, size_t default_option,
                                     unsigned page)
{
    if (!oled_menu_ready) return;
    char lines[8][22] = {{0}};
    const char *view[8];
    for (size_t i = 0; i < 8; ++i) view[i] = lines[i];
    snprintf(lines[0], sizeof lines[0], "FUZZBALL SYSTEM BIOS");
    snprintf(lines[1], sizeof lines[1], "SYSTEM CONFIGURATION");
    static const char *const page_name[] = {"MAIN", "BOOT", "STORAGE", "HARDWARE", "INFO"};
    snprintf(lines[2], sizeof lines[2], "SECTION %u %s", page + 1,
             page_name[page]);
    if (page == 0) {
        snprintf(lines[3], sizeof lines[3], "%c POST %s",
                 selected_row == 0 ? '>' : ' ', settings->show_post ? "ON" : "OFF");
        snprintf(lines[4], sizeof lines[4], "%c AUTO START %s",
                 selected_row == 1 ? '>' : ' ', settings->auto_start ? "ON" : "OFF");
        snprintf(lines[5], sizeof lines[5], "%c RESTORE DEFAULTS",
                 selected_row == 2 ? '>' : ' ');
        snprintf(lines[6], sizeof lines[6], "%c SAVE AND EXIT",
                 selected_row == 3 ? '>' : ' ');
    } else if (page == 1) {
        snprintf(lines[3], sizeof lines[3], "%c DEFAULT BOOT",
                 selected_row == 0 ? '>' : ' ');
        snprintf(lines[4], sizeof lines[4], "%s",
                 entry_count ? entries[default_option].label : "No boot images");
        snprintf(lines[5], sizeof lines[5], "%c TIMEOUT %u SECONDS",
                 selected_row == 1 ? '>' : ' ', settings->timeout_s);
        snprintf(lines[6], sizeof lines[6], "%c SAVE AND EXIT",
                 selected_row == 2 ? '>' : ' ');
    } else if (page == 2) {
        uint64_t total = 0, free_bytes = 0;
        if (esp_vfs_fat_info("/sdcard", &total, &free_bytes) == ESP_OK)
            snprintf(lines[3], sizeof lines[3], "SD %u/%u MB FREE",
                     (unsigned)(free_bytes / (1024 * 1024)),
                     (unsigned)(total / (1024 * 1024)));
        else
            snprintf(lines[3], sizeof lines[3], "SD NOT MOUNTED");
        snprintf(lines[4], sizeof lines[4], "W OR ENTER: UPLOAD");
    } else if (page == 3) {
        snprintf(lines[3], sizeof lines[3], "H/ENTER: DIAGNOSTICS");
        snprintf(lines[4], sizeof lines[4], "PROBES ARE READ ONLY");
    } else {
        snprintf(lines[3], sizeof lines[3], "ESP-IDF %s", esp_get_idf_version());
        snprintf(lines[4], sizeof lines[4], "FUZZBALL BOOT MANAGER");
    }
    snprintf(lines[7], sizeof lines[7], "1-5 TAB S SAVE Q BACK");
    esp_err_t err = oled_ssd1306_render_lines(view, 8);
    if (err != ESP_OK) {
        ESP_LOGW("boot_menu", "OLED setup update failed: %s", esp_err_to_name(err));
        oled_menu_ready = false;
    }
}

static void oled_menu_init(void)
{
    const oled_ssd1306_config_t config = {
        .port = I2C_NUM_0,
        .sda_gpio = GPIO_NUM_18,
        .scl_gpio = GPIO_NUM_17,
        .address = 0,
        .clock_hz = 400000,
    };
    esp_err_t err = oled_ssd1306_init(&config);
    oled_menu_ready = err == ESP_OK;
    if (!oled_menu_ready)
        ESP_LOGW("boot_menu", "OLED unavailable; continuing on serial: %s",
                 esp_err_to_name(err));
}
#endif

static void print_storage_status(void)
{
    uint64_t total = 0, free_bytes = 0;
    if (esp_vfs_fat_info("/sdcard", &total, &free_bytes) == ESP_OK) {
        printf("  SD CARD     %" PRIu64 " MiB total  |  %" PRIu64 " MiB free\n",
               total / (1024 * 1024), free_bytes / (1024 * 1024));
    } else {
        printf("  SD CARD     %snot mounted%s\n", ANSI_YELLOW, ANSI_RESET);
    }
}

static void print_display_status(void)
{
#if CONFIG_ESPPDP_BOOT_OLED
    uint8_t address;
    if (oled_menu_ready && oled_ssd1306_get_address(&address))
        printf("  DISPLAY     %sSSD1306-compatible OLED at I2C 0x%02X%s\n",
               ANSI_GREEN, address, ANSI_RESET);
    else
        printf("  DISPLAY     %sOLED unavailable; serial active%s\n",
               ANSI_YELLOW, ANSI_RESET);
#else
    printf("  DISPLAY     serial console (OLED disabled)\n");
#endif
}

static const char *usb_keyboard_status_name(void)
{
    switch (usb_keyboard_get_status()) {
    case USB_KEYBOARD_START_FAILED: return "host start failed";
    case USB_KEYBOARD_HOST_READY: return "host ready; no keyboard";
    case USB_KEYBOARD_CONNECTED: return "HID keyboard connected";
    case USB_KEYBOARD_NOT_STARTED: return "host not started";
    }
    return "unknown";
}

static void print_radio_capabilities(const esp_chip_info_t *chip)
{
    printf("  RADIO       Wi-Fi %s; BLE %s; Classic BT %s\n",
           chip->features & CHIP_FEATURE_WIFI_BGN ? "present (off)" : "absent",
           chip->features & CHIP_FEATURE_BLE ? "present (off)" : "absent",
           chip->features & CHIP_FEATURE_BT ? "present" : "absent");
}

static void oled_render_hardware_screen(const esp_chip_info_t *chip,
                                        const uint8_t *addresses,
                                        size_t address_count,
                                        esp_err_t scan_result)
{
#if CONFIG_ESPPDP_BOOT_OLED
    if (!oled_menu_ready) return;
    char lines[8][22] = {{0}};
    const char *view[8];
    for (size_t i = 0; i < 8; ++i) view[i] = lines[i];
    snprintf(lines[0], sizeof lines[0], "HARDWARE INVENTORY");
    snprintf(lines[1], sizeof lines[1], "%s rev%u %u cores",
             chip_model_name(chip->model), chip->revision, chip->cores);
    snprintf(lines[2], sizeof lines[2], "WiFi:%s BLE:%s",
             chip->features & CHIP_FEATURE_WIFI_BGN ? "CHIP" : "N/A",
             chip->features & CHIP_FEATURE_BLE ? "CHIP" : "N/A");
    snprintf(lines[3], sizeof lines[3], "USB %s",
             usb_keyboard_get_status() == USB_KEYBOARD_CONNECTED
                 ? "KEYBOARD" : usb_keyboard_get_status() == USB_KEYBOARD_HOST_READY
                 ? "HOST READY" : usb_keyboard_get_status() == USB_KEYBOARD_START_FAILED
                 ? "ERROR" : "NOT STARTED");
    uint8_t oled_address;
    if (oled_menu_ready && oled_ssd1306_get_address(&oled_address))
        snprintf(lines[4], sizeof lines[4], "OLED SSD1306 0x%02X",
                 oled_address);
    else
        snprintf(lines[4], sizeof lines[4], "OLED NOT DETECTED");
    if (scan_result == ESP_OK && address_count) {
        size_t used = (size_t)snprintf(lines[5], sizeof lines[5], "I2C ACK:");
        size_t shown = address_count < 3 ? address_count : 3;
        for (size_t i = 0; i < shown && used < sizeof lines[5] - 1; ++i)
            used += (size_t)snprintf(lines[5] + used, sizeof lines[5] - used,
                                     " %02X", addresses[i]);
        if (address_count > shown && used < sizeof lines[5] - 1)
            snprintf(lines[5] + used, sizeof lines[5] - used, " +%u",
                     (unsigned)(address_count - shown));
    } else {
        snprintf(lines[5], sizeof lines[5], "I2C %s",
                 scan_result == ESP_OK ? "NO ACK" : "SCAN ERROR");
    }
    snprintf(lines[6], sizeof lines[6], "SD:%s SPIFFS:%s",
             esp_vfs_fat_info("/sdcard", &(uint64_t){0}, &(uint64_t){0}) == ESP_OK
                 ? "OK" : "N/A",
             esp_spiffs_info(NULL, &(size_t){0}, &(size_t){0}) == ESP_OK
                 ? "OK" : "N/A");
    snprintf(lines[7], sizeof lines[7], "Q/ESC BACK  R REFRESH");
    if (oled_ssd1306_render_lines(view, 8) != ESP_OK)
        oled_menu_ready = false;
#endif
}

static void show_hardware_inventory(void)
{
    for (;;) {
        esp_chip_info_t chip;
        esp_chip_info(&chip);
        uint32_t flash_bytes = 0;
        esp_err_t flash_result = esp_flash_get_size(NULL, &flash_bytes);
        size_t spiffs_total = 0, spiffs_used = 0;
        esp_err_t spiffs_result = esp_spiffs_info(NULL, &spiffs_total, &spiffs_used);
        uint64_t sd_total = 0, sd_free = 0;
        esp_err_t sd_result = esp_vfs_fat_info("/sdcard", &sd_total, &sd_free);
        uint8_t addresses[I2C_SCAN_ADDRESS_COUNT];
        size_t address_count = 0;
        uint8_t failed_address = 0xff;
#if CONFIG_ESPPDP_BOOT_OLED
        esp_err_t scan_result = oled_ssd1306_scan_i2c(
            addresses, I2C_SCAN_ADDRESS_COUNT, &address_count, &failed_address);
        uint8_t display_address = 0;
        bool display_known = oled_menu_ready &&
                             oled_ssd1306_get_address(&display_address);
#else
        esp_err_t scan_result = ESP_ERR_NOT_SUPPORTED;
#endif

        printf("\033[2J\033[H");
        printf(ANSI_CYAN "+--------------------------------------------------------------------+\n"
               "|                     HARDWARE INVENTORY                            |\n"
               "+--------------------------------------------------------------------+\n" ANSI_RESET);
        printf("  PROCESSOR   %s rev %u, %u core(s), ESP-IDF %s\n",
               chip_model_name(chip.model), chip.revision, chip.cores,
               esp_get_idf_version());
        printf("  HEAP        %u KiB free\n",
               (unsigned)(esp_get_free_heap_size() / 1024));
        if (esp_psram_is_initialized())
            printf("  PSRAM       %u MiB total, %u MiB free\n",
                   (unsigned)(esp_psram_get_size() / (1024 * 1024)),
                   (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) /
                             (1024 * 1024)));
        else
            printf("  PSRAM       not initialized / not present\n");
        if (flash_result == ESP_OK)
            printf("  FLASH       %u MiB detected\n",
                   (unsigned)(flash_bytes / (1024 * 1024)));
        else
            printf("  FLASH       size query failed: %s\n",
                   esp_err_to_name(flash_result));
        if (spiffs_result == ESP_OK)
            printf("  SPIFFS      mounted, %u KiB free of %u KiB\n",
                   (unsigned)((spiffs_total - spiffs_used) / 1024),
                   (unsigned)(spiffs_total / 1024));
        else
            printf("  SPIFFS      not mounted / unavailable\n");
        if (sd_result == ESP_OK)
            printf("  SD CARD     mounted, %" PRIu64 " MiB free of %" PRIu64 " MiB\n",
                   sd_free / (1024 * 1024), sd_total / (1024 * 1024));
        else
            printf("  SD CARD     not mounted / unavailable\n");
        print_radio_capabilities(&chip);
        printf("  RADIO STATE Wi-Fi and BLE stacks are off in the boot menu.\n");
        printf("  USB HID     %s\n", usb_keyboard_status_name());
        printf("  I2C BUS     GPIO18 SDA / GPIO17 SCL, scan 0x08-0x77\n");
        if (scan_result == ESP_OK) {
            printf("  I2C DEVICES %u responder(s):", (unsigned)address_count);
            if (!address_count) printf(" none detected");
            for (size_t i = 0; i < address_count; ++i) {
                printf("\n              0x%02X  ", addresses[i]);
#if CONFIG_ESPPDP_BOOT_OLED
                if (display_known && addresses[i] == display_address)
                    printf("SSD1306-compatible OLED (boot display)");
                else
#endif
                    printf("unidentified I2C responder");
            }
            printf("\n");
        } else if (failed_address != 0xff) {
            printf("  I2C SCAN    incomplete at 0x%02X: %s\n",
                   failed_address, esp_err_to_name(scan_result));
        } else {
            printf("  I2C SCAN    unavailable: %s\n", esp_err_to_name(scan_result));
        }
#if CONFIG_ESPPDP_BOOT_OLED
        if (display_known)
            printf("  DISPLAY     SSD1306-compatible OLED at I2C 0x%02X "
                   "(SDA GPIO18, SCL GPIO17)\n", display_address);
        else
            printf("  DISPLAY     OLED not detected or boot OLED disabled at runtime\n");
#else
        printf("  DISPLAY     serial console active; boot OLED disabled\n");
#endif
        printf("\n  Probes cover configured board devices only; other GPIO, sensors,\n");
        printf("  accessory buses, and peripherals have not been scanned.\n");
        printf("\n  R: refresh   Q/ESC: return to boot menu\n");
        fflush(stdout);
        oled_render_hardware_screen(&chip, addresses, address_count, scan_result);

        for (;;) {
            int ch = poll_menu_key();
            if (ch == 'q' || ch == 'Q' || ch == 0x1b) return;
            if (ch == 'r' || ch == 'R') break;
            vTaskDelay(pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : 1);
        }
    }
}

static void render_boot_screen(size_t selected, size_t remembered,
                               bool auto_start, unsigned seconds_left)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    printf("\033[2J\033[H");
    printf(ANSI_CYAN "+--------------------------------------------------------------------+\n"
           "|                     FUZZBALL SYSTEM BIOS                          |\n"
           "|                       PDP-11 BOOT MANAGER                          |\n"
           "+--------------------------------------------------------------------+\n" ANSI_RESET);
    printf("  Firmware runtime: ESP-IDF %s\n", esp_get_idf_version());
    printf("\nSYSTEM STATUS\n");
    printf("  %-11s %sDETECTED%s  |  %u core(s)  |  %u KB heap free (all)\n",
           chip_model_name(chip.model), ANSI_GREEN, ANSI_RESET, (unsigned)chip.cores,
           (unsigned)(esp_get_free_heap_size() / 1024));
    if (esp_psram_is_initialized()) {
        printf("  PSRAM       %u MiB total  |  %u MiB free\n",
               (unsigned)(esp_psram_get_size() / (1024 * 1024)),
               (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) /
                         (1024 * 1024)));
    } else {
        printf("  PSRAM       %snot detected%s\n", ANSI_DIM, ANSI_RESET);
    }
    print_storage_status();
    print_display_status();
    print_radio_capabilities(&chip);
    printf("  USB HID     %s\n", usb_keyboard_status_name());
    printf("\nBOOT OPTIONS\n");
    if (!entry_count) {
        printf("  %sNo supported boot images found.%s\n", ANSI_YELLOW, ANSI_RESET);
    }
    for (size_t i = 0; i < entry_count; ++i) {
        bool active = i == selected;
        printf("  %s%c %u. %-24.24s %-21s %s%s%s\n",
               active ? ANSI_YELLOW : "", active ? '>' : ' ',
               (unsigned)(i + 1), entries[i].label,
               profile_description(entries[i].boot.profile),
               strncmp(entries[i].boot.path, "/spiffs/", 8) == 0
                   ? "SPIFFS" : "SD CARD",
               i == remembered ? "  [DEFAULT]" : "",
               active ? ANSI_RESET : "");
    }
    if (other_sd_file_count)
        printf("\n  %u other SD-card file(s) are stored but have no boot profile.\n",
               (unsigned)other_sd_file_count);
    if (invalid_bos6_image)
        printf("  %sBOS6.IMG has the wrong size or is not a regular file; skipped.%s\n",
               ANSI_YELLOW, ANSI_RESET);
    if (invalid_rk05_count)
        printf("  %s%u .RK05 candidate(s) are empty or not regular files; skipped.%s\n",
               ANSI_YELLOW, (unsigned)invalid_rk05_count, ANSI_RESET);
    if (entry_limit_reached)
        printf("  %sBoot list limit reached; some supported images are hidden.%s\n",
               ANSI_YELLOW, ANSI_RESET);
    if (entry_count) {
        const boot_entry_t *entry = &entries[selected];
        printf("\nSELECTED SYSTEM\n");
        printf("  Name:     %s\n", entry->label);
        printf("  Machine:  %s\n", profile_description(entry->boot.profile));
        printf("  Memory:   %s\n", profile_memory_description(entry->boot.profile));
        printf("  Image:    %s\n", entry->boot.path);
        printf("  Media:    %s; guest writes can modify this image.\n",
               strncmp(entry->boot.path, "/spiffs/", 8) == 0
                   ? "built-in SPIFFS image" : "SD card (writable)");
    }
    if (entry_count)
        printf("\nKEYS  Up/Down or J/K: select   Enter: boot   1-%u: boot now\n",
               (unsigned)entry_count);
    else {
        printf("\nKEYS  H: hardware   W: SD upload   C: Wi-Fi setup   S: setup\n");
        printf("      Q: exit\n");
    }
    if (entry_count)
        printf("      H: hardware   W: SD upload   C: Wi-Fi setup   S: setup\n");
    if (auto_start && entry_count) {
        printf("\n");
        fflush(stdout);
    } else if (entry_count) {
        printf("\nAUTO-START PAUSED  |  Press Enter to boot the selected system.\n");
    }
    fflush(stdout);
#if CONFIG_ESPPDP_BOOT_OLED
    if (oled_menu_ready)
        oled_render_boot_screen(selected, remembered, auto_start, seconds_left);
#endif
}

static int poll_escape_sequence(void)
{
    const int64_t timeout_us = 50000;
    int64_t deadline = esp_timer_get_time() + timeout_us;
    int second = -1;
    while (esp_timer_get_time() < deadline) {
        t_stat input = sim_poll_kbd();
        if (input & SCPE_KFLAG) {
            second = input & 0xff;
            break;
        }
        vTaskDelay(1);
    }
    if (second != '[' && second != 'O') return 0x1b;
    deadline = esp_timer_get_time() + timeout_us;
    while (esp_timer_get_time() < deadline) {
        t_stat input = sim_poll_kbd();
        if (input & SCPE_KFLAG) {
            int third = input & 0xff;
            if (third == 'A') return MENU_KEY_UP;
            if (third == 'B') return MENU_KEY_DOWN;
            return 0x1b;
        }
        vTaskDelay(1);
    }
    return 0x1b;
}

static int poll_menu_key(void)
{
    t_stat input = sim_poll_kbd();
    if (!(input & SCPE_KFLAG)) return MENU_KEY_NONE;
    int ch = input & 0xff;
    return ch == 0x1b ? poll_escape_sequence() : ch;
}

static void print_countdown(unsigned seconds)
{
    printf("\r\033[2K" ANSI_YELLOW "Auto-start in %u second%s..." ANSI_RESET,
           seconds, seconds == 1 ? "" : "s");
    fflush(stdout);
}

static bool reset_requires_manual_recovery(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
        return true;
    default:
        return false;
    }
}

void boot_menu_select(void)
{
	if (sim_ttinit() != SCPE_OK) {
		puts("Boot menu: console input initialization failed");
		return;
	}
#if CONFIG_ESPPDP_BOOT_OLED
    oled_menu_init();
#endif
    choice.path[0] = '\0';
    choice_stable_id[0] = '\0';
    bool manual_recovery = reset_requires_manual_recovery();
    if (manual_recovery)
        puts("RECOVERY: watchdog, panic, or brownout reset detected; "
             "automatic boot is paused to protect writable disk images.\n");
    bool post_pending = true;
    for (;;) {
        discover_entries();
        boot_settings_t settings;
        load_settings(&settings);
        size_t remembered = entry_count ? configured_default_index(&settings) : 0;
        if (post_pending) {
            post_pending = false;
            if (settings.show_post && show_post_screen() == 'S') {
                (void)configure_system_settings(remembered);
                continue;
            }
        }
        size_t selected = remembered;
        bool auto_start = entry_count != 0 && settings.auto_start &&
                          !manual_recovery;
        int64_t deadline = esp_timer_get_time() +
            (int64_t)settings.timeout_s * 1000000;
        int last_countdown = -1;
        render_boot_screen(selected, remembered, auto_start, settings.timeout_s);
        bool rescan = false;
        bool launch = false;
        for (;;) {
            if (auto_start) {
                int64_t now = esp_timer_get_time();
                if (now >= deadline) {
                    launch = true;
                    break;
                }
                int remaining = (int)((deadline - now + 999999) / 1000000);
                if (remaining != last_countdown) {
                    print_countdown((unsigned)remaining);
                    last_countdown = remaining;
#if CONFIG_ESPPDP_BOOT_OLED
                    if (oled_menu_ready)
                        oled_render_boot_screen(selected, remembered, true,
                                                (unsigned)remaining);
#endif
                }
            }

            int ch = poll_menu_key();
            if (ch == MENU_KEY_NONE) {
                vTaskDelay(pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : 1);
                continue;
            }
            if (skip_line_feed) {
                skip_line_feed = false;
                if (ch == '\n') continue;
            }
            if (ch == 'w' || ch == 'W') {
                puts("");
                esp_err_t result = sd_upload_portal_run(upload_cancel_requested);
                if (result != ESP_OK)
                    printf("SD upload portal could not start: %s\n",
                           esp_err_to_name(result));
                rescan = true;
                break;
            }
            if (ch == 'c' || ch == 'C') {
                puts("");
                configure_wifi_serial();
                rescan = true;
                break;
            }
            if (ch == 's' || ch == 'S') {
                puts("");
                (void)configure_system_settings(remembered);
                rescan = true;
                break;
            }
            if (ch == 'h' || ch == 'H') {
                auto_start = false;
                puts("");
                show_hardware_inventory();
                render_boot_screen(selected, remembered, false, 0);
                continue;
            }
            if (entry_count && ch >= '1' && ch < '1' + (int)entry_count) {
                selected = (size_t)(ch - '1');
                launch = true;
                break;
            }
            if (entry_count && (ch == '\r' || ch == '\n')) {
                launch = true;
                break;
            }
            if (!entry_count && (ch == 'q' || ch == 'Q')) return;
            if (entry_count && (ch == MENU_KEY_UP || ch == 'k' || ch == 'K')) {
                selected = selected ? selected - 1 : entry_count - 1;
                auto_start = false;
                puts("");
                render_boot_screen(selected, remembered, auto_start, 0);
                continue;
            }
            if (entry_count && (ch == MENU_KEY_DOWN || ch == 'j' || ch == 'J')) {
                selected = (selected + 1) % entry_count;
                auto_start = false;
                puts("");
                render_boot_screen(selected, remembered, auto_start, 0);
                continue;
            }
            if (entry_count && ch == 0x1b) {
                auto_start = false;
                puts("");
                render_boot_screen(selected, remembered, auto_start, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : 1);
        }
        if (rescan) continue;
        if (!entry_count) return;
        if (!launch) continue;
        if (!auto_start || last_countdown >= 0) putchar('\n');
        choice = entries[selected].boot;
        snprintf(choice_stable_id, sizeof choice_stable_id, "%s",
                 entries[selected].stable_id);
        printf("\nBOOT OUTCOME: selection accepted\n");
        printf("  System:   %s\n  Profile: %s\n  Memory: %s\n  Image: %s\n",
               entries[selected].label,
               profile_description(choice.profile),
               profile_memory_description(choice.profile), choice.path);
        printf("  Warning: simulator disk images are writable.\n");
        return;
    }
}

const boot_choice_t *boot_menu_choice(void)
{
    return choice.path[0] ? &choice : NULL;
}

void boot_menu_remember(void)
{
    if (!choice.path[0] || !choice_stable_id[0]) return;
    nvs_handle_t handle;
    if (nvs_open("bootmenu", NVS_READWRITE, &handle) != ESP_OK) return;
    esp_err_t id_result = nvs_set_str(handle, "last_id", choice_stable_id);
    esp_err_t path_result = nvs_set_str(handle, "last_path", choice.path);
    if (id_result == ESP_OK && path_result == ESP_OK)
        (void)nvs_commit(handle);
    nvs_close(handle);
}
