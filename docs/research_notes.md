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
| CN105-CN108 | Small 2/3-pin headers | Silk "DRIVE 1", "DRIVE 2", "DRIVE 2 (UPPER)". Per-drive LED / eject switch? Ties to firmware DRIVE1/DRIVE2 (DuoDisk). |

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
3. Host connector pinout on CN102 vs SmartPort DB-19; which pins carry /EN,
   WRPROT/ACK, RDDATA/WRDATA.
4. Eject button and LED wiring (CN105-108?).
5. ~~IWM inside CXD1013?~~ No: discrete AMI 344-0041-B (IC103). Resolved.
6. Do we have a working unit to sniff, or is the mechanism all we have?
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
after the last ID is assigned).

Flat 20-pin mapping (right column): DB-19 pin n -> flat pin 2n-1 for 1-10
(odd side), pins 11-19 -> flat 2,4,...18 (even side), flat 20 unused.

## 10. Rear D-sub <-> CN102 continuity (Rico, dead board, 2026-10-07) **[measured]**

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

## Drive supply requirement [measured] 2026-10-10 (M3)

With +5V at CN104 pin 11 sagging to 4.0-4.8 V under motor load (bench wiring
resistance), the mechanism's read data on RD disappears or breaks up, while
status, tach, stepping and eject keep working. At about 5 V at the pin, both
test mechanisms read every sector. The PCB needs low-resistance +5V/+12V/GND
to CN104 and bulk caps at the connector: the IF-83 uses 4.7 uF 50 V electrolytics [photo/read by Rico]; use the same part.
