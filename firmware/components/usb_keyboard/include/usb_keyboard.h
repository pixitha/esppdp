#pragma once

typedef enum {
    USB_KEYBOARD_NOT_STARTED,
    USB_KEYBOARD_START_FAILED,
    USB_KEYBOARD_HOST_READY,
    USB_KEYBOARD_CONNECTED
} usb_keyboard_status_t;

/* Start the native USB-OTG HID host and queue keyboard characters. */
void usb_keyboard_start(void);

/* Report host initialization and whether a HID keyboard is connected. */
usb_keyboard_status_t usb_keyboard_get_status(void);

/* Return one queued ASCII character, or -1 when no key is available. */
int usb_keyboard_getchar(void);
