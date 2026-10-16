# Handoff

Last updated: 2026-10-15. **M6 passed on the bench** and is merged
to main: ProDOS boots from the board's RAM disk through the Liron card,
saves and loads files, ACK 5.3 us after /WRREQ rises (card window about
105 us). **M7 passed on the bench** and is merged: ProDOS boots from a real
800K floppy through the board (sentinel disk written by the UniDisk, checksums
out to track 72), Copy II Plus writes through it, and SPCOMPARE matches the
working UniDisk byte for byte with a disk, write-protected and with no disk.
J2 is on an off-the-shelf breakout, and M7.5 moved the rig onto IIe power
(2026-10-15). **M8 passed on the bench** and is merged (2026-10-15, `a1e51db`):
daisy chain with the working UniDisk in both orders, full disk copied to it.
Next: commit, then M9. Bench photo of the whole rig: `docs/photos/m7/2026-10-12_bench-all-wired.jpg`.

## Where things stand

- M0 to M5 done (`docs/build_log/m5.md`). No remote.
- M6 (`docs/build_log/m6.md`):
  - 2026-10-11: homemade DB-19 adapter bridged -12 V onto /WRREQ. U1 burned
    (replaced); Liron IWM replaced with a donor from a dead IF-83 (socketed at
    IC105). Card reads and writes again.
  - 2026-10-12: host side on J1 (KingPow IDC20P-B, IDC to screw terminals).
    The DB-19 to IDC20 cable uses Apple's drive-internal mapping: **ACK = J1.20**,
    J1.19 no wire. Odd row = GND + Apple rails (9, 11, 13, 15 left empty), even
    row = signals. Full J1 map and beep results in `m6.md`.
  - Low-power connect (Pico unplugged) clean: 0.04 mA, rails at J1 nominal.
  - Pico on (`61d4690`, U3 /OE held high): host inputs read over SWD match J1
    after fixing PH2/PH3 jumpers shifted at the Pico header. `PR#5` sampling
    saw PH0+PH2 reset, REQ, and /WRREQ low (packet).
  - Scope: WR idled at 3.40 V (below the 8T245's 3.5 V VIH at VCCB = 5 V)
    because of our 10k pull-down. **WR pull-down on U1 B7 is now 100k**; idle
    3.96 V (host drives about 4.0 V through about 1.9k). Packet start decodes
    `FF 3F CF F3 FC FF C3 81`, cell 3.91 us. Captures in `docs/captures/m6/`.
- M6 firmware (merged; last `2e7311a`): SmartPort device serving an 800K ProDOS RAM
  disk (flash image + 384-block RAM overlay). Results: `docs/build_log/m6.md`
  "M6 passed on the bench". Spec
  `docs/superpowers/specs/2026-10-12-m6-smartport-ramdisk-design.md`, plan
  `docs/superpowers/plans/2026-10-12-m6-smartport-ramdisk.md`. Bench steps:
  `docs/build_log/m6.md` "Firmware ready for the bench".
- The image `docs/private/ref/m6_boot800k.po` (PRODOS + BASIC.SYSTEM, from
  ProDOS 2.4.1 via AppleCommander) is git-ignored; CMake links it when present.
- Spare IWMs: one left, on the other dead IF-83.
- `blinky/` (RP2350 bring-up test) is stashed: `stash@{0}: blinky: RP2350 bring-up test`. Its git-ignored `blinky/build/` output stays on disk.

## Resume order

1. **M7.5 host power: done 2026-10-15** (`docs/build_log/m7_5.md`). Bench
   supply retired; board and mechanism on the IIe rails via J1. Sentinel boot
   and SPCOMPARE match M7. +5 read 4.54 V; thin +5 jumpers
   replaced with 24 AWG in M8 (4.7 V at the UniDisk feed). Not yet run on
   IIe power: full read scan, SPCOMPARE write-protected / no-disk.
   Eject works on IIe power (button, M8).
2. **M8 brainstorm and spec: daisy chain.** Our board in front of the working
   UniDisk, then behind it. Rear port J3 mapped in `pin_budget.md` (IDC40
   breakout, ribbon crimped flat, DB-19 soldered in Apple mapping, J1.n ->
   J3.n except 8/12/14, plus J1.12 -> GND). Rear port wired and proven
   2026-10-15 (`docs/build_log/m8.md`): U4 and U5 fitted, cable loopback
   with the `loop` command, end-to-end DB-19 beep.
   Chain firmware on the bench 2026-10-15 (`a1e51db`, main):
   PIO2 pass-through of PH3 and /ENBL after our INIT, stale-reply fix. Both
   orders work; ProDOS lists S5,D1 and S5,D2. /ENBL rule confirmed. Full
   disk copied D1 -> D2 (UniDisk) and the sentinel boots from the copy.
   **M8 passed on the bench.** Next: commit the firmware.
3. Eject button: wired 2026-10-15 (GP22 pin 29 to GND pin 28, internal
   pull-up, on by default). Works.
4. Re-format disk 2 with our board (`imgcopy` puts the M6 image on it). Not
   time critical.

## Bench state at shutdown

- Pico 2 flashed with main `a1e51db` (chain pass-through, stale-reply fix;
  eject button on by default). The mechanism server, U2 and U4 (shared
  GP21) are on from boot; the floppy is the SmartPort disk (`sp disk ram` for the M6
  RAM disk).
- Power from the IIe through J1 (M7.5); no bench supply.
- Pico powered from the IIe (2026-10-15): board +5 -> small diode (red
  glass, black band to VSYS) -> VSYS, pin 39. The IIe switch powers
  everything. With USB plugged in for the console, unplug it too when the
  IIe goes off, or the Pico keeps running across the IIe power cycle.
- Mechanism on the J2 breakout; test disk 1 = `/M7TEST` (window open =
  write-protected at the end of the session).
- Our DB-19 cable on the Liron card (slot 5); the working UniDisk is off the
  card. A Disk II in slot 6 boots first on a cold start; `PR#5` boots our
  board.
- Scope probes on J1: CH1 J1.10 /WRREQ, CH2 J1.20 ACK, CH3 J1.2 PH0 (REQ),
  ground J1.5.
- Pico console `/dev/cu.usbmodem114401`, Debug Probe `114202`. `make flash`
  does an OpenOCD rescue reset, then programs. If OpenOCD cannot find the
  CMSIS-DAP device, replug the Debug Probe.

## Notes

- Bench supply current readout is unreliable below about 30 mA (showed 0.03 A
  for a 0.1 mA load) and its 0.01 A limit trips early. Use the DMM in series
  for small currents; 0.05 A is the practical limit floor.
- oscope MCP `save_waveform`: the CSV time axis has the delay sign inverted.
  Correct by hand (shift by -2 x delay) until fixed.
- Console tail window: open on the laptop screen as a normal window, not full
  screen (iTerm default profile is full screen).
- bytehamr: "M6: ProDOS boots from my board" and "M7: a real floppy through
  the Liron card" published 2026-10-12, with the project page sections
  (Current state, Architecture, Gateware and firmware, Open problems) brought
  up to date.
- Test disks: `tools/apple/mkimage.sh` (M6 image, flash) and
  `tools/apple/mktest.sh` (M7 sentinel disk). Images in `docs/private/ref/`;
  `M7TEST.po` also in ADTPro's disks folder.
- The `pins` console command proposal is folded into `sp` (prints the host
  input levels).
- Publish only full-resolution screen captures (no `--downscale-to-native`).
- No Hackaday draft (Rico does Hackaday log by log, once proven).
- `project_byte_hamr`: three untracked files from 2026-09-14 stashed there as
  `stash@{0}: dino - wip`.
