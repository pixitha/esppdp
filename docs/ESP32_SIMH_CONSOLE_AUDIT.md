# ESP32-S3 SIMH control-console source audit

Status (2026-10-10): the first SCP command-runtime stage was flashed and
tested on the ESP32-S3. The second-reset fix has now been verified by a
successful `BOOT RQ0`; `SHOW RQ`, `EXAMINE PC`, `STEP`, and `CONTINUE` also
worked on hardware. `RUN` has not been retested. This is not yet the full
upstream SIMH control program.

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
- `firmware/main/scp.h` still declares many upstream APIs but defines
  `sim_brk_test`, `sim_brk_match_type`, and related breakpoint interfaces
  as no-ops. The first-stage `STEP` uses the same scheduled-unit stop
  mechanism as upstream SCP; `BREAK` is deliberately not registered until
  its real breakpoint table and CPU hooks can be restored.
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
currently provides `EXAMINE`/`DEPOSIT` for scalar CPU registers and one
CPU memory word, `GO`, `RUN`, `RESET`, `STEP`, `CONTINUE`, `BOOT`, basic
`SHOW`, and `HELP`. Upstream-style abbreviated command names work.
SCP pauses timer services while at the prompt; `RUN` and `BOOT` clear the
old event schedule and reset devices before restarting, as upstream does.

This is deliberately narrower than full SCP: no address ranges, register
arrays, symbolic disassembly, device modifiers, attach/detach, command
scripts, logging commands, or breakpoints. `SHOW` reports device/unit state
but not all upstream device-specific details. Octal is the PDP-11 default;
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

For the first hardware check, use a disposable or backed-up SD image, press
Ctrl+E in the guest, then try `SHOW RQ`, `EXAMINE PC`, `EXAMINE R0`,
`EXAMINE 1000`, `STEP`, `EXAMINE PC`, and `CONTINUE`. `DEPOSIT`, `RESET`,
`RUN`, and `BOOT` alter guest state; test those only after recording a clean
boot result.

## Restoration sequence

1. Verify the first-stage commands on the S3 against a copied RT-11/BOS6
   disk and compare their transcript with the pinned Mac baseline. Preserve
   the pinned Mac source unchanged.
2. Bring over upstream register-array/range, symbol, switch, and device
   modifier parsing instead of expanding the first-stage scalar handlers.
3. Restore the full breakpoint table and CPU hooks; then enable `BREAK`,
   `NOBREAK`, `NEXT`, and their `SHOW` counterparts.
4. Bring over upstream media and script commands with SD-specific safety
   checks, then compare UART0 and native USB serial separately. A control
   prompt is not a guest shutdown or guaranteed disk flush.
