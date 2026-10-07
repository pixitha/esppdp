// ESP32-S3 has no Classic Bluetooth HID controller.  Keep the terminal input
// API linkable while leaving Bluetooth input disabled for initial bring-up.

void bthid_start(void) {}
/* No Bluetooth HID input is available on the ESP32-S3.  Match the real
 * bthid_getchar() contract: -1 means no character is pending. */
int bthid_getchar(void) { return -1; }
int bthid_connected(void) { return 0; }
