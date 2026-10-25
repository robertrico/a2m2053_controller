# Research notes: A2M2053 (UniDisk 3.5) controller

Status legend: **[doc]** stated in an Apple document, **[fw]** from the drive
firmware disassembly, **[photo]** read off eBay board photos (verify on real
hardware), **[guess]** inference, not yet verified.

## 1. What the drive is

- Intelligent drive: microprocessor controller inside the enclosure, talks
  SmartPort packets to the host through the IWM port. **[doc TN#5]**
- Contrast: Apple 3.5 Drive (A9M0106) is dumb, host drives the mechanism. **[doc TN#5]**
- Controller CPU runs "slightly under 2 MHz", regular cycles. **[doc TN#5]**
- IWM lives in controller memory at `$0A00`. **[doc TN#5, fw]**
- Spare RAM in the controller for host-downloaded read/write/ID routines
  (CONTROL calls SET_DOWN_ADR `$06`, DOWNLOAD `$07`). **[doc TN#4, TN#5]**
- Powered entirely from the host cable. **[doc manual p2]**
- Front panel: eject button (electrical, only works with host powered and
  "in use" LED off), manual eject hole, red "in use" LED. **[doc manual p9-11]**

## 2. Host-visible behaviour a replacement must match

- DIB type `$01`, subtype `$00` (Apple 3.5 Drive is `$01`/`$C0`). **[doc TN#5]**
- STATUS statcode `$00` general status byte: b7 block dev, b6 write allowed,
  b5 read allowed, b4 online/disk in, b3 0=format allowed, b2 0=write
  protected, b1 interrupting (IIc), b0 open (char dev). **[doc TN#3]**
- Known bug in original: STATUS does not return WRITE PROTECT error; WRITE
  does. **[doc TN#3]** (Decide later whether to be bug-compatible.)
- Block size 512. Mac-compatible extended READ `$08` / WRITE `$09` move 524
  byte blocks (512 + 12 tag bytes). **[doc TN#4]**
- Format: 800K double sided, 4:1 interleave by default. Zero-page knobs:
  `$62` interleave (`$02` Mac, `$04` UniDisk), `$63` sides (`$00` single,
  `$80` double). Reset to defaults on INIT. **[doc TN#4, fw `Se7bc`]**
- After a 800K -> 400K disk swap, STATUS block count is stale until a
  READ/WRITE. **[doc TN#4]**
- Disk format "essentially identical" to Macintosh GCR. **[doc TN#4]**

### Host disk port, UniDisk 3.5 Controller (Liron card) DB-19 **[doc Misc TN#6, `Apple II IWM Port Info.pdf`]**

| Signal | Pin | | Signal | Pin |
|---|---|---|---|---|
| Phase 0 | 11 | | /WrtReqII | 15 |
| Phase 1 | 12 | | /HstEnbl | 17 |
| Phase 2 | 13 | | Rd | 18 |
| Phase 3 | 14 | | Wr | 19 |
| Wrt Prot (= SmartPort ACK) | 10 | | Gnd | 1,2,3,4 |
| -12V | 5 | | +5V | 6 |
| +12V | 7,8 | | N/C | 9,16 |

The drive's own power comes from these pins (+5, +12, -12). IIc and IIgs
pinouts are in the same note.

## 3. Original controller memory map **[fw]**

From Eric Smith's disassembly
(`~/Development/project_byte_hamr/gateware/rev2/smart_hamr/liron.asm`).

| Addr | Use |
|---|---|
| `$0000-$07FF` | 2K RAM (zero page vars, buffers, download area) |
| `$0800` ga_reg0 | R: `LASTONE/ BUSEN/ WRREQ /GATENBL HDSEL` (d4..d0). W: `TRIGGER ENBUS PH3EN IWMDIR HDSEL` |
| `$0801` ga_reg1 | R: `SENSE BLATCH1 BLATCH2 LIRONEN CA0`. W: `/RSTIWM /BLATCH-CLR1 /BLATCH-CLR2 DRIVE1 DRIVE2` |
| `$0A00-$0A0F` | IWM: PH0-3 off/on, motor off/on, Q6L/H, Q7L/H (drive sel unused) |
| `$E000-$FFFF` | ROM. RESET/IRQ/NMI vectors all -> `reset` |

- `IWMDIR=0`: IWM wired to 3.5" mechanism. `IWMDIR=1`: IWM wired to SmartPort,
  `ph_0` used as /BSY handshake. One IWM time-shares both sides.
- `ga_init`: write `$10` to reg0, `$00` then `$1C` to reg1 (pulse IWM reset,
  clear BLATCH1/2).
- `DRIVE1`/`DRIVE2` + DuoDisk code paths: firmware supports two mechanisms
  (unreleased DuoDisk 3.5).
- Mechanism control (step, motor, eject, sense) goes through IWM phase lines,
  not dedicated gate-array bits. Phase pattern in firmware around `Se64d`
  (`ph_3` strobe, `ph_0/1/2` select). **[fw; guess on meaning]**

## 4. Board IF-83 (photos) **[photo]**

Board marking IF-83, 1-616-129-12, silk "apple computer (C) 1985" and
"LIRON". Sold as service exchange module P/N 661-0307 (box: A-8080-109-A,
Made in Japan, Sony-built). Two photo sets: eBay (shielded top + bottom) and
an unshielded top (`IF-83_802_top_unshielded.webp`, stamp "802").

Top side (unshielded photo):

| Ref | Part | Notes |
|---|---|---|
| IC101 | GTE G65SC02P-2 (8727) | 65C02, 2 MHz grade. |
| IC102 | Toshiba TC5365P "A2M2053-V1.0" (8718) | 8K mask ROM -> `$E000-$FFFF`. Matches disassembly size. (eBay board has TC54256AP instead; later rev, same "V1.0" label.) |
| IC103 | AMI 8750MDB **344-0041-B** (C) Apple 82,83 | **Discrete IWM.** Same part as IIc / Liron card. |
| IC104 | Sony CXK5816PN-12L | 2K x 8 SRAM -> `$0000-$07FF`. |
| X101 | Crystal | Value not legible yet. Firmware sets IWM 8 MHz mode bit, so likely ~8 MHz class (Mac uses 7.8336). Verify. |
| CN102 | 20-pin, silk "FROM HOST" | Silk (bottom): ACK, HSTPH0-3, +5V, +12V, -12V, FG. Host SmartPort cable. |
| D-sub | Rear connector | Daisy-chain out. |
| CN104 | 20-pin + ribbon | Silk (bottom): ENBL1/ENBL2, CA0, CA1, CA2, LSTRB, HDSEL, RD, WR, GND. **Mechanism cable.** |
| CN105-CN108 | TE AMP CT headers, 2.0 mm, black (6-292161-2 / 6-292161-3) | Silk "DRIVE 1", "DRIVE 2", "DRIVE 2 (UPPER)". CN107 (2-pin) = in-use LED, CN108 (3-pin) = eject switch. Per-drive pairs, ties to firmware DRIVE1/DRIVE2 (DuoDisk). Connector ID **[measured]** 2026-10-24: calipers against TE drawing C-292161 (2.0 mm pitch, 0.6 mm round posts, 3-wall shroud). Layout **[photo]** `photos/IF-83_505_top_with_mechanism.jpeg`: with the DB-19 edge toward you and the silk upright, the 3-pin (CN108/CN106) sits left of the 2-pin (CN107/CN105), pin 1 at the right end of each. |

Bottom side (eBay photo): **Sony CXD1013** QFP gate array (date 8838) lives
on the bottom. Test pads: TRIGGER, SENSE, /RST IWM, DEVSEL, PH0, RSTCAP.
These match the gate-array register names in the firmware.

So the board has: 65C02 + 8K ROM + 2K RAM + discrete IWM + CXD1013 (address
decode, IWM steering between host and mechanism, BLATCH, CA0/SENSE readback,
reset). Only the CXD1013 is undocumented.

### IWM modes used **[fw]**

- `Se58c` mode set. `$1F` (latch, async, timer off, **fast**, 8 MHz) before
  mechanism work (`Le9dd`). `$17` (latch, async, timer off, slow, 8 MHz) in
  `Sebfd`, likely SmartPort side (4 us cells). **[fw; side assignment = guess]**
- Fast mode = 2 us cells = Mac 3.5 GCR rate. Consequence: UncleBernie's
  IWMless CPLD (no FAST mode, IIc/Liron card only) cannot sub for the IWM in
  this drive.

Key takeaway: mechanism-side signal names (CA0-2, LSTRB, ENBL, HDSEL, RD, WR,
SENSE) are the Macintosh 3.5" drive interface. That interface is publicly
documented (Inside Macintosh, Mac/IIgs hardware refs, MAME, BMOW Floppy Emu).
A replacement board can drive the mechanism directly and never needs the
CXD1013. **[guess, strongly supported; confirm mechanism pinout on a real unit]**

## 5. Open questions

1. Exact mechanism part (Sony MP-F51W-xx variant?) and whether speed control
   is internal to the mech (800K Sony mechs) or needs PWM from the controller.
2. ~~CN104 pin-by-pin map.~~ Orientation confirmed by continuity 2026-10-10
   (pin 1 = GND, pin 8 = LSTRB). Resolved.
3. ~~Host connector pinout on CN102 vs SmartPort DB-19.~~ Resolved: sec. 10
   and "Host bus on the Liron card (M6)" (ACK on CN102/flat 20).
4. ~~Eject button and LED wiring~~ Eject cable: 3 pins, a changeover switch.
   Released, the outer two are connected; pressed, they open and the middle
   pin connects to one outer pin. **[measured]** 2026-10-15. LED cable:
   RED and BLK to a small LED board; meter diode test shows nothing either
   way (likely a series resistor on that board). **[measured]** 2026-10-14.
   Polarity from the wire colors only. **[guess]** Plugs on both leads are
   TE AMP CT (2.0 mm, receptacle 173977); board headers are TE 292161-2
   (LED) and 292161-3 (eject), black 6- prefix on the original.
   **[measured]** 2026-10-24.
   IF-83 LED circuit, traced on the board **[measured]** 2026-10-24: one
   LED pin is pulled high (180 ohm to +5V, measured 2026-10-25); the
   other goes to the
   collector of Q101 (3-leg, two legs one side and one the other, marked
   "L-5"). Q101 emitter to GND, base through 10k to a CXD1013 pin. So the
   CXD1013 drives the LED active high through Q101 and the connector-side
   signal is active low (cathode pulled to GND). Our board drives the anode
   from U4 through R17 and grounds the cathode: same current direction, no
   change needed. Eject switch: two of the three header pins run to
   individual CXD1013 pins **[measured]** 2026-10-24, so the original reads
   both contacts of the changeover switch; the third pin is the common
   (GND assumed, confirm). Our J4 (middle pin to GP22, outer pins to GND)
   reads only the pressed contact.
5. ~~IWM inside CXD1013?~~ No: discrete AMI 344-0041-B (IC103). Resolved.
6. ~~Working unit to sniff?~~ Resolved: one working UniDisk, used through its
   cable only (never opened); M7 compares against it with SPCOMPARE.
7. X101 frequency.

## 6. Sources

- Apple II Technical Notes, UniDisk 3.5 #1-#5, rev. Nov 1988 (this folder).
- UniDisk 3.5 Owner's Manual 030-1151-A (this folder).
- Drive firmware disassembly, Eric Smith 2018 (byte_hamr repo, path above).
- IWMless (UncleBernie, Applefritter, beta Aug 2025): CPLD IWM substitute,
  ispLSI1016, no FAST mode, IIc + Liron card only. Not usable here.
  https://www.applefritter.com/content/announcing-iwmless-iwm-substitution-call-beta-testers
- Further reference named by Apple: Apple IIgs Firmware Reference (UniDisk
  internals moved there), Apple IIc Technical Reference 2nd ed. Not yet in
  this folder; worth adding.

## 7. Mechanism: Sony 800K double-sided (Apple 699-0321) **[doc ERS Aug85]**

Source: `private/bitsavers_sony/MP-F51_specs/699-0321_800K_Double-Sided_ERS_Aug85.pdf`
(Apple engineering requirements spec for the 800K mech, text layer is OCR,
some garble). Sep86 rev 669-0452-A alongside. Schematics (.tif) for
MP-F51W internal / external / -03 unified in the same folder.

Rico's mechanism has a Sony **CX20185** (QFP-44) on its board: FDD read/write
head amplifier (equivalents Sanyo LA8200M, Motorola MC2871). It lives on the
mechanism, not on our controller. Analog read chain is already done for us;
RD out of the mech is digital flux transitions.

### 20-pin mechanism connector (ERS sec. 3.6) = CN104 on IF-83

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | GND | 2 | CA0 |
| 3 | GND | 4 | CA1 |
| 5 | GND | 6 | CA2 |
| 7 | GND | 8 | LSTRB |
| 9 | N/C | 10 | /WRTGATE (= /WRREQ) |
| 11 | +5V | 12 | SEL |
| 13 | +12V | 14 | /ENBL |
| 15 | +12V | 16 | RD |
| 17 | +12V | 18 | WRTDATA |
| 19 | +12V | 20 | /PWM |

Matches tashnotes 2x10 header map (`private/bitsavers_sony/tashnotes_floppy/connector.md`).
Pin 20 is PWM on 400K, N/C on black-label 800K, /CSTOUT on red-label 800K.
**Orientation [measured] 2026-10-10:** continuity on a dead-board drive. Pin 1
beeps to GND on the test cable, the mechanism board and the GND test point.
Pin 8 beeps to the LSTRB test point through connector and test cable.
Pin 11 beeps to the 5VF test point.

**Status bits [measured] 2026-10-10 (M1, dead-board drive mechanism):**
DRVIN=0, SIDES=1, REVISED=1, SUPERDRIVE=0: an 800K double-sided mechanism.
CSTIN 1 = no disk, 0 = disk in. WRPROT 0 = write-protected (tab hole open),
1 = write-enabled; reads 0 with no disk. MOTORON=1 (off) and DIRTN=0 while idle.
The mechanism spins the motor for about 0.5 s on its own when a disk is inserted.
RD follows a CA0 change in about 27 ns (RD falling) and about 36 ns (RD rising,
read off the screen), measured at CN104. RD is actively driven in both
directions. The 10 us firmware settle default is far longer than needed.

### Speed: mechanism self-regulates **[doc]**

Five zones, drive picks speed from track:

| Tracks | RPM |
|---|---|
| 0-15 | 394 |
| 16-31 | 429 |
| 32-47 | 472 (garbled in OCR, standard value) |
| 48-63 | 525 |
| 64-79 | 590 |

Tolerance +/-2.5 % while /READY low. Data rate 489.6 kbit/s, ~2 us cells.
PWM (22 kHz) only drives the 400K single-sided mech (699-0285); on the 800K
"if PWM held high, the read signal will be valid". **No speed control needed
from our board.**

### Timing (ERS, partial)

- LSTRB: set CA0-2/SEL, then LSTRB high then low; command acts on rising edge.
- Step: 36 ms per track without speed-zone change, 152 ms max with change.
- PWM/RDDATA: 4 us max to valid RDDATA after PWM stops (400K concern only).
- Sector interleave per speed zone listed in ERS (~line "Speed group 1..5").

Other downloaded refs (same folder): `Apple_drive_command_and_status_codes.pdf`,
`Software_control_of_IWM.pdf`, `IWM_undoco_features.pdf`,
`Mac_v_IIgs_sectors.pdf`, `Apple_3.5_Drive_Schematic.pdf` (dumb Apple 3.5
drive, A9M0106), `699-0285-A_400K_Disk_Dec84.pdf`, `Liron/` photos (bitsavers
`liron_t/b.jpg`, likely this board), `MP-F51W-23/` mech photos, tashnotes
(`mci.md`, `gcr_checksum.md`). All from
https://mirrors.apple2.org.za/www.bitsavers.org/pdf/apple/disk/sony/

## 8. Mechanism board (Rico's dead-board drive, photo 2026-10-07) **[photo]**

`photos/mechanism_board_cx23077a_cx20185.jpeg`. Underside of the mechanism.

| Part | Marking | Role |
|---|---|---|
| Sony CX23077A | 8515 Z07 (1985 wk15) | Drive logic ASIC on the mechanism: CA0-2/SEL/LSTRB decode, status mux onto RD, stepper/eject/motor control. **[guess, fits the ERS and the 1985 date]** No datasheet expected. We don't need one: we only talk to it through CN1. |
| Sony CX20185 | 5D51 | Read/write head amplifier (QFP-44; equivalents LA8200M, MC2871). |
| Direct-drive spindle motor | - | Speed regulated on the mechanism (ERS 5 zones). |
| CN1 | 2x10, silk 1/2 ... 19/20 | Ribbon to controller CN104. Test points near it: CA0, CA1, CA2, LSTRB, RD, TRK0, 5VF, 12VF, 5VC, 12VC, GND, RESET. |
| CN2, CN3, CN4 | small headers | Internal to the mechanism (motors / sensors). Not our concern. |

- CN104/CN1 pin 20 (/PWM) is tied to +5V on the controller board. **Measured by Rico.**
  Our board does the same.
- 1985 date code means an early 800K mechanism, consistent with the Aug 1985
  ERS (699-0321).

## 9. Apple_3.5_Drive_Schematic.pdf: contents **[doc]**

`private/bitsavers_sony/Apple_3.5_Drive_Schematic.pdf`, 4 pages, scanned.

- p1-2: "Unified 3.5" Disk Interface Chip (DSIC)" Rev 1.2, 12/3/85, Apple
  CONFIDENTIAL. A 74LS-level logic version of the Apple 3.5 Drive's chain
  chip. J1 daisy chain in, J2 internal drive (20-pin), J3 daisy chain out
  (DB-19). In this (dumb-drive) design PH0-3 *all* go out through gates
  (74LS08), plus /EN3.5, /DRV1, /DRV2. Eject switch + drive LED circuit shown.
- p3: Sony gate-array logic diagram, type CXD10?3 (last digits hard to read),
  designer T. Naga, dated 86-03. Pins: /ENBL1, /ENBL2, /CSTOUT, /XEJECT,
  MEJECT, HSTPH0-3, ACK, RDDAT, /WRREQ, HDSEL, /INTDRV, XTAL. Signal names
  match the IF-83 silk (HSTPH0-3, ACK). Related to the CXD1013 family; maybe
  not the same part. **[guess]**
- p4: handwritten Apple table "Pin assignment on Apple floppy disk drive",
  8/20/85, rev 11/12/85. DB-19 pinout for Ext IIc, UniDisk 5.25, DuoDisk,
  Mac SS/DS, **LIRON**, proposed Universal 3.5, IIe port, plus the flat
  20-pin mapping.

### LIRON column (DB-19) **[doc, handwritten]**

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1-4 | GND | 5 | -12V |
| 6 | +5V | 7, 8 | +12V |
| 9 | "H/01 (HD SEL)"? partly illegible | 10 | WRPROT (= ACK) |
| 11 | PHA (PH0) | 12 | PHB (PH1) |
| 13 | PHC (PH2) | 14 | PHD (PH3) |
| 15 | /WRREQ | 16 | **LAST ONE** |
| 17 | /ENBL | 18 | RDDATA |
| 19 | WRDATA | | |

Pin 16 note, verbatim: **"GND on input port, +5V -2k-> on output port."**
So LASTONE/ = rear (output) port pin 16, pulled up to +5V through 2k. A
downstream drive's cable (its input port) grounds pin 16, so we read low =
drive behind us, high = we're last. Matches the firmware (`LASTONE/` read
after the last ID is assigned). Input-port ground confirmed on a dead IF-83:
CN102.12 (DB-19 pin 16 on the host side) beeps to GND. **[measured]**
2026-10-13

Flat 20-pin mapping (right column): DB-19 pin n -> flat pin 2n-1 for 1-10
(odd side), pins 11-19 -> flat 2,4,...18 (even side), flat 20 unused.

## Writing to the mechanism (M5) 2026-10-11

- WRTDATA: each level change writes a flux transition; a 1 bit is a toggle, a 0
  bit no toggle. Polarity does not matter. **[doc ERS86 3.2.6]** Our PIO toggles
  per 1 bit at 2.040-2.048 us per cell. **[measured]**
- /WRTGATE timing **[doc ERS86 p24, p29]**: READY low before the gate (T1); SEL
  settled >= 100 us before (T6); gate low to first edge >= 1.8 us (T3); last edge
  to gate high >= 2 us (T4); erase tail 480-590 us and RD valid / /STEP allowed
  620 us after the gate goes high. Ours: T3 2.17 us, T4 4.02 us. **[measured]**
- The drive records only with /WRTGATE low, /ENBL low and the tab open.
  **[doc ERS86 3.2.5]**
- Track layout written by the drive ROM: preamble 200 x `FF 3F CF F3 FC`
  (1000 bytes, four 10-bit sync groups per 5 bytes); per sector 35 sync bytes,
  `FF`, address field, `FF`, `FF 3F CF F3 FC FF`, data field, `FF`. A sector
  rewrite starts right after the header's `DE AA`. No index pulse: a format
  writes 1000 + nsect x 763 bytes in one go and the tail wraps over the
  preamble. **[fw liron.asm 488-593, 2085-2156]** Reproduced by our board; the
  working UniDisk reads the result. **[measured]**
- No write precompensation in the ROM or the docs. None used; reads back clean.
  **[fw] [measured]**
- Header-to-gate latency on our board: 3.1 us after the decoder can see `DE AA`
  (ROM: about 16-30 us). **[measured]**
- READY lags a recalibration: a seek that only recalibrates must still wait for
  READY before writing. **[measured]**
- +5V at CN104 pin 11 must stay >= 4.75 V with the motor running, or read data
  vanishes (M3 and again M5). **[measured]**

## 10. Rear D-sub <-> CN102 continuity (Rico, dead board, 2026-10-07; re-beeped 2026-10-13, same map) **[measured]**

Expected map = Apple "regular" flat mapping (DB-19 n -> flat 2n-1 for 1-10,
2(n-10) for 11-19). Everything matched that, except:

| Rear D-sub | Goes to | Meaning |
|---|---|---|
| 9 | CN102:17, silk **HDSEL** | Straight pass-through. Pin 9 = HDSEL (Apple table "HD SEL"). |
| 10 (WRPROT/ACK) | **CN102:20** (not 19) | ACK sits on flat pin 20, the "special mapping" in Apple's table. CN102:19 = no wire (table: "(19) NO WIRE"). |
| 14 (PH3) | **CXD1013 pin 4** | Gated PH3 out (PH3EN). As predicted. |
| 16 (LASTONE) | resistor -> **CXD1013 pin 5** | LASTONE/ input. As predicted. |
| 17 (/ENBL) | **CXD1013 pins 12 and 13**, NOT CN102:14 | Downstream /ENBL is generated by the gate array, not passed through. **New.** |

Pass-through (straight copper, rear <-> CN102): GND, -12, +5, +12, HDSEL(9),
ACK(10), PH0-2 (11-13), /WRREQ (15), RD (18), WR (19).

Open: why two CXD1013 pins (12 and 13) on /ENBL out (paralleled drivers for
current? or one output + one readback). And what generates it: probably
host /ENBL (CN102:14) gated the same way as PH3 (only after we're
assigned). **[guess]** Firmware has no separate ENBL bit, so the gating is
likely tied to PH3EN or ENBUS inside the CXD1013.

## Host bus on the Liron card (M6) **[measured]** 2026-10-12

Liron card (820-0117-A) in slot 5, IIe at BASIC, our board on J1. Details and
captures: `docs/build_log/m6.md`, `docs/captures/m6/`.

- Idle levels at the DB-19 (our pulls in place): PH1 + PH3 high, PH0 + PH2
  low; /WRREQ and /HSTEN high. -12 V rail -12.16 V, +5 V 4.73 V, +12 V
  12.38 V.
- WR idle: about 4.0 V Thevenin through about 1.9k (from 3.40 V with a 10k
  pull-down and 3.96 V with 100k). WR high during a packet 4.36-4.84 V.
- `PR#5`: PH0 + PH2 reset pulse, then REQ (PH0) with PH1 + PH3, then
  /WRREQ low for 879 us (one command packet, about 220 cells).
- REQ rises 7.7 us before /WRREQ falls; first WR edge 3.95 us after /WRREQ
  falls. Cell width 3.91 us (7.16 MHz / 28).
- Packet start decodes (transition = 1): `FF 3F CF F3 FC FF` sync, `C3`
  packet begin, `81` destination.
- With ACK pulled high and nobody answering, the Liron sends the packet
  anyway and the IIe reports no device connected.
- Our DB-19 to IDC20 cable uses the same flat mapping as the drive's CN102
  (sec. 10): ACK (DB-19 10) on flat 20, flat 19 no wire.

## SmartPort handshake from the two ROMs **[fw]** 2026-10-12

Card = `liron-if.asm` (host, 65C02 at 1.023 MHz); drive = `liron.asm`.
Used by the M6 spec (sec. 3, 4).

- ACK is the card IWM's SENSE (status bit 7): 1 = deasserted/ready.
- Card send: wait for ACK high, 50 polls x 11 cycles (about 540 us), else the
  whole send is retried (3000x via `Sca8a`; INIT in `Scded` only once). REQ
  on, `FF` then `3F CF F3 FC FF C3`, packet, `C8`; wait for underrun; then 9
  polls x 12 cycles (about 105 us) for ACK low, else REQ off and retry.
- Card read: wait for ACK high with **no timeout** (the device's hold-off),
  REQ on, `C3` within 30 nibbles (nibble waits have no timeout), checksum and
  `C8`; then wait for ACK low (no timeout), REQ off. A bad reply returns with
  REQ still high; `smartport_bus_read_packet_with_retries` waits 1 ms, drops
  REQ and reads again (5 tries).
- Drive receive (`Sea29`): ENBUS off, wait REQ, hunt `C3`, filter the
  destination (any while IDs are unassigned), check the type, checksum, `C8`;
  then `ph_0_off` (ACK low) and ENBUS on, wait REQ low. No ACK for a bad
  packet.
- Drive reply (`spb_send_packet`): ACK high, wait REQ (about 0.5 s), `FF`
  + `3F CF F3 FC FF 3F CF F3 FC FF C3`, packet with type `$81` whatever the
  content, `C8`, ACK low, wait REQ low (about 1 ms, else send again).
- WRITEBLOCK and CONTROL: the drive receives a data packet (type `$82`)
  before it works; ENBUS off lets ACK float high, which tells the card to
  send it.
- INIT reply status `$40` when LASTONE/ is high (nothing behind); the card's
  `Scded` loop stops on a non-zero status.
- Bus reset (PH0+PH2, 80 ms) resets the drive in hardware (RSTCAP).
- The host IWM counts falling RDDATA edges only (IWM spec 1982 p. 9); the
  card writes WR as a level change per 1.
- Checksum = XOR of the seven header bytes as sent and the decoded data;
  sent as `c | $AA`, `(c >> 1) | $AA`. GRP7CNT and ODDCNT are 7-bit, so a
  packet carries at most 127 x 7 + 6 = 895 data bytes.
- **[measured] 2026-10-12 (M6):** our board ACKs 5.3-6.7 us after /WRREQ
  rises, and the card drops REQ 9-23 us after ACK. The card accepted every
  ACK in that range (no retries in 193 packets).

## UniDisk STATUS replies **[measured]** 2026-10-12 (M7)

From SPCOMPARE on the working UniDisk (slot 5 unit 1), `/M7TEST` disk:

- STATUS `$00`: `F8 40 06 00` writable, `FC 40 06 00` write-protected,
  `E8 40 06 00` no disk; the call returns `$00` in all three.
- STATUS `$03`: those 4 bytes + `08 "DISK 3.5" + 8 spaces, 01 00 00 10`.
- STATUS `$05`: `00 00 00 00 00 00 00 FF` (byte 7, ROM `$5D`, is `$FF`).
- STATUS `$01`: error `$21`. READBLOCK 1600: `$2D` with a disk, `$2F` with
  none (the disk check comes before the block range, ROM `Le8b2`).
- Our board (M7, `f54d07d`) returns the same bytes in all three states.

## Mechanism supply on the bench **[measured]** 2026-10-12 (M7)

With the bench +5 limited to 0.03-0.05 A (fine for the host side alone),
the motor start pulls the rail into current limit: READY never comes (spin-up
timeout), the Pico's USB drops and U1 browns out. Limits of about 0.5 A
(+5) and 1.0 A (+12) work.

## Drive supply requirement [measured] 2026-10-10 (M3)

With +5V at CN104 pin 11 sagging to 4.0-4.8 V under motor load (bench wiring
resistance), the mechanism's read data on RD disappears or breaks up, while
status, tach, stepping and eject keep working. At about 5 V at the pin, both
test mechanisms read every sector. The PCB needs low-resistance +5V/+12V/GND
to CN104 and bulk caps at the connector: the IF-83 uses 4.7 uF 50 V electrolytics [photo/read by Rico]; use the same part.
