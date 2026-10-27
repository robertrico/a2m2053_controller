# Bill of materials: Disk Hamr rev 1.0 (A2M2053 controller)

Source of truth: `hardware/kicad/a2m2053.kicad_sch` (45 parts, all on the
top side; the 46th footprint on the PCB is the logo). This file adds part
numbers, vendors, quantities and the reason for each part. Regenerate the
reference list from the schematic if the two disagree; the schematic wins.

Machine-readable copy for ordering: `hardware/bom.csv`.

**Price provenance.** None of the prices in this file or in `bom.csv`
were checked against a distributor when the BOM was written (2026-10-18;
the distributor sites were unreachable from the writing environment).

- SN74LVC8T245DWR: copied from `FABRICATION.md` (Digikey 296-23280-1-ND,
  about $1.10 at qty 25, $0.98 at qty 100, noted there as Oct 2026).
- Pico 2: $6.12 at DigiKey (2648-SC1631CT-ND, quote 19455158, 2026-10-24).
  Earlier estimate was $12 (2026-10-18).
- Everything else: from memory of typical distributor pricing, `[guess]`.
  The vendor part numbers for the passives and headers in `bom.csv` are
  also from memory. Put them in a cart and check before ordering.

Every price is "per board", not per reel.

## Summary

Real numbers from the orders of 2026-10-25, five boards. DigiKey quantities
carry 10% attrition and the price breaks DigiKey applied, so the parts
column is what was paid divided by five.

| Group | Parts | Per board |
|---|---|---|
| Pico 2 | A1 | $6.12 |
| Level shifters | U1-U5 | $6.18 (28 at $1.10) |
| Schottky | D1 | $0.77 |
| 100 nF 0805 | C1-C10 | $0.34 |
| 4.7 uF 50 V radial | C11-C13 | $0.49 |
| Resistors 1206 | R1-R18 | $0.74 |
| Box headers | J2, J3 | $1.95 |
| TE CT headers | J4, J5 | $1.32 |
| DigiKey parts, subtotal | | **$17.93** |
| DB-19 female right angle | J1 | **$14.50** (eBay) |
| Bare PCB | | **$7.32** (PCBWay, 5 at $36.61) |
| Stencil, one for the run | | $2.00 ($10 / 5) |
| 20-way IDC ribbon, ships with the board | CBL1 | $2.50 [guess] |
| Mounting | H1, H2 | $0.00, the drive's own M3 screws |
| **Parts and board, per unit** | | **about $44** |

Landed cost, with shipping, tariff, tax and fees spread over five:

| | Order | Per board |
|---|---|---|
| DigiKey, web ID 378380678, 2026-10-25 | $109.20 | $21.84 |
| PCBWay, PCB + stencil, DHL quote | about $103 | about $20.70 |
| DB-19 | | $14.50 |
| **Landed, per unit** | | **about $60** |

Well under the $150 target (`CLAUDE.md`, "Cost matters").

## Parts

### A1: Raspberry Pi Pico 2 (RP2350A module)

| Ref | Value | Footprint | MPN | Qty |
|---|---|---|---|---|
| A1 | Pico 2 | `Module:RaspberryPi_Pico_SMD_HandSolder` | Raspberry Pi SC1631 (Pico 2, no headers) | 1 |

- Soldered flat by its castellated edge pads, no headers (`FABRICATION.md`,
  2026-10-13). Order the plain Pico 2, **not** the "H" variant with
  pre-soldered headers and **not** Pico 2 W (no need for radio, and the
  antenna keep-out costs board area).
- Flashing is SWD through the 3-pin JST-SH debug connector on the module.
  A Raspberry Pi Debug Probe (SC0889) is bench equipment, not a board part.
- The bare RP2350A (QFN-60, 0.4 mm) fails the hand-solder rule.
- Price: $6.12 at DigiKey, 2648-SC1631CT-ND (quote 19455158, 2026-10-24).
  The earlier $12 figure was the eBay/Amazon price.

### U1-U5: level shifters, SN74LVC8T245DWR

