# Handoff

Last updated: 2026-10-11. **On order pause:** waiting for the IDC breakouts.
Nothing to do on the bench until they land.

## Where things stand

- M0 to M5 done (`docs/build_log/m5.md`). No remote.
- M6 started and paused the same day (`docs/build_log/m6.md`). The host side
  was wired and checked, then a loose strand on the homemade DB-19 adapter
  bridged -12 V (strip pos 9) onto /WRREQ (pos 10) on first Apple connect.
  U1 burned (replaced). The Liron card's IWM WRDATA died; Rico desoldered it,
  fitted a socket at IC105 and a donor IWM from a dead IF-83. The card reads
  and writes again (ADTPro REF800K onto disk 2 through the working UniDisk).
  Pico inputs checked OK over SWD.
- No M6 firmware. No M6 spec yet (brainstorming stopped at the host wiring;
  architectural path: spec, then plan).
- Spare IWMs: one left, on the other dead IF-83.
- `blinky/` (RP2350 bring-up test) is stashed: `stash@{0}: blinky: RP2350 bring-up test`. Its git-ignored `blinky/build/` output stays on disk.

## Waiting on (Rico)

- Proper IDC breakouts (2x10 IDC to screw terminals), two ordered: one for the
  host side (replaces the homemade DB-19 adapter), one for the mechanism side
  (replaces the homemade CN104 ribbon tap, same weaknesses).

## Resume order (when the breakouts land)

1. Quick ring-out of each assembly with its ribbon plugged in (end connector to
   terminals): catches a bad crimp or a flipped ribbon, and pin 1 / key matches
   the stripe. Ferrules or tinned/solid wire in the clamps; no stray strands.
2. Rebuild the host side on one breakout. **Rail-isolation checks before
   any Apple power, every time** (each Apple rail position vs its neighbours
   and every signal row). Low current limit on the bench +5 for first connect.
   Wiring plan and pin numbers: `docs/build_log/m6.md`.
3. Rebuild the mechanism side on the other breakout (CN104). Same isolation
   checks: +12 V (13/15/17/19) vs RD (16), WRTDATA (18) and every signal.
   Use this rework for the owed +5V/GND path fix: short heavy runs to pin 11
   and GND 1/3/5/7, bulk caps at the terminals. Re-run a disk read scan on
   drive A at 5.0 V to confirm the IR drop is gone before going on.
4. Re-format disk 2 with our board (it lost the M5 board-written copy).
5. Optional: scope `PR#5` with no drive for a baseline of the host bus.
6. Resume M6 brainstorming: RAM disk content (proposed: ProDOS image in flash
   plus a RAM write overlay), then the spec. The spec must also replace the
   74LVC1G07 references (spec sec. 1 and 3.5, `level_shifting.md`) with "ACK on
   U3 with RD". `CLAUDE.md`, `pin_budget.md` and `pins.h` are already updated.
7. Proposed small firmware addition (not approved yet): a `pins` console
   command that prints the host input levels.

## Bench state at shutdown

- Pico 2 flashed with `61d4690` (M5 + READY fix).
- Bench +5 supply at about 5.0 V (it was 5.55 V for M5 because of the IR drop
  to CN104). Rework the +5V/GND path before the mechanism reads disks again.
- New U1 seated; host wiring being cleaned up by Rico. Homemade DB-19 adapter
  retired.
- Liron card in slot 5 with the donor IWM in its new socket; working UniDisk on
  it.
- Scope probes off CN104; reattach per the task.
- Pico console `/dev/cu.usbmodem114401`, Debug Probe `114202`. GPIO state can
  be read over SWD without halting (`mdw 0xd0000004`, pad registers at
  `0x40038004 + 4n`).

## Notes

- bytehamr: all published 2026-10-11 ("M6: a loose strand put -12 V on
  /WRREQ", "The Liron card runs on a UniDisk's IWM now", M5 console
  screenshots). The project page Hardware table still lists the 74LVC1G07
  (fix with the M6 spec).
- Publish only full-resolution screen captures (no `--downscale-to-native`).
- No Hackaday draft (Rico does Hackaday log by log, once proven).
- `project_byte_hamr`: three untracked files from 2026-09-14 stashed there as
  `stash@{0}: dino - wip`.
