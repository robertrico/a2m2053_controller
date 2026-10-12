# Pin budget: Pico 2 (RP2350A)

Pico 2 brings out **26 usable GPIO**: GP0-GP22 and GP26-GP28. GP23-25 and
GP29 are used on the board (SMPS mode, VBUS sense, LED, VSYS ADC).

## Signals needed (single drive prototype)

| Group | Signals | Count |
|---|---|---|
| Host inputs | PH0-3, /WRREQ, /HSTEN, WR | 7 |
| Host outputs | RD, ACK | 2 |
| Host RD shifter /OE | release RD when not addressed | 1 |
| Mechanism outputs | CA0, CA1, CA2, LSTRB, SEL, /ENBL, /WRTGATE, WRTDATA | 8 |
| Mechanism input | RD (data + status) | 1 |
| Mechanism shifter /OE | keep it Hi-Z through boot | 1 |
| **Core total** | | **20** |
| Front panel | eject button in, LED out | 2 |
| Debug UART (optional, USB CDC also works) | TX, RX | 2 |
| **Total** | | **24 of 26** |

Mechanism pin 20 (/PWM) is tied to +5V, so it uses no GPIO.
No SD card: the media is the floppy.

## Proposed map

| GPIO | Signal | Dir | Shifter |
|---|---|---|---|
| GP0 | ~~UART TX~~ -> PH3_OUT (see Revised totals) | out | U4 |
| GP1 | ~~UART RX~~ -> LASTONE/ (see Revised totals) | in | U5 |
| GP2 | PH0 | in | U1 |
| GP3 | PH1 | in | U1 |
| GP4 | PH2 | in | U1 |
| GP5 | PH3 | in | U1 |
| GP6 | host /WRREQ | in | U1 |
| GP7 | host /HSTEN | in | U1 |
| GP8 | host WR (FM data in) | in | U1 |
| GP9 | ACK | out | U3 (with host RD), 10k pull-up to +5 on B |
| GP10 | host RD (FM data out) | out | U3 |
| GP11 | U3 /OE (host RD + ACK release) | out | none (10k pull-up to 3V3, fitted M6: Hi-Z through reset and SWD flash) |
| GP12 | CA0 | out | U2 |
| GP13 | CA1 | out | U2 |
| GP14 | CA2 | out | U2 |
| GP15 | LSTRB | out | U2 |
| GP16 | SEL | out | U2 |
| GP17 | /ENBL | out | U2 |
| GP18 | /WRTGATE | out | U2 |
| GP19 | WRTDATA (GCR) | out | U2 |
| GP20 | mechanism RD | in | U1 |
| GP21 | U2 /OE (mechanism outputs) | out | none (10k pull-up to 3.3V) |
| GP22 | eject button | in | none (switch to GND, internal pull-up) |
| GP26 | LED | out | transistor, or a U3 spare channel |
| GP27 | spare | - | |
| GP28 | spare | - | |

Notes on the layout:
- PH0-3 sit on GP2-5 and ACK on GP9, the same as PicoPort, so its pin
  assignments carry over. WR and RD moved (PicoPort used GP21 and GP11).
- CA0, CA1, CA2, LSTRB and SEL are contiguous (GP12-16), so one register
  write or one PIO `out` can set the address and strobe together.
- PIO can reach every GPIO on the RP2350A. Each state machine only needs its
  own pins contiguous: FM rx on GP8, FM tx on GP10, GCR write on GP19, GCR
  read on GP20.

## Shifter rule (hard constraint)

Each 8T245 board has ONE DIR and ONE /OE for all 8 channels. Every signal on
a board goes the same direction and is enabled or released together.
Group signals by (direction, when-enabled), never by function.

## Daisy chain (rear D-sub, parity with original)

From the drive firmware (gate array reg0 writes through `Se67c`, value in
the high nibble, mask in the low nibble):

