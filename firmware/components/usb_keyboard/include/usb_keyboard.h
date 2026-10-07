#pragma once

/* Start the native USB-OTG HID host and queue keyboard characters. */
void usb_keyboard_start(void);

/* Return one queued ASCII character, or -1 when no key is available. */
int usb_keyboard_getchar(void);
