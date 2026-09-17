# ESP32-S3-DevKitC-1 N16R8 reference

This document records the ESP32-S3 board selected for the ESP-PDP11/Fuzzball
bring-up. It is a hardware reference, not evidence of a successful Fuzzball
boot.

## Board identity

| Item | Value |
| --- | --- |
| Board | Espressif ESP32-S3-DevKitC-1, v1.1 family |
| Module | ESP32-S3-WROOM-1, N16R8 as marked/ordered |
| Silicon observed | ESP32-S3 QFN56, revision v0.2 |
| CPU | Dual-core Xtensa LX7, up to 240 MHz, plus LP core |
| Wireless | 2.4 GHz Wi-Fi and Bluetooth LE |
| Flash observed | 16 MB, 3.3 V quad mode |
| PSRAM observed | 8 MB embedded, 3.3 V |
| Crystal | 40 MHz |
| USB device seen | `/dev/cu.usbmodem5CBD0162491` (host-specific; do not assume stable) |

The flash and PSRAM values above were read with `esptool` on 2026-09-17. The
device MAC address was intentionally not recorded here.

## Determining v1.0 versus v1.1

The ESP32-S3 chip and module probe do not identify the DevKitC PCB revision;
both revisions use the same ESP32-S3-WROOM family and memory options. The
documented board-level difference is the onboard addressable RGB LED pin:

| Board revision | RGB LED data GPIO |
| --- | --- |
| v1.0 / initial release | GPIO48 |
| v1.1 | GPIO38 |

If the PCB silkscreen does not identify the revision, a small `led_strip` test
that sends a visible color first on GPIO38 and then on GPIO48 is deterministic:
only the connected LED pin will respond. Either revision is otherwise suitable
for this port; the current profile follows v1.1. `esptool` cannot distinguish
the PCB revisions.

## Board connections

The DevKitC exposes most usable module GPIOs on two headers and provides both a
USB-to-UART port and the ESP32-S3 native USB OTG port. The Boot button enters
download mode when held while resetting; the Reset button restarts the board.
The v1.1 RGB LED is driven by GPIO38.

For the octal-memory variant, GPIO35, GPIO36, and GPIO37 are reserved for the
internal flash/PSRAM interface and must not be assigned to external devices.

Useful exposed pins for the initial bench setup include GPIO4, 5, 8--14,
15--21, 38--44, 45, 46, 47, and 48, subject to USB/JTAG and application-level
conflicts. GPIO43/44 are the USB-to-UART console TX/RX signals; GPIO19/20 are
the native USB D-/D+ signals.

Power may be supplied through either USB connector, the 5 V/GND header pins,
or the 3V3/GND pins. Do not drive the 3V3 pin while also supplying an
independent regulated 3.3 V source.

## ESP-PDP11 profile

The repository profile is selected by `CONFIG_ESPPDP_HW_S3_DEVKITC` and the
defaults file:

```text
firmware/sdkconfig.defaults.esp32s3_n16r8
```

The current external-peripheral reservation is:

| Function | GPIO assignment |
| --- | --- |
| IE15 SPI display MOSI/SCLK/CS/DC | 11 / 12 / 10 / 9 |
| IE15 display reset/backlight | not connected (`GPIO_NUM_NC`) |
| SPI microSD MOSI/SCLK/MISO/CS | 13 / 14 / 4 / 5 |

These assignments are a placeholder for later wiring; the bare DevKitC has no
onboard LCD or microSD socket. SPI2 is reserved for the display path and SPI3
for the SD path in the profile.

Classic Bluetooth HID is disabled for this target because ESP32-S3 provides
Bluetooth LE, not the Classic-Bluetooth API used by the legacy HID component.
The S3 profile supplies a no-op HID shim for initial terminal/emulator
bring-up.

## Validation status

- `esptool chip-id`: connected and identified ESP32-S3 revision v0.2.
- `esptool flash-id`: detected 16 MB flash.
- ESP-IDF 6.0.2 S3 build: passes; image size `0x125610`, 8% free in the
  current `0x140000` app partition.
- Firmware flash: not yet performed.
- ESP32 runtime, PSRAM allocation, SD/display operation, and Fuzzball guest
  boot: not yet validated.

## Primary references

- [ESP32-S3-DevKitC-1 v1.1 user guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)
- [ESP32-S3-WROOM-1/1U datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)
