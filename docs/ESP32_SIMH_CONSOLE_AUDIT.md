# ESP32-S3 SIMH control-console source audit

Status (2026-10-11): the first SCP command-runtime stage was flashed and
tested on the ESP32-S3. `RUN`, `BOOT`, register examine/deposit, `STEP`, and
`CONTINUE` now have hardware transcripts. A second stage restores the CPU
breakpoint table and `BREAK`/`NOBREAK`/`SHOW BREAK`; the basic stop, step,
clear, and resume path has also been tested on the board. This is not yet
the full upstream SIMH control program.

## Source and behavior boundary

- The firmware inherits Sprite_tm's deliberately reduced `firmware/main/scp.c`
  and `scp.h`. The current `scp.c` is about 1,200 lines, including the new
  BOS6 boot setup. The pinned, working Mac SIMH baseline at
  `fuzzball_workspace/vendor/simh-host/scp.c` is about 16,800 lines and
  contains the normal command table, parser, `SHOW`, `EXAMINE`/`DEPOSIT`,
  `RUN`/`STEP`/`CONTINUE`, reset, media commands, and breakpoint machinery.
  The ESP source headers identify SIMH 4.0-era interfaces, but the exact
  upstream `scp.c` commit from which the reduced 2021 port was cut has not
  yet been proven. Match that revision before transplanting command code.
- `firmware/main/scp.c` selects the boot profile, attaches its disk, boots
  once, and calls `sim_instr()` in a loop. Ctrl+E and simulator stops now
  enter `scp_console_run()` in `scp_console.c`. That command reader uses
  SIMH's `get_glyph_cmd()` and `CTAB` dispatch rather than the removed
  three-command string-comparison monitor.
- `firmware/main/scp_break.c` now provides the address/type table, proceed
  counts, same-instruction hit suppression, and type summary used by the
  existing PDP-11 execute/read/write hooks. `BREAK` defaults to virtual-PC
  type E, and `-E`, `-P`, `-R`, `-S`, `-W`, and `-X` match the CPU's advertised
  types. SCP action scripts, ranges, and dynamic step-over breakpoints are
  still unsupported. The first-stage `STEP` uses the same scheduled-unit
  stop mechanism as upstream SCP.
- The S3 input adapter in `sim_term.c` already receives from UART0 and native
  USB Serial/JTAG. Output is flushed per guest character. The prompt must
  consume those same queues while guest input is suspended; it must not
  call the ESP-IDF stdin VFS poll on every idle iteration.
- The modern Mac SCP source includes tape, video, socket, host-file,
  front-panel, asynchronous-I/O, and help dependencies that are absent or
  stubbed in this firmware. Replacing `scp.c` wholesale would conflict with
  its existing boot setup and duplicate SIMH globals and helper functions.

## Current implementation in the working tree

After `sim_instr()` returns, a non-Ctrl+E stop reports its reason and
enters the command prompt instead of silently restarting. The reader yields
one FreeRTOS tick while waiting for input. Blank lines stay at `sim>`;
`CONTINUE` is explicit, matching normal SCP behavior. The command table
currently provides `EXAMINE`/`DEPOSIT` for scalar CPU registers, bounded
plain-array indices, and bounded CPU memory ranges, plus `GO`, `RUN`,
`RESET`, `STEP`, `CONTINUE`, `BOOT`, basic
`SHOW`, `BREAK`, `NOBREAK`, and `HELP`. Upstream-style abbreviated command
names work.
SCP pauses timer services while at the prompt; `RUN` and `BOOT` clear the
old event schedule and reset devices before restarting, as upstream does.

This is deliberately narrower than full SCP: no unbounded address ranges,
structure-backed register arrays, attach/detach, command scripts, logging
commands, or breakpoint actions. The later parser stage added bounded
circular/unit-backed arrays, symbolic disassembly, and device/unit modifiers.
`SHOW` reports device/unit state but not all upstream device-specific details.
Octal is the PDP-11 default; the later parser accepts radix, symbolic, and
virtual-address switches for examine/deposit.

