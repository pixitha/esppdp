#include "usb_keyboard.h"

#include <stdbool.h>
#include <stdint.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"

static const char *TAG = "usb_keyboard";
static QueueHandle_t key_queue;
static bool started;
static bool report_logged;

static int key_to_ascii(uint8_t modifier, uint8_t key)
{
    bool shift = (modifier & (HID_LEFT_SHIFT | HID_RIGHT_SHIFT)) != 0;
    if (key >= HID_KEY_A && key <= HID_KEY_Z)
        return (shift ? 'A' : 'a') + (key - HID_KEY_A);
    if (key >= HID_KEY_1 && key <= HID_KEY_9)
        return shift ? "!@#$%^&*("[key - HID_KEY_1] : ('1' + key - HID_KEY_1);
    if (key == HID_KEY_0) return shift ? ')' : '0';
    switch (key) {
    case HID_KEY_ENTER: return '\n';
    case HID_KEY_SPACE: return ' ';
    case HID_KEY_TAB: return '\t';
    case HID_KEY_DEL: return '\b';
    case HID_KEY_MINUS: return shift ? '_' : '-';
    case HID_KEY_EQUAL: return shift ? '+' : '=';
    case HID_KEY_OPEN_BRACKET: return shift ? '{' : '[';
    case HID_KEY_CLOSE_BRACKET: return shift ? '}' : ']';
    case HID_KEY_BACK_SLASH: return shift ? '|' : '\\';
    case HID_KEY_COLON: return shift ? ':' : ';';
    case HID_KEY_QUOTE: return shift ? '"' : '\'';
    case HID_KEY_TILDE: return shift ? '~' : '`';
    case HID_KEY_LESS: return shift ? '<' : ',';
    case HID_KEY_GREATER: return shift ? '>' : '.';
    case HID_KEY_SLASH: return shift ? '?' : '/';
    default: return -1;
    }
}

static void hid_interface_callback(hid_host_device_handle_t device,
                                   hid_host_interface_event_t event, void *arg)
{
    (void)arg;
    hid_host_dev_params_t params;
    if (hid_host_device_get_params(device, &params) != ESP_OK) return;

    if (event == HID_HOST_INTERFACE_EVENT_INPUT_REPORT &&
        params.proto == HID_PROTOCOL_KEYBOARD) {
        uint8_t report[64];
        size_t length = 0;
        if (hid_host_device_get_raw_input_report_data(device, report,
                                                       sizeof(report), &length) != ESP_OK ||
            length < 8) return;
        if (!report_logged) {
            ESP_LOGI(TAG, "first keyboard report: len=%u modifier=0x%02x keys=%02x %02x %02x %02x %02x %02x",
                     (unsigned)length, report[0], report[2], report[3], report[4],
                     report[5], report[6], report[7]);
            report_logged = true;
        }
        /* Boot keyboard reports contain modifier, reserved, and six keycodes. */
        for (size_t i = 2; i < 8 && i < length; ++i) {
            int c = key_to_ascii(report[0], report[i]);
            if (c >= 0) xQueueSend(key_queue, &c, 0);
        }
    } else if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
        ESP_LOGI(TAG, "keyboard disconnected");
        report_logged = false;
        hid_host_device_close(device);
    } else if (event == HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR) {
        ESP_LOGW(TAG, "keyboard input transfer error");
    }
}

static void hid_device_callback(hid_host_device_handle_t device,
                                hid_host_driver_event_t event, void *arg)
{
    (void)arg;
    hid_host_dev_params_t params;
    esp_err_t err = hid_host_device_get_params(device, &params);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HID device event %d, get_params failed: %s", event,
                 esp_err_to_name(err));
        return;
    }
    hid_host_dev_info_t info = {0};
    err = hid_host_get_device_info(device, &info);
    ESP_LOGI(TAG, "HID device event=%d addr=%u iface=%u vid=0x%04x pid=0x%04x subclass=%u protocol=%u info=%s",
             event, params.addr, params.iface_num, info.VID, info.PID,
             params.sub_class, params.proto,
             esp_err_to_name(err));
    if (event != HID_HOST_DRIVER_EVENT_CONNECTED) return;
    if (params.proto != HID_PROTOCOL_KEYBOARD) {
        ESP_LOGW(TAG, "HID device is not a boot keyboard; no console input enabled");
        return;
    }

    const hid_host_device_config_t config = {
        .callback = hid_interface_callback,
        .callback_arg = NULL,
    };
    err = hid_host_device_open(device, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HID keyboard open failed: %s", esp_err_to_name(err));
        return;
    }
    if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
        err = hid_class_request_set_protocol(device, HID_REPORT_PROTOCOL_BOOT);
        ESP_LOGI(TAG, "set boot protocol: %s", esp_err_to_name(err));
        err = hid_class_request_set_idle(device, 0, 0);
        ESP_LOGI(TAG, "set idle: %s", esp_err_to_name(err));
    }
    if (hid_host_device_start(device) == ESP_OK)
        ESP_LOGI(TAG, "USB HID keyboard connected");
}

static void usb_events_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) break;
    }
    vTaskDelete(NULL);
}

void usb_keyboard_start(void)
{
    if (started) return;
    key_queue = xQueueCreate(32, sizeof(int));
    if (!key_queue) return;
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB host install failed: %s", esp_err_to_name(err));
        return;
    }
    if (xTaskCreatePinnedToCore(usb_events_task, "usb_events", 4096, NULL, 2,
                                NULL, 0) != pdPASS) return;
    const hid_host_driver_config_t config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_device_callback,
        .callback_arg = NULL,
    };
    err = hid_host_install(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HID host install failed: %s", esp_err_to_name(err));
        return;
    }
    started = true;
    ESP_LOGI(TAG, "USB HID keyboard host ready on native USB OTG");
}

int usb_keyboard_getchar(void)
{
    int c;
    return (key_queue && xQueueReceive(key_queue, &c, 0) == pdTRUE) ? c : -1;
}
