# ESP32-S3 boot menu

Status (2026-10-18): the ESP32-S3 N16R8 boot menu and SD upload portal have
been built and exercised on hardware. The portal starts from the boot menu,
shows card inventory and free space in a browser, accepts file uploads to the
SD-card root, and can save Wi-Fi credentials. Serial Wi-Fi setup (`C`),
masked password entry, and clearing saved credentials were also exercised.
The board joined a saved Wi-Fi network in station mode during the latest test.
Failure fallback to the setup access point still needs validation.

The browser-visible card inventory and file sizes were confirmed on
2026-10-18. That check does not establish that every image boots or verify
the contents of every uploaded file.

Earlier hardware testing confirmed 4-bit SDMMC mount at 20 MHz, SD image
attachment, RT-11/BOS6 startup, and a DMILLS login (see
`logs/esp32-bos6-20261007-124214.log`). Disk images are writable; a later
portal test briefly auto-started BOS6 and logged guest disk writes. Treat the
card copy as mutable and disposable. Safe guest shutdown, DMV traffic, and
Ethernet packet I/O remain unverified. The USB-host enumeration warning
(`CHECK_SHORT_DEV_DESC`) is separate from the boot menu and portal.

After mounting the SD card and SPIFFS, but before starting the SIMH task, the
firmware displays a BIOS-style serial boot screen. It reports the detected
ESP32 model/core count, point-in-time free heap, PSRAM total/free memory, SD
capacity, discovered profiles, and the selected image's writable status.
It also shows Wi-Fi/BLE chip capabilities and USB HID host state. Press `H` to
open a detailed hardware inventory; `R` refreshes the snapshot and `Q` or
Escape returns to the menu. The inventory reports CPU model/revision, heap,
PSRAM, flash, SPIFFS and SD capacity, radio capabilities, keyboard connection,
and OLED detection. Radio capability is separate from service state: Wi-Fi and
BLE are not started by opening diagnostics. The scan is limited to configured
board devices; unassigned pins, sensors, and accessories are reported as not
checked rather than inferred absent. When initialized, the display row names
the SSD1306-compatible OLED and shows its detected 7-bit I²C address alongside
the configured SDA/SCL GPIO pins. The `H` page also probes standard I²C
addresses `0x08` through `0x77` on the configured GPIO18/GPIO17 bus. It sends
address-only probes with a 20 ms per-address timeout, and `R` repeats the
scan. If an ACKing address matches a device already initialized by a configured
driver, the inventory labels it with that driver's functional device name;
other responders remain unidentified. The displayed OLED label means the
SSD1306-compatible boot display driver initialized at that address; it does not
identify the exact controller model. A bus error stops the scan and is shown as
incomplete.
The screen uses ANSI clear/color sequences; plain-text terminals retain the
content but may not render the intended layout. Use Up/Down or `J/K` to move
the highlight and pause auto-start, Enter to boot the highlighted entry, or a
listed digit to boot immediately. Otherwise, the remembered entry starts
after the configured countdown. Press `W` to start the SD upload portal, `C`
to configure/clear Wi-Fi from the serial console, or `S` to open System
Configuration. That screen can change the default entry, select a 5, 10, 15,
or 30-second countdown, and toggle auto-start. Press Enter to change a value,
`S` to save, or `Q` to cancel. Settings are stored in NVS. If a configured
default is unavailable, the menu falls back to the last successfully started
entry. UART0, native USB Serial/JTAG, and the USB HID keyboard feed the same
input path.
The startup status screen (POST) is separately skippable through the persisted
`Show POST` setting. Press `S` during POST to enter setup immediately. The
configuration screen now has Main, Boot, Storage, Hardware, and Info pages;
Main includes POST and auto-start toggles plus Restore Defaults, while Boot
selects the default entry and timeout. Restore Defaults resets these boot-menu
settings only. Save & Exit commits the settings to NVS. Console appearance
options are still deferred because the current renderer does not implement
them.
The ESP32-S3 build mirrors compact status and menu pages to an optional
128x64 SSD1306 OLED. When SIMH starts, it replaces the BIOS menu with a static
simulator-active status page; it does not mirror simulator text. This build has
that option enabled; it probes I2C address
`0x3C`/`0x3D` on GPIO18/17 and leaves the serial menu available when no OLED
responds. The connected display acknowledged at `0x3C` during the
2026-10-18 hardware check. On that date, the expanded inventory was built and
app-flashed; serial validation confirmed the reported 16 MiB flash, 8 MiB
PSRAM, mounted SPIFFS/SD, Wi-Fi/BLE chip features, USB host state, and OLED
response. A subsequent hardware-inventory check found one I²C responder at
`0x3C` and displayed it as `SSD1306-compatible OLED (boot display)`, matching
the initialized display driver's address. This identifies the configured
functional device, not the exact controller model; additional responders would
remain unidentified. `H`, `R`, and `Q` opened, refreshed,
and exited the page, and the countdown paused while viewing it. The OLED
renderer was exercised for the inventory page without a reported I2C error,
though visual readability has not been independently checked. Keyboard input
has not been verified because the required USB adapter is unavailable. Serial
navigation was checked independently. App-only flashing preserved the
existing partitions and card data. A hardware reset confirmed that saved boot
settings survive reset; the temporary test values were restored afterward.
The menu starts with the bundled `/spiffs/Unix_V6.RK05` when present, adds
`/sdcard/BOS6.IMG` only at the expected RD54 image size, then lists `.RK05`
files from the root of `/sdcard` in name order, up to the nine total menu
slots. If present and space remains, the older `/sdcard/rq.dsk` RA92 and
`/spiffs/floppy.dsk` RX profiles are listed too.
Each RK05 selection uses the PDP-11/40, 256 KiB, RK11/RK05 profile. The
selection is stored in NVS only after SIMH reports successful disk attachment
and controller bootstrap; this is **not** proof that the guest finished
booting. It is the fallback default on a later reset unless System Configuration
sets another default. If the chosen SD file is missing, the menu falls back to
the last successfully started entry, then the first available entry.
Stable entry IDs are now stored in NVS after a successful attachment and
bootstrap. Existing `last_path` preferences remain a fallback for migration;
the path is also saved for compatibility.

