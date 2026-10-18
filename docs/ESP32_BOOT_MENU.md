# ESP32-S3 boot menu

Status (2026-10-18): the ESP32-S3 N16R8 boot menu and SD upload portal have
been built and exercised on hardware. The portal starts from the boot menu,
shows card inventory and free space in a browser, accepts file uploads to the
SD-card root, and can save Wi-Fi credentials. Serial Wi-Fi setup (`C`),
masked password entry, and clearing saved credentials were also exercised.
The board joined a temporary setup access point after test credentials were
cleared. Joining the user's personal Wi-Fi network has not been tested.

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
firmware displays a ten-second serial boot menu. Press a listed digit to boot
immediately, Enter for the marked default, or wait for the countdown. Press
`W` to start the SD upload portal or `C` to configure/clear Wi-Fi from the
serial console. UART0, native USB Serial/JTAG, and the USB HID keyboard feed
the same input path.
The menu starts with the bundled `/spiffs/Unix_V6.RK05` when present, adds
`/sdcard/BOS6.IMG` only at the expected RD54 image size, then lists `.RK05`
files from the root of `/sdcard` in name order, up to the nine total menu
slots. If present and space remains, the older `/sdcard/rq.dsk` RA92 and
`/spiffs/floppy.dsk` RX profiles are listed too.
Each RK05 selection uses the PDP-11/40, 256 KiB, RK11/RK05 profile. The
selection is stored in NVS only after SIMH reports successful disk attachment
and controller bootstrap; this is **not** proof that the guest finished
booting. On a later reset it is the default. If the chosen SD file is missing,
the menu falls back to the first available entry; reinserting the card
restores the remembered default because the NVS path is retained.

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
later sessions. Upload, Wi-Fi change, and stop actions require the per-session
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

Further physical test checklist:

1. Compare a large portal upload, such as the disposable BOS6 image, against
   the source file's size and SHA-256. The current browser inventory confirms
   names and sizes, not file-content integrity.
2. Test station-mode joining on a trusted Wi-Fi network, confirm the printed
   URL, then test failure fallback with unavailable credentials.
3. Exercise duplicate-name rejection, low-space handling, and an interrupted
   upload using disposable files; confirm incomplete temporary files do not
   appear as normal card files and are cleaned up on portal start.
4. Continue boot-profile checks with disposable media, including a distinct
   `.RK05` image selected from the menu. Do not use canonical images for
   writable guest tests.
5. Test serial and HID menu input separately. The USB-host enumeration error
   previously seen without a keyboard remains a separate hardware issue.