| Ref | Role | DIR | /OE |
|---|---|---|---|
| U1 | Host inputs: PH0-3, /WRREQ, LASTONE/, /HSTEN, host WR | B->A | tied low |
| U2 | Mechanism outputs: LSTRB, CA2, CA1, CA0, SEL, /ENBL, /WRTGATE, WRTDATA | A->B | GP21 + R13 |
| U3 | Host outputs: RD, ACK (ENBUS) | A->B | GP11 + R12 |
| U4 | Rear port outputs + LED: PH3_OUT, ENBL_OUT, LED | A->B | GP21 + R13 (shared with U2) |
| U5 | Mechanism RD | B->A | tied low |

- Package SOIC-24 wide (DW), 7.5 mm body, 1.27 mm pitch. Footprint
  `Package_SO:SOIC-24W_7.5x15.4mm_P1.27mm`.
- Digikey 296-23280-1-ND, about $1.10 each at qty 25, $0.98 at qty 100
  (`FABRICATION.md`, Oct 2026).
- Alternates if the DW package dries up (both TSSOP-24, 0.65 mm, the
  hand-solder floor): Nexperia 74LVC8T245PW, TI SN74LXC8T245PWR. Different
  footprint, so they need a board respin, not a drop-in.
- Never substitute TXB0108 (auto-direction) or an AVC-family part (3.6 V
  max).

### D1: Schottky, Pico VSYS feed

| Ref | Value | Footprint | MPN | Qty |
|---|---|---|---|---|
| D1 | STPS2L40AF | `Diode_SMD:D_SMA` | STMicro STPS2L40AF (2 A, 40 V, SMA) | 1 |

- IIe +5V -> D1 -> Pico VSYS. Blocks USB VBUS from back-feeding the host
  rail when the console cable is plugged in. Same part as Byte Hamr rev2.
- Any SMA Schottky with Vf under about 0.4 V at 200 mA and 2 A rating
  will do (SS24, B240A). Keep Vf low: the Pico's 3V3 regulator wants VSYS
  above about 3.0 V after the drop, and the IIe rail reads 4.5 to 4.7 V on
  the bench.
- About $0.40 to $0.60. [guess]

### C1-C10: decoupling, 100 nF 0805

| Ref | Rail | Where |
|---|---|---|
| C1, C3, C5, C7, C9 | +3V3 | U1-U5 VCCA (one each) |
| C2, C4, C6, C8, C10 | +5V | U1-U5 VCCB (one each) |

- 100 nF, 50 V, X7R, 0805. Footprint `Capacitor_SMD:C_0805_2012Metric`.
- Any brand. Example: Samsung CL21B104KBCNNNC, Kemet C0805C104K5RAC.
- Order 15 to allow for losses. About $0.03 each. [guess]

### C11-C13: bulk, 4.7 uF 50 V electrolytic, radial

| Ref | Rail | Where |
|---|---|---|
| C11 | +12V | at J2 (CN104), as on the IF-83 |
| C12 | +5V | at J2 (CN104), as on the IF-83 |
| C13 | +3V3 | 3V3 bulk at the Pico |

- 4.7 uF, 50 V, radial, 5 mm diameter, 2.0 mm lead pitch. Footprint
  `Capacitor_THT:CP_Radial_D5.0mm_P2.00mm`. Polarized: watch the stripe.
- Same value and voltage the IF-83 uses at CN104 (read off the board,
  `docs/research_notes.md` sec. on +5V sag, `docs/build_log/m3.md`). The
  read amp fails silently when +5V sags at the connector; these stay.
- C13 could be 16 V, but one part number for all three is simpler.
- Example: Panasonic ECA-1HM4R7, Nichicon UVR1H4R7MDD, Rubycon
  50YXF4R7M5X11. About $0.15 each. [guess]

### R1-R18: resistors, 1206

All 1206, 1% or 5%, 1/4 W. Footprint `Resistor_SMD:R_1206_3216Metric`.
Any brand (Yageo RC1206, Panasonic ERJ-8). About $0.02 each. [guess]

