# Handoff

Last updated: 2026-10-16. **M6 passed on the bench** and is merged
to main: ProDOS boots from the board's RAM disk through the Liron card,
saves and loads files, ACK 5.3 us after /WRREQ rises (card window about
105 us). **M7 passed on the bench** and is merged: ProDOS boots from a real
800K floppy through the board (sentinel disk written by the UniDisk, checksums
out to track 72), Copy II Plus writes through it, and SPCOMPARE matches the
working UniDisk byte for byte with a disk, write-protected and with no disk.
J2 is on an off-the-shelf breakout, and M7.5 moved the rig onto IIe power
(2026-10-15). **M8 passed on the bench** and is merged (2026-10-15, `a1e51db`):
daisy chain with the working UniDisk in both orders, full disk copied to it.
**M9 passed on the bench** (2026-10-15, `docs/build_log/m9.md`). **PCB** (2026-10-16): R18 (LSTRB pull-down) added, shifter channels re-ordered for routing (see "Make prototype match PCB"), board named Disk Hamr, routed, DRC clean, STL/STEP exported for the case print (see "PCB layout state" and its "Next" list). Bench photo of the whole rig: `docs/photos/m7/2026-10-12_bench-all-wired.jpg`.

## Where things stand

- M0 to M5 done (`docs/build_log/m5.md`). Remote: private GitHub repo `robertrico/a2m2053_controller` (2026-10-16).
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
   replaced with 24 AWG in M8 (4.7 V at the UniDisk feed). Full read scan,
   SPCOMPARE write-protected / no-disk and eject all run on IIe power in M8
   and match M7.
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
   **M8 passed on the bench.** Firmware committed (`a1e51db`). Clean-boot
   check: 0 timeouts, 0 re-reads.
3. Eject button: wired 2026-10-15 (GP22 pin 29 to GND pin 28, internal
   pull-up, on by default). Works.
4. **M9 robustness: passed 2026-10-15** (`docs/build_log/m9.md`). Swaps,
   swap-then-write, write-protect, empty drive, eject and resets
   mid-transfer, 1610 writes with 0 errors. No 3V3 supervisor. Next: PCB.
5. **PCB design: revisit the eject button (J4).** The front-panel button is a
   changeover switch, not a plain one (meter, 2026-10-15): released, the two
   outer pins connect; pressed, the middle pin connects to one outer pin.
   The current J4 (pin 2 to GP22, pins 1 and 3 to GND) works either way.
   The IF-83 does read both contacts: two of the three header pins run to
   separate CXD1013 pins (traced 2026-10-24).
   Options: put the second contact on a spare GPIO if one is free, or
   ignore it (it may just have been the switch Sony had on hand). The J4
   Description in the schematic still says the outer pins are tied; fix it
   then.
6. Re-format disk 2 with our board (`imgcopy` puts the M6 image on it). Not
   time critical.

## PCB layout state

