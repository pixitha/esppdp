# ESP32-S3 DevKitC external microSD plan

Status (2026-09-22): the user reports the breakout wired to the proposed
pins, and confirmed the test card is safe to use. SDMMC initialization and a
512-byte write/readback have passed in 1-bit mode at 5 MHz. Four-bit mode
passed at both 5 and 20 MHz; a 1 MiB write/readback also passed at 20 MHz.
The diagnostic-only app was physically tested. The working-tree configuration
now disables `CONFIG_ESPPDP_SD_CARD_DIAG`. The pre-SIMH RK05 boot menu was
app-flashed on 2026-09-22, preserving SPIFFS, and tested with the card in
4-bit mode at 20 MHz. UART selection and remembered-default timeout attached
the SD RK05 image successfully; no interactive guest boot was established.
See [ESP32_BOOT_MENU.md](ESP32_BOOT_MENU.md) for the multi-image behavior and
remaining checks.
The separate BOS6/RD54 SIMH profile remains future work.

## Why this is needed

The known BOS6 RD54 file is 159,334,400 bytes (152 MiB), much larger than the
N16R8 board's 16 MiB flash. We need removable storage for a *copy* of that
image. A 4–32 GB FAT32 microSD card is ample for the first test. Keep the
source image and its checksum unchanged; do not boot the only copy writable.

## Breakout choice