| Ref | Value | Net | Purpose |
|---|---|---|---|
| R1 | 10k | /MECH_RD to +5V | Mech RD pull-up (IWM has a 10k on RDDATA; drive may tri-state RD) |
| R2 | 10k | /MECH_ENBL to +5V | Mech /ENBL inactive while U2 is Hi-Z |
| R3 | 10k | /MECH_WRTGATE to +5V | Mech /WRTGATE inactive while U2 is Hi-Z |
| R4 | 10k | /HOST_PH0 to GND | Host PH0 reads 0 with no host |
| R5 | 10k | /HOST_PH1 to GND | Host PH1 reads 0 with no host |
| R6 | 10k | /HOST_PH2 to GND | Host PH2 reads 0 with no host |
| R7 | 10k | /HOST_PH3 to GND | Host PH3 reads 0 with no host |
| R8 | 10k | /HOST_WRREQ to +5V | Host /WRREQ inactive with no host |
| R9 | 10k | /HOST_HSTEN to +5V | Host /HSTEN inactive with no host |
| R10 | **100k** | /HOST_WR to GND | Host WR idle. 10k dragged the Liron's idle to 3.40 V, under VIH; 100k idles at 3.96 V **[measured]** |
| R11 | 10k | /HOST_ACK to +5V | ACK reads "ready" to the Liron **[measured]** |
| R12 | 10k | /U3_OE to +3V3 | U3 Hi-Z through reset and SWD flash |
| R13 | 10k | /U2_OE to +3V3 | U2 and U4 Hi-Z through boot |
| R14 | 10k | /PH3_OUT to GND | Rear PH3 quiet while U4 is Hi-Z (chain blocked until INIT) |
| R15 | 10k | /ENBL_OUT to +5V | Rear /ENBL inactive while U4 is Hi-Z |
| R16 | **2k** | /LASTONE to +5V | Rear pin 16 pull-up, parity with the IF-83 |
| R17 | **180** | /LED_B to /LED | In-use LED series resistor, same value as the IF-83 LED anode pull-up (measured 2026-10-25). About 17 mA from 5 V, inside the 8T245 rating |
| R18 | 10k | /MECH_LSTRB to GND | Mech LSTRB idle low, no stray strobe while U2 is Hi-Z (bench since M0) |

Order: 15 x 10k, 1 x 100k, 1 x 2k, 1 x 180, plus a few spares of each.

### J1-J5: connectors

| Ref | What | Footprint | Qty | Notes |
|---|---|---|---|---|
| J1 | DB-19 female, right angle, PCB mount, with board locks | `a2m2053:DSUB-19_Female_Horizontal_P2.77x2.84mm_MountingHoles38.8mm` | 1 | Rear daisy-chain port. Board locks 38.8 mm apart, both on GND. Fit-tested on a 3D print 2026-10-14 against the real connector |
| J2 | 2x10 shrouded box header, 2.54 mm, vertical | `Connector_IDC:IDC-Header_2x10_P2.54mm_Vertical` | 1 | CN104, mechanism ribbon. Pin 20 (/PWM) tied to +5V on the board |
| J3 | 2x10 shrouded box header, 2.54 mm, vertical | `Connector_IDC:IDC-Header_2x10_P2.54mm_Vertical` | 1 | CN102, host SmartPort ribbon ("FROM HOST"). ACK on pin 20, pin 19 no wire |
| J4 | TE AMP CT header, 1x3, 2.0 mm, vertical, 3-wall shroud | `a2m2053:TE_AMP_CT_1x03_P2.00mm_Vertical` (to draw; no stock KiCad footprint) | 1 | Front-panel eject button, CN108 on the IF-83: pin 2 GP22, pins 1 and 3 GND. TE 292161-3 (natural) or 6-292161-3 (black, as original). Identified 2026-10-24 from the TE drawing and calipers [measured] |
| J5 | TE AMP CT header, 1x2, 2.0 mm, vertical, 3-wall shroud | `a2m2053:TE_AMP_CT_1x02_P2.00mm_Vertical` (to draw) | 1 | Front-panel in-use LED, CN107 on the IF-83. Pin 1 (nearer J4) GND, pin 2 (toward the board edge) /LED through R17; matches the IF-83, where the far pin carries the 180 ohm pull-up and the near pin the Q101 sink (measured 2026-10-25). TE 292161-2 (natural) or 6-292161-2 (black). Same ID as J4 |

