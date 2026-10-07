# ESP32 Fuzzball port plan

Status (2026-10-07): the ESP32-S3 N16R8 build runs under ESP-IDF 6.0.2,
boots RT-11 and BOS6 from a four-bit SDMMC card, and has reached a DMILLS
login. The latest serial run is `logs/esp32-bos6-20261007-124214.log`.
The remaining gates are a full SIMH control console, safe writable-media
shutdown/recovery, and demonstrated packet I/O. The S3 Ethernet backend is
currently disabled; this is a no-peer boot result, not a network result.
Later dated sections preserve the evidence available at each stage and may
describe earlier build-only or pre-boot states.

## Objective

Run the recovered Fuzzball/BOS configuration on an ESP32-hosted PDP-11
simulator.  Reuse existing implementations wherever possible and keep the
desktop SIMH configuration as the behavioral reference.

The acceptance ladder is deliberately strict:

1. Build and flash a known upstream-derived ESP32 image.
2. Boot RT-11 from a copied, writable test image.
3. Boot the selected Fuzzball/BOS image.
4. Confirm required timer and network-device initialization.
5. Demonstrate packet I/O with a defined peer.

Each level is separate evidence.  A successful RT-11 or BOS boot is not proof
of device compatibility or network operation.

## Deferred storage lifecycle: orderly shutdown and writable media

The bundled boot images are currently stored in SPIFFS and an ESP32 reset is
an immediate hardware reset: it does not give SIMH an opportunity to detach
or flush an emulated disk.  Before treating an ESP-hosted guest disk as
writable, define and validate all of the following:

1. A user-requested guest shutdown path that halts SIMH cleanly, flushes and
   closes attached media, then reports that power/reset is safe.
2. A reset/power-loss policy with explicit recovery behavior; never claim that
   an abrupt reset preserves a guest write.
3. A read-only, checksummed bundled baseline image and a separately selected
   writable copy or overlay for experiments.
4. Flash-wear limits and a backup/export procedure for writable guest state.

Until then, use the bundled image only as a disposable boot fixture.  A normal
`idf.py flash` rewrites the packaged SPIFFS image; pressing the board Reset
button only reboots the firmware and auto-boots whatever storage state remains.

On 2026-09-22, the running 16 MiB N16R8 board reported only 151 KiB free in
the original 0x2a0000-byte SPIFFS partition (2311 KiB used of 2463 KiB
usable).  Repeated RK error 66 messages accompanied failed writes to the
Unix V6 image.  The firmware now allocates 0x600000 bytes to SPIFFS at
0x150000, within the verified flash size.  This is a capacity experiment,
not yet proof that guest writes survive a long run or reset.  The disk I/O
diagnostic now reports the failing operation and actual errno; `ferror()`
is only a stream error flag and must not be interpreted as errno 4/EINTR.
The first flashed boot with the 0x600000-byte partition reported 2311 KiB
used of 5640 KiB usable (3328 KiB free) and successfully attached RK0.
Sustained guest writes and reset recovery remain unverified.

Changing the partition size requires flashing the partition table and a new
SPIFFS image.  Back up the board's old 0x2a0000-byte partition first if its
guest changes matter.  A full `idf.py flash` replaces its contents with the
bundled source image.  Automatic formatting on SPIFFS mount failure is
disabled so a mount error remains visible rather than erasing guest state.
After exiting the serial monitor, the N16R8 bench board can be backed up with
`python -m esptool --chip esp32s3 -p /dev/cu.usbmodem5CBD0162491 read-flash
0x150000 0x2a0000 ../logs/storage-before-6m-2026-09-22.bin` from the
`firmware/` directory.  Preserve that file and its checksum before running
`idf.py -p /dev/cu.usbmodem5CBD0162491 flash monitor`.

## Workspace layout

`esppdp-fuzzball/` is a staging collection, not a Git repository:

```text
esppdp-fuzzball/
  esppdp/                 active Git checkout and future Pixitha changes
    docs/                 active design, port, and test evidence
  toolchains/
    esp-idf-v5.2.1/       compatibility baseline for Sven's candidate
    esp-idf-v6.1/         primary active development toolchain
  reference/
    Pico_1140/            Ian Schofield/RP2040-derived reference checkout
    cpp11/                Dave Cheney C++ PDP-11 reference checkout
```

