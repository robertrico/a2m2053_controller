# Handoff

Last updated: 2026-10-12 (night). **M6 passed on the bench** and is merged
to main: ProDOS boots from the board's RAM disk through the Liron card,
saves and loads files, ACK 5.3 us after /WRREQ rises (card window about
105 us). **M7 passed on the bench** and is merged: ProDOS boots from a real
800K floppy through the board (sentinel disk written by the UniDisk, checksums
out to track 72), Copy II Plus writes through it, and SPCOMPARE matches the
working UniDisk byte for byte with a disk, write-protected and with no disk.
Next: J2 breakout rework, then M8 (daisy chain).

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

## Waiting on

- J2 breakout (mechanism side) arrives 2026-10-13.

## Resume order

1. **J2 breakout** (arrives 2026-10-13): mechanism side on the breakout
   (J2 = CN104, straight IDC ribbon). Ring out the ribbon to the terminals,
   then isolation checks: +12 V (13/15/17/19) vs RD (16), WRTDATA (18) and
   every signal. Short heavy +5 V run to pin 11 and GND to 1/3/5/7, bulk caps
   (4.7 uF, as on the IF-83) at the terminals. Re-run a disk read scan on
   drive A with the bench at 5.0 V (`server off` first). Check the U2 channel
   order against GP12-19 (not recorded in the M0 notes). M7 ran on the old tap.
2. **M8 brainstorm and spec: daisy chain.** Our board in front of the working
   UniDisk, then behind it. PH3_OUT / ENBL_OUT (U4) and LASTONE/ (U5): check
   what is actually wired (`pin_budget.md`), and the /ENBL pass-through rule
   (parent spec 3.5, unverified).
3. Eject button: a plain switch straight to GP22 (no shifter, internal pull-up),
   settled 2026-10-12. Firmware on main has it on by default (not flashed yet:
   the Pico runs `f54d07d`, where it is off until `ejbtn on`). Wire the button.
4. Re-format disk 2 with our board (`imgcopy` puts the M6 image on it). Not
   time critical.

## Bench state at shutdown

- Pico 2 flashed with `f54d07d` (M7). The mechanism server and U2/U4 are on
  from boot; the floppy is the SmartPort disk (`sp disk ram` for the M6 RAM
  disk).
- Supply limits for mechanism work: about 0.5 A on +5, 1.0 A on +12. Lower
  limits brown the board out at motor start (M7).
- Power order: Pico, bench +5, +12, IIe. Shutdown: IIe, +12, +5, Pico USB.
- Mechanism on the old CN104 tap; test disk 1 = `/M7TEST` (window open =
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
- bytehamr: "M6: ProDOS boots from my board" and the M7 log written
  2026-10-12 (local commits in byte-hamr-labs, not published).
- Test disks: `tools/apple/mkimage.sh` (M6 image, flash) and
  `tools/apple/mktest.sh` (M7 sentinel disk). Images in `docs/private/ref/`;
  `M7TEST.po` also in ADTPro's disks folder.
- The `pins` console command proposal is folded into `sp` (prints the host
  input levels).
- Publish only full-resolution screen captures (no `--downscale-to-native`).
- No Hackaday draft (Rico does Hackaday log by log, once proven).
- `project_byte_hamr`: three untracked files from 2026-09-14 stashed there as
  `stash@{0}: dino - wip`.
