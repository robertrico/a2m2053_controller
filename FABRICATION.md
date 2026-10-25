# Fabrication: PCB design phase (Phase 2)

Requirements and part choices for the drop-in PCB (spec sec. 0, Phase 2).
Read this before starting the schematic. It collects decisions made during
the perfboard prototype so they aren't rediscovered at layout time.

## Board outline

Parts list with part numbers, vendors and cost: `hardware/bom.md` (CSV for
ordering: `hardware/bom.csv`).

KiCad project: `hardware/kicad/a2m2053.kicad_pro`. The outline on Edge.Cuts
is drawn by `tools/kicad_outline.py` (KiCad IPC API, run against the open
PCB editor). It matches the original IF-83 board (photo of the solder side,
mirrored for the component-side view).

**The outline is a hard constraint.** A 3D-printed copy was fitted against
the IF-83 on 2026-10-13 and both mounting holes line up. The PCB must fit
within this geometry; parts and copper go inside it, never the other way
round.


- Body 100 mm wide, 105 mm from the bottom edge to the top of the 94 mm
  section.
- 94 mm section (calipers) at the top, centered (3 mm step on each side).
  The steps are not level: 12 mm deep on the left, 6 mm on the right
  (component-side view, fitted with a 3D-printed outline). Each step is a
  wide, shallow cove as on the IF-83: a quarter-ellipse 2.5 mm wide and
  1.8 mm tall, then a 0.5 mm radius onto the 100 mm edge. The steps are
  cosmetic: no chassis hardware sits there. The mounting holes are what
  must line up.
- Tab 15.5 mm wide (calipers), 5 mm tall, above the 94 mm top edge. Its
  left edge is 22 mm from the left edge of the 94 mm section, in the
  component-side view.
- The two bottom corners (the edge away from the tab) 1 mm radius. All
  other corners square.
- Mounting holes: M3, 3.2 mm drill (the IF-83's measure about 3.35 mm
  with calipers). Plated, on net FG (frame ground, hatched copper on the
  IF-83), as schematic symbols H1/H2. H2 uses the stock
  MountingHole_3.2mm_M3_Pad_Via (6.4 mm pad). H1 sits too close to the edge
  for that, so it uses a project footprint with a 5.2 mm pad
  (`hardware/kicad/a2m2053.pretty`), which keeps 0.5 mm copper-to-edge.
  Open: how FG ties to GND (direct, RC, or not at all). Probe FG on a
  dead-board IF-83 to see what Sony did.
  Hole 1: right side (component-side view), hole edge 1.5 mm from the
  board edge, center 17 mm below the 94 mm top edge. Both holes checked
  against the IF-83 with a 3D-printed outline.
  Hole 2: right of the tab, center 40 mm from the left edge of the
  94 mm section, hole edge 3 mm below the 94 mm top edge.
- Rear DB-19 (J1, female right angle, daisy-chain port): footprint
  `a2m2053:DSUB-19_Female_Horizontal_P2.77x2.84mm_MountingHoles38.8mm`.
  Pin pitch and pad sizes match KiCad's stock D-sub sockets. Board-lock
  holes are 38.8 mm apart, checked against the part. Both board-lock pads
  are numbered SH and sit on FG. The face points at the 94 mm top edge (rotation 180). Pin 1 at (159.115, 59.92); the
  board-lock holes at (152.18, 58.5) and (190.98, 58.5). The right one is
  10.36 mm from H1 center to center, the left one 9.97 mm from H2. The body's
  right end sits 3 mm from the right edge of the 94 mm section. Checked on
  2026-10-14 with a 3D print holding the real connector: the holes take its
  board locks, the print sits on the IF-83 mounting points, and the D-shell
  lines up with the case opening.
  The distance from the pins to the front face is 12.68 mm, a stock D-sub
  value, not a measurement. It moves only the silkscreen and the edge
  line, not the holes. The silkscreen overhangs the board edge (DRC
  warning, expected: the shell sits past the edge as on the IF-83).