2026-10-16, end of day. Board named **Disk Hamr**, rev 1.0, ID `DH-1-0`.
Silk: Byte Hamr hammer (`a2m2053.pretty/logo_sm`), title, rev and ID block
in Katari 0.1 mm, left/bottom justified. Katari lives in
`~/Library/Fonts/Katari.ttc` (copied from Apple's font asset cache so
KiCad's fontconfig finds it). `main` at `d8d5f31`, pushed.

Routing is the Freerouting 2.5 clean-field result, hand-fixed in pcbnew.
The earlier hand-routed board is in history at `746e44e`.

- How it was made: all tracks wiped, pours hidden from the router, every
  net autorouted in 12 s, session imported, zones refilled, neck-down
  segments widened, ENBL_OUT completed, GND stitch vias and the mech +5
  feed added by hand, dangling vias removed. 870 segments, 84 vias.
- Zones: `5V_F` (+5V, F.Cu, whole board), `12V_F` (+12V band, priority 1),
  `GND_B` (GND, B.Cu, whole board). IC GND pins reach the plane through
  stitch vias; GND also runs as traces on F.Cu.
- Checks (2026-10-16 14:05): DRC with zone refill 0 errors / 0 unconnected;
  schematic parity only "extra footprint G1" (the logo, board-only); ERC
  only the Pico AGND/GND pin-type error. +5V, +12V and GND each one
  connectivity cluster. Warnings left: silk overlaps, footprint/library
  mismatches on A1, R18, G1, two "dangling" vias that are fine (GND stitch
  at 161.85/58.0, +5V via feeding R15 through the pour at 197.05/62.0).
- Fab numbers: 100 x 110 mm, min track 0.25, clearance 0.2, drill 0.3,
  via ring 0.15, 46 parts all on top.
- FG is gone: H1, H2 and the J1 shield are GND in schematic and PCB.
- Known soft spot: a 30 x 4 mm slot in the GND plane between J2 and J3
  where the 1 mm +5/+12 B.Cu runs sit side by side. Plane still one piece.
  Fix if wanted: move one run to F.Cu.
- Exports (git-ignored `hardware/export/`): `disk_hamr_rev1.stl` and
  `.step`, board plus parts, no DB-19 body (no 3D model; real connector
  already fit-tested). Rico has the STL in PrusaSlicer for a case print.
- Freerouting: v2.5.0 needs Java 25 (`/opt/homebrew/opt/openjdk@25`).
  Jar: `gh release download v2.5.0 -R freerouting/freerouting -p
  freerouting-2.5.0.jar`. Headless runs bail early on this board; the GUI
  with the pours stripped from the DSN worked. Locked pre-routes and the
  pour-as-plane heuristic were the problem, not the layout. DSN/SES files
  under `hardware/` are git-ignored scratch.
- Site: log "Disk Hamr: the PCB is routed" published 2026-10-16 on the
  unidisk-controller project. Site logs never name internal docs; drafts
  until Rico has proofread.

### Next

1. Print the STL, try it in the case with the mechanism and the IF-83
   rails. Note anything that collides.
2. Rewire the prototype to the PCB's channel order (table below), boot the
   sentinel, run SPCOMPARE. Only then order.
3. Measurements before ordering: ~~J4/J5 header pitch~~ (TE AMP CT, 2.0 mm,
   2026-10-24; CT footprints placed in the IF-83's order and shroud
   orientation; LED wire-to-pin still to confirm), J2/J3 positions on
   one more print, J1 pins-to-face, J4 pinout on the real front-panel
   cable.
4. Decide on the GND-plane slot and the eject button's second contact.
5. Fab outputs (gerbers, drill, BOM, position) once 1-4 are done.

## Make prototype match PCB

2026-10-16: the PCB schematic moved eight signals to different 8T245
channels so the host bus drops straight from J3 into U1, mech RD sits
next to J2, and the CA0-2/LSTRB fan from U2 to the Pico does not cross
itself (U2 channels 1-4 reversed). Same chips, same DIR and /OE, same pull values, same Pico pins.
Only the channel changes. Rewire the breadboard to match before the board
order, then rerun the M9 checks (SPCOMPARE, chain both orders, eject,
disk swap).

Bench board names: bench J1 = host IDC20 (PCB J3), bench J2 = mechanism
(same on both), bench J3 = rear IDC40 (PCB J1 DB-19).

| Signal | Was | Now | 8T245 pins (B / A) | Pico pin | Pull (B side) |
|---|---|---|---|---|---|
| LASTONE/ | U5 B1 / A1 | **U1 B6 / A6** | 16 / 8 | 2 | 2k to +5V, moves with it |
| /HSTEN | U1 B6 / A6 | **U1 B7 / A7** | 15 / 9 | 10 | 10k to +5V, moves with it |
| WR (host WRDATA) | U1 B7 / A7 | **U1 B8 / A8** | 14 / 10 | 11 | 100k to GND, moves with it |
| Mech RD | U1 B8 / A8 | **U5 B1 / A1** | 21 / 3 | 26 | 10k to +5V, moves with it |
| LSTRB | U2 B4 / A4 | **U2 B1 / A1** | 21 / 3 | 20 | 10k to GND (R18), moves with it |
| CA2 | U2 B3 / A3 | **U2 B2 / A2** | 20 / 4 | 19 | none |
| CA1 | U2 B2 / A2 | **U2 B3 / A3** | 19 / 5 | 17 | none |
| CA0 | U2 B1 / A1 | **U2 B4 / A4** | 18 / 6 | 16 | none |

Cable ends that move:

| Wire | From | To |
|---|---|---|
| Rear LASTONE/ (bench J3.12) | U5 B1 | U1 B6 |
| Host /HSTEN (bench J1.14) | U1 B6 | U1 B7 |
| Host WR (bench J1.18) | U1 B7 | U1 B8 |
| Mech RD (J2.16) | U1 B8 | U5 B1 |
| Mech LSTRB (J2.8) | U2 B4 | U2 B1 |
| Mech CA2 (J2.6) | U2 B3 | U2 B2 |
| Mech CA1 (J2.4) | U2 B2 | U2 B3 |
| Mech CA0 (J2.2) | U2 B1 | U2 B4 |
| Pico pin 2 (GP1) | U5 A1 | U1 A6 |
| Pico pin 10 (GP7) | U1 A6 | U1 A7 |
| Pico pin 11 (GP8) | U1 A7 | U1 A8 |
| Pico pin 26 (GP20) | U1 A8 | U5 A1 |
| Pico pin 20 (GP15, LSTRB) | U2 A4 | U2 A1 |
| Pico pin 19 (GP14, CA2) | U2 A3 | U2 A2 |
| Pico pin 17 (GP13, CA1) | U2 A2 | U2 A3 |
| Pico pin 16 (GP12, CA0) | U2 A1 | U2 A4 |

Unchanged: U1 B1-B5 (PH0-3, /WRREQ), U2 B5-B8 (SEL, /ENBL, /WRTGATE, WRTDATA), U3, U4, U5 DIR and
/OE straps, the LSTRB 10k pull-down (now R18 in the schematic, to GND on
J2.8, fixed on the rig 2026-10-16). U5 B2-B8 stay on GND.

Full U1 order after the change, top to bottom: B1 PH0, B2 PH1, B3 PH2,
B4 PH3, B5 /WRREQ, B6 LASTONE/, B7 /HSTEN, B8 WR. That is J3's pin order.

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
