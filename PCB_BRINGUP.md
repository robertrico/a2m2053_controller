# PCB bring-up: Disk Hamr rev 1.0

What to do when the PCBWay boards and the DigiKey parts arrive and the
first board is soldered. Four gates, in order. Each gate answers one
question and nothing downstream happens until it passes:

| Gate | Question | Hardware on the board |
|---|---|---|
| 1 | Can it damage the Apple IIe or the Liron card? | bench supply only |
| 2 | Does the PCB match the proven prototype? | + mechanism |
| 3 | Does it work as a drive? | + Apple IIe, then the working UniDisk |
| 4 | Does it keep working? | Apple IIe, RAM disk, mechanism idle |

The prototype passed M0 to M9 (`docs/build_log/`). The PCB carries the
same chips, straps, pulls and Pico pins, with eight signals on different
8T245 channels (`HANDOFF.md`, "Prototype vs PCB channel map"). The
channel moves are copper only; the firmware addresses Pico pins and is
unchanged. Rewiring the breadboard to the PCB order was dropped, so the
first assembled board is where the new channel order gets proven. That
is what Gates 1 and 2 are for. Bring-up checks the board, never the
design.

Every measured number goes in `docs/build_log/pcb_rev1.md` with the
board's serial (write the serial on the board in marker, 1 to 5).

## Names

PCB and bench rig connector names differ. This file uses PCB names.

| PCB | Bench rig | What |
|---|---|---|
| J1 | bench J3 | rear DB-19, daisy-chain output |
| J2 | bench J2 | mechanism, 20-pin box header (CN104) |
| J3 | bench J1 | host, 20-pin box header, ribbon CBL1 from the front DB-19 (CN102) |
| J4 | | eject button, 3 pins |
| J5 | | in-use LED, 2 pins |

Apple rails enter on J3: odd pins are GND except 9 (-12 V), 11 (+5 V),
13 and 15 (+12 V); even pins are signals, ACK on 20 (full map in
`docs/build_log/m6.md`). The -12 V on pin 9 passes straight through to
J1 for the next drive and is the rail that killed U1 and the Liron IWM in
M6. The bench supply feeds J3 through the
KingPow IDC20-to-screw-terminal breakout, the same one the prototype used.

8T245 pins (DW package, `docs/private/` datasheet): 1 DIR, 2 VCCA,
3-10 A1-A8, 11-13 GND, 14-21 B8-B1, 22 /OE, 23-24 VCCB.

Shifter straps, from `docs/pin_budget.md`:

| Board | Signals | DIR | /OE |
|---|---|---|---|
| U1 | PH0-3, /WRREQ, LASTONE/, /HSTEN, host WR (inputs) | low (B->A) | tied low |
| U2 | CA0-2, LSTRB, SEL, /ENBL, /WRTGATE, WRTDATA | high (A->B) | GP21 + R13 pull-up to 3V3 |
| U3 | host RD, ACK | high (A->B) | GP11 + R12 pull-up to 3V3 |
| U4 | PH3_OUT, ENBL_OUT, LED | high (A->B) | GP21 + R13, shared with U2 |
| U5 | mechanism RD (input) | low (B->A) | tied low |

The only lines this board ever drives toward the Apple are RD and ACK on
U3, and only while GP11 is low. U1 must be input-only. A wrong DIR strap
or a bridge on U1 would drive PH0-3, /WRREQ, /HSTEN or WR against the
IWM. That is the one fault that can kill the Liron card, and Gate 1 is
built around proving it is absent.

## Equipment

- Bench supply with current limit, +5 V and +12 V. Its current readout is
  unreliable below about 30 mA (`HANDOFF.md`), so use the DMM in series
  for the idle baseline.
- DMM with diode test. Magnifier or microscope for the SOIC-24 joints.
- Scope, three channels (oscope MCP). Note the CSV time-axis sign bug.
- Pico Debug Probe on the Pico 2's JST-SH connector, `make flash`.
- USB console cable. Unplug it whenever the IIe is powered off, or the
  Pico stays up across the power cycle.
- Two test-bed mechanisms (dead boards, good mechanisms). The working
  UniDisk stays closed and is used as an oracle only, cabled normally.
- Disks: the reference disk written by the working UniDisk (`/REF800K`,
  image `docs/private/ref/REF800K.po`), the M7 sentinel boot disk, one
  scratch disk.
- 1 kOhm resistor with clip leads, for the contention check.

## Gate 0: before any power

Board on the bench, nothing plugged in, Pico soldered.

1. Visual pass under the magnifier: every 8T245 pin, the Pico castellations,
   D1 orientation (cathode band toward VSYS), C11-C13 stripes, J2/J3 pin 1
   marks, J4/J5 shroud openings.
