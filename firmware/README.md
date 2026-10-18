# ESP32 firmware

This firmware runs the PDP-11 SIMH port on ESP32 boards and provides the
console, storage mount, and optional display support. The current S3/Fuzzball
work has a serial boot menu and an SD-card web portal.

## Boot menu

After mounting SPIFFS and the FAT SD card, the firmware lists supported boot
images. Select an entry by number, press Enter for the remembered default, or
wait for the ten-second timeout. The menu includes the bundled Unix V6 RK05,
`BOS6.IMG` when it matches the expected 159,334,400-byte RD54 image size,
root-level `.RK05` files, and the existing `rq.dsk` and floppy profiles.

At the menu:

- Press **`W`** to start the SD upload portal while SIMH is stopped.
- Press **`C`** to set Wi-Fi credentials from the serial console. Enter an
  SSID and password; password characters are masked. An empty password selects
  an open network. To clear saved credentials, submit an empty SSID and
  confirm with `Y`.
- Press a listed number to start that boot profile.

Credentials are stored in the ESP32's NVS partition. The firmware does not
put network passwords in source code. Use the serial console at the configured
ESP-IDF monitor baud rate to enter them. `Ctrl-C` or Escape cancels a prompt.

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
the transfer completes. Interrupted temporary uploads are discarded on the
next portal start. The portal does not delete, rename, format, or modify
existing files or disk-image contents.

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
