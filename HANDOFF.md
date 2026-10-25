# Handoff

Last updated: 2026-10-25, end of day.

## Where it is

The perfboard prototype is finished: M0 to M9 passed on the bench in
October 2026. ProDOS boots from a real 800K floppy through the Liron
card, the daisy chain works in both orders with a stock UniDisk, and the
M9 robustness set (swaps, write-protect, empty drive, eject and reset
mid-transfer, 1610 block writes) matches the original drive. Per-milestone
notes with measurements: `docs/build_log/m0.md` to `m9.md`.

The PCB, **Disk Hamr rev 1.0** (ID `DH-1-0`), is in its second layout,
DRC clean, Gerbers re-plotted after the J5 fix and uploaded to PCBWay.
Parts are ordered from DigiKey. The owner's manual is drafted. **Paused
until the parts and the boards land.** Both repos clean and pushed; the
site log for the fit and the orders went out 2026-10-25.

## Next

Nothing on the bench until the deliveries.

1. **PCBWay**: PCB W936711ASX11 (5 pcs, $36.61, auto-passed) and stencil
   S-X12W936711A ($10, under audit) are in the cart, unpaid, waiting on
   PCBWay's sponsorship answer. Pay both together so they ship as one.
   The order goes either way.
2. **Optional before PCBWay is paid**: put "mechanism", "from host",
   "LED", "eject" on F.SilkS next to J2/J3/J5/J4. The Value fields are on
   Fab and do not print; the silk only says J2..J5. GUI edit, re-plot,
   re-zip, re-upload. Cheap now, gone after.
3. **DigiKey**: web ID 378380678, placed 2026-10-25 14:23. The "Disk Hamr"
   list for 5 assemblies with 10% attrition (Pico 0%), Stackpole
   RMCF1206FT2K00 standing in for the Yageo 2k, 180 ohm for R17. $89.64
   parts, $109.20 landed, USPS Ground Advantage. Arrives before the boards.
4. **Open measurements**, next time a drive is open: J4 pinout on the
   real front-panel cable (which two contacts the IF-83 reads; ours reads
   the pressed one). CT header shroud orientation against the real leads
   (the print took them, so likely right). Lid screw count and direction,
   mechanism header pin 1 mark, LED behaviour at power-on. All feed the
   manual.
5. **Build**, when the boards land: paste, stencil and hot plate for every
   SMD except the Pico; Pico castellations by iron afterwards; through-hole
   by iron; DB-19 tacked on one pin and squared to the case opening first.
   Then the M9 checks on the real board, LED brightness against the stock
   drive, and whether the LED lights as J5 is wired.
6. **Manual**: `docs/manual/disk_hamr_manual.md` is the source,
   `Disk_Hamr_Owners_Guide_DRAFT.pdf` the styled draft (hammer logo from
   `docs/manual/assets/hamr_logo.svg`). Waiting on the photo session (shot
   list in the file's design notes), the `[CONFIRM]` facts, the repository
   URL, and the ribbon's part number and length (BOM line CBL1). No site
   log for the manual by itself; it joins the log for the first assembled
   and tested board. Manual edits fold into the one manual commit
   (`1846492`) and force-push with lease until it ships, with later
   commits re-applied on top.
7. **Dropped**: rewiring the prototype to the PCB's channel order. Same
   chips, same Pico pins, only the shifter channel moves, so it proves
   nothing the PCB won't and risks the prototype. The first assembled
   board is the test. The channel map is kept below for reference.
8. **Rev 1.1 notes**: U5 carries one signal (mech RD); a SN74LVC1G17 at
   3.3 V would do. `docs/pin_budget.md` shifter table still says U1 carries
   mech RD and U5 LASTONE/; the BOM and board are right. A UniDisk 3.5
   teardown page (iFixit style) for findability. The prototype's +5
   lesson is in copper (1 mm traces); watch the UniDisk-behind-us voltage
   on the real board anyway.

## PCB layout state

2026-10-25. Gerbers and drills in `hardware/kicad/fabrication/`, zip
`hardware/kicad/disk_hamr_2026_10_25b.zip` (the earlier
`disk_hamr_2026_10_25.zip` has J5 mirrored; do not use). 5 pcs, 2 layer,
100 x 110 mm, 1.6 mm, FR-4 TG150, 1 oz, HASL lead-free, green, 6/6 mil,
0.3 mm min hole, product number location specified. Stencil non-framework,
top, 0.12 mm.

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
  +12V, /HSTEN, LASTONE) is bridged with a top-side GND jumper.