2. Rails, ohmmeter, both polarities:

   | Pair | Expect |
   |---|---|
   | +5 to GND | more than 1 kOhm, diode test shows no short |
   | +12 to GND | open apart from C11 charging |
   | +12 to +5 | open |
   | -12 (J3 pin 9) to GND, +5, +12 | open |
   | +3V3 to GND | more than 1 kOhm (Pico regulator output) |
   | VSYS to +5 | diode drop one way (D1), open the other |
   | FG (mounting hole) to GND | record what it reads; `FABRICATION.md` left how FG ties to GND open |

3. -12 (J3 pin 9), +12 (pin 13) and +5 (pin 11) to every signal pin on
   J3 and J1: open. M6 lost U1 and the Liron IWM to -12 V on /WRREQ from a
   cable fault; this is the PCB version of that check. Pin 9 sits next to
   pin 10 (/WRREQ) on the header, so that pair gets a second look under
   the magnifier.
4. Adjacent-pin bridges: J3 pins 1-20 pairwise, J2 pins 1-20 pairwise, and
   each 8T245 pin against its neighbors. Open everywhere except the GND
   pins that are meant to share a net.
5. Straps: meter pin 1 (DIR) and pin 22 (/OE) of U1-U5 against the strap
   table above. U1 and U5 DIR to GND, /OE to GND. U2, U3, U4 DIR to 3V3.
   U2 and U4 /OE to the same node (R13, GP21). U3 /OE to R12 and GP11.
6. Pulls on the B side, meter to the rail: WR 100k to GND, /HSTEN 10k to
   +5, LASTONE/ 2k to +5 (J1 pin 16), LSTRB R18 10k to GND, ACK and RD
   10k to +5, ENBL_OUT 10k to +5, PH3_OUT 10k to GND.

Record: the FG reading and any rework.

## Gate 1: can it hurt the Apple?

Bench supply on J3, current limit 100 mA on +5 and 50 mA on +12. No
mechanism on J2, nothing on J1, no Apple.

### 1.1 First power, Pico idle

Power +5 only. Pico boots (USB enumerates, console answers `help`). DMM
in series on +5: record the idle current. This number becomes the
baseline for every later board. The prototype read about 10 mA logic-only
on the supply's coarse readout (`m0.md`); the DMM figure from this board
replaces it.

Then +12. Current must stay at zero with no mechanism.

| Measure | Where | Expect |
|---|---|---|
| +5 at J3 | pin 11 | supply voltage within 0.1 V |
| VSYS | Pico test pad | +5 minus D1's drop, above 4.2 V |
| 3V3 | C13 | 3.2 to 3.4 V |
| +12 at J2 | CN104 +12 pin | supply voltage |
| -12 at J1 | rear DB-19, the -12 position | 0 V with nothing feeding J3 pin 9; the net exists only to pass through |
| +5 at J2 | CN104 pin 11 | supply voltage within 0.1 V. The ERS wants 4.75 V minimum here; the prototype's thin jumpers read 4.54 V (`m7_5.md`). The PCB's 1 mm traces should do better. Record it |

### 1.2 Shifter release at boot

Repeat the M0 boot-safety scope check on the PCB. CH1 GP21 at R13, CH2
LSTRB at J2 pin 8, CH3 CA1 at J2 pin 4. Trigger on each channel in turn,
reset with `make reset`, then with a supply power cycle.

Pass: GP21 stays high through reset and boot, no LSTRB or CA1 pulse
(`m0.md` Step 4). The supply power cycle is the case the IIe produces:
3V3 and +5 rise together, so the brief U2 drive-high seen at a Pico USB
replug (`m0.md` Step 4b) should be shorter or absent. Capture it either
way to `docs/captures/pcb_rev1/`.

### 1.3 Host lines are inputs

This is the Liron-safety proof. Firmware up, `sp off` (U3 released),
`oe off`.

For each of J3's host-driven signals, PH0, PH1, PH2, PH3, /WRREQ, /HSTEN
and WR: clip the 1 kOhm from the pin to GND, read the pin voltage, then
from the pin to +5, read again. The pin must follow the resistor (under
0.5 V to GND, above 4.5 V to +5). A pin that holds its own level is being
driven by U1 and the board does not go near the Apple until that is
found.

Confirm the firmware sees each pin move: `sp` prints the eight host input
levels. Pull one, read `sp`, release, read `sp`.

Then RD and ACK with `sp off`: 1 kOhm to GND pulls them to about 0.45 V
(the 10k pull-up in parallel). They are released. `sp on` with no host
traffic must leave them released too; ACK and RD only drive inside a
packet reply.

Rear J1 with `oe off`: PH3_OUT reads 0 V (10k to GND), ENBL_OUT reads +5
(10k to +5). Both follow a 1 kOhm pull. U4 is Hi-Z.

