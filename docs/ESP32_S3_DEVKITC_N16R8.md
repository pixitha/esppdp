# ESP32-S3-DevKitC-1 N16R8 reference

This document records the ESP32-S3 board selected for the ESP-PDP11/Fuzzball
bring-up. It is a hardware reference, not evidence of a successful Fuzzball
boot.

## Board identity

| Item | Value |
| --- | --- |
| Board | ESP32-S3-DevKitC-1 form factor, GPIO48 LED variant; exact PCB revision not physically marked/verified here |
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

The diagnostic probe was run on this board and the addressable LED responded
on GPIO48. We therefore record this physical board as the GPIO48 variant and
use GPIO48 in the ESP-PDP11 profile. `esptool` cannot distinguish the PCB
revisions, so the observed LED response is the authoritative identification
for our hardware.

## Board connections

The DevKitC exposes most usable module GPIOs on two headers and provides both a
USB-to-UART port and the ESP32-S3 native USB OTG port. The Boot button enters
download mode when held while resetting; the Reset button restarts the board.
This board's addressable RGB LED is driven by GPIO48. The separate red power
indicator and green/blue USB-UART activity indicators are not controlled by
the ESP-PDP11 status LED driver.

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
| Proposed microSD SDMMC CLK/CMD/D0/D1/D2/D3 | 14 / 13 / 4 / 6 / 15 / 5 |
| SPI microSD fallback MOSI/SCLK/MISO/CS | 13 / 14 / 4 / 5 |

The separate 1.54-inch e-paper bring-up uses the side-header wiring documented in
[`EPAPER_GDEW0154Z04_WIRING.md`](EPAPER_GDEW0154Z04_WIRING.md); it is not the
DevKitC's onboard display path.

The 0.96-inch I2C OLED wiring and current heat/failure caution are documented in
[`OLED_SSD1306_WIRING.md`](OLED_SSD1306_WIRING.md).

The bare DevKitC has no onboard LCD or microSD socket. The external SDMMC
breakout has mounted successfully in 4-bit mode at 20 MHz, including during
the pre-SIMH boot-menu test. The OLED is currently disconnected. See
[`ESP32_S3_SD_CARD_PLAN.md`](ESP32_S3_SD_CARD_PLAN.md). SPI2 remains reserved
for the legacy display path.

## Native USB host power finding

The local Rev 1.1 schematic (`SCH_ESP32-S3-DevKitC-1_V1.1_20221130.pdf`,
sheet 2) confirms that the native ESP USB connector has the correct data
routing: connector D- and D+ go to GPIO19 and GPIO20 through the USB ESD
network (D8--D10). Its VBUS path is input-only. The connector's `VBUSB` net
feeds `VCC_5V` through Schottky diode D7, just as the USB-to-UART connector
feeds that rail through D1. There is no host-power switch, current limiter, or
return path from `VCC_5V` to the native connector VBUS in this schematic.

The board therefore has the correct USB data traces but is not electrically
host-ready for a bus-powered keyboard by itself. Powering the board from the
`5V` header (or either USB connector) powers the ESP32, but does not prove
that a keyboard receives 5 V on USB VBUS. Use a powered OTG hub or a
current-limited 5 V VBUS injection/adapter for keyboard testing. Do not inject
5 V into `3V3`, and do not tie independent 5 V sources together.

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
