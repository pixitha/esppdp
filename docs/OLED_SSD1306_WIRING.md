# 0.96-inch I2C OLED console

The current console display test uses a common 0.96-inch, four-pin SSD1306 OLED breakout.

| OLED label | ESP32-S3 DevKitC header label | Official J1 position |
| --- | --- | --- |
| GND | G / GND | 22 (or a verified GND elsewhere) |
| VCC | 3V3, **not 5V** | 1 or 2 |
| SCL | 17 / GPIO17 | 10 |
| SDA | 18 / GPIO18 | 11 |

These are **electrical net names, not the order of the OLED's four pins**.
Read the silkscreen on this particular OLED breakout, then trace each wire
from that label to the DevKitC label. Espressif's v1.0 and v1.1 DevKitC guides
agree on these four J1 positions. On J1, `5V` is position 21 immediately
beside `G` at position 22, while the two `3V3` pins are at positions 1 and 2
at the opposite end. That layout makes a one-position mistake near ground
especially consequential. The observed GPIO48 addressable LED identifies our
board as the *initial/GPIO48 variant* (or a board following that design), not
proof that it is an official v1.1 PCB; the OLED J1 pin mapping is the same in
both official revisions.

The firmware probes both common SSD1306 addresses, `0x3C` and `0x3D`; this board
responded at `0x3C`. The diagnostic draws a checkerboard, then clears the panel
and routes Fuzzball terminal characters to the OLED console.

The OLED is powered at 3.3 V even when the ESP32-S3 board itself is powered
through its 5 V/VIN input.

## Current hardware status (2026-09-22)

The OLED was previously disconnected after it failed to light and became hot.
The user subsequently measured 3.3 V at the OLED input and requested a
retest. The OLED test was re-enabled in `firmware/sdkconfig`; the ESP-IDF
6.0.2 build passed, and the app-only flash verified. On the new boot, the
display ACKed at `0x3C`, initialization completed, and the user reported
visible text and a cool module. This establishes a working display on the
present wiring; the earlier heating cause is still unknown. Stop and remove
power if heating recurs.

The same run mounted the SD card and attached the bundled
`/spiffs/Unix_V6.RK05` image. The serial console showed `@` after a delay,
and received `rkunix` over UART0, but no subsequent guest echo or login was
observed before the monitor was closed. OLED success and guest-boot success
must be tracked separately.

Safe check before any future rewiring or if heating recurs:

1. Unplug *all* board power. Read the labels printed beside each pin on both
   sides of the OLED, and verify continuity from each loose lead to its
   intended board header. Do not infer polarity from wire color or physical
   left-to-right order.
2. With the OLED still disconnected, power the ESP32 and measure the voltage
   **at the loose OLED-end wires**: red meter probe on the intended VCC lead,
   black on the intended GND lead. It should be approximately **+3.3 V**,
   never negative or +5 V. Power off before moving wires.
3. With all power removed, check for a near-short between the OLED module's
   own VCC and GND pins. A clearly shorted or heat-damaged module should not
   be reconnected. If the exact breakout is not rated for 3.3 V supply, stop
   and identify its requirements first.
4. Only after polarity and voltage are confirmed should the display be
   reconnected for a brief test. If it heats again, disconnect power
   immediately; do not attempt to solve heating in firmware.

Reversing SDA and SCL can prevent an I2C ACK, but ordinary signal-pin reversal
would not normally make the display hot. Heating points more strongly to
incorrect VCC/GND, 5 V on a 3.3 V-only module, a short, or a damaged display.