### 1.4 Host-side loopback

The `loop` console command drives each line of the J1/J3 pair and reads
it back through the loopback cable from M8 (`docs/build_log/m8.md`).
Needs `server off` and `sp off` first. Pass: every line reads back, no
line moves when its neighbor is driven.

Gate 1 passes when 1.1 to 1.4 are clean. Only then does a mechanism go on
J2.

## Gate 2: does the PCB match the prototype?

Test-bed mechanism on J2, pin 1 to pin 1. Still on the bench supply;
raise the +12 limit to 1 A for the motors. Power order from M0: logic
first, then the mechanism supply; reverse on the way down.

Three commands prove the mechanism-side channel order, since every J2
line takes part in one of them:

| Step | Command | Pass | Source |
|---|---|---|---|
| ID word | `oe on`, `enbl on`, `status` | DRVIN=0 SIDES=1 REVISED=1 SUPERDRIVE=0, no disk: CSTIN=1 WRPROT=0 | `m1.md` |
| Sensors | insert the scratch disk, `status`; flip the tab, `status` | CSTIN goes 0; WRPROT 1 with the hole covered, 0 with it open | `m1.md` |
| Recal | `recal`, `status 5` | OK, TK0=0 | `m2.md` |
| Speed | `tach` at track 0; `seek 40`, `tach`; `seek 79`, `tach` | 394, 472, 590 rpm within 2 %; ERS zone table, M2 read -0.5 % | `m2.md` |
| Read | `server on`, `readblk 0` with the reference disk | block returns, no retries | `m4.md` |
| Write | `writeblk 100` on the scratch disk | verify passes | `m5.md` |
| Eject | `eject` | CSTIN back to 1 | `m2.md` |

A wrong answer here is a channel swap on U2 or U5, since the same
firmware ran these on the prototype and only the copper changed. Compare
against the channel table in `HANDOFF.md`.

Front panel: J4 and J5 plugged in. Press the button with the disk in: it
ejects (counts against the eject budget below). LED lights during
`readblk` and goes out after. If the LED stays dark, check J5 pin 1 is
GND and R17 is 180 Ohm.

Current with the motor on: record +5 and +12 from the DMM. +5 at CN104
pin 11 with the motor running: record, compare with 4.54 V on the
prototype.

Gate 2 passes with the table clean and the panel working. The mechanism
has now done one recal, three seeks, one eject.

## Gate 3: does it work as a drive?

Board into a test-bed drive chassis. Bench supply off and disconnected;
the drive runs on the IIe through the DB-19 cable, as in M7.5. Pico
console still on USB. Liron card in slot 5, the working UniDisk off the
bus for now.

### 3.1 Staged connect

1. IIe off. Cable from the Liron card to the drive's front DB-19.
2. Firmware setting before power: `sp off`. The board listens and never
   drives RD or ACK.
3. IIe on. Record +5 and +12 at J2 on IIe power. `sp` shows the host pin
   levels; `PR#5` shows PH0 and PH2 toggling and /WRREQ low during the
   card's INIT attempt (the pattern from `m6.md`). The boot fails, which
   is expected.
4. `sp on`. `PR#5` again. ProDOS boots from the sentinel disk.

Scope on the first boot, same probes as M7: CH1 /WRREQ (J3 pin 10), CH2
ACK (J3 pin 20), CH3 PH0 (J3 pin 2). ACK follows /WRREQ by 4 to 6 us on
command packets (`m7.md`).

### 3.2 Function

| Test | Pass | Source |
|---|---|---|
| Sentinel boot, `CAT` | boots, lists the sentinel files | `m7.md` |
| `sp` counters after boot plus catalog | cksum, bad, tmo, rereads, late, late_entry all 0; ack_us max under 10 us, data_max under 40 us | `m7.md`: 4-6 us, 34 us |
| Full read scan | `tools/a2mflux.py disk --compare REF800K.po` over USB | 1600/1600 byte-identical | `m3.md`, `m4.md` |
| Write disk | `a2mflux.py writedisk` of a pattern image to the scratch disk, then read back and compare | identical | `m5.md` |
| SPCOMPARE | `RUN SPCOMPARE` from the flash image, with a disk, write-protected, with no disk | matches the UniDisk transcript from M7 | `m7.md`, `tools/apple/SPCOMPARE.bas` |
| Oracle cross-read | scratch disk written by this board into the working UniDisk, ADTPro image | byte-identical to the pattern image | `m5.md` |
| Swap, then write | swap to a second disk at the prompt, copy one file onto it, catalog | file lands, other files intact | `m9.md` |
| Write-protected | Copy II Plus format attempt | refused, "DISK IS WRITE-PROTECTED" | `m9.md` |
| Empty drive | `CAT` with no disk | I/O error, `sp` shows `$2F` on reads | `m9.md` |
| Eject button from ProDOS | press during `CAT` | error on the Apple, next disk works with no reboot | `m9.md` |

