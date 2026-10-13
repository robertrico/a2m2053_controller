# Handoff

Last updated: 2026-10-12 (night). **M6 passed on the bench** and is merged
to main: ProDOS boots from the board's RAM disk through the Liron card,
saves and loads files, ACK 5.3 us after /WRREQ rises (card window about
105 us). Next: M7 (floppy behind the SmartPort engine). J2 breakout arrives
2026-10-13.

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

1. **M7 brainstorm, then spec**: SmartPort engine (M6) + mechanism server
   (M5) on the two cores; READBLOCK/WRITEBLOCK/FORMAT go to core 1 while ACK
   holds the card off (no host timeout on the reply). Measure the worst-case
   hold-off. Oracle comparison program against the working UniDisk.
2. When J2 lands: mechanism side on the breakout (J2 = CN104, straight IDC
   ribbon). Ring out the ribbon to the terminals, then isolation checks:
   +12 V (13/15/17/19) vs RD (16), WRTDATA (18) and every signal. Short heavy
   +5 V run to pin 11 and GND to 1/3/5/7, bulk caps (4.7 uF, as on the IF-83)
   at the terminals. Re-run a disk read scan on drive A with the bench at
   5.0 V to confirm the IR drop is gone. Check the U2 channel order against
   GP12-19 (not recorded in the M0 notes).
3. Re-format disk 2 with our board. Not time critical.

## Bench state at shutdown

- Pico 2 flashed with `2e7311a` (M6). Power order: Pico, board (bench +5 at
  5.00 V), IIe; at shutdown everything off, Pico included (a Pico left
  running across an IIe power cycle twice left the IIe not starting).
- Bench +12 off. Mechanism ribbon unplugged.
- Our DB-19 cable is on the Liron card (slot 5). A Disk II in slot 6 boots
  first on a cold start; `PR#5` boots our board.
- Scope probes on J1: CH1 J1.10 /WRREQ, CH2 J1.20 ACK, CH3 J1.2 PH0 (REQ),
  ground J1.5.
- Pico console `/dev/cu.usbmodem114401`, Debug Probe `114202`. `make flash`
  does an OpenOCD rescue reset, then programs (the 864 KB image needs it).

## Notes

- Bench supply current readout is unreliable below about 30 mA (showed 0.03 A
  for a 0.1 mA load) and its 0.01 A limit trips early. Use the DMM in series
  for small currents; 0.05 A is the practical limit floor.
- oscope MCP `save_waveform`: the CSV time axis has the delay sign inverted.
  Correct by hand (shift by -2 x delay) until fixed.
- Console tail window: open on the laptop screen as a normal window, not full
  screen (iTerm default profile is full screen).
- bytehamr: published 2026-10-12 "M6: the Liron's first packet reaches my
  board". The project page Hardware table still lists the 74LVC1G07 (the
  repo docs are fixed; the page is not).
- The `pins` console command proposal is folded into `sp` (prints the host
  input levels).
- Publish only full-resolution screen captures (no `--downscale-to-native`).
- No Hackaday draft (Rico does Hackaday log by log, once proven).
- `project_byte_hamr`: three untracked files from 2026-09-14 stashed there as
  `stash@{0}: dino - wip`.