The references are comparison material only.  Do not merge or edit them as a
side effect of ESP32 work.  Preserve their independent Git histories and any
user-owned local changes.

## Repositories and provenance

The active checkout is `pixitha/esppdp` at commit
`7f097e74effd724879c6381565c8dae27025acfb` on `master`.  It is a fork of
SvenMb's `master`, itself based on Sprite_tm's original ESP32 SIMH port.

Configured remotes:

| Remote | Role | Current selection |
| --- | --- | --- |
| `origin` | Pixitha-owned active fork | `master`, commit `7f097e74` |
| `sven` | compatibility and hardware candidate | `Sunton_2432S028`, commit `79285c42` |
| `upstream` | original Sprite_tm baseline | `master`, commit `f744ac16` |

Direct-fork audit on 2026-09-15 found 23 forks of `Spritetm/esppdp`.  Only
three are ahead of the original:

| Fork | Delta from Sprite_tm | Use |
| --- | ---: | --- |
| `SvenMb/esppdp` `Sunton_2432S028` | 21 commits | Primary candidate.  Includes the five commits already in `origin/master`, then ESP-IDF 5.2.1, PSRAM, WROVER/Sunton hardware selection, and SDMMC/SPI-SD support. |
| `Isysxp/esppdp` | 2 commits | Donor only.  It contains small IDF-5 compile adjustments but hard-codes a Windows ESP-IDF include path. |
| `mafrmt00/esppdp` | 1 commit | SD wiring reference only; do not adopt as a base. |

All other direct forks were identical to the original or one commit behind.

## Base-selection rule

Do not start a fresh PDP-11 port and do not copy the Pico implementation into
the ESP32 project.  First build and flash `origin/master`, then test the
Sven `Sunton_2432S028` candidate as an isolated branch.  Carry forward only
the deltas that build and work on the actual target hardware.

The recommended branch sequence is:

```text
origin/master
  -> candidate/sven-sunton       unmodified candidate validation
  -> pixitha/fuzzball            selected platform deltas plus Fuzzball work
```

Never make Fuzzball changes directly on `candidate/sven-sunton`; it must stay
reviewable as an upstream-derived comparison point.

## Known simulator boundary

The ESP32 source set includes XQ (DEQNA/DELQA) support.  Its PDP-11 system
table declares DMC but leaves it disabled, and the staged source set has no
KWV11 or DMV implementation.  Therefore Sven's work solves a platform and
toolchain problem, not the known Fuzzball device gap.

The desktop Fuzzball path remains the device oracle:

- KWV11 support and its configuration must be compared against the working
  desktop SIMH source and boot profile.
- Network-device choice must match the recovered BOS configuration; a
  superficially similar XQ path is not a substitute for an expected DMV/DMC
  device.
- Keep the existing desktop boot profile, copied test media, console logs, and
  peer test as reproducible comparison evidence.

## ESP32-S3 SIMH console restoration plan

Scope: restore an interactive SIMH control/debug console on the ESP32-S3 only.
Do not extend this work to the Pico/Pico 2 emulator, whose CPU, bus, and monitor
are a separate implementation.  Do not replace SIMH's SCP command processor
with an expanding hand-written command subset.

### Current state

