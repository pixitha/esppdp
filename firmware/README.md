# ESP32 firmware

This firmware runs the PDP-11 SIMH port on ESP32 boards and provides the
console, storage mount, and optional display support. The current S3/Fuzzball
work has a serial boot menu and an SD-card web portal.

## Boot menu

After mounting SPIFFS and the FAT SD card, the firmware shows a BIOS-style
serial POST screen with detected ESP32 model, point-in-time free heap, PSRAM,
SD capacity, and supported boot images. `Show POST` controls whether the status
screen is shown; `S` enters setup from POST. Use Up/Down or `J/K` to highlight
an entry; navigation pauses auto-start. Press Enter to start the highlighted
entry, press its number to boot it immediately, or wait for the configured
default countdown. The menu includes the bundled Unix V6 RK05,
`BOS6.IMG` when it matches the expected 159,334,400-byte RD54 image size,
root-level `.RK05` files, and the existing `rq.dsk` and floppy profiles.

At the menu:

- Press **`H`** to view the detected board, radio capabilities, USB keyboard,
  display, flash, PSRAM, SPIFFS, and SD-card status. Press `R` to refresh the
  inventory or `Q`/Escape to return. The radio rows distinguish chip support
  from an active Wi-Fi/BLE service; opening diagnostics does not start either.
  The page also scans standard addresses on the configured I²C bus and lists
  ACKs without guessing device types.
- Press **`W`** to start the SD upload portal while SIMH is stopped.
- Press **`C`** to set Wi-Fi credentials from the serial console. Enter an
  SSID and password; password characters are masked. An empty password selects
  an open network. To clear saved credentials, submit an empty SSID and
  confirm with `Y`.
- Press a listed number to start that boot profile.
- Use Up/Down or `J/K` to move the highlight; press Enter to launch it.
- Press `S` to open System Configuration. Set the default entry, choose a
  5/10/15/30-second timeout, toggle POST and auto-start, restore boot-menu
  defaults, then use Save & Exit. Setup is organized into Main, Boot, Storage,
  Hardware, and Info pages. Restore Defaults changes boot-menu settings only.

Credentials are stored in the ESP32's NVS partition. The firmware does not
put network passwords in source code. Use the serial console at the configured
ESP-IDF monitor baud rate to enter them. `Ctrl-C` or Escape cancels a prompt.
The menu uses ANSI clear-screen and color sequences; terminals that do not
interpret ANSI still receive the plain-text content, though colors and screen
refresh will not look as intended.
Configuration is stored in NVS; an unavailable saved default falls back to the
last successfully started entry. A hardware reset confirmed persistence of
the boot settings. Serial setup navigation was checked; OLED readability and
HID navigation require separate physical checks. HID testing is pending a USB
adapter.

On the ESP32-S3 DevKitC profile, `CONFIG_ESPPDP_BOOT_OLED` mirrors a compact
version of the BIOS status, boot choices, countdown, and setup page to an
optional 128x64 SSD1306 OLED on GPIO 18 (SDA) and GPIO 17 (SCL). It probes
addresses `0x3C` and `0x3D`; if neither responds, the boot menu continues on
serial. The `H` inventory screen is mirrored there as well. When SIMH starts,
the boot-menu frame is replaced with a static simulator status screen. It does
not mirror guest or serial-monitor text; the serial console remains the input,
output, and recovery path. Diagnostics cover configured board devices; they do
not scan unassigned GPIO pins or guess whether unspecified sensors and
accessories are present.

## SD-card web portal

1. Stop at the serial boot menu and press **`W`**.
2. On first use, connect a phone or computer to the temporary WPA2 network
   whose SSID and password are printed in the serial console. Open
   `http://192.168.4.1/`. If Wi-Fi credentials were saved earlier, the portal
   instead joins that network and prints its assigned URL to the console.
3. The page reports free space and lists files on the card. Enter the
   per-session access code printed in the serial console for uploads, Wi-Fi
   changes, and stopping the portal. Choose files and upload them to the
   SD-card root; the page displays transfer progress.
4. To save a Wi-Fi network for a future portal session, enter its SSID and
   password on the page. Finish and return to the boot menu, then press `W`
   again to use the saved network. If station connection fails, the portal
   falls back to its temporary access point.
5. Choose **Finish and return** in the page, press **`Q`** in the serial
   console, or wait ten minutes without activity to stop the portal.

Uploads are limited to 1 GiB per file, require free space to remain on the
card, and accept plain filenames made of letters, numbers, dots, dashes, and
underscores. Uploads do not overwrite a file with the same name. The firmware
writes to a hidden temporary file and publishes the final filename only after
the transfer completes. It reopens the completed file and reports its
on-card SHA-256 digest. Interrupted temporary uploads are discarded on the
next portal start. The portal does not delete, rename, format, or modify
existing files or disk-image contents. Read-back hashing has been implemented;
large-file and interrupted-upload integrity checks remain on the validation
list.

The web interface and Wi-Fi portal are independent of the simulator's
`wifi_if_esp32` network-device path. The portal runs before SIMH starts and
returns to the boot menu when it stops.

The portal uses plain HTTP, not HTTPS. The access code controls write and stop
actions but does not hide the read-only file list or encrypt traffic. Use it
only on a trusted local network; do not port-forward it or expose it to the
internet.

## Building and flashing

The current ESP32-S3 configuration is built with ESP-IDF 6.0.2. From this
directory, after activating the ESP-IDF environment:

```sh
idf.py build
idf.py app-flash
idf.py monitor
```

`app-flash` updates the application image only and is appropriate when the
device already has the matching partition table. The partition layout was
changed to make room for the app and SPIFFS image; for a new device or a
partition-layout change, inspect `partitions.csv` and the board's existing
SPIFFS contents before flashing a full layout. A full flash can erase or
replace SPIFFS data. See [the boot-menu notes](../docs/ESP32_BOOT_MENU.md) for
the partition offsets and test evidence.

## Disk-image caution

SIMH opens attached disk images writable. Use disposable copies on the SD card
for experiments. Resetting or powering off the ESP32 does not shut down the
guest OS cleanly, so an image may be changed or left inconsistent. The portal
copies files; it does not make writable guest disks safe or immutable.

## Hardware and project history

The repository contains board-specific support for the final hardware,
ESP32-Wrover-Kit, Sunton 2432S028/2832S028 variants, and ESP32-S3 work. Select
the intended board in the active ESP-IDF configuration and consult the
`docs/` and `boards/` wiring notes before connecting displays or storage.

This firmware descends from an earlier ESP32 PDP-11 simulator project. Some
historical upstream notes referred to ESP-IDF 4.2/5.2.1, Bluetooth keyboard
pairing, Tetris, and the old `rq.dsk` boot flow; they do not describe the
current S3/Fuzzball boot-menu and SD-portal workflow.