- Ribbon headers, 2x10 shrouded, 2.54 mm
  (`Connector_IDC:IDC-Header_2x10_P2.54mm_Vertical`), long side parallel to
  the top edge, pin 1 at the bottom left, odd pins on the bottom row
  (rotation 90). Positions from Rico's measurements on the IF-83; not
  printed yet (ribbon cables, some play).
  J2 (CN104, mechanism): shroud's upper-left corner 3.5 mm from the board's
  left edge (below the step) and 18.5 mm below the 94 mm top edge. Pin 1 at
  (108.6, 74.22).
  J3 (CN102, host): centered on J1, shroud top 2.5 mm below the back of
  J1's housing (12.83 mm from the top edge, calipers). Pin 1 at
  (160.15, 71.05).
- No other mounting holes.

## Rule: hand-solderable

Rico must be able to assemble the board by hand with an iron (drag solder,
flux, chisel tip). No hot air, no reflow oven, no stencil required.

- Fine-pitch limit: 0.65 mm pitch is the floor, and only where no coarser
  option exists. Prefer 1.27 mm (SOIC).
- No QFN, DFN, BGA or any part with a center pad or hidden leads.
- Passives 0805 or larger.
- Connectors through-hole (DB-19 host, CN104 20-pin mechanism, rear D-sub).
- The bare RP2350A (QFN-60, 0.4 mm pitch, exposed pad) fails this rule. The
  board carries a Pico 2 module instead, soldered flat by its castellated
  edge pads (no headers): as flat as possible. Decided by Rico, 2026-10-13.
  - Footprint: Pico castellated land pattern. No vias or exposed copper
    under the module; the Pico's underside test pads sit on solder mask.
  - Put the micro-USB end at a board edge so the cable still plugs in.
  - Keep the 3-pin SWD header at the far end reachable from above
    (flashing is SWD via OpenOCD, not UF2).

## Level shifters: SN74LVC8T245DWR

Same silicon and pinout as the 8T245 boards on the prototype, so everything
validated on the bench carries over (`docs/level_shifting.md`,
`docs/pin_budget.md`).

- Package: SOIC-24 wide (DW), 1.27 mm pitch, 7.5 mm body. The easiest '245
  package to hand solder.
- VCCA and VCCB each 1.65 V to 5.5 V. Ioff: outputs Hi-Z if either rail is
  unpowered. DIR and /OE referenced to VCCA.
- Digikey 296-23280-1-ND (cut tape): about $1.10 at qty 25, $0.98 at qty 100
  (Oct 2026). Five per board (U1-U5).
- Datasheet: https://www.ti.com/lit/gpn/SN74LVC8T245

### Pinout (all 24-pin variants)

| Pin | Name | Pin | Name |
|---|---|---|---|
| 1 | VCCA | 24 | VCCB |
| 2 | DIR | 23 | VCCB |
| 3 | A1 | 22 | /OE |
| 4 | A2 | 21 | B1 |
| 5 | A3 | 20 | B2 |
| 6 | A4 | 19 | B3 |
| 7 | A5 | 18 | B4 |
| 8 | A6 | 17 | B5 |
| 9 | A7 | 16 | B6 |
| 10 | A8 | 15 | B7 |
| 11 | GND | 14 | B8 |
| 12 | GND | 13 | GND |

DIR high: A -> B. DIR low: B -> A. /OE high: both ports Hi-Z. Pull /OE up to
VCCA so the chip stays tristated through power-up and RP2350 boot.

### Other packages (same part)

| Suffix | Package | Pitch | Use here? |
|---|---|---|---|
| DWR | SOIC-24 wide | 1.27 mm | **Yes.** In stock, easy to solder. |
| PWR | TSSOP-24 | 0.65 mm | Fallback only. Opens Nexperia 74LVC8T245PW and TI SN74LXC8T245PWR as alternates. |
| DBQR | QSOP-24 | 0.635 mm | No. Supply bad (Oct 2026); Rico's stock is reserved for another board. |
| DBR | SSOP-24 (5.3 mm) | 0.65 mm | No. Uncommon. |

### If TI SOIC dries up

- Nexperia 74LVC8T245PW or TI SN74LXC8T245PWR (TSSOP-24, pin-compatible,
  same control scheme). Both are at the 0.65 mm floor.
