# 1.54-inch e-paper side-header wiring

This is the wiring reference for the MH-ET/GDEW0154Z04-style e-paper panel used with the ESP32-S3 Fuzzball work. The display is connected through the **side header pins**, not through the keyed connector plug.

## Header order and wire colors

| Side-header signal | Wire color | ESP32-S3 target |
| --- | --- | ---: |
| VCC | red | 3V3 |
| GND | black | GND |
| SDI (MOSI) | purple | GPIO11 |
| SCLK | white | GPIO12 |
| CS | blue | GPIO10 |
| D/C | orange | GPIO9 |
| RESET | green | GPIO8 |
| BUSY | yellow | GPIO7 |

The panel logic and supply are 3.3 V. Do not connect VCC to 5 V, and do not assume the keyed connector has the same pin order as this side header.

The native driver is in `firmware/components/epaper_154c/` and targets the 200x200, black/white/red GDEW0154Z04 / IL0376F panel family. This panel requires full refreshes; it is not a live high-rate terminal display.

## Bring-up checklist

1. Verify the side-header labels before applying power.
2. Connect red to 3V3 and black to GND first.
3. Check that the ESP32-S3 GPIO assignments match the table above.
4. Run the dedicated e-paper diagnostic before enabling console rendering.
