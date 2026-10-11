# ESP32-S3 SIMH control-console source audit

Status (2026-10-10): the first SCP command-runtime stage was flashed and
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
circular/structure-backed register arrays, symbolic disassembly, device
modifiers, attach/detach, command scripts, logging commands, or breakpoint
actions. `SHOW` reports device/unit
state but not all upstream device-specific details. Octal is the PDP-11 default;
`-D`, `-O`, and CPU memory `-V` are recognized by examine/deposit.

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
Do not test a memory-range `DEPOSIT` on a running Fuzzball session without
a copied disk and a known-safe guest RAM region. This particular run used
full `flash`, which also rewrote SPIFFS at `0x150000`; use `app-flash` for
future code-only updates that should preserve SPIFFS writes.

## Restoration sequence

1. Verify the first-stage commands on the S3 against a copied RT-11/BOS6
   disk and compare their transcript with the pinned Mac baseline. Preserve
   the pinned Mac source unchanged.
2. Extend upstream examine/deposit loops and device targeting, with host
   tests for ESP adapter bounds and read-only behavior; then bring over the
   remaining upstream symbol, switch, circular-array, and device-modifier
   parsing instead of expanding the first-stage handlers indefinitely.
3. Extend the S3 breakpoint check on a disposable image to cover proceed
   counts, other types, and repeated hits at a known-safe loop address.
   `NEXT` remains deferred until the dynamic step-over and parser machinery
   exist.
4. Bring over upstream media and script commands with SD-specific safety
   checks, then compare UART0 and native USB serial separately. A control
   prompt is not a guest shutdown or guaranteed disk flush.
