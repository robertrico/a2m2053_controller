# Handoff

Last updated: 2026-10-25. **M6 passed on the bench** and is merged
to main: ProDOS boots from the board's RAM disk through the Liron card,
saves and loads files, ACK 5.3 us after /WRREQ rises (card window about
105 us). **M7 passed on the bench** and is merged: ProDOS boots from a real
800K floppy through the board (sentinel disk written by the UniDisk, checksums
out to track 72), Copy II Plus writes through it, and SPCOMPARE matches the
working UniDisk byte for byte with a disk, write-protected and with no disk.
J2 is on an off-the-shelf breakout, and M7.5 moved the rig onto IIe power
(2026-10-15). **M8 passed on the bench** and is merged (2026-10-15, `a1e51db`):
daisy chain with the working UniDisk in both orders, full disk copied to it.
**M9 passed on the bench** (2026-10-15, `docs/build_log/m9.md`). **PCB** (2026-10-16): R18 (LSTRB pull-down) added, shifter channels re-ordered for routing (see "Make prototype match PCB"), board named Disk Hamr, routed, DRC clean. **PCB second layout** (2026-10-24/25): J2 moved 11 mm toward the edge after the print, board re-placed by hand and re-routed, +5V as 1 mm traces (no top pour), +12V on B.Cu, J4/J5 in the IF-83's order, R17 = 180 from the IF-83 measurement, J5 pin order swapped to match the IF-83, Gerbers re-plotted 2026-10-25 for PCBWay (see "PCB layout state"). Bench photo of the whole rig: `docs/photos/m7/2026-10-12_bench-all-wired.jpg`.

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

2026-10-25. Board **Disk Hamr** rev 1.0, ID `DH-1-0`. `main` at `783fccf`,
pushed. Gerbers and drills for PCBWay, re-plotted 2026-10-25 after the J5 swap
(`hardware/kicad/fabrication/`, zip `hardware/kicad/disk_hamr_2026_10_25b.zip`;
the earlier `disk_hamr_2026_10_25.zip` has J5 mirrored, do not use),
5 pcs, 2 layer, 1.6 mm, FR-4 TG150, 1 oz, HASL lead-free, green, 6/6 mil,
0.3 mm min hole, product number location specified. Order on hold until
PCBWay answers about sponsorship. Stencil ordered with it (non-framework,
top, 0.12 mm) for hot-plate paste reflow.

- Second layout, 2026-10-24: J2 moved from y 74.2 to 62.9 (the print put
  it 11 mm too far from the edge). All parts re-placed by hand: U2/U5 by
  J2, U1/U3/U4 in a column under J1/J3, Pico along the left edge.
  Freerouting 2.4.1 headless on the exported DSN with the +5V plane block
  cut out (`strip_planes.py` variant in `hardware/kicad/scratch/`),
  session imported, hand fixes. 768 segments, 123 vias (47 GND stitches).
- Power: no top pour. +5V is the Power net class, 1 mm track / 1 mm via,
  routed as traces (436 mm). `GND_B` plane on B.Cu, `12V_F` zone on B.Cu
  (priority 1). Freerouting treats any pour as an already-connected plane
  and routes nothing on that net; the old +5V pour was in 11 islands.
- A GND pocket under J1 (J3 pin 12 + two vias, fenced by /WRREQ, +5V,
  +12V, /HSTEN, LASTONE) was bridged with a top-side GND jumper.
- J4 (3-pin eject) and J5 (2-pin LED) swapped to the IF-83's order, TE AMP
  CT 2.00 mm footprints (`a2m2053.pretty/TE_AMP_CT_1x0N_P2.00mm_Vertical`)
  with the vendor STEP models (`hardware/kicad/3d/`, git-ignored, see
  `docs/SOURCES.md`). Shroud open side toward the board edge.
- R17 = 180 (IF-83 LED anode pull-up measured 2026-10-25). Schematic,
  board, BOM and docs agree.
- G1 logo re-placed from the library with `board_only`, so Update PCB
  from Schematic leaves it alone (it had deleted it twice).
- DRC with zone refill (2026-10-25): 0 errors, 0 unconnected. Warnings
  left: J1 silk over the tab edge x2, A1 library groups, title text
  thickness (TrueType; plots as filled regions, fine).
- DigiKey list "Disk Hamr" (12 lines, about $20) matches `hardware/bom.csv`
  with the Stackpole RMCF1206FT2K00 standing in for the out-of-stock Yageo
  2k and 180 ohm for R17. Quantities bumped (100 nF 15, 10k 20). Order
  waits on the print fit.
- Exports (git-ignored `hardware/export/`): `disk_hamr_rev1_flat.stl`
  for the case print: KiCad STL (`kicad-cli pcb export stl --subst-models`),
  clipped at the board bottom with trimesh (no pins below), board body
  thickened to 1.86 mm so every part's standoff is inside it, Pico USB
  jack filled solid. Venv `hardware/kicad/scratch/mesh-venv/` (trimesh,
  numpy, shapely, networkx). The build script lives only in the 2026-10-25
  chat; rewrite from this description if needed. `disk_hamr_full.stl` and
  `disk_hamr_board_only.stl` beside it. No DB-19 body (J1 has no 3D model).
- Tooling: Konnect live writes (`update_footprints_from_library` apply,
  `place_component`) crashed Pcbnew three times on 2026-10-24. Read-only
  Konnect, `kicad-cli` on the saved file and GUI edits only. Freerouting
  jar `~/Development/tools/freerouting/freerouting-2.4.1.jar`, Java 25:
  `java -Djava.awt.headless=true -jar ... -de X.dsn -do X.ses -mp 50 -mt 8`.
- Site: "Disk Hamr: laid out again, by hand this time" published
  2026-10-25 (source commit `dbd26d5`). Site logs never name internal docs;
  drafts until Rico has proofread.

### Next

1. Print `disk_hamr_rev1_flat.stl`, fit it in the case with the mechanism
   and the real DB-19. Note collisions. J2 position is the thing to check.
2. If it fits: confirm the PCBWay order, place the DigiKey list.
3. Rewire the prototype to the PCB's channel order (table below), boot the
   sentinel, run SPCOMPARE, before boards arrive.
4. ~~J5 pin order~~ measured 2026-10-25 and fixed: on the IF-83 the CN107
   pin nearer the 3-pin header is the Q101 sink, the far pin the 180 ohm
   pull-up. Our J5 swapped to pin 1 GND (nearer J4), pin 2 /LED. Gerbers
   re-plotted (`hardware/kicad/fabrication/`, zip `disk_hamr_2026_10_25b.zip`),
   re-upload to PCBWay. Still open: J4 pinout on the real front-panel
   cable. STEP-vs-footprint open-side chirality on the CT headers (check
   on the print).
5. Rev 1.1 candidates: U5 carries one signal (mech RD), a SN74LVC1G17 at
   3.3 V would do; `docs/pin_budget.md` shifter table still says U1 carries
   mech RD and U5 LASTONE/, the BOM and board are right.
6. Assembly plan: hot plate + stencil + paste for the SMD, then the
   through-hole headers and electrolytics by iron.

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
