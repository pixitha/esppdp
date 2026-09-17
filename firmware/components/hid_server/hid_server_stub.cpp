// Classic-Bluetooth HID is not available on ESP32-S3.  The Fuzzball
// bring-up does not require a Bluetooth keyboard, so provide the small
// interface consumed by bthid.c as a no-op component on that target.

extern "C" void hid_server_start() {}
extern "C" int hid_server_connected() { return 0; }
extern "C" int hid_server_getchar() { return 0; }
