# ESP32-S3 diagnostic ROM report

- Date: 2026-09-17
- Board: ESP32-S3-WROOM-1 N16R8 on DevKitC
- Chip: ESP32-S3 revision 0.2
- Diagnostic image: `esppdp_flashdiag`, ESP-IDF 6.0.2
- Flash profile: DIO, 20 MHz (temporary conservative bring-up profile)

## Observed results

The ROM and second-stage bootloader load successfully. The bootloader reports
16 MB flash and loads the diagnostic application. PSRAM initialization passes:

```text
esp_psram: SPI SRAM memory test OK
spi_flash: detected chip: boya
spi_flash: flash io: dio
flashdiag: chip revision 2, cores 2, PSRAM 8388608 bytes
flashdiag: free internal heap: 393487, free PSRAM heap: 8386156
```

The diagnostic application then panics before printing its flash-test result:

```text
Guru Meditation Error: Core 0 panic'ed (LoadProhibited)
EXCVADDR: 0x0000001b
```

The complete raw serial capture is preserved in
[`logs/ESP32_S3_DIAGNOSTIC_2026-09-17.serial.log`](../logs/ESP32_S3_DIAGNOSTIC_2026-09-17.serial.log).

## March/flash run captured from `idf.py monitor`

The first complete run produced:

```text
PASS: full flash read (0x01000000 bytes), checksum 0x790b30ef
internal RAM March C-: PASS (65536 bytes)
FAIL: PSRAM allocation (8388608 bytes)
RESULT: FAIL
```

The flash walk briefly triggered the task watchdog because it did not yield
while reading. The read continued and completed; this is a scheduling issue,
not a flash error. The PSRAM failure was an over-large allocation request: IDF
reports 8 MiB installed, but reserves a small amount for runtime use.

## Interpretation

The capture proves bootloader, flash mapping, application loading, the complete
flash read, and internal RAM March C-. The next image yields during the flash
walk and tests the largest contiguous usable PSRAM block, reporting any
reserved bytes explicitly. The 20 MHz setting is not being treated as a board
requirement; it was only used to remove timing as a variable while investigating
the original image hash failure.
