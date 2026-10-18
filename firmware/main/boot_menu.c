#include "boot_menu.h"
#include "sd_upload_portal.h"
#include "sdkconfig.h"
#include "sim_defs.h"
#ifdef BIT
#undef BIT
#endif
#include "sim_term.h"

#include <dirent.h>
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
#define BOOT_TIMEOUT_US (10LL * 1000 * 1000)
#define CONSOLE_LINE_TIMEOUT_US (120LL * 1000 * 1000)
#define BOS6_RD54_IMAGE_SIZE 159334400

static boot_choice_t entries[BOOT_MAX_ENTRIES];
static size_t entry_count;
static boot_choice_t choice;
static bool skip_line_feed;

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
    const boot_choice_t *a = left;
    const boot_choice_t *b = right;
    return strcmp(a->path, b->path);
}

static void add_entry(boot_profile_t profile, const char *path)
{
    if (entry_count == BOOT_MAX_ENTRIES || !regular_file(path)) return;
    entries[entry_count].profile = profile;
    snprintf(entries[entry_count].path, sizeof entries[entry_count].path, "%s", path);
    ++entry_count;
}

static void add_sd_images(void)
{
    size_t first_sd = entry_count;
    DIR *dir = opendir("/sdcard");
    if (!dir) return;
    struct dirent *item;
    while ((item = readdir(dir)) != NULL && entry_count < BOOT_MAX_ENTRIES) {
        if (!rk05_name(item->d_name)) continue;
        char path[sizeof choice.path];
        int length = snprintf(path, sizeof path, "/sdcard/%s", item->d_name);
        if (length < 0 || (size_t)length >= sizeof path) continue;
        add_entry(BOOT_RK05, path);
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
        printf("Boot menu: %s must be a %u-byte regular RD54 image; skipping\n",
               path, (unsigned)BOS6_RD54_IMAGE_SIZE);
        return;
    }
    add_entry(BOOT_BOS6_RD54, path);
}

static void discover_entries(void)
{
    entry_count = 0;
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
    char last[sizeof choice.path];
    size_t length = sizeof last;
    if (nvs_open("bootmenu", NVS_READONLY, &handle) != ESP_OK) return 0;
    esp_err_t result = nvs_get_str(handle, "last_path", last, &length);
    nvs_close(handle);
    if (result != ESP_OK) return 0;
    for (size_t i = 0; i < entry_count; ++i)
        if (strcmp(entries[i].path, last) == 0) return i;
    return 0;
}

void boot_menu_select(void)
{
	if (sim_ttinit() != SCPE_OK) {
		puts("Boot menu: console input initialization failed");
		return;
	}
    choice.path[0] = '\0';
    for (;;) {
        discover_entries();
        size_t selected = entry_count ? default_index() : 0;
        puts("\nPDP-11 boot menu (RK05: PDP-11/40; BOS6: PDP-11/73):");
        for (size_t i = 0; i < entry_count; ++i)
            printf("  %u. %s%s\n", (unsigned)(i + 1), entries[i].path,
                   i == selected ? " [default]" : "");
        if (!entry_count) puts("  No supported disk images found.");
        if (entry_count) {
            printf("Select 1-%u, W for SD upload, C for Wi-Fi setup, or wait 10 seconds: ",
                   (unsigned)entry_count);
        } else {
            printf("W for SD upload, C for Wi-Fi setup, or Q to continue without a disk: ");
        }
        fflush(stdout);

        int64_t deadline = entry_count
            ? esp_timer_get_time() + BOOT_TIMEOUT_US : INT64_MAX;
        bool rescan = false;
        while (esp_timer_get_time() < deadline) {
            t_stat input = sim_poll_kbd();
            if (input & SCPE_KFLAG) {
                int ch = input & 0xff;
                if (skip_line_feed) {
                    skip_line_feed = false;
                    if (ch == '\n') continue;
                }
                if (ch == 'w' || ch == 'W') {
                    esp_err_t result = sd_upload_portal_run(upload_cancel_requested);
                    if (result != ESP_OK)
                        printf("SD upload portal could not start: %s\n",
                               esp_err_to_name(result));
                    rescan = true;
                    break;
                }
                if (ch == 'c' || ch == 'C') {
                    configure_wifi_serial();
                    rescan = true;
                    break;
                }
                if (entry_count && ch >= '1' && ch < '1' + (int)entry_count) {
                    selected = (size_t)(ch - '1');
                    deadline = 0;
                    break;
                }
                if (entry_count && (ch == '\r' || ch == '\n')) {
                    deadline = 0;
                    break;
                }
                if (!entry_count && (ch == 'q' || ch == 'Q')) return;
            }
            vTaskDelay(pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : 1);
        }
        if (rescan) continue;
        if (!entry_count) return;
        choice = entries[selected];
        printf("\nBooting %s\n", choice.path);
        return;
    }
}

const boot_choice_t *boot_menu_choice(void)
{
    return choice.path[0] ? &choice : NULL;
}

void boot_menu_remember(void)
{
    if (!choice.path[0]) return;
    nvs_handle_t handle;
    if (nvs_open("bootmenu", NVS_READWRITE, &handle) != ESP_OK) return;
    char last[sizeof choice.path];
    size_t length = sizeof last;
    if (nvs_get_str(handle, "last_path", last, &length) != ESP_OK ||
        strcmp(last, choice.path) != 0) {
        if (nvs_set_str(handle, "last_path", choice.path) == ESP_OK)
            (void)nvs_commit(handle);
    }
    nvs_close(handle);
}