- Avoid TXB0108 (auto-direction, weak drive, bad on the shared SmartPort
  bus) and the AVC family (3.6 V max, can't do 5 V).

## Board requirements carried from the bench

- **Power paths** (`docs/build_log/m3.md`): low-resistance +5V, +12V and GND
  from the input connector to CN104 (wide traces or pours), all four CN104
  GND pins tied, 4.7 uF 50 V electrolytic at CN104 on both rails as on the
  IF-83. The read amplifier fails silently when +5V sags at the connector.
- **Rear-port pass-through rails** (`docs/build_log/m8.md`): +5V, +12V and
  GND from the host connector to the rear DB-19 as wide traces too. The
  drive chained behind us takes its whole supply through them; thin bench
  wire dropped it to 4.3 V and it could not read.
- **Pico power:** VSYS from the IIe +5V rail through a Schottky (D1,
  STPS2L40AF, SMA), the part Byte Hamr rev2 uses. It keeps USB VBUS from
  back-feeding the host rail when USB is plugged in (Rico, 2026-10-14).
- **Shifter /OE pull-ups** (`docs/level_shifting.md`): 10k to VCCA on U2 and
  U3 /OE (and any shifter that must stay quiet through boot).
- **Pull-ups / pull-downs**: as listed in `docs/level_shifting.md`
  "Pull-ups / idle states". Host WR pull-down is 100k, not 10k. **The
  schematic is missing the LSTRB pull-down:** `/MECH_LSTRB` (U2.B4, J2.8)
  needs 10k to GND, as on the bench since M0. Every other pull on the list
  is in the schematic (checked 2026-10-14).
- **No 3V3 supervisor.** Dropped at M9 (Rico, 2026-10-15). The M0 power-edge
  strobe on LSTRB (`docs/build_log/m0.md`) can at worst make the mechanism
  step or blip at power-on, as the stock UniDisk and DuoDisk do anyway; no
  disk spins and nothing is written then. Many IIe and Pico power cycles in
  M9 showed no stray mechanism action.
- **CN104 pin 20 (/PWM)** tied to +5V, as on the original board.
- **Rear daisy-chain port** (`docs/pin_budget.md`): PH3_OUT, ENBL_OUT and
  LASTONE/ (rear pin 16, 2k pull-up).
- **Cost:** whole board well under $150 (the unsold eBay spare).

## Confirm on the prototype at M9

Guesses in the schematic, picked to be safe if wrong. Check each one on the
perfboard prototype before the PCB order.

- **In-use LED (J5):** polarity confirmed at M9 (RED +, BLK GND): the LED
  blinks with drive activity. Blink timing is a firmware detail. On the
  IF-83 the LED anode is pulled to +5V through 180 ohm (measured
  2026-10-25) and the cathode is pulled low by Q101 (base from the CXD1013
  through 10k), traced 2026-10-24. Same current direction as ours, so
  R17 = 180 for the same brightness: about 17 mA from 5 V, inside the
  8T245's 32 mA output rating.
- **Eject button (J4):** pin 2 to GP22, pins 1 and 3 to GND. The button is a
  changeover switch (meter, 2026-10-15): released, pins 1 and 3 connect;
  pressed, pin 2 connects to one outer pin. With both outer pins on GND a
  press pulls GP22 low whichever outer pin is the common. Still to confirm
  with the real cable on the board. The IF-83 runs two of the three pins to
  separate CXD1013 pins (traced 2026-10-24), so it reads both contacts; we
  read one. Decision on the second contact (GP28) is still open.
- **J4/J5 plug:** identified 2026-10-24 as TE AMP CT, 2.0 mm pitch, round
  0.6 mm posts, 3-wall shroud (TE drawing C-292161 plus calipers). Headers
  292161-3 (J4) and 292161-2 (J5). CT footprints drawn and placed on the
  routed board 2026-10-24 (`a2m2053.pretty`). Placed to match the IF-83
  (photo plus the board in hand, 2026-10-24): with the DB-19 at the top,
  the 2-pin LED header (J5) sits nearer the board's left edge, the 3-pin
  eject header (J4) toward the middle, and both shrouds open toward the
  board edge (back wall facing inboard), so the drive's own plugs seat
  without re-pinning. Still to confirm: which J5 pin the RED (anode) wire
  lands on once the plug is keyed by the shroud.
- **J2/J3 positions:** from calipers, not printed. Print once more if the
  ribbons don't reach.
- **Pico on D1:** confirmed on the prototype (M8, a small diode in place of
  the Schottky): the Pico boots from the IIe alone, and the diode was tested
  live against USB back-feed.
