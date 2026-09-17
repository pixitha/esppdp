// ESP32-S3 has no Classic Bluetooth HID controller.  Keep the terminal input
// API linkable while leaving Bluetooth input disabled for initial bring-up.

void bthid_start(void) {}
int bthid_getchar(void) { return 0; }
int bthid_connected(void) { return 0; }