The first on-board transcript is
`logs/esp32-bos6-20261010-171352.log`. It confirms the SD card mounts,
RT-11 starts BOS6, Ctrl+E reaches `sim>`, and `SHOW` reports the current
device set. `EX` without an operand correctly says "Missing argument".
During the prompt, guest instruction and event counts fall to zero. Both
`RUN` and `BOOT` returned error 101 (`SCPE_IERR`). The port had commented
out the upstream `PCQ` register lookup in `cpu_reset()` but left its
`else return SCPE_IERR`, so every reset after initial memory allocation
failed. The orphaned return was removed.

The follow-up hardware transcript,
`logs/esp32-bos6-20261010-172702.log`, confirms `SHOW RQ` reports the
attached SD image, `EX PC` reads the CPU register, `STEP` stops with
"Step expired" and a changed PC, and `CONTINUE` resumes guest execution.
`BOOT RQ0` then succeeds and boots RT-11 from the SD image again, proving
the second-reset failure is fixed for that command. The console remains
available after another Ctrl+E. The log has no panic or disk I/O error.
It does not verify `RUN`, a complete BOS6 login, or long-running disk
stability. The user's Ctrl+C during the second `PGWAIT` run interrupted
the guest startup sequence, so that sequence cannot establish BOS6 boot
health.

The later `logs/esp32-bos6-20261010-173502.log` verifies `RUN` no longer
returns error 101 and `BOOT RQ0` recovers from the resulting HALT. In
`logs/esp32-bos6-20261010-174559.log`, the guest reported `WAIT: 0`; `STEP 1`
and `STEP 5` expired, `CONTINUE` resumed, and a scalar `DEPOSIT R0` was read
back and restored. These logs predate the breakpoint stage. The host-only
`firmware/tests/test_scp_break.c` checks set/clear, type summaries, proceed
counts, and same-instruction suppression, including under sanitizers.

The S3 transcript `logs/esp32-bos6-20261010-175917.log` verifies the basic
breakpoint path: at `WAIT: 0`, `EX PC` returned `150226`; `BREAK` followed by
`SHOW BREAK` listed `150226: -E`; `CONTINUE` stopped for `Breakpoint` at the
same PC. `NOBREAK ALL` emptied the table, and the next `CONTINUE` resumed
guest instructions and terminal output. No panic or disk I/O error appears
in this run. The stop message printed `37777750226` because the signed CPU
register was cast to a wide `t_value` without a 16-bit mask.

After masking that PC value, `logs/esp32-bos6-20261010-180552.log` shows
the stop address correctly as `150066`. With that breakpoint still set,
`STEP 1` expired at `150072`, and `CONTINUE` ran guest instructions until
the user returned to SCP. The mistyped `nobreak al` was rejected; the
subsequent `NOBREAK ALL` succeeded, and `SHOW BREAK` reported no breakpoints.
No panic or disk I/O error appears. This verifies the basic step-past-hit
path, but not breakpoint proceed counts, other breakpoint types, repeated
hits at a loop address, or a complete BOS6 login.