The ESP port's `scp.c` is explicitly a trimmed launcher: it selects and boots
an image, then loops forever around `sim_instr()`.  It has no normal SCP command
reader/dispatcher, so CPU register definitions and device command tables are
present without commands such as `EXAMINE`, `SHOW`, or `SET` to reach them.
Ctrl+E (SIMH's WRU character) was not intercepted at the host-input boundary.

A temporary ESP-only monitor has been added to the current working build.  It
uses Ctrl+E to stop guest execution and offers only `CONTINUE`, `BOOT`, and
`HELP`.  It is a diagnostic bridge, not the target interface; it has built with
ESP-IDF 6.0.2 but still needs a physical test.  The OLED console is disabled to
keep terminal I/O from blocking on full-frame refreshes.  Current guest evidence
is RT-11FB V05.05 running from the SD-backed BOS6 image; a normal Fuzzball
startup/login and durable writable-disk lifecycle are not yet established.

### Target behavior

1. Ctrl+E from either supported serial console stops the PDP-11 at a safe
   instruction/event boundary, preserves CPU/device state, and presents the
   `sim>` prompt.  Ctrl+C remains guest input unless a documented SIMH command
   or break mode says otherwise.
2. Restore SIMH's command loop and parser from a compatible upstream-derived
   source revision, adapting its I/O and scheduling boundaries for ESP-IDF.
   Preserve the existing boot-menu path and do not re-run setup or reset the
   guest when entering the prompt.
3. Make the normal inspection/control commands usable first: `HELP`, `SHOW
   CPU`/device state, `EXAMINE` registers and memory, `DEPOSIT`, `CONTINUE`/
   `GO`, `STEP`, `RESET`, and `BOOT`.  Then restore the remaining SCP commands
   supported by the selected source revision, including configuration and
   media commands where they can be made safe on-device.  Document any
   intentionally unsupported host-only commands.
4. Keep prompt work out of the CPU instruction loop.  Use existing SIMH
   command/device APIs where available; keep output bounded and serial-first,
   with optional display output decoupled from emulation timing.

### Implementation and validation gates

1. **Source audit:** identify the exact SCP/parser revision corresponding to
   this ESP fork; list omitted source files, command tables, host I/O calls,
   and global state transitions.  Compare behavior with desktop SIMH rather
   than copying a different revision wholesale.
2. **Stop/resume boundary:** implement host-side WRU interception for UART0
   and native USB Serial/JTAG.  Verify Ctrl+E is not delivered to the guest,
   the CPU stops without resetting, event/timer state remains coherent, and
   `CONTINUE` resumes at the next instruction.
3. **Parser restoration:** port the SCP line reader, tokenizer, command
   dispatch/help tables, and only the platform adapters they require.  Check
   linker/app-partition size and internal-heap/PSRAM headroom at each stage.
4. **Debugger commands:** validate `SHOW CPU`, register and memory `EXAMINE`,
   `DEPOSIT`, single-step, breakpoints, continue, reset, and boot against a
   known RT-11 state and a desktop SIMH transcript.  Confirm octal defaults
   and PDP-11 address-width behavior match SIMH.
5. **Physical S3 test:** use a copied, disposable BOS6 image on SD.  From the
   serial console, pause at RT-11, inspect known registers/memory, resume, and
   demonstrate that the same guest session continues.  Repeat at a Fuzzball
   breakpoint/startup point.  Test both the USB-UART and native USB serial
   inputs independently.
6. **Performance/regression:** compare timed guest progress and console output
   with the current OLED-disabled build; ensure command polling is dormant
   during execution, output does not block the CPU loop, and no watchdog,
   interrupt, or SD I/O regressions are introduced.

Completion means a board-tested SIMH command prompt with working register and
memory inspection and reliable pause/resume.  It does not by itself prove a
Fuzzball boot, a clean guest shutdown, preserved disk writes, or working DMV
network I/O.

## Work phases

### Phase 0 — freeze the platform baseline

For both `origin/master` and the Sven candidate, record the exact commit,
ESP-IDF version, target board/module, PSRAM size and mode, flash size,
display/keyboard configuration, storage wiring, and serial console settings.
Build and flash each without Fuzzball changes.  Capture `idf.py build` output
and boot console output.

### Phase 1 — establish a disposable RT-11 boot baseline

Use copied writable media only.  Identify the ESP32 storage path expected by
the emulator, confirm image geometry, and boot RT-11.  Record memory size and
enabled devices from the ESP32 console.  Do not modify recovered Fuzzball
media or treat an embedded RTX-11/Tetris boot as Fuzzball evidence.

### Phase 2 — port required desktop-SIMH configuration

Compare the desktop SIMH source, device registration, build inputs, and boot
profile with ESP32 `firmware/main`.  Make a minimal, separately reviewable
port for required CPU/memory configuration and KWV11.  Add a device-register
probe before attempting the BOS boot.

### Phase 3 — boot BOS/Fuzzball

Use a fresh copied BOS image for every experiment.  Preserve a named console
log containing source revision, ESP-IDF version, hardware configuration,
image checksum, command sequence, and the observed result.  Diagnose failures
against a matching desktop trace rather than changing several device behaviors
at once.

### Phase 4 — network device and packet I/O

Select the network device only after confirming what the recovered
configuration expects.  Port it from the proven desktop implementation, then
test CSR access, interrupt delivery, DMA/buffer behavior, initialization, and
traffic with a defined peer in that order.  Report a Fuzzball network success
only after guest payload exchange is demonstrated.

### Current Phase 3 gate — BOS media and machine profile (2026-09-22)

The known desktop no-peer boot uses
`fuzzball_workspace/profiles/active/boot_bos6_autostart.ini` with
`fuzzball_workspace/disks/active/fuzzball_bos6_autostart.img` (159,334,400
bytes, SHA-256 `ef33a6cd1f8c4f35ade69e933f295d5675f2c2a5b269a13cfbf3fe82d11b911b`).
It selects an 11/73 with 2048K, 8-bit TTI/TTO, a 60 Hz clock and KWV11,
and boots an RD54 on RQ0.  Its DMV at CSR `17760020` uses temporary vector
`0300`; DLI/DLO supply the discrete line CSR map.  That vector is a no-peer
boot workaround, not a network validation profile.

This RD54 file cannot fit in the N16R8 board's 16 MiB flash. The DevKitC
profile mounts an external microSD card over four-bit SDMMC; bounded FAT
read/write diagnostics passed at 20 MHz on 2026-09-22. The ESP boot menu now
has a dedicated `BOS6.IMG` selection and configures the 11/73, 2 MiB,
60 Hz, RQ0/RD54, KWV11, DMV, and DLI/DLO no-peer boot profile. The image is
accepted only at the known 159,334,400-byte size. ESP-IDF 6.0.2 build passed
after this addition; it has not yet been flashed or guest-tested. Copy the
canonical desktop fixture to the SD card as a disposable writable copy and
verify its SHA-256 before selecting it. Keep the desktop source unchanged.

The on-hand 3.3 V breakout, proposed four-bit SDMMC wiring (with SPI fallback),
and staged validation are documented
in [the ESP32-S3 SD card plan](ESP32_S3_SD_CARD_PLAN.md). The basic SDMMC
pinout is now hardware-tested; sustained and guest-disk operation are not.

Temporary CPU, console, clock, and RK traces are now behind
`CONFIG_ESPPDP_BOOT_TRACE` (off by default) to keep normal logs readable.
Re-enable the setting for a focused boot diagnosis; disk and controller
errors print independently of it.

## Initial deliverables

1. A reproducible platform-baseline build record for `origin/master` and the
   Sven candidate.
2. A short device matrix: desktop requirement, ESP32 availability, porting
   source, and test method.
3. Copied-media RT-11 and BOS test profiles with checksums and serial logs.
4. Focused device probes and, later, a peer-backed packet-I/O test.

## Guardrails

- Do not overwrite original disk images, recovered sources, or prior logs.
- Do not commit generated build directories, flash artifacts, private network
  credentials, Wi-Fi credentials, or captured secrets.
- Keep changes upstream-derived, narrow, and attributable; preserve existing
  copyright and license notices.
- Treat ESP-IDF build success, physical flash/boot, guest boot, and live
  network traffic as distinct validation levels.

## Phase 0 evidence — 2026-09-15

The Sven candidate builds successfully with the staged ESP-IDF v5.2.1
toolchain (`espressif/esp-idf` source `a322e6bd`). The application image is
`0x124d80` bytes, with `0x1b280` bytes free in the `0x140000` app partition.
No board was attached, so this is build-only evidence and not a flash or boot
result.

The Sprite/origin baseline was configured with ESP-IDF v5.2.1 and compiled
far enough to reach the `ie15term` component, then stopped because
`ie15lcd.c` includes `driver/spi_master.h` without declaring the IDF `driver`
component requirement. This confirms the Sven branch contains material
toolchain/component fixes; it is not a Fuzzball feature result.

The origin and candidate references remain unmodified after restoring
IDF-generated `sdkconfig` changes. The next work branch is created from the
clean Sven commit and is the only checkout intended for ESP32 Fuzzball edits.

## Initial device matrix

| Requirement | Desktop reference | ESP32 state | First test |
| --- | --- | --- | --- |
| Programmable real-time clock | `PDP11/pdp11_kwv11.c` in the dirty SIMH workspace | absent from ESP source list | register read/write and interrupt probe |
| Fuzzball network interface | desktop `pdp11_dmc.c` (`DMC`/`DMV`) | DMC declaration is present but disabled; DMV implementation absent | CSR map, interrupt, DMA, then peer-backed packet test |
| Existing Ethernet path | SIMH `XQ` plus ESP Wi-Fi packet adapter | `pdp11_xq.c`, `wifi_if_esp32_packet_filter.c` present | RT-11/XQ initialization only; not a Fuzzball equivalence claim |

The first code task on `pixitha/fuzzball` is an ESP-IDF v6.1 build-system
migration with no simulator behavior changes.  Once that builds, add minimal
KWV11 integration and a probe. Network-device porting remains gated on KWV
and guest-boot evidence, plus a deliberate DMC/DMV-versus-XQ decision.

The first compatibility pass has now completed under ESP-IDF v6.0.2. It
required only dependency/API updates: split SPI/GPIO/UART requirements,
`SPI2_HOST` in the classic ESP32 board profiles, removal of the obsolete
`esp_spi_flash.h`, a const-correct `strtol` end pointer, and replacement of
removed `ESP_IF_WIFI_STA` with `WIFI_IF_STA` plus the corrected reconnect
prototype. The resulting application image is `0x117340` bytes with 13%
free space in the app partition. This remains build-only evidence; no board
was attached.

## Toolchain policy

ESP-IDF v6.1 is the active target because v5.2 reached end-of-life in August
2026.  Keep v5.2.1 pinned for reproducing the Sven candidate and for bisecting
platform regressions, but do not make new Fuzzball functionality depend on
it.  The v6 migration must explicitly declare split driver dependencies (for
example `esp_driver_spi`) instead of relying on the old umbrella `driver`
component.

The pinned, verified host baseline (`vendor/simh-host`, commit
`36bea593`) does not force kernel, supervisor, or user D-space page-7 PDR
writes to `077406`. That ESP-only KERTAB workaround was removed on
2026-09-27 to bring `APR_wr` back in line with the working host source. The
resulting IDF 6.0.2 build was `0xb82c0` bytes (42% of the app partition
free), but its BOS6 profile enabled RX after auto-configuration was disabled,
leaving it without an assigned CSR/vector. That order was corrected on
2026-09-27; the corrected build is `0xb8310` bytes (42% free) and has not yet
been flashed or guest-boot tested.

The follow-up SIMH correctness fixes also build cleanly: mask invalid PSW bits
when loading a trap frame, and accept the exact Q22 I/O-page base in the
autoconfiguration address checks. These remain behavior-only simulator fixes;
the IDF 6.0.2 image remains `0x117360` bytes.

## SIMH delta audit — Fuzzball-specific requirements

The dirty desktop SIMH tree contains more than the new KWV11 file. Comparing
its semantic changes with the recovered DCN6 configuration gives this
transplant order:

1. **KERTAB/MMU workaround (not in the pinned working host baseline).** The
   earlier desktop experiment forced kernel, supervisor, and user D-space
   page-7 PDR writes to `077406`. Keep it out of the ESP port unless a
   controlled host comparison demonstrates it is required; the pinned host
   source accepts the guest's PDR writes unchanged.
2. **KWV11 (required for `HDWCLK=3`).** Port `pdp11_kwv11.c`, its BR6/vector
   definitions, and the auto-configuration entry, using ESP timer/event
   primitives rather than desktop-only threading.
3. **DLV11/DLI lines (required for DCN6 initialization).** `DAT6.MAC` uses
   TT2 at `176540`, WWV at `176560`, and NBS at `176520`; ESP has only the
   console DL11 and leaves `dli_dev`/`dlo_dev` disabled. We need a bounded
   three-line implementation or deterministic no-host serial backend.
4. **MSCP/RQ and RX storage (already present, verify).** DCN6 names DU at
   `172150` and DY at `177170`; ESP includes `pdp11_rq.c` and `pdp11_rx.c`.
5. **DMV11/DMC transport (required for full DCN6 networking).** Desktop
   `pdp11_dmc.c` contains DMC/DMP and the newer DMV Q-bus model. ESP has DDCMP
   helpers but no controller implementation; desktop TMXR sockets, modem
   state, and peer listeners require an ESP transport contract first.

The desktop CPU abort counters, trap registers, and `IOW` print are useful
SIMH diagnostics, but are not guest requirements and should not be copied
unless an ESP probe demonstrates a specific need.

## Cumulative SIMH fix intake

The ESP source is based on the 2023-era SIMH code and should not be compared
only with the current dirty diff.  These later upstream changes deserve a
targeted review:

| SIMH change | ESP disposition | Reason |
| --- | --- | --- |
| `86a995b8` (2022 11/45/70 trap and MMU corrections) | **Port before BOS** | The ESP emulates the J/11 family and Fuzzball depends on exact MMU/trap behavior. Review the complete CPU/CPUMOD/FP interaction, not just one hunk. |
| `6e9324e0` (Terak CPU PSW mask and I/O-page boundary) | **Port** | Small core correctness fixes; the I/O boundary affects Q22 device registration. |
| `2396fd03` (RQ/MSCP model and DTYPE updates) | **Review against DU media** | Mostly additional disk models, but the DTYPE-width change may affect existing MSCP images. |
| `40d46093`, `8b33921c`, `5465707d` (XQ filtering/Coverity fixes) | **Port applicable hunks** | DCN6 uses DEQNA; retain ESP's Wi-Fi packet adapter while taking guest-visible filter/broadcast safety fixes. |
| `560f30d1` (throttle calibration) | **Usually omit** | Desktop throttling is not the ESP execution model unless timing tests show a regression. |
| `f4c39a32` and later socket/vmnet/ETH_MAC changes | **Do not copy wholesale** | These are desktop host networking and memory-safety infrastructure; ESP needs its own packet backend. |

The desktop DMV work is also cumulative: `pdp11_dmc.c` and its focused tests
landed in commits `5ba55543` and the following DMV fixes.  We should port the
guest-visible behavior from that final model, but not the desktop TMXR/socket
implementation as-is.

### 86a995b8 intake status

The first J/11-safe subset is now on the active ESP branch and builds under
IDF 6.0.2:

- trap priority now places NXM above MME, matching the corrected SIMH trap
  vector ordering;
- abort/trap clearing is mutually exclusive for red-stack, odd-address, NXM,
  and MME events;
- trap entry clears MMR1 and conditionally records MMR2, with instruction-trap
  exceptions matching SIMH's `trap_load_mmr2` table;
- trap-frame PSW masking remains applied at the vector load;
- MMR0 trap/abort bookkeeping and PDR A/W updates now preserve frozen-MMU
  semantics and avoid re-raising an already-pending MME trap.

The resulting image is `0x1173c0` bytes with 13% free in the app partition.
This is build evidence only: no physical ESP32 flash or guest boot has been
validated.  The remaining `pdp11_cpumod.c` byte-lane fixes, FP11/J11 MMR1
behavior, and RK/RL NXM bookkeeping from the same SIMH commit are still
separate review items; they should not be copied blindly until the matching
ESP device/register paths are confirmed.

The CPUMOD byte-lane subset has now been reviewed against the ESP register
handlers and applied. Even-byte writes to PIRQ/STKLIM are ignored where the
hardware requires it, odd-byte writes are shifted onto the PDP-11 bus, and
the J11/KDF11 CDR paths reject the wrong byte lane. IDF 6.0.2 still builds the
same `0x1173c0` image (13% free). This remains compile-only evidence.

The directly applicable FP11 fixes from `86a995b8` are also applied: the MODf
load is correctly scoped, and undefined-variable handling explicitly preserves
the enabled-trap NOP behavior. The ESP build remains green at `0x1173c0`.
Full 11/44/70 MMR1-on-specifier behavior is not applicable to the configured
J11/11-73 path and remains out of this port.

The applicable XQ/DEQNA safety fixes from `5465707d` are now applied: the
turbo transmit descriptor read checks its `Map_ReadW` status before use, and
loopback tracing uses a stable label. The newer desktop `eth_filter_hash_ex`
and DELQA-Plus broadcast-filter changes are intentionally not copied because
the ESP packet backend does not provide that API or host Ethernet filter
model. IDF 6.0.2 builds the unchanged `0x1173c0` image.

## Future project: physical DMV inter-router links

Full DMV11/DMC11 serial support is intentionally deferred from the ESP boot
port. A later project can revisit the complete DDCMP controller and connect
two ESP boards directly over GPIO pins, providing physical serial-style
inter-router links between Fuzzball instances. That work should have its own
electrical-layer, framing, flow-control, peer-recovery, and guest-validation
plan; it is separate from the near-term Ethernet path through XQ/DEQNA.

## KWV11 and DMV boot-compatibility slice

The Mac-tested combined source revision is
`9f916ca8d89cc9cdffe9a023988501c82dd0358a` (KWV11-A/C), directly on top of
DMV commit `5ba55543e19c7947ac0c7b6c5deebf237fa279fa`. The ESP
`pdp11_kwv11.c` has now been replaced with the full KWV11 implementation from
that exact commit, including interval scheduling, CSR/BPR semantics,
maintenance pulses, capture/overflow status, and both interrupt vectors. The
ESP interrupt definitions now include the second KWV vector as well.

On 2026-09-27, `logs/esp32-bos6-20260927-180451.log` isolated a later BOS6
stall at guest PC `007254`, the `GTCLK` polling loop for KWV11 ST2 capture.
The CPU and device events continued, but the guest stopped reading TTI after
the loop. The KWV11 model stopped mode 2/3 counting on the first 16-bit
overflow (about 65.5 seconds at BOS6's 1 kHz rate), contrary to the DEC
KWV11-A manual, which says those modes continue through overflow until GO is
cleared by software. The ESP copy now keeps mode 2/3 running; IDF 6.0.2 build
passed before the follow-up hardware test below. The pinned Mac
source has the same latent device-model bug and is deliberately unchanged so
the reproducible host baseline stays pinned; track its correction separately.

The 2026-10-07 hardware run in `logs/esp32-bos6-20261007-105309.log`
confirmed two mode-2 overflows with `GO=1`, continued console input after both,
and a successful DMILLS login. It also showed guest time move backward from
`00:01:12` to `00:00:15` across the first rollover. The DEC manual specifies
that KWV11 CSR status flags are cleared by writing zero, whereas the copied
model cleared them on read; BOS6's `GTCLK` reads the CSR before explicitly
clearing `OVFLO`. The ESP model now preserves flags on read and uses
write-zero-to-clear. This second correction built before the follow-up
hardware test below. Keep performance diagnosis separate from clock validation.

The follow-up run `logs/esp32-bos6-20261007-110453.log` crossed two overflows
with `GO=1`, logged in as DMILLS, and accepted commands afterward. The second
overflow entered with CSR `040145` (the earlier overflow flag had been cleared
by the guest) and left with `040345`, consistent with write-zero-to-clear.
No guest time reading was captured after that rollover, so monotonic time is
not yet confirmed. There were no disk I/O errors, panic, or watchdog messages
in this run. The remaining perceived slowness needs focused CPU/disk/TTO
measurement; the five-second instruction samples include guest idle time.

The targeted `TIME` run in `logs/esp32-bos6-20261007-111822.log` confirmed
monotonic guest time across the second KWV11 overflow: `00:01:45` before,
`00:02:37` after, and `00:03:25` later. The overflow flag was cleared before
the second event and `GO` remained set. No panic or disk I/O error was logged.
The clock rollover issue is hardware-validated for this run; the separate
performance issue is not yet localized.

A dedicated performance build now enables `CONFIG_ESPPDP_PERF_TRACE` and adds
separate synchronous SD read/write counts, KiB, and busy milliseconds alongside
guest instructions per second and terminal-output blocking. The two hardware
runs `logs/esp32-bos6-20261007-112603.log` and
`logs/esp32-bos6-20261007-113954.log` flashed the same ELF (`3c3e0fb86...`).
Both spend about 25 seconds from `Main sim start` to the RT-11 banner; most of
that interval repeatedly executes around guest PC `005250` with no disk or
terminal I/O. The busiest measured five-second SD interval spent 1167 ms in
reads, while 868 terminal characters blocked host output for only 23 ms.
This excludes serial output as the primary bottleneck, but does not yet
separate guest boot polling, PDP-11 `WAIT`, and SIMH event-service time.

The next diagnostic image adds per-five-second `WAIT` and event-service wall
time plus sampled PC/instruction hotspots. It builds under ESP-IDF 6.0.2 as
`firmware/build/esppdp.bin` (`0xb6be0` bytes; SHA-256
`d49a398fe1a5dc287a3fa96074665d6da5f0956cbec46611411a19f62240d415`).
These new counters are build-validated only until a distinct ELF is flashed
and its boot output is captured.

The ESP `DMV` remains a deliberate no-peer boot shim at the existing
DMC/DMV device slot. It does not yet implement the desktop model's full DMA,
serial peer, or DDCMP transport. This is a port-scope difference, not evidence
that the full desktop DMV implementation is unnecessary for networking.

The combined ESP-IDF 6.0.2 image builds successfully at `0xb0b30` bytes, with
45% free in the `0x140000` app partition. This is source/build evidence only;
the exact KWV register behavior, interrupt delivery, and Fuzzball guest boot
still need hardware validation.

The DLI/DLO compatibility layer is now also registered. It provides safe
CSR/data access and interrupt clearing for the configured line slots without
host serial I/O. The IDF 6.0.2 image is `0x118000` (12% app space free).

The DMV shim now models the guest-visible BSEL0/BSEL2/BSEL4/BSEL6 register
block rather than a generic CSR/data pair. Master clear returns the expected
idle signature; an input request produces a deterministic RDI response and
interrupt, while command/data fields remain transport-free. The software-only
IDF 6.0.2 build is `0x118140` (12% app space free). Hardware and guest probes
remain pending until the ESP boards are located.

### DLI address status

The DLI/DLO layer now maps five contiguous DL11 lines starting at full
I/O-page address `17776520`, with vector base `0320`. The three DCN6 ports are
on physical lines 0, 2, and 4: `17776520`, `17776540`, and `17776560` (`020`
octal, or 16-byte, spacing). Each line has four registers:
receive CSR/buffer, then transmit CSR/buffer. This matches the desktop SIMH
DL11 register layout for source-level integration. It still
provides no host serial I/O, and the shared interrupt/vector behavior needs a
guest probe once hardware is available.

The IDF 6.0.2 software-only image is `0x1181e0` (12% app space free).

The DLI/DLO shim records the sparse line that caused an output request. Its
single DLI DIB owns both read and write accesses; DLO is the companion logical
device, not a second overlapping bus registration. Receive vectors are `0320`,
`0340`, or `0360`; transmit vectors are four higher. This is compile-validated
only; real receive interrupts and per-line guest dispatch still require a
board probe. The transport remains a stub.

### Hardware candidates on hand

The currently configured image targets the original Xtensa ESP32 with external
PSRAM and a board-specific LCD/SD pin map. The newly located M5 boards therefore
need to be treated as separate ports, not drop-in flash targets:

| Board | Assessment |
| --- | --- |
| M5StampC5 (ESP32-C5) | RISC-V, 384 KB SRAM, 4 MB flash, no PSRAM; useful later for a reduced emulator or peripheral work, not the current Fuzzball memory footprint. |
| NanoC6 (ESP32-C6) | RISC-V Wi-Fi 6 board; similarly not compatible with the current Xtensa/PSRAM assumptions and has no display/SD wiring matching this image. |
| Tab5 (ESP32-P4 + C6 wireless module) | Best memory headroom (32 MB PSRAM), but a major P4/C6 split-port: the current monolithic ESP32 Wi-Fi and LCD/SD paths do not apply directly. |
| Cardputer Adv (ESP32-S3) | Closest CPU family (Xtensa) and includes display, keyboard, and microSD, but the FN8 configuration is 8 MB flash without the external PSRAM assumed by the current image; requires an S3 board definition and a memory-footprint decision. |

The first practical hardware experiment is therefore the Cardputer only if we
reduce/verify the PDP-11 memory requirement; otherwise the Tab5 is the stronger
long-term target once its P4/C6 BSP split is implemented. C5/C6 remain useful
for future GPIO-linked DMV experiments, not as the initial Fuzzball host.

### ESP32-S3 DevKitC N16R8 bring-up

Three Espressif ESP32-S3-DevKitC boards are now available. The first board
identifies as ESP32-S3 revision 0.2 with 8 MB embedded PSRAM and 16 MB flash.
The dedicated profile is `ESPPDP_HW_S3_DEVKITC`; it leaves the classic ESP32
configuration untouched, disables the unsupported Classic-Bluetooth HID path,
and reserves GPIO35--37 for octal flash/PSRAM. The bare DevKitC has no LCD or
microSD socket, so the profile's exposed GPIO assignments are for later
external wiring.

The S3 profile builds successfully under ESP-IDF 6.0.2. The generated image is
`0x125610` with 8% free in the current `0x140000` app partition. To reproduce
the clean-target build, preserve the existing `sdkconfig`, remove it, then run
`SDKCONFIG_DEFAULTS=sdkconfig.defaults.esp32s3_n16r8 idf.py set-target esp32s3`
followed by the same defaults-qualified `idf.py build`. This is source/build
evidence only; flashing and runtime validation are still pending.