- **J1:** DB-19 female right angle, $15 each on eBay: the rarest and most
  expensive part on the board. Purchased and fit-tested in the printed
  outline and the case opening (M7.2, 2026-10-14). Footprint pitch
  2.77 x 2.84 mm, board locks 38.8 mm apart, matching the part in hand.
- J2 and J3: any shrouded 2x10 0.1" box header with the key notch
  (Wurth 61202021621, Amphenol 10056845-120LF, Sullins SBH11-PBPC-D10-ST-BK,
  or generic). About $0.30 to $0.60 each. [guess]
- J4 and J5: TE AMP CT series (AMP Japan's 2.0 mm wire-to-board, the
  original CN107/CN108 on the IF-83). Round 0.6 mm posts, 3.6 mm wide
  housing, 6.8 mm tall, 3-wall shroud with end-wall lock windows. DigiKey
  stocks the natural 292161-2 / 292161-3 at about $0.70 each; black is the
  6- prefix (6-292161-2, 6-292161-3), check stock. Mating receptacle is TE
  173977, already on the drive's own LED and eject leads. Drawing:
  `docs/private/datasheets/TE_AMP_CT_292161_header_drawing.pdf`.
  Footprint from the drawing: 2.0 mm pitch, 0.8 mm hole (0.85 drilled),
  body 5.8 (2 pos) / 7.8 (3 pos) x 3.6 mm, pin row 1.1 mm from the back
  wall, pin 1 at the 1.6 mm end.
- The cables (mechanism ribbon, host SmartPort ribbon, front-panel LED and
  eject leads) come with the drive and are reused. Nothing to order.

### H1, H2: mounting holes

| Ref | What | Footprint | Qty |
|---|---|---|---|
| H1 | M3, 3.2 mm drill, 5.2 mm pad | `a2m2053:MountingHole_3.2mm_M3_Pad_5.2mm` | 1 |
| H2 | M3, 3.2 mm drill, 6.4 mm pad | `MountingHole:MountingHole_3.2mm_M3_Pad_Via` | 1 |

- Plated, on GND (FG merged into GND, 2026-10-16). Line up with the IF-83
  mounting points (3D-print check 2026-10-13).
- Hardware: the drive's own M3 screws and standoffs. Nothing to order
  unless the originals are missing.

### Bare PCB

- 2 layer, 100 x 110 mm, 1.6 mm FR-4, HASL or ENIG (ENIG is nicer for the
  Pico castellations and the SOIC drag-solder).
- Min track 0.25 mm, clearance 0.2 mm, drill 0.3 mm, via ring 0.15 mm.
  Within any prototype house's standard rules (JLCPCB, PCBWay, OSH Park).
- Outline matches the IF-83 (`FABRICATION.md`, "Board outline"). The DB-19
  shell overhangs the edge on purpose.
- Qty 5 at a Chinese prototype house: about $4 to $8 per board plus
  shipping. OSH Park (US, ENIG, 3 boards) is about $55 for the set. [guess]

## Not on the board (bench and build)

| Item | Why |
|---|---|
| Raspberry Pi Debug Probe (SC0889) | SWD flashing via OpenOCD. One per bench, not per board |
| Micro-USB cable | Console (USB CDC) |
| 3-pin JST-SH debug cable | Comes with the Debug Probe |
| Flux, 60/40 or SAC solder, chisel tip | Hand-solder rule: drag solder the SOIC-24s and the Pico castellations |

## Open items that change this BOM

From `FABRICATION.md` "Confirm on the prototype at M9":

1. ~~J4 and J5 pitch and plug type.~~ TE AMP CT, 2.0 mm, 2026-10-24.
   Footprints drawn and placed in the IF-83's order (2-pin outboard, 3-pin
   inboard, shrouds open toward the board edge).
2. ~~R17 value.~~ 180, the IF-83's LED anode pull-up, measured 2026-10-25.
3. Whether the eject button's second contact gets a spare GPIO (GP28) and
   so a 4th J4 pin. Current J4 works either way. The IF-83 reads both
   contacts (two header pins to separate CXD1013 pins, 2026-10-24).
