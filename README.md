# esppdp Fuzzball

This fork runs a PDP-11 simulator on ESP32 hardware and provides a small serial
and display-based console. The current Fuzzball work adds boot selection for
known disk profiles and a local web portal for transferring files to the SD
card.

## Current ESP32-S3 features

- Early boot menu with the bundled Unix V6 RK05, a size-checked BOS6 RD54
  image, supported `.RK05` files from the SD-card root, and the existing
  `rq.dsk` and floppy profiles.
- SD-card upload portal, started from the boot menu with `W`. It reports card
  space and lists files; uploads show progress and are written to the SD-card
  root. Existing filenames are not overwritten.
- Wi-Fi setup from the serial boot menu with `C`. Credentials can also be
  saved in the portal. They are stored in device NVS. The portal first tries
  the saved network and falls back to a temporary access point if it cannot
  connect.
- Optional board display support; see the wiring notes for display paths.

For exact controls, Wi-Fi setup, upload steps, supported media, hardware test
results, and limitations, see [the firmware guide](firmware/README.md),
[the ESP32 boot-menu notes](docs/ESP32_BOOT_MENU.md), and the
[boot-menu expansion plan](docs/ESP32_BOOT_MENU_EXPANSION_PLAN.md).

## Safety and validation

Attached guest disk images are writable. Use disposable copies and shut down a
guest cleanly when possible; resetting the ESP32 is not a guest shutdown. The
web portal uploads files but does not edit, delete, rename, or format existing
card contents. A successful upload or simulator disk attachment does not by
itself establish that the guest OS booted successfully.

The S3 portal, serial Wi-Fi credential entry, and SD-card file listing have
been exercised on hardware. Joining a user's personal Wi-Fi network and every
uploaded image's guest boot behavior are separate checks.

## Fork history

This repository began as an ESP32 PDP-11 simulator fork. Earlier work included
ESP-IDF 5.x build fixes and integration for the Sunton 2832S028 board. The
current Fuzzball branch uses ESP-IDF 6.0.2 for the ESP32-S3 work; the older
upstream setup notes in `firmware/README.md` are retained as historical
context where they differ from the current S3 workflow.
