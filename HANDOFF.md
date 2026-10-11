# Handoff

Last updated: 2026-10-11, M5 bench done.

## Where things stand

- M0 to M5 done. M5 (write + format) passed on the bench 2026-10-11:
  `docs/build_log/m5.md`. ADTPro image of the board-written disk, read through
  the working UniDisk, is byte-identical to REF800K. Merged into `main`. No remote.
- Untracked `blinky/` is not part of this work; leave it alone.

## Open

- bytehamr log for M5: not written yet. No Hackaday draft (Rico does Hackaday log
  by log, once proven).

## Bench state at shutdown

- Pico 2 flashed with `61d4690` (M5 + READY fix).
- Bench +5V supply set to 5.55 V to get 4.94-4.96 V at CN104 pin 11 (0.6 V lost
  in the wiring). Rework the +5V/GND path, then set the supply back to about 5 V.
- Scope probes on CN104: CH1 pin 10 /WRTGATE, CH2 pin 18 WRTDATA, CH3 pin 16 RD,
  ground pin 1. Pins 13/15/17/19 are +12 V. Attach probes with the mechanism
  supply off.
- Disk 1 ("TEST write") holds a board-written REF800K copy. Disk 2 is in the
  working UniDisk.
- Pico console `/dev/cu.usbmodem114401`, Debug Probe `114202`. Power-down order:
  mechanism supply off before any Pico USB unplug. Flash XIP error: power cycle
  the Pico.

## Tools added this session

- Bench console log: every command and reply goes to
  `docs/captures/mNN/<date>_console.log`, tailed live in an iTerm2 window.
- Window-only screenshots: `~/.claude/scripts/winsnap.sh <app> <name> <out.png>`;
  scope and capture notes in `~/.claude/CLAUDE.md` ("Bench captures").
- Apple II screen: the obs-screenshot skill (HDMI capture).

## Next

- Host tool: `Console.writeblk` polls the port with 0.2 s reads; likely most of
  the 0.5-0.65 s per block in `writedisk`.
- M6: SmartPort on the RP2350 (PicoPort port, RAM disk, ProDOS boot via Liron).