Use the on-hand [Adafruit Micro SD SPI or SDIO Card Breakout, 3V only, PID
4682](https://www.adafruit.com/product/4682). Its documented pinout exposes
both SPI and SDIO and provides pull-ups on the SD logic pins. This matters
because [Espressif says the DevKitC-1 has no SD pull-ups](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/sd_pullup_requirements.html).
The breakout's `3V` pin and logic are **3.3 V only** even if the ESP32 board
itself is powered through its 5 V input. Do not connect its `3V` pin to 5 V.

The older Pico research also mentioned the [SparkFun microSD Transflash
Breakout BOB-00544](https://www.sparkfun.com/sparkfun-microsd-transflash-breakout.html).
It remains an alternative, but verify its schematic and pull-ups before using
it here. Avoid unidentified 5 V Arduino SD modules with resistor-divider level
shifting; the Pico notes warn that these can degrade 3.3 V SD signaling.

## Proposed first wiring: four-bit SDMMC

Use short leads and a common ground. The GPIO numbers below are the board
silkscreen names, **not** physical header positions. This preserves the OLED
on GPIO17/18, native USB on 19/20, UART on 43/44, RGB LED on 48, and the
separate e-paper experiment on 7–12. It avoids GPIO35–37, used internally by
the N16R8 module's octal PSRAM. All six proposed signal GPIOs are on the
DevKitC's J1 header, keeping prototype wiring on one side of the board.
Verify the breakout's printed labels before powering it.

| Adafruit breakout | SDMMC signal | ESP32-S3 DevKitC |
| --- | --- | --- |
| `3V` | power | `3V3` |
| `GND` | common ground | `GND` |
| `CLK` | clock | GPIO14 |
| `CMD` | bidirectional command | GPIO13 |
| `D0` | data bit 0 | GPIO4 |
| `D1` | data bit 1 | GPIO6 |
| `DAT2` | data bit 2 | GPIO15 |
| `D3` | data bit 3 | GPIO5 |
| `DET` | card detect, optional | leave open initially |

The Adafruit pin names and onboard pull-ups are documented in its
[pinout guide](https://learn.adafruit.com/adafruit-microsd-spi-sdio/pinouts).
The DevKitC header exposure and pin names are in Espressif's
[v1.1 board guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html).
This assignment keeps the old SPI proposal's GPIO14 clock, GPIO13 card
command, GPIO4 data-out, and GPIO5 chip-select/card-D3 signals, adding
GPIO6/15 for D1/D2. The CLK/CMD/D0 part of this map passed a 1-bit card test;
D1-D3 also passed a 4-bit card test at 5 MHz. The actual breakout must be
wired using its SDIO labels, not the SPI-only legend.

The ESP32-S3's SDMMC host can route these signals through the GPIO matrix;
ESP-IDF 6.0 documents explicit `slot.clk`, `slot.cmd`, and `slot.d0`–`slot.d3`
assignments. The original `SD_MMC` branch in `firmware/main/main.c` did
not set those S3 pins and contained comments for older ESP32 wiring. The
S3 profile now assigns all six pins explicitly. [ESP-IDF 6.0
SDMMC host guide](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/peripherals/sdmmc_host.html).

## Bring-up sequence

1. Inspect the actual breakout and card before powering. Confirm it is a
   3.3 V-only breakout with pull-ups, not an unknown 5 V module. Check power
   and ground polarity with a meter; keep wires short.
2. Add an S3-specific SDMMC configuration with the six GPIO assignments
   above. Retain the other board profiles unchanged. Set `slot.width = 1` for
   the first mount, despite wiring all four data lines. Start below the
   default 20 MHz if jumper-wire signal integrity requires it.
3. First firmware test should mount a disposable FAT32 card, print card
   information, read a known test file, then create/read back a small scratch
   file. No SIMH disk image yet. On failure, check card seating, power and
   wiring before changing controller code. Once 1-bit read/write is stable,
   switch to 4-bit at the default 20 MHz and repeat the same tests. Measure
   throughput and error rate before trying 40 MHz.
4. Copy the known BOS6 RD54 image to the card under a **new filename** and
   verify its size and SHA-256 on the Mac. Keep an immutable desktop source
   image and a separately named writable board copy. Verify the board can
   open and read the copied file before enabling any guest writes.
5. Add the separate 11/73, 2 MiB, RQ0/RD54 BOS startup profile. Verify
   attach, boot-monitor output, and guest activity separately. Only then run
   controlled read/write and reset-recovery tests; the physical reset button
   is not a clean Unix shutdown.

SPI3 remains a fallback if SDMMC proves electrically unreliable on the
prototype wiring. In that case, reconfigure the breakout as SPI with GPIO14
CLK, GPIO13 SI/MOSI, GPIO4 SO/MISO, and GPIO5 CS; do not run both host drivers
against one card at the same time. The existing SDSPI/FAT mount branch can be
adapted for that fallback. The SDMMC path has passed bounded physical tests;
SPI has not been tested on this breakout.

## First physical observations (2026-09-22)

- ESP-IDF 6.0.2 app build and app-only flash passed. The boot log confirms
  1-bit SDMMC at 5 MHz and the intended GPIOs (14/13/4/6/15/5).
- First boot stopped in an optional SDIO I/O-device reset probe with invalid
  response `0x108`. The S3 profile now disables
  `CONFIG_SD_ENABLE_SDIO_SUPPORT`; that option is for SDIO peripherals, not
  the SD memory-card 1-bit/4-bit bus.
- After the rebuild and app-only flash, initialization advanced to
  `sdmmc_init_ocr: send_op_cond (1) returned 0x107` (timeout). No FAT mount
  occurred, so the scratch-file write/verify code did not run.
- The OLED gave no I2C ACK at `0x3c` or `0x3d` because it was intentionally
  disconnected: it had not turned on and was getting hot. Leave it disconnected
  until its power polarity, pin labels, and wiring have been checked. This is
  separate from the SD-card initialization timeout.
- The serial monitor was exited cleanly after each observation. Before another
  firmware change or speed increase, confirm the card is fully seated,
  breakout `3V` is on board `3V3`, and grounds are common; then recheck each
  signal against the table. A single clean power cycle is the next probe.

## Reconnected-card result (2026-09-22)

- With the OLED still disconnected and the SD card reconnected, the existing
  diagnostic image booted and mounted the card over 1-bit SDMMC at 5 MHz.
  ESP-IDF identified an SDHC card of 7618 MB (15,601,664 512-byte sectors).
- A 512-byte scratch file, `/sdcard/TC9C3570.TST`, passed write, close,
  reopen, readback, and byte comparison. The diagnostic leaves this file on
  the card; it was not removed. No guest disk image was accessed.
- `No OLED ACK` remains expected with the OLED unplugged. The diagnostic
  reported completion and deliberately did not start SIMH. The serial monitor
  was exited cleanly.
- This validates basic SD card initialization and FAT read/write at 5 MHz,
  not sustained I/O, 4-bit mode, Unix V6 media operation, or safe reset/shutdown.

## Four-bit, 5 MHz result (2026-09-22)

- An app-only flash retained the SD diagnostic build and SPIFFS image, but
  changed the SDMMC slot width to four bits at the same 5 MHz clock.
- The card mounted as SDHC, 7618 MB, and reported `SSR: bus_width=4`.
  A second 512-byte scratch file, `/sdcard/TECA3751.TST`, passed write,
  close, reopen, readback, and byte comparison. It remains on the test card.
- The serial monitor was closed cleanly. This validates the additional data
  lines for basic transfers, but not sustained throughput or a SIMH image.

## Four-bit, 20 MHz result (2026-09-22)

- The SDHC card mounted with `SSR: bus_width=4` and `Speed: 20.00 MHz`.
  A 512-byte scratch-file write/readback passed.
- A separate 1 MiB patterned scratch-file write, close, reopen, full
  byte-for-byte readback, and close passed. The FAT file path measured about
  692 KiB/s write and 1370 KiB/s read on the first run. The diagnostic now
  creates its scratch file exclusively and removes it after a successful
  comparison; if a test fails, the file is left for inspection.
- A final app-only flash added a read-only `/sdcard/BOS6.IMG` size and
  SHA-256 probe. A second 1 MiB scratch test passed at about 681 KiB/s write
  and 1221 KiB/s read. The board then reported that `BOS6.IMG` is absent,
  as expected. The image-hashing branch has **not** yet been exercised on
  hardware. After disabling the unplugged OLED's optional boot probe, a
  final boot passed the 1 MiB test again (687 KiB/s write, 1381 KiB/s read)
  and reported the BOS image pending without an OLED error. SIMH remained
  stopped and the serial monitor was closed.
- These are short functional tests, not a long-duration signal-integrity or
  power-loss qualification. Keep 20 MHz as the tested prototype setting;
  there is no need to try 40 MHz before the first image validation.

The standalone board diagnostic now includes a separate 8 MiB sequential
FAT/VFS SD benchmark. Its first hardware run passed at 4-bit/20 MHz: 686 KiB/s
write-and-close (11,941 ms) and 1,224 KiB/s reopen/read/verify/close
(6,692 ms). The test's exclusive scratch file was removed and the card
unmounted. The Fuzzball diagnostic app was then restored by app-only flash
and passed its 1 MiB check again. See
[`firmware_diag/README.md`](../firmware_diag/README.md) for method and limits.

## Prepare the BOS image for the next read-only probe

The source file is
`/Users/kduren/projects/pdp11_rt11/fuzzball_workspace/disks/active/fuzzball_bos6_autostart.img`.
Its current size is 159,334,400 bytes and SHA-256 is
`ef33a6cd1f8c4f35ade69e933f295d5675f2c2a5b269a13cfbf3fe82d11b911b`.
Preserve it unchanged. Power off the ESP32 before removing the microSD card,
put the card in a Mac reader, and identify its actual mounted volume. Do not
format it or overwrite a pre-existing `BOS6.IMG`. Copy the source as
`BOS6.IMG` at the card's filesystem root, verify the copied file's size and
SHA-256 on the Mac, and eject the card before returning it to the breakout.
For example, after substituting the card's real volume name:

```bash
diskutil list external physical
sd_volume="/Volumes/YOUR_CARD_VOLUME"
ls -l "$sd_volume/BOS6.IMG"  # stop if this already exists
cp -n /Users/kduren/projects/pdp11_rt11/fuzzball_workspace/disks/active/fuzzball_bos6_autostart.img "$sd_volume/BOS6.IMG"
stat -f '%z bytes %N' "$sd_volume/BOS6.IMG"
shasum -a 256 "$sd_volume/BOS6.IMG"
diskutil eject "$sd_volume"
```

The `cp -n` option prevents overwriting an existing card file. Confirm the
printed size and hash match the values above before putting the card back.
The current firmware will then stream and hash that file read-only and stop
before SIMH starts. A matching board-side SHA-256 is the gate for the later
RQ0/RD54 startup profile; it is not a guest boot claim.
