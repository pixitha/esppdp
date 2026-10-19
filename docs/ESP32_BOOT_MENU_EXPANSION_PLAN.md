# ESP32 boot menu expansion plan

Status: implementation in progress, 2026-10-18

## Purpose

Grow the current early-boot disk selector into a small, dependable front end
for choosing and starting the PDP-11 environments supported by this firmware.
Use the ESP32 [Launcher project](https://github.com/bmorcelli/Launcher) as
inspiration for discoverability, persistent preferences, and multiple input
methods. Do not treat it as a firmware to port or as a reason to reproduce its
OTA, partition-management, or general-purpose file-management features.

## Starting point

`firmware/main/boot_menu.c` currently builds a fixed list of up to nine items
after storage is mounted. It offers the bundled Unix V6 RK05 image, a
size-checked `BOS6.IMG`, root-level `.RK05` files on the SD card, and the
existing `rq.dsk` and `floppy.dsk` profiles. It selects a remembered path from
NVS, accepts a digit or Enter through SIMH's keyboard poll path, and starts
after ten seconds by default. `scp.c` applies the selected machine profile;
the selected path is remembered after disk attachment and bootstrap succeed.

SD/FAT is already mounted at `/sdcard`, but the current firmware has no
browser-based file transfer UI or HTTP routes. The existing Wi-Fi code is tied
to SIMH networking for other profiles; the ESP32-S3 DevKitC profile currently
stubs that interface out. Treat browser access as a separate, explicitly
managed service rather than reusing the simulator's packet path.

The path has demonstrated RT-11/BOS6 startup and a DMILLS login on the S3.
That does not establish packet I/O, orderly guest shutdown, or safe persistence
of guest disk writes. Treat SD images as writable and disposable until the
separate storage lifecycle work defines and validates a safe shutdown and
recovery model. The current SSD1306 component is a minimal console renderer;
its integration as a boot-menu display and its hardware behavior in this flow
are not yet validated.

## Product boundary

The menu manages SIMH boot choices and safe system tools. A focused local web
portal for copying files onto the SD card is in scope. It does not flash
alternate ESP32 firmware, rewrite flash partitions, modify the contents of
disk images, infer machine settings for unknown media, or promise that a guest
has shut down safely. It must keep serial boot and recovery available when
there is no display, network, or usable SD card.

## Proposed user experience

The supplied screens suggest a BIOS-style presentation: a brief POST, a
default boot countdown, a primary boot menu, and a setup screen with Main,
Boot, Storage, Hardware, and Info sections. Treat the ASCII screens as
interaction and visual direction, not as a claim that every listed device or
function already exists.

1. Show a short startup/POST screen with the Fuzzball System BIOS identity,
   version, and a clear route to setup or the boot menu. Keep timeout and
   default configurable; initially use the last successful choice, with a
   safe built-in fallback.
2. Show status rows only for components the firmware can probe. Use explicit
   states such as `OK`, `Not detected`, `Unavailable`, or `Not checked`; do
   not print `OK` for external storage, a keyboard controller, or other
   unimplemented hardware. Hardware entries in the mockup are placeholders
   until the board and interface are confirmed.
3. Present boot choices with descriptive names, machine/profile, media source,
   and availability. For example: `Fuzzball / RT-11 — PDP-11/73 — SD card`,
   `PDP-11 Diagnostics` when a concrete diagnostic image and profile are
   configured, and `SIMH Monitor` for the existing simulator console.
4. Add `System Configuration` for persistent boot and console preferences.
   Start with default boot, timeout, show POST, and auto-start. Terminal mode,
   font, and cursor style may appear only when the chosen renderer actually
   supports them; otherwise keep them documented as future options.
5. Treat `Storage Manager` as an inventory/status view with an `Upload files`
   action backed by the local web portal. Support adding files to documented SD
   locations. Defer delete, rename, format, backup, and restore actions until
   their data-loss and writable-media behavior is designed.
6. Add `Hardware Diagnostics` as read-only probes with honest per-device
   results and a detailed report, building toward the example below. Keep
   destructive or long-running tests behind a deliberate action and report
   their scope before running them.
7. Keep `Firmware Manager` out of the first implementation. Alternate firmware
   updates, partition changes, and recovery workflows require a separate
   design, signed/verified artifact handling, and board-specific validation.
8. Navigate with whichever input is present: serial keys and existing USB HID
   keyboard support first, with F-key shortcuts only when the input stack
   delivers them. Add the small display as an optional view, not a new
   requirement. Do not assume touch or board buttons without verifying the
   target hardware and wiring.
9. Before starting, show the selected machine, memory, controller, and disk.
   Make writable-media status visible. Confirm before any future action that
   can erase, format, or overwrite data.
10. After launch, show clear progress or failure at the existing milestones:
    media found, machine configured, disk attached, bootstrap started. Do not
    label these milestones as a successful guest boot or network test.

### Initial menu map

| Screen item | Initial behavior | Boundary |
| --- | --- | --- |
| Fuzzball / RT-11 | Start a configured, available RT-11/Fuzzball entry | Require a known profile and image validation |
| PDP-11 Diagnostics | Start a named diagnostic image/profile when one is supplied | Do not imply a diagnostic suite exists before it does |
| SIMH Monitor | Enter the supported SIMH monitor/console path | Keep behavior consistent with the existing console lifecycle |
| System Configuration | View/edit supported preferences and save them | Only expose settings implemented by the active renderer/input |
| Storage Manager | Show mount and image inventory; open local upload portal | Upload only in a controlled maintenance state |
| Hardware Diagnostics | Run supported read-only component probes | Report unavailable/unverified hardware accurately |
| Firmware Manager | Reserved concept | Deferred from this boot-menu project |

The mockup's `F1 Setup`, `F2 Boot Menu`, and `ESC Monitor` are useful shortcut
labels, but shortcuts must be capability-detected. Preserve digit/Enter and
serial access if a keyboard cannot generate function keys.

### Hardware diagnostic report

The rich report should group results by subsystem and mix pass/fail states with
measured values and firmware-reported information. This is a target shape, not
a transcript of current hardware validation:

```text
FUZZBALL HARDWARE DIAGNOSTICS

ESP32:
  CPU ...................... DETECTED
  Free Heap ................ <measured> KB
  PSRAM .................... <measured size / unavailable>

SIMH:
  Runtime .................. <READY / ERROR / NOT STARTED>
  Emulator State ........... <STOPPED / RUNNING / unavailable>

STORAGE:
  SD Card .................. <PASS / FAIL / NOT PRESENT>
  Filesystem ............... <detected type / unavailable>
  Disk Images .............. <supported count> found

INTERFACES:
  Keyboard ................. <available input sources>
  Display .................. <initialized display / serial only>
```

Every value must come from a probe or a documented runtime measurement. Use
`NOT PRESENT`, `UNAVAILABLE`, or `NOT CHECKED` when a subsystem cannot be
tested; reserve `PASS` for a completed check with a defined success condition.
For example, CPU detection is not a CPU self-test, and successfully mounting
FAT is not a check that every disk image boots. Heap should be labeled as a
point-in-time free amount (and optionally minimum-ever free amount) rather
than a generic pass. PSRAM should report detected capacity and, if available,
free capacity instead of a bare pass. Storage image counts should distinguish
supported entries from arbitrary files. Keyboard/display rows should state
which input or output path initialized. The RP2350 rows from the illustrative
mockup are omitted: the target ESP32 board has no RP2350 coprocessor. SIMH
runtime status is shown instead because the PDP-11 emulator runs on the ESP32.

The initial diagnostics implementation should be read-only, bounded in time,
and safe to run before starting SIMH. Each probe should have a short timeout,
report its result without preventing entry to the boot menu, and avoid writing
to SD images or NVS. A later interactive diagnostics mode may add deliberate
tests, but it must state the test scope and any side effects before starting.

The current S3 boot menu includes a compact capability/input summary and an
`H` hardware inventory page. It reports ESP model/revision/cores, point-in-time
heap and PSRAM, detected flash size, SPIFFS and SD mount/capacity, USB HID host
and keyboard state, OLED response, and Wi-Fi/BLE/Classic Bluetooth chip
features. Radio feature flags describe what the chip supports; the inventory
does not start Wi-Fi or BLE. The OLED mirrors a compact subset. Refresh is
manual with `R`, keeping the page stable while avoiding repeated probes. Only
configured board paths are checked; unassigned pins and accessory buses remain
explicitly unscanned.

The configured I²C bus can be scanned on demand from the inventory screen.
Probe only standard 7-bit addresses (`0x08`–`0x77`) with address-only
transactions and a bounded timeout. When an ACKing address matches a device
already initialized by a configured driver, label it with that driver's
functional device name; describe the SSD1306 as compatible rather than
claiming an exact controller model. Leave other ACKing addresses unidentified.
An ACK is discovery evidence, not a functional test. Stop and report an
incomplete scan if the bus times out. Do not probe other GPIO pairs or attempt
register reads/writes without a known device and datasheet.

### Local SD upload portal

The Launcher-inspired web UI is a priority feature because transferring files
to the card is currently a manual friction point. Keep its first version
focused on moving files from a phone or computer to the mounted SD card:

- Start a lightweight HTTP server from the boot-menu maintenance action, with
  the PDP-11 simulator stopped. On first use, start a temporary,
  WPA2-protected ESP32 access point. Allow the user to save Wi-Fi credentials
  through the authenticated page; later sessions join that network in station
  mode and display the assigned local URL. If joining fails, return to the
  temporary setup access point. Provide a bounded session and a clear
  stop/return action.
- Keep the HTTP server and its Wi-Fi lifecycle separate from `wifi_if_esp32`,
  which is the simulator's packet interface. The portal runs before SIMH
  starts, and the current S3 profile's SIMH Wi-Fi path is not used by it.
- Start with available-space reporting and uploading files to documented SD
  locations. Show filename, size, progress, and final result. Restrict paths
  to the SD mount; reject traversal, absolute paths, invalid names, duplicate
  names unless overwrite is explicitly confirmed, and files exceeding the
  configured size/free-space limits. Do not expose SPIFFS, NVS, firmware
  partitions, or arbitrary filesystem paths.
- Protect write operations with an authenticated local session. Do not expose
  an open, unauthenticated upload endpoint. Credentials must be generated or
  configured without embedding reusable passwords in source or docs; display
  or deliver them only through the local setup flow.
- Stream uploads in bounded chunks to a temporary filename on the same FAT
  volume. On completion, close and validate the file, then atomically rename
  it into place so a partial transfer never appears as a bootable image.
  Report and clean up incomplete temporary files after interruption.
- Keep boot discovery from selecting a file during upload. Only publish
  completed, validated files to the boot list, and require an explicit profile
  for formats that cannot be safely identified by the current rules.
- Do not allow mutation of an image attached to SIMH. Initially the portal is
  unavailable while the simulator is running; future concurrent access needs
  an explicit ownership/locking design.
- Defer SD-to-SD copy, delete, rename, formatting, backup, restore, arbitrary
  internet downloads, OTA, firmware flashing, and disk-image editing.
  Uploading a disposable guest image from a computer is the first workflow.

Suggested flow: select the boot-menu upload action; connect to the temporary
AP on first use, or use the saved Wi-Fi network on later sessions; open the
displayed local URL; choose one or more files; review
destination, size, and free space; upload; wait for validation and completion;
return to the boot menu and select the newly discovered entry. A lost Wi-Fi
connection or reset must leave prior card contents intact and partial files
clearly distinguishable from usable images.

Implementation checkpoint (2026-10-18): the first serial-triggered portal
(`W` from the boot menu, `Q` to exit) is implemented with a browser file list,
free-space reporting, per-session upload authorization, streamed writes,
duplicate-name rejection, hidden temporary files, and boot-menu rediscovery
after exit. Uploads go to the SD-card root, are limited to 1 GiB, and require
space to remain free; incomplete temporary files are removed on the next
portal start. Wi-Fi credentials can be entered at the serial menu with `C`
(password masked), saved in NVS from the authenticated page, or cleared from
the serial prompt. Portal starts try the saved network and fall back to a
temporary WPA2 access point if credentials are absent or the connection times
out. Exit through the browser, `Q` on serial, or the ten-minute idle timeout.

The ESP-IDF 6.0.2 build passed and the application was flashed to the
ESP32-S3 N16R8 using app-only flash. Hardware validation confirmed the setup
access point and HTTP server start, serial credential save/masked input/clear,
and browser-visible SD inventory with file sizes. A BIOS-style serial selector
now reports measured ESP32/PSRAM/SD status, names each supported profile,
shows the selected image and writable-media warning, supports Up/Down and
`J/K`, and pauses its countdown on navigation. Stable entry IDs are persisted
after successful attachment/bootstrap, with the previous `last_path` setting
as a migration fallback. The flashed OLED-enabled build was 0x1598b0 bytes,
leaving about 6% of the
0x170000-byte app partition free. App-only flash preserves the partition
table and SPIFFS; the source layout reserves 0x5d0000 for SPIFFS at 0x180000.
The board joined its saved Wi-Fi network, received a DHCP address, and the
browser loaded the portal. An 8 MiB upload attempt stalled at about 2% and
triggered an interrupt watchdog reset before completion. A following reboot
auto-started `/sdcard/BOS6.IMG`; it reached the RT-11/BOS6 prompt, and the
writable image may have been modified. The board was left running the guest
without another reset. Do not treat this SD image as pristine. Source changes
now disable station modem sleep during portal use, pin HTTP handling to CPU1 on
dual-core targets, yield during upload/hash work, log transfer stages, and
suppress auto-start after watchdog, panic, or brownout resets. The WDT backtrace
ran through Wi-Fi power management on CPU0, but the underlying trigger is not
proven. These changes build successfully but have not been flashed or validated
on hardware.

System Configuration now has Main, Boot, Storage, Hardware, and Info pages,
with default entry, timeout, Show POST, Auto Start, Restore Defaults, and Save
& Exit. A hardware reset confirmed settings persistence; temporary test values
were restored. Serial navigation was checked. OLED visual readability remains
unchecked, and HID navigation awaits the needed USB adapter. Low-space upload
testing remains deferred to a disposable or nearly-full card.

The optional SSD1306 view is enabled for the S3 DevKitC configuration and
uses GPIO18/17 with address detection at `0x3C`/`0x3D`. On 2026-10-18 the
connected panel acknowledged at `0x3C`; boot and setup frames were transmitted
without I2C errors. The serial display-status row reports the probe result.
The hardware inventory was subsequently checked on the board: its I²C scan
reported one responder at `0x3C` and identified it as the configured
SSD1306-compatible boot display. The address match is grounded in the active
display driver; any other responding addresses remain unidentified. The scan
does not establish an exact controller model. Visual readability, contrast, or
clipping still need a direct check. If the panel is absent, boot continues
through serial.

### Configuration behavior

The setup screen should separate settings into Main, Boot, Storage, Hardware,
and Info sections, while only displaying options that have a defined effect.
The initial persistent settings are:

- Default Boot: a stable ID for an available boot entry.
- Boot Timeout: a bounded duration, including a documented no-timeout option
  only if recovery remains straightforward.
- Show POST: enable or skip the status screen, while preserving a way to enter
  setup at startup.
- Auto Start: control whether the default entry launches after the countdown.
- Defaults: restore firmware defaults without erasing media or unrelated NVS.
- Save & Exit: validate and commit settings atomically, then return to startup.

Implemented in setup: Main, Boot, Storage, Hardware, and Info pages; default
boot selection, a bounded 5/10/15/30-second timeout, Show POST, auto-start,
Restore Defaults, and Save & Exit. Defaults restore only boot-menu settings.
Serial navigation and persistence across a hardware reset were checked; test
values were restored after the persistence check. OLED visual readability and
HID input still need separate physical checks. Console appearance options
remain deferred until a renderer supports them.

Console appearance settings such as VT100 mode, DEC font, and cursor style are
future renderer capabilities. Do not persist options that the current serial
or OLED backend ignores. A failed setting write must leave the prior committed
configuration intact and report the failure.

## Implementation phases

### 1. Define choices and boot outcomes

- Replace the implicit path/profile pairing with a boot-entry description
  containing a stable ID, display name, machine profile, media path, source,
  validation result, and writable-media note.
- Keep profile selection explicit. Continue to recognize only known image
  kinds and known profiles; do not apply RK05 defaults to arbitrary files.
- Separate discovery, validation, user selection, launch, and outcome
  reporting so each can be reviewed and tested independently.
- Keep the current serial menu behavior as the compatibility fallback while
  this structure is introduced.

### 2. Make discovery predictable

- Preserve the bundled images and existing SD locations as initial sources.
- Add optional named directories only after documenting their layout and
  confirming the SD mount supports the required operations.
- Sort entries deterministically and show unsupported or malformed candidates
  as diagnostics rather than silently treating them as bootable.
- Use bounded path lengths and entry counts. Report when the list is full.
- Introduce a small manifest only if filename and size checks cannot safely
  express the supported machine profiles. A manifest must be data-only, have
  strict limits, and never provide arbitrary simulator commands.

### 3. Add the local SD upload portal

- Add a maintenance state in which SIMH is stopped and the SD card is mounted
  for management.
- Add isolated access-point networking and a small authenticated web server;
  do not route management HTTP through the SIMH network interface.
- Implement bounded, streamed uploads to temporary files, completion
  validation, atomic publication, progress/status UI, and cleanup after
  interrupted transfers.
- Verify unsupported files remain stored but are not offered as boot choices
  until a known machine profile is associated with them.

### 4. Add the richer selector

- Add a compact menu renderer interface independent of the boot logic.
- Render first to serial. Add the SSD1306 display through the existing
  component behind a build-time/runtime availability check; display failure
  must leave serial boot usable.
- Support up/down (or equivalent), select, and back/cancel where the input
  device provides them. Keep digit selection and Enter compatibility.
- Show the default and remaining timeout, and let user activity cancel the
  countdown. Define what happens when input stops midway.
- Persist a stable entry ID rather than only a path, with a migration or
  fallback path for the existing `last_path` setting. Remember the last
  successfully started choice, not merely the last highlighted item.

### 5. Add safe simulator actions

- Add a details screen before launch, then show explicit startup phases and
  actionable errors (missing media, invalid profile, attachment failure).
- Add a return-to-menu action only after the simulator can stop, detach, flush,
  and close disks in a defined order.
- Keep reset behavior explicit: hardware reset can interrupt writes. Do not
  claim data integrity or add guest-disk backup/restore until a validated
  design exists for writable copies, recovery, and export.
- Defer SD-to-SD disk copying, deletion, rename, formatting, network downloads,
  OTA, and partition tools. They are outside the initial transfer goal and
  increase the risk of losing guest media.

### 6. Validate the end-to-end flow

- Host/build checks: deterministic discovery and profile mapping for absent
  media, each known image type, invalid sizes, too many entries, long names,
  and remembered choices.
- Board checks: serial selection, timeout, invalid key handling, card absent
  and reinserted, multiple RK05 files, BOS6 selection, and NVS persistence.
- Upload checks: local connection/authentication, multiple file sizes, low
  free space, duplicate names, invalid paths, interrupted transfers, reset
  during transfer, and validation before boot-list discovery.
- Display checks: menu readable on the actual SSD1306 hardware, selection
  updates, countdown, and display failure with serial fallback.
- Boot checks: capture selected entry and profile, attachment/bootstrap
  results, and guest prompt as distinct outcomes. Reuse disposable images.
- Storage checks remain separate: demonstrate clean shutdown/flush and reset
  recovery before describing writable media as safe.
- Network checks remain separate: a successful boot-menu path or guest boot
  does not prove packet I/O.

## Acceptance criteria for the first useful version

- A user can identify available supported environments and select one without
  memorizing filenames or serial menu numbers.
- POST reports only real probes and distinguishes healthy, missing,
  unavailable, and unchecked components.
- Supported settings survive reboot, invalid values recover to safe defaults,
  and saving configuration does not touch guest media.
- The menu works with serial alone and remains usable if optional display
  initialization fails.
- Every entry has an explicit machine profile; unsupported files cannot be
  started under a guessed profile.
- The timeout/default behavior is visible, configurable, and recoverable.
- The remembered default survives reboot and safely falls back when media is
  missing.
- A user can transfer a file from a phone or computer to the SD card over a
  local authenticated web session without removing the card.
- Startup output distinguishes selection, attachment, bootstrap, guest boot,
  and network validation.
- No menu action can silently erase or rewrite media.
- Interrupted uploads never appear as completed boot entries, and the portal
  cannot modify a disk image while SIMH has it attached.

## Launcher features considered

The most relevant Launcher patterns are an explicit home screen, navigation
that adapts to available inputs, persistent user preferences, clear status for
installed/available choices, and a browser UI for moving files onto storage.
Adopt the focused local SD upload workflow; defer OTA installation, firmware
catalogs, partition management, broad file editing, and internet downloads.
The project README describes those capabilities and navigation patterns:
[bmorcelli/Launcher](https://github.com/bmorcelli/Launcher).

## Open design questions

- Which board/display is the first target for the richer menu? The current
  demonstrated target is the ESP32-S3 N16R8; the SSD1306 is optional until
  verified in the boot path.
- Should the default timeout stay at ten seconds, become configurable in NVS,
  or be disabled when a display/input device is attached?
- Should SD discovery remain root-only initially, or should a documented
  directory convention be introduced with the entry-description work?
- Is a data-only manifest warranted for adding further machine profiles, or
  should the first version keep the current explicit filename/profile rules?

## Suggested order

Deliver phases 1 and 2 first, then make the authenticated local SD upload
portal the first major user-facing addition while keeping SIMH stopped. Follow
with the richer selector and optional display. Add simulator return and
broader media-management features only after the independent shutdown and
recovery work establishes safe behavior.