- J4 (3-pin eject) and J5 (2-pin LED) in the IF-83's order, TE AMP CT
  2.00 mm footprints (`a2m2053.pretty/TE_AMP_CT_1x0N_P2.00mm_Vertical`)
  with the vendor STEP models (`hardware/kicad/3d/`, git-ignored, see
  `docs/SOURCES.md`). Shroud open side toward the board edge.
- J5 pin order measured on the IF-83 2026-10-25: the CN107 pin nearer the
  3-pin header is the Q101 sink, the far pin the 180 ohm pull-up. Ours
  was mirrored; now pin 1 (nearer J4) GND, pin 2 /LED through R17.
- R17 = 180 (that pull-up). Schematic, board, BOM and docs agree.
- G1 logo placed from the library with `board_only`, so Update PCB from
  Schematic leaves it alone (it had deleted it twice).
- DRC with zone refill: 0 errors, 0 unconnected. Warnings left: J1 silk
  over the tab edge x2, A1 library groups, title text thickness (TrueType;
  plots as filled regions, fine).
- Per-unit cost (`hardware/bom.md`): $41.75 parts and board, about $57
  landed over five; the DB-19 at $14.50 is the biggest line. Three sold
  at $70 would leave the two kept at about $37 each.
- Exports (git-ignored `hardware/export/`): `disk_hamr_rev1_flat.stl` for
  the case print: KiCad STL (`kicad-cli pcb export stl --subst-models`),
  clipped at the board bottom with trimesh (no pins below), board body
  thickened to 1.86 mm so every part's standoff is inside it, Pico USB
  jack filled solid. Venv `hardware/kicad/scratch/mesh-venv/` (trimesh,
  numpy, shapely, networkx). The build script lives only in the
  2026-10-25 chat; rewrite from this description if needed.
  `disk_hamr_full.stl` and `disk_hamr_board_only.stl` beside it. No DB-19
  body (J1 has no 3D model). Five prints so far; the fifth fits with the
  mechanism and the real DB-19.
- Tooling: Konnect live writes (`update_footprints_from_library` apply,
  `place_component`) crashed Pcbnew three times on 2026-10-24. Read-only
  Konnect, `kicad-cli` on the saved file and GUI edits only. Freerouting
  jar `~/Development/tools/freerouting/freerouting-2.4.1.jar`, Java 25:
  `java -Djava.awt.headless=true -jar ... -de X.dsn -do X.ses -mp 50 -mt 8`.
  Katari for the silk lives in `~/Library/Fonts/Katari.ttc`.
- Site: "Disk Hamr: laid out again, by hand this time" and "Disk Hamr:
  printed, fitted, ordered", both 2026-10-25. Site logs never name
  internal docs; drafts until Rico has proofread; after every log, refresh
  the home "Right now I'm on" paragraph and the project's "Current state".

## Prototype vs PCB channel map

The PCB schematic moved eight signals to different 8T245 channels so the
host bus drops straight from J3 into U1, mech RD sits next to J2, and the
CA0-2/LSTRB fan from U2 to the Pico does not cross itself (U2 channels 1-4
reversed). Same chips, same DIR and /OE, same pull values, same Pico pins.
The breadboard still has the old order; rewiring it was dropped (Next, 7).

Bench board names: bench J1 = host IDC20 (PCB J3), bench J2 = mechanism
(same on both), bench J3 = rear IDC40 (PCB J1 DB-19).

| Signal | Prototype | PCB | 8T245 pins (B / A) | Pico pin | Pull (B side) |
|---|---|---|---|---|---|
| LASTONE/ | U5 B1 / A1 | **U1 B6 / A6** | 16 / 8 | 2 | 2k to +5V |
| /HSTEN | U1 B6 / A6 | **U1 B7 / A7** | 15 / 9 | 10 | 10k to +5V |
| WR (host WRDATA) | U1 B7 / A7 | **U1 B8 / A8** | 14 / 10 | 11 | 100k to GND |
| Mech RD | U1 B8 / A8 | **U5 B1 / A1** | 21 / 3 | 26 | 10k to +5V |
| LSTRB | U2 B4 / A4 | **U2 B1 / A1** | 21 / 3 | 20 | 10k to GND (R18) |
| CA2 | U2 B3 / A3 | **U2 B2 / A2** | 20 / 4 | 19 | none |
| CA1 | U2 B2 / A2 | **U2 B3 / A3** | 19 / 5 | 17 | none |
| CA0 | U2 B1 / A1 | **U2 B4 / A4** | 18 / 6 | 16 | none |

