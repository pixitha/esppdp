# ESP32-S3 boot menu

Status (2026-10-07): the BOS6 menu selection has now been tested on the S3,
including SD attachment, RT-11/BOS6 startup, and a DMILLS login. See
`logs/esp32-bos6-20261007-124214.log`. Network traffic and safe guest-disk
shutdown remain unverified. The following dated notes describe the earlier
bring-up state.

On 2026-09-22, the menu was implemented, built with ESP-IDF 6.0.2, and tested on the
ESP32-S3 N16R8. The S3 configuration has `CONFIG_ESPPDP_SD_CARD_DIAG`
disabled, allowing normal SIMH startup. The firmware was installed with
`app-flash`, preserving the existing SPIFFS partition. Hardware testing
confirmed 4-bit SDMMC mount at 20 MHz, a three-entry menu, timeout to the
bundled Unix image, UART0 selection of `/sdcard/Unix_V6.RK05`, successful
SIMH attachment of that SD image, and NVS persistence of the SD selection
across reset. The second reset also timed out to and attached the SD image.
This does **not** establish an interactive guest boot; the capture ended at
`Main sim start` without a guest prompt. The USB-host enumeration warning
(`CHECK_SHORT_DEV_DESC`) still appeared and remains a separate issue.

A working-tree addition now offers `/sdcard/BOS6.IMG` when it is exactly
159,334,400 bytes. It selects an 11/73, 2 MiB, 60 Hz, RQ0/RD54 profile with
KWV11, DMV, and DLI/DLO configured to match the desktop no-peer boot setup.
This addition builds with ESP-IDF 6.0.2, but has not been flashed or tested on
the board. No Fuzzball guest boot has been demonstrated on the S3.

After mounting the SD card and SPIFFS, but before starting the SIMH task, the
firmware displays a ten-second serial boot menu. Press a listed digit to boot
immediately, Enter for the marked default, or wait for the countdown. UART0,
native USB Serial/JTAG, and the USB HID keyboard feed the same input path.
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
The menu does not copy images onto the card; do that on the Mac with the card
unmounted from the ESP32. It also does not inspect an image's guest OS or
validate RK05 geometry beyond an openable non-empty regular file. A wrong or
corrupt image can still fail after selection.

Other `.IMG`/`.DSK` files are not inferred as bootable images. Additional disk
types need explicit machine profiles and verified copies; avoid auto-booting
arbitrary files using the Unix RK05 settings.

Further physical test checklist:

1. Copy the 159,334,400-byte BOS6 fixture to the card as `BOS6.IMG`; verify
   the exact size and SHA-256 before reinserting it. Select it by number and
   confirm the log reports 11/73, 2048K, and RQ0/RD54 before assessing guest
   boot. This copy is writable and disposable.
2. Boot with two distinct copied `.RK05` files on SD; select each by number.
   Confirm the selected path and expected boot behavior.
3. Remove the card and confirm fallback, then reinsert it and confirm the
   remembered SD choice returns.
4. Test input on the intended serial connector and HID keyboard separately.
   The USB host enumeration error previously seen without a keyboard remains
   a separate hardware/USB-host issue.