The next parser increment is built and read-tested on the S3.
`EXAMINE`/`DEPOSIT` now accept
numeric memory ranges (`1000-1010`, `1000:1010`, `1000/10`) and indexed
plain C-array registers (`IREQ[0-3]`), limited to 64 values per command.
The `get_range`, `strtotv`, `get_rval`, and `put_rval` primitives in
`firmware/main/scp_exdep_upstream.c` are adapted from SIMH commit
`8bbec614838bca7d5b8a4963b2a877c6be335c17` (pre-register-layout
rewrite, matching this fork's `REG` structure). Adaptations are explicit:
`const REG` signatures, libc character classification because this fork
lacks `sim_is*` helpers, numeric overflow rejection, and omission of the
newer `REG_DEPOSIT` callback that does not exist in this layout. The ESP
command reader remains a narrower adapter, **not** upstream `exdep_cmd`:
it checks bounds, enforces the 64-value limit, and currently rejects
circular, structure-backed, and unit-backed arrays. Host tests cover the
upstream primitives, range errors, plain arrays, and PC updates.

The S3 transcript `logs/esp32-bos6-20261011-104455.log` shows SDMMC
mounting in 4-bit mode, attachment of `/sdcard/BOS6.IMG`, and an RT-11
prompt after the operator interrupted `PGWAIT`. At `sim>`, `EX PC` returned
`150254`; `EX 1000-1010` returned five words at even addresses; and
`EX IREQ[0-3]` returned four array values. `CONTINUE` resumed guest
execution and printed the RT-11 `.`; the operator then used Ctrl+E to
return to `sim>`. No panic or guest disk I/O error appears. This is
read-only SCP evidence, **not** a full BOS6 login or a range-deposit test.
After this run, the range-count comparison was made overflow-safe; that
small follow-up has host/build validation but has not separately run on
the board.

The next source increment adapts the matching historical upstream
`exdep_reg_loop`, `exdep_addr_loop`, and `find_unit` paths in
`firmware/main/scp_exdep_adapter.c` and `scp_exdep_target.c`. The ESP command
reader now accepts an optional device/unit before a register or address,
including `EX RQ0 SA`, `EX CPU PC`, register ranges such as `R0-R5`, and
unindexed plain register arrays. It still limits each range to 64 values.
The adapter preflights every selected register before a deposit, checks
descriptor storage bounds and read-only flags, and refuses writes to a
read-only or disabled unit. It uses the device's own address increment and
`get_aval`/`deposit` callbacks, but does not expose upstream interactive,
search, symbolic, output-file, or raw-media deposit modes. `STATE` remains
the older scalar-register summary, not upstream's full array expansion.
Host tests in `firmware/tests/test_scp_exdep_adapter.c` and
`test_scp_exdep_target.c` pass with address/array bounds, read-only, partial
write prevention, and unit-resolution cases. ESP-IDF 6.0.2 builds an S3
image. The S3 log `logs/esp32-bos6-20261011-110233.log` subsequently
confirmed `EX CPU PC`, `EX CPU R0-R5`, `EX RQ0 SA`, and `CONTINUE` on the
board. It did not test deposits.

The following parser increment reuses the historical `get_sim_sw` grammar
and the existing PDP-11 `fprint_sym`/`parse_sym` hooks. The bounded ESP
option adapter accepts supported radix, symbolic, and virtual-address
switches before or after a device/unit modifier. Symbolic address examine
advances by the decoded instruction length; symbolic deposit can emit the
extra instruction words, with range, buffer, read-only, and fixed-capacity
checks before writing. The older numeric parser now uses upstream-style
`strtotv` so full-width values do not depend on ESP32 `long` size, with a
bounded K/M suffix multiplication. Circular and unit-backed arrays are now
descriptor-checked; the latter also checks the destination unit's read-only
flag.
Structure-backed arrays remain unsupported because no active PDP-11 device
uses one and the descriptor alone does not prove its backing allocation.
This remains narrower than upstream: search expressions, output files,
interactive examine/deposit, and raw attached-media deposits are absent.
Host sanitizer tests and the ESP-IDF 6.0.2 build pass; **this parser
increment has not yet been flashed or board-tested**. Read-only S3 probes
should include `EX -M CPU 150100-150110`, `EX CPU PCQ[0-3]`, and
`EX RQ0 CPKT[0-3]` after confirming those ranges in the current profile.
`PCQ` tests circular-array addressing only: the older ESP CPU port leaves
the upstream live `PCQ` queue-pointer update disabled, so its chronological
ordering is not yet trustworthy.

The first S3 parser run, `logs/esp32-bos6-20261011-155739.log`, confirmed
`EX -M CPU 150100-150110` and `EX CPU PCQ[0-3]`. Both attempts at
`EX RQ0 CPKT[0-3]` returned `SCP: Function not implemented`. The device and
register parsed correctly; the ESP preflight incorrectly required every
`REG_UNIT` field to be at least 32 bits, while RQ `CPKT` is the 16-bit
`UNIT.us9` field. The matching historical upstream `get_rval`/`put_rval`
path also dereferenced `REG_UNIT` fields as `uint32`, which would be unsafe
for `us9` on ESP32. The follow-up uses the descriptor's actual 8/16/32-bit
field size for unit-array access, matching the size-aware behavior of newer
upstream SIMH, and retains read-only/bounds checks. A host sanitizer test
checks 16-bit `CPKT`-style reads and writes without changing adjacent
`UNIT.us10`. The S3 re-test in `logs/esp32-bos6-20261011-160201.log`
shows two successful `EX RQ0 CPKT[0-3]` reads, all zero while the RQ
units were idle. `logs/esp32-bos6-20261011-160826.log` then read
`EX RQ0 CAPAC[0-3]` as `311200`, `311200`, `311200`, `800`, matching the
three RD54 and one RX50 capacities in `pdp11_rq.c`. These are read-only
S3 checks of both 16-bit and 32-bit unit-backed register fields, not a
deposit or active-transfer `CPKT` check.

`logs/esp32-bos6-20261011-161120.log` adds an S3 breakpoint-table check:
`BREAK -E 150100[2]` and `BREAK -P 150100` appeared in `SHOW BREAK` as
`-E [2]` and `-P`; `NOBREAK ALL` removed both. The guest was not resumed
with these breakpoints installed, so this confirms count/type parsing,
display, and clearing, not a two-hit stop or physical execute trigger.

`logs/esp32-bos6-20261011-161652.log` confirms `BREAK 1000`/`RUN 1000`
stopped at `001000`, then `DEPOSIT 1000 777` created a self-loop in guest
RAM. After `BREAK -E 1000[2]` and `CONTINUE`, millions of instructions ran
at `001000` without a second breakpoint stop. The cause is in the ESP
event-queue port: `UPDATE_SIM_TIME` was defined empty, so `sim_gtime()`
never advanced and the breakpoint table suppressed every later visit as
the same instruction. The upstream time-accounting macro is now restored,
with a host test exercising a two-hit loop and queued-event time. Host
sanitizers and the ESP-IDF 6.0.2 build pass. The first commands in the
transcript were entered at the RT-11 prompt before Ctrl+E, then repeated
correctly at `sim>`.

The S3 re-test in `logs/esp32-bos6-20261011-162138.log` used the corrected
firmware (`e9fdf50-dirty`, built at 16:19:44). It stopped at `001000` on
`BREAK 1000`/`RUN 1000`, then stopped there again immediately after
`BREAK -E 1000[2]`/`CONTINUE`, confirming counted-breakpoint triggering
on the board. No panic or disk I/O error appears in this short run. This
does not validate physical breakpoints, prolonged operation, or a complete
BOS6 login.

Do not test a memory-range `DEPOSIT` on a running Fuzzball session without
a copied disk and a known-safe guest RAM region. This particular run used
full `flash`, which also rewrote SPIFFS at `0x150000`; use `app-flash` for
future code-only updates that should preserve SPIFFS writes.

## Restoration sequence

1. Verify the first-stage commands on the S3 against a copied RT-11/BOS6
   disk and compare their transcript with the pinned Mac baseline. Preserve
   the pinned Mac source unchanged.
2. The symbolic/switch/circular/unit-array parser has read-only S3 probes:
   disassembly, `PCQ`, `CPKT`, and `CAPAC`. Deposits and active-transfer
   `CPKT` have not been tested. Keep search expressions, output files, and
   interactive modes separate until their upstream dependencies are ported.
3. Extend the S3 breakpoint check on a disposable image to cover proceed
   counts, other types, and repeated hits at a known-safe loop address.
   `NEXT` remains deferred until the dynamic step-over and parser machinery
   exist.
4. Bring over upstream media and script commands with SD-specific safety
   checks, then compare UART0 and native USB serial separately. A control
   prompt is not a guest shutdown or guaranteed disk flush.