| Firmware | Gate array bit | Meaning |
|---|---|---|
| `$44` once all our IDs are assigned (`Z3d`=0) | PH3EN=1 | Pass PH3 to the downstream drive. Unassigned devices block PH3, which is how SmartPort walks the chain during INIT. |
| `$88` before replying / `$08` after | ENBUS=1/0 | Drive our RD/ACK onto the host bus only while we're answering. = our U3 /OE. |
| `$22` / `$02` | IWMDIR=1/0 | IWM to SmartPort vs to mechanism. We don't need it (separate pins). |
| read `LASTONE/` (reg0 d4) after the last ID is assigned | input | Is there a drive downstream? If not, we flag ourselves last in the INIT reply (`Z5e=$C0`). |

So daisy chain costs us: **PH3_OUT (1 output) + ENBL_OUT (1 output) + LASTONE/
(1 input)**. ENBL_OUT was found by continuity (research_notes sec. 10): rear
pin 17 comes from the CXD1013, not straight from CN102. The other
bus lines (PH0-2, WR, /WRREQ, RD, ACK, power) are probably wired straight
through to the rear connector. **Verify on a dead board:** continuity
CN102 pin <-> rear D-sub pin for every pin, and note which ones go to the
CXD1013 instead.

The internal second mechanism (DRIVE1/DRIVE2, DuoDisk) never shipped. Not
needed for parity, so /ENBL2 is dropped.

## Revised totals

24 (above) + PH3_OUT + ENBL_OUT + LASTONE/ = 27. Debug moves to USB CDC
(frees GP0/GP1) -> **25 of 26 used, 1 spare (GP28).**

| GPIO | Signal | Dir | Shifter |
|---|---|---|---|
| GP0 | PH3_OUT (downstream) | out | U4 |
| GP1 | LASTONE/ | in | U5 |
| GP27 | ENBL_OUT (downstream /ENBL, rear pin 17) | out | U4 |
| GP28 | spare | - | |
| GP2-26 | as in the map above | | |

Pass-through, no GPIO (wires CN102 <-> rear D-sub): GND, -12V, +5V, +12V,
HDSEL (rear 9 / CN102 17), ACK (rear 10 / CN102 20), PH0-2, /WRREQ, RD, WR.

PH3_OUT and ENBL_OUT copy host PH3 and host /ENBL when we enable
pass-through. Do it in a PIO state machine (input -> output copy), not in a
software loop: tens of ns, versus SmartPort timing in us.

## Shifter usage (5 boards)

| Board | DIR | /OE | Channels | Used |
|---|---|---|---|---|
| U1 | B->A | tied low (always on) | PH0, PH1, PH2, PH3, /WRREQ, /HSTEN, host WR, mechanism RD | 8/8 |
| U2 | A->B | GP21 + 10k pull-up (Hi-Z through boot) | CA0, CA1, CA2, LSTRB, SEL, /ENBL, /WRTGATE, WRTDATA | 8/8 |
| U3 | A->B | GP11 (= ENBUS) | host RD | 1/8 |
| U4 | A->B | **GP21 (shared with U2)** | PH3_OUT, ENBL_OUT, LED | 3/8 |
| U5 | B->A | tied low | LASTONE/, eject button | 2/8 |

U4 must be gated: RP2350 pins default low-ish at boot, and a low on ENBL_OUT
would enable the downstream drive. B-side pulls: ENBL_OUT 10k to +5V
(inactive), PH3_OUT 10k to GND, so the chain is quiet while U4 is Hi-Z.

ACK rides U3 with host RD: driven only while U3 /OE (ENBUS) is low, released otherwise, with a 10k pull-up to +5 on the B side (2026-10-11, M6 bench). No 74LVC1G07.
Downstream RD/ACK share the host bus wires directly. Every device releases
them when it's not answering, which is why U3 needs its own /OE.

## Open

1. ~~Rear D-sub pinout~~ Measured, see research_notes sec. 10. Apple handwritten table (research_notes sec. 9): rear
   port = same Liron DB-19 pinout. Remaining check: beep test that PH0-2,
   WR, RD, ACK, /WRREQ, /ENBL and power go straight CN102 -> rear D-sub, and
   that PH3 doesn't (it's gated).
2. ~~LASTONE/~~ Resolved: rear pin 16, 2k pull-up to +5V on our output port;
   our input-side cable grounds pin 16. Our board: 2k pull-up on rear pin 16,
   into U5.
3. Eject button + LED: electrical (CN105-108).
