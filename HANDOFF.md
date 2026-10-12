# Handoff

Last updated: 2026-10-11, M6 paused; Liron repaired and write-tested.

## Where things stand

- M0 to M5 done (`docs/build_log/m5.md`). No remote.
- M6 started and paused the same day: `docs/build_log/m6.md`. Host-side wiring
  was built and checked; on first Apple connect a loose strand on the homemade
  DB-19 adapter bridged -12 V (strip pos 9) onto /WRREQ (pos 10). U1 burned
  (replaced) and the Liron card's IWM WRDATA output died. **Liron repaired the
  same day** with a donor IWM from a dead IF-83; the IIe sees the working
  UniDisk again (S5,D1, 1600 blocks). Pico inputs checked OK over SWD.
- No M6 firmware. No M6 spec yet (brainstorming stopped at the host wiring;
  architectural path: spec, then plan).
- Untracked `blinky/` is not part of this work; leave it alone.

## Blocked on (Rico)

- Proper IDC breakouts (ordered) to replace the homemade DB-19 adapter.

## Resume order

1. Liron write test done (ADTPro REF800K onto disk 2 through the working
   UniDisk). Disk 2 lost its M5 board-written copy: re-format it with our board.
2. Optional: scope `PR#5` with no drive for a baseline of the host bus.
3. Rebuild the host side on the IDC breakouts. **Rail-isolation checks before
   any Apple power, every time** (each Apple rail position vs its neighbours
   and every signal row). Low current limit on the bench +5 for first connect.
4. Resume M6 brainstorming: RAM disk content (proposed: ProDOS image in flash
   plus a RAM write overlay), then the spec. The spec must also replace the
   74LVC1G07 references (spec sec. 1 and 3.5, `level_shifting.md`) with "ACK on
   U3 with RD". `CLAUDE.md`, `pin_budget.md` and `pins.h` are already updated.
5. Proposed small firmware addition (not approved yet): a `pins` console
   command that prints the host input levels.

## Bench state at shutdown

- Pico 2 flashed with `61d4690` (M5 + READY fix).
- Bench +5 supply was set back to about 5.0 V for the host test (it was 5.55 V
  for M5 because of the IR drop to CN104). Rework the +5V/GND path before the
  mechanism needs to read disks again.
- New U1 seated; host wiring being cleaned up by Rico.
- Liron card back in slot 5 with the donor IWM; working UniDisk on it.
- Scope probes: moved off CN104 during M6; reattach per the task.
- Pico console `/dev/cu.usbmodem114401`, Debug Probe `114202`. GPIO state can
  be read over SWD without halting (`mdw 0xd0000004`, pad registers at
  `0x40038004 + 4n`).

## Notes

- bytehamr: published 2026-10-11, "M6: a loose strand put -12 V on /WRREQ" and
  "The Liron card runs on a UniDisk's IWM now". Hardware table on the project
  page still lists the 74LVC1G07 (fix with the M6 spec).
- No Hackaday draft (Rico does Hackaday log by log, once proven).
- `project_byte_hamr`: three untracked files from 2026-09-14 stashed there as
  `stash@{0}: dino - wip`.