The `BOS6.IMG` selection uses the PDP-11/73 with 2 MiB RAM, a 60 Hz clock,
RQ0/RD54, KWV11, temporary DMV vector `0300`, and five DLI/DLO lines. This is
a no-peer boot profile; DMV traffic and Ethernet packet I/O are not validated
by selecting it. The attached disk is writable. Copy the canonical
`fuzzball_bos6_autostart.img` to the SD card as `BOS6.IMG`, verify the copy's
size and SHA-256 (`ef33a6cd1f8c4f35ade69e933f295d5675f2c2a5b269a13cfbf3fe82d11b911b`),
and treat the card copy as disposable guest media.

For initial testing, copy **disposable** RK05 images onto a FAT32 card under
distinct names ending in `.RK05`. Do not use a canonical source disk: SIMH
opens attached disks writable, and pressing reset is not a guest shutdown.
The boot menu does not copy images onto the card; use the SD upload portal or
copy them from a computer while the card is unmounted from the ESP32. The menu
does not inspect an image's guest OS or validate RK05 geometry beyond an
openable non-empty regular file. A wrong or corrupt image can still fail after
selection.

## SD upload portal

The portal runs before SIMH starts, while the boot menu is active. Press `W`
to start it. With no saved Wi-Fi credentials, it starts a temporary WPA2
access point and prints the network name, generated password, upload access
code, and `http://192.168.4.1/` address to the serial console. With saved
credentials, it tries station mode and prints the assigned URL; if connection
fails, it falls back to the setup access point.

The browser page displays free space and the root-directory file list, accepts
multiple uploads with progress, and offers a way to save Wi-Fi credentials for
later sessions. After a completed upload, the portal reads the file back from
the card and reports its SHA-256 digest. Upload, Wi-Fi change, and stop actions require the per-session
access code printed on serial. Read-only status and file listing are available
without the code. Files are placed in the SD root; duplicate names are
rejected, upload names are restricted, and transfers use hidden temporary
files that are renamed after a complete write. The portal reserves space on
the card and allows uploads up to 1 GiB. It does not offer delete, rename,
format, or disk-image editing. Finish and return in the browser, press `Q` on
serial, or leave the portal idle for ten minutes to return to the boot menu.

The portal uses plain HTTP, not HTTPS. Use it only on a trusted local network;
do not port-forward or expose it to the internet.

To configure Wi-Fi from serial, press `C`, enter the SSID and password (the
password is masked), then press Enter. An empty password is accepted for an
open network. To clear credentials, leave the SSID blank and confirm with
`Y`. Credentials are held in NVS; test networks can be removed this way.

Other `.IMG`/`.DSK` files are not inferred as bootable images. Additional disk
types need explicit machine profiles and verified copies; avoid auto-booting
arbitrary files using the Unix RK05 settings.

Remaining validation checklist:

1. Upload a disposable large file, then compare its reported on-card SHA-256
   and size with the source. An 8 MiB upload attempt did not complete: the
   ESP32 hit an interrupt watchdog and rebooted while receiving it. The browser
   reached the portal, but no upload integrity result was produced. The WDT
   backtrace ran through Wi-Fi power management on CPU0; the underlying trigger
   is not proven. Current source disables station modem sleep during portal
   use, pins HTTP handling to CPU1 on dual-core targets, yields during transfer
   and hashing, and logs receive/flush/hash stages. This mitigation builds but
   has not been flashed or retested yet.
2. Test station-mode failure fallback with unavailable credentials. Successful
   station association to the saved network, DHCP address assignment, and
   browser reachability were confirmed.
3. Exercise duplicate-name rejection, low-space handling, and an interrupted
   upload using disposable files; confirm incomplete temporary files do not
   appear as normal card files and are cleaned up on portal start. Low-space
   physical testing requires disposable or nearly-full media.
4. Check OLED text for clipping/readability when convenient. Test HID menu
   input separately when the required adapter is available; until then, HID
   navigation remains unverified.
5. Continue boot-profile checks with disposable media, including a distinct
   `.RK05` image selected from the menu. Do not use canonical images for
   writable guest tests.

During the failed upload attempt, the ESP32 rebooted and its configured
auto-start launched `/sdcard/BOS6.IMG`. The guest reached the RT-11/BOS6
prompt; disk writes may have occurred before reaching it. The board was left
running without another reset because simulator return is not yet safe. An
unclean-reset guard has been added to pause auto-start after watchdog, panic,
and brownout resets. The guard builds successfully but has not been flashed or
validated on hardware.
