# ESP32 Fuzzball port plan

Status: base selection complete; active work is migrating the candidate to
ESP-IDF v6.1 before adding Fuzzball devices.  No ESP32 Fuzzball boot or
network claim has been made.

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

The first simulator-correctness intake is also building: the KERTAB/Q22 fix
that forces kernel, supervisor, and user D-space page-7 PDR writes to `077406`
is now present on `pixitha/fuzzball`. The post-fix IDF 6.0.2 image is
`0x117360` bytes (13% free). No guest boot claim has been made.

The follow-up SIMH correctness fixes also build cleanly: mask invalid PSW bits
when loading a trap frame, and accept the exact Q22 I/O-page base in the
autoconfiguration address checks. These remain behavior-only simulator fixes;
the IDF 6.0.2 image remains `0x117360` bytes.

## SIMH delta audit — Fuzzball-specific requirements

The dirty desktop SIMH tree contains more than the new KWV11 file. Comparing
its semantic changes with the recovered DCN6 configuration gives this
transplant order:

1. **KERTAB/MMU correction (required before BOS).** The desktop CPU change
   forces kernel, supervisor, and user D-space page-7 PDR to `077406` for the
   Q22 I/O page. This addresses the documented KERTAB page-length fault.
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