Unchanged: U1 B1-B5 (PH0-3, /WRREQ), U2 B5-B8 (SEL, /ENBL, /WRTGATE,
WRTDATA), U3, U4, the U5 DIR and /OE straps, U5 B2-B8 on GND. PCB U1
order top to bottom: B1 PH0, B2 PH1, B3 PH2, B4 PH3, B5 /WRREQ, B6
LASTONE/, B7 /HSTEN, B8 WR, which is J3's pin order.

## Bench state

Idle since 2026-10-16. As left:

- Pico 2 flashed with main `a1e51db` (chain pass-through, stale-reply
  fix; eject button on by default). The mechanism server, U2 and U4
  (shared GP21) are on from boot; the floppy is the SmartPort disk
  (`sp disk ram` for the M6 RAM disk).
- Power from the IIe through J1; no bench supply. Pico powered from the
  IIe: board +5 -> small diode (red glass, black band to VSYS) -> VSYS,
  pin 39. The IIe switch powers everything. With USB plugged in for the
  console, unplug it too when the IIe goes off, or the Pico keeps running
  across the IIe power cycle.
- Mechanism on the J2 breakout; test disk 1 = `/M7TEST` (write-protected
  at the end of the last session).
- Our DB-19 cable on the Liron card (slot 5); the working UniDisk is off
  the card. A Disk II in slot 6 boots first on a cold start; `PR#5` boots
  our board.
- Scope probes on J1: CH1 J1.10 /WRREQ, CH2 J1.20 ACK, CH3 J1.2 PH0
  (REQ), ground J1.5.
- Pico console `/dev/cu.usbmodem114401`, Debug Probe `114202`. `make
  flash` does an OpenOCD rescue reset, then programs. If OpenOCD cannot
  find the CMSIS-DAP device, replug the Debug Probe.
- Spare IWMs: one left, on the other dead IF-83. The Liron card runs on a
  donor IWM since 2026-10-11 (the homemade DB-19 adapter put -12 V on
  /WRREQ and burned the original).
- The working UniDisk is hands-off, cable use only.

## History

| Milestone | What | Notes |
|---|---|---|
| M0-M5 | Bench rig, mechanism status and commands, flux capture, whole-disk read, write and format | `docs/build_log/m0.md` to `m5.md` |
| M6 | SmartPort on the RP2350; ProDOS boots from a RAM disk through the Liron card. WR pull-down became 100k (idle 3.96 V). | `docs/build_log/m6.md`, spec `docs/superpowers/specs/2026-10-12-m6-smartport-ramdisk-design.md` |
| M7 | ProDOS boots from a real floppy; SPCOMPARE matches the UniDisk byte for byte | `docs/build_log/m7.md` |
| M7.5 | Bench supply retired; board and mechanism on the IIe rails | `docs/build_log/m7_5.md` |
| M8 | Daisy chain with the working UniDisk, both orders; full disk copied across | `docs/build_log/m8.md` |
| M9 | Swaps, write-protect, empty drive, eject and resets mid-transfer, 1610 writes clean | `docs/build_log/m9.md` |
| PCB v1 | First layout, routed 2026-10-16; hand-routed board in history at `746e44e` | site log "Disk Hamr: the PCB is routed" |
| PCB v2 | Second layout, 2026-10-24/25, this handoff | site logs of 2026-10-25 |

Firmware on main: `a1e51db` and later; `blinky/` (RP2350 bring-up) is
stashed as `stash@{0}: blinky: RP2350 bring-up test`. The image
`docs/private/ref/m6_boot800k.po` is git-ignored; CMake links it when
present. Remote: private GitHub repo `robertrico/a2m2053_controller`.

## Notes

- Bench supply current readout is unreliable below about 30 mA (showed
  0.03 A for a 0.1 mA load) and its 0.01 A limit trips early. Use the DMM
  in series for small currents.
- oscope MCP `save_waveform`: the CSV time axis has the delay sign
  inverted. Correct by hand (shift by -2 x delay) until fixed.
- Console tail window: open on the laptop screen as a normal window; the
  iTerm default profile is full screen.
- Test disks: `tools/apple/mkimage.sh` (M6 image, flash) and
  `tools/apple/mktest.sh` (M7 sentinel disk). Images in
  `docs/private/ref/`; `M7TEST.po` also in ADTPro's disks folder.
- The `pins` console command proposal is folded into `sp` (prints the
  host input levels).
- Publish only full-resolution screen captures (no `--downscale-to-native`).
- No Hackaday draft (Rico does Hackaday log by log, once proven).
- Site prose rules: no "X, not Y" contrasts, no em dashes, no internal doc
  names in logs; the voice reference is the site's own intro.
- `project_byte_hamr`: three untracked files from 2026-09-14 stashed there
  as `stash@{0}: dino - wip`.
