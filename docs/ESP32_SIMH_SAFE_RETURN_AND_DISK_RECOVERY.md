# ESP32 SIMH safe return and disk recovery design

Status: design only; do not advertise writable guest disks as safe.

## Current lifecycle

The boot menu selects an image and profile, then `app_main()` starts `simh_task`
on CPU1. The task calls the SIMH `main()` function. `scp.c` then runs an
unbounded `sim_instr()` loop and enters the SCP command console when the guest
halts or the operator requests it. There is no supported path from that console
back to the boot menu; returning from `main()` would end the task without
recreating `app_main()`.

The ESP SCP console supports explicit media attach/detach commands and refuses
to detach a unit with a pending simulator event. It has no forced-detach path.
The ESP disk write path calls `fflush()` after sector writes. Disk detach also
calls `fflush()` and `fclose()`, but it does not currently call `fsync()`, and
the caller cannot safely return to a menu after a failed close. Guest filesystems
and application-level disk consistency are outside those host-side operations.

## Return-to-menu contract

Implement a deliberate `RETURN MENU` command only after the following sequence
has been designed and tested:

1. The guest is shut down using its own operating-system procedure. The SIMH
   CPU is then stopped and the operator is at the SCP console. A simulator HALT
   alone does not prove that the guest filesystem was cleanly shut down.
2. The operator confirms the return. The command reports which writable images
   are attached and that return will flush and close them before restarting.
3. The CPU task owns the transition. It disables host-escape/run resumption,
   stops timer services, and verifies that no disk unit has a pending event or
   active I/O. The SD upload portal must be stopped and may not access the card
   concurrently.
4. Preflight every attached disk stream before detaching any: check stream
   errors, `fflush()` each stream, and use the ESP VFS durability operation
   (`fsync(fileno(stream))` or its verified FATFS equivalent). A preflight
   failure leaves all attachments intact, keeps the simulator stopped, and
   reports the failing unit so the operator can retry or reset deliberately.
5. After every stream passes preflight, detach through each device's normal
   callback. Check every result. A failure blocks the menu restart and reports
   the state of every unit; never claim the disks are cleanly closed after a
   partial detach.
6. Restart the ESP32 only after all disk units are detached successfully. The
   reset returns through normal startup and the boot menu. Do not try to reuse
   the current SIMH task or invoke `app_main()` a second time.

The command must be unavailable while the guest is executing, while a DO script
is active, or when any media unit has a pending event. It must not silently
reset the board, detach media, or treat a simulator stop as a guest shutdown.
If preflight or close fails, stay in the stopped SCP console; recovery must be
an explicit operator choice.

## Disk safety and recovery policy

- Treat every SD-backed guest image as mutable. Keep an immutable source copy
  on another device and record its byte size and SHA-256 before copying a
  disposable working image to the card.
- Run guest-disk tests only against disposable copies. A successful host-side
  flush does not prove that the guest operating system left its filesystem
  consistent.
- After a confirmed clean guest shutdown and `RETURN MENU`, compare the card
  image size and hash to the expected post-run copy. If the guest was not
  cleanly shut down, preserve the image as evidence and repair a duplicate with
  guest-native filesystem tools before reuse.
- If power is lost or reset occurs during guest writes, do not automatically
  boot that image again. Preserve it, copy it off the card, check the SD FAT
  filesystem read-only on a host, and run any guest-filesystem recovery on a
  duplicate. Neither check can reconstruct unflushed guest writes.
- Keep uploads disabled whenever SIMH owns the SD card. The existing portal's
  before-SIMH maintenance lifecycle is the required ownership boundary.
- Do not add automatic backups, image repair, format, or overwrite actions to
  the boot menu until each has a separate reviewed recovery procedure.

## Validation required before implementation is called safe

Use disposable images and capture serial logs for normal return, each supported
controller, multiple attached units, a pending-event refusal, forced I/O/flush
failure, close failure, reset after successful detach, reset during guest
writes, and confirmation that the portal cannot start while SIMH owns the SD
card. Verify guest filesystem integrity independently from SIMH's attach,
bootstrap, and CPU-stop status. A clean task return or successful `fflush()` is
not a substitute for these checks.