### 3.3 Chain

Working UniDisk on J1 behind our board. ProDOS lists S5,D1 and S5,D2.
Then the reverse order, our board behind the UniDisk. One file copied
each way. Pass-through packet count in `sp` grows with UniDisk traffic
(`m8.md`). LASTONE/ reads 1 with nothing on J1 and 0 with the UniDisk
plugged in.

This is the only time the working UniDisk is on the bench: two boots and
two file copies.

Gate 3 passes with every row clean. The board is a drive.

## Gate 4: does it keep working?

Stress goes on the board, never on the mechanism. `sp disk ram` serves
the M6 RAM disk, so the Apple hammers the shifters, the SmartPort PIO,
ACK timing and the Pico's power path with the motor off. Mechanism in the
chassis, disk out.

Counters run from power-on. Power-cycle the Pico before each run, then
read `sp` at the end. Pass for every run: cksum, bad, tmo, resets,
rereads, late, late_entry all 0; ack_us max and data_max no worse than
the Gate 3 figures; exec_ms max under 1000 (M9 read 884 ms with the
floppy, the RAM disk should be far under that).

| Run | Setup | Length | Watches |
|---|---|---|---|
| Copy loop | BASIC program on the RAM disk: BLOAD a 120K file, BSAVE under a new name, delete the old, repeat | 4 hours or longer | counters, `splog on` sample of the ACK tail |
| Copy loop, chained | same, working UniDisk behind on J1, idle after boot | 1 hour | counters, pass-through count stays flat |
| Cold boots | IIe off, wait, on, `PR#5`, catalog | 30 cycles | boots every time; scope LSTRB at J2 pin 8 on a few of them for the power-edge strobe |
| Resets mid-transfer | Ctrl-Reset during the copy loop | 50 | recovers, `sp` shows resets counted and nothing wedged (`wedged=0`) |
| Thermal | after the copy loop | | U1-U5, D1 and the Pico by touch or IR; VSYS and 3V3 unchanged from Gate 1 |
| Brown-out | bench supply at 4.5 V on J3, copy loop | 30 minutes | counters 0. The IIe rail read 4.54 V on the prototype; this shows the board has margin there |

Then one short floppy pass to close: `sp disk floppy`, sentinel boot,
catalog, `sp` clean. A few seconds of motor time.

## Mechanism wear budget

The test-bed mechanisms are forty years old. The eject motor and its gear
train are the known weak point on the Sony 800K, and the head carriage has
no spare. Per board verified:

| Action | Budget | Where it is spent |
|---|---|---|
| Ejects | 5 | Gate 2 (two), Gate 3 (swap, eject-mid-catalog, cross-read) |
| Full seek sweeps | 3 | Gate 2 recal and zone seeks, Gate 3 read scan and write disk |
| Motor on | under 15 minutes | Gate 2 and Gate 3 |
| Formats | 1, scratch disk only | Gate 3 write-protected test formats nothing; the pattern write needs no format |

Anything beyond the budget waits for a reason. Soak runs use the RAM
disk. The working UniDisk does two boots and two copies in Gate 3 and
then goes back in its box.

## Firmware additions before bring-up

Small, all on the console:

- `vsys`: read VSYS through ADC3 (GP29, the Pico's VSYS/3 divider, free
  on this board) and print it. Gate 1.1 and the thermal check use it.
- `led on|off`: drive the LED directly. Gate 2 checks J5 without waiting
  for disk activity.
- `sp clear`: zero the counters so Gate 4 runs need no Pico power cycle.
- `splog` already prints per-packet ACK times; keep it.

Nothing else. The `loop`, `status`, `tach`, `seek`, `readblk`,
`writeblk`, `sp` and `splog` commands cover the rest.

## Record

`docs/build_log/pcb_rev1.md`, one section per board serial:

- Gate 0: FG reading, rework done.
- Gate 1: idle current on +5, VSYS, 3V3, +5 and +12 at J2, boot-safety
  capture filenames, contention check result per pin.
- Gate 2: the status word, rpm per zone, +5 at CN104 pin 11 with the
  motor on, motor-on current.
- Gate 3: `sp` block after boot and after the read scan, ACK capture
  filename, SPCOMPARE result, cross-read result.
- Gate 4: `sp` block per run, run lengths, temperatures.
- Ejects used, seeks used.

A board that passes all four gates is a Disk Hamr. Update `HANDOFF.md`
and the manual's "Tested on" section when the first one does.
