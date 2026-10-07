# ESP32-S3 flash diagnostic image

This is a deliberately separate, minimal ESP-IDF project for board bring-up.
It uses the conservative DIO/20 MHz **SPI flash** profile and reports chip,
flash, PSRAM, heap, repeated flash-read, and RAM pattern-test results over
the USB serial console. It also mounts the external microSD card over 4-bit
SDMMC at 20 MHz and benchmarks an 8 MiB FAT scratch file. If the 0.96-inch
SSD1306 OLED is connected, phase and final-result labels are also shown on
the display; an absent OLED is non-fatal. The OLED that became hot should
remain disconnected until its wiring and condition are checked. This image
does not initialize SIMH or Wi-Fi.

The SD benchmark uses the verified Adafruit breakout wiring: `CLK=GPIO14`,
`CMD=GPIO13`, `D0=GPIO4`, `D1=GPIO6`, `D2=GPIO15`, `D3=GPIO5`, breakout `3V`
to board `3V3`, and common ground. It never formats the card. It checks for
at least 9 MiB free, creates a new 8.3-named scratch file exclusively under
`/sdcard`, writes 8 MiB in 16 KiB chunks, closes it, reopens it, verifies
every byte while reading, and reports end-to-end FAT write/read throughput in
KiB/s. A successful test removes its own scratch file and unmounts the card.
A failed test leaves its named scratch file for inspection; no pre-existing
file or SIMH disk image is opened for writing. Expect 8 MiB of card writes
each time this diagnostic boots successfully.

OLED wiring for the ESP32-S3 DevKitC-1 N16R8:

```
OLED GND -> ESP32 GND
OLED VCC -> ESP32 3V3
OLED SCL -> GPIO17
OLED SDA -> GPIO18
```

The driver probes I2C addresses `0x3C` and `0x3D`.

Build from this directory:

```sh
source /Users/kduren/.espressif/tools/activate_idf_v6.0.2.sh
idf.py set-target esp32s3
idf.py build
```

On a board that already has the Fuzzball partition table and SPIFFS disk
image, flash **only the application partition**:

```sh
idf.py -p /dev/cu.usbmodemPORT app-flash monitor
```

Do not use `idf.py flash` on that board: a full flash replaces its partition
table and may make the existing SPIFFS guest image inaccessible. The
diagnostic app temporarily replaces the Fuzzball app; restore the Fuzzball
app afterward using `app-flash` from `../firmware`. App-only flashing also
retains the installed bootloader's SPI-flash clock setting; the benchmark's
20 MHz SDMMC clock is independent of that setting.

## First hardware benchmark (2026-09-22)

On the ESP32-S3-WROOM-1 N16R8 with the Adafruit SDIO breakout and an SDHC
card, the standalone diagnostic reported `SSR: bus_width=4` and
`Speed: 20.00 MHz`. One 8 MiB end-to-end FAT run passed byte-for-byte:

| Phase | Time | Throughput |
| --- | ---: | ---: |
| Write and close | 11,941 ms | 686 KiB/s |
| Reopen, read, verify, and close | 6,692 ms | 1,224 KiB/s |

The diagnostic reported `RESULT: PASS`, removed its own scratch file
`/sdcard/B6D651E3.TMP`, and unmounted the card. Flash read, internal RAM,
and PSRAM tests also passed. The OLED was disconnected and continued
serial-only. These numbers are one run through the FAT/VFS path, including
pattern generation/verification and file close; they are **not** a raw SDMMC
bus-speed measurement or a long-duration reliability claim. The Fuzzball SD
diagnostic app was restored afterward with app-only flash and passed its
1 MiB check again; its `BOS6.IMG` checksum probe remains pending until that
file is copied to the card.
