# Level shifting: RP2350 (3.3 V) <-> host + mechanism (5 V)

Part: TI **SN74LVC8T245** (same as Byte Hamr card.kicad_sch, 7 used there).
Datasheet: `private/datasheets/sn74lvc8t245.pdf` (SCES584B). Copied from
`~/Development/ironsteel-fpga-disk-controller/hardware/datasheets/`.

Only the datasheet came from ironsteel. That repo is a defunct IWM attempt;
don't use it for research. Protocol truth = project_byte_hamr.

Rico's built shifters: 8T245, 8 channels per board (8 A / 8 B), **DIR and OE
both broken out**, fully configurable. So the 3-chip plan below maps straight
onto 3 boards: strap DIR, put OE on MCU GPIO where noted.

## Part behaviour

- One 8-bit section, one DIR, one OE (active low). DIR and OE are referenced
  to VCCA.
- Function table (datasheet Table 1):

| OE | DIR | Operation |
|---|---|---|
| L | L | B -> A |
| L | H | A -> B |
| H | x | both ports Hi-Z |

- Wiring for us: **A side = VCCA = 3.3 V (RP2350), B side = VCCB = 5 V
  (host / mechanism).**
- Ioff spec: outputs Hi-Z when powered down, so unpowered sides don't
  back-feed.

## Timing (datasheet 8.9, VCCA = 3.3 V +/-0.3, VCCB = 5 V +/-0.5)

| Path | Min | Max |
|---|---|---|
| A -> B (tPLH/tPHL) | 0.5 ns | 4.4 ns |
| B -> A | 0.6 ns | 6.0 ns |
| OE -> A Hi-Z (tPHZ/tPLZ) | 1.6 ns | 8.2 ns |
| OE -> B Hi-Z | 0.8 ns | 6.3 ns |
| OE -> A enable (tPZH/tPZL) | 0.8 ns | 8.1 ns |
| OE -> B enable | 0.9 ns | 6.4 ns |

Budget check: smallest timing we care about is the 2 us GCR bit cell (FAST
mode, 489.6 kbit/s). Worst-case skew across shifters ~6 ns = 0.3 % of a cell.
SmartPort FM cells are 4 us. **Shifter delay is irrelevant to protocol
timing.** The only place it might matter: comparing two signals that go
through *different* chips (e.g. host PH lines vs our ACK). Still < 10 ns.

## Do we need to flip DIR? **No, every signal is one-way.**

Naming below is from **our board's** point of view (PicoPort names are from
the Apple's point of view; careful).

### Host side (DB-19 / CN102 "FROM HOST", Liron card pinout)

| Signal | DB-19 pin | Dir (us) | Notes |
|---|---|---|---|
| PH0 | 11 | in | SmartPort phase / REQ (PH0) |
| PH1 | 12 | in | |
| PH2 | 13 | in | |
| PH3 | 14 | in | |
| /WRREQ | 15 | in | host write request |
| /HSTEN (/HstEnbl) | 17 | in | host enable for this drive |
| WR (host WRDATA) | 19 | in | FM data host -> drive |
| RD (host RDDATA) | 18 | **out** | FM data drive -> host |
| WRPROT / ACK | 10 | **out** | SmartPort ACK. Push-pull on U3, released with RD by U3 /OE. |
| +5 / +12 / -12 / GND | 6 / 7,8 / 5 / 1-4 | power | Board + mechanism are powered from here. |

### Mechanism side (CN104, 20-pin, ERS 699-0321 sec. 3.6)

| Signal | Pin | Dir (us) | Notes |
|---|---|---|---|
| CA0 | 2 | out | register select |
| CA1 | 4 | out | |
| CA2 | 6 | out | |
| LSTRB | 8 | out | command strobe, acts on rising edge |
| /WRTGATE (/WRREQ) | 10 | out | write enable |
| SEL (HDSEL) | 12 | out | head / register select |
| /ENBL | 14 | out | drive enable |
| WRTDATA | 18 | out | GCR flux transitions |
| **RD** | 16 | **in** | Read data **and** SENSE. CA0-2 + SEL pick one of 16 status bits; the mechanism puts that bit on RD. During a read it carries flux transitions (falling edge = transition). |
| /PWM | 20 | **tie to +5V** | Measured 2026-10-07: original IF-83 ties CN104 pin 20 straight to +5V. Matches ERS 800K: "if PWM held high, read signal valid". Not a signal, no shifter channel. |
| +5 / +12 / GND | 11 / 13,15,17,19 / 1,3,5,7 | power | |

There is no separate SENSE pin. On the original board, the CXD1013 "SENSE"
pad is the RD line routed to the IWM's sense input.

### Channel count -> 3 x 8T245, fixed DIR

| Chip | DIR | Channels |
|---|---|---|
| U1 | B->A (DIR = L) | PH0, PH1, PH2, PH3, /WRREQ, /HSTEN, host WR, mech RD (8/8) |
| U2 | A->B (DIR = H) | CA0, CA1, CA2, LSTRB, SEL, /ENBL, /WRTGATE, WRTDATA (8/8) |
| U3 | A->B (DIR = H) | host RD, (ACK, see below), spares: front LED, etc. |

DIR is strapped. **OE is the only dynamic control** and only on U3 (and
optionally U2):

- **Host RD must be released when we're not the addressed device**
  (SmartPort bus is shared by every device on the chain). Drive U3 OE from
  the RP2350, enabled only while we transmit. RD is low-rate (4 us cells),
  so the 6.4 ns enable time doesn't matter.
- **ACK rides U3 with RD** (decided 2026-10-11, M6 bench; no 74LVC1G07).
  The original drive does the same: its gate array drives RD and ACK onto
  the bus only while ENBUS is on, and releases both together. Only one
  device answers at a time, so no wired-OR is needed. With U3 /OE high, the
  10k pull-up on the B side holds ACK high ("ready"). Firmware sets ACK and
  RD to the wanted levels before it pulls U3 /OE low (M6 spec sec. 5.3).
- **U2 OE:** hold high (Hi-Z) through RP2350 reset/boot, so the mechanism
  doesn't see random CA/LSTRB pulses (a stray LSTRB could eject the disk or
  step the head). Add a 10k pull-up on OE, then the MCU drives it low once
  the pins are configured. The same idea applies to U3 (keep the host bus
  quiet during boot).

### Pull-ups / idle states

- Mech RD: 10k pull-up on the B side (the IWM has an internal 10k on RDDATA;
  the drive may tri-state RD when /ENBL is high).
- Mech /ENBL, /WRTGATE: 10k pull-ups on the B side, so they stay inactive
  while U2 is Hi-Z.
- Host PH0-3: 10k pull-downs on the B side (read 0 with no host).
- Host /WRREQ, /HSTEN: 10k pull-ups on the B side, so they read inactive with
  no host (IIe off or cable out).
- Host WR: **100k** pull-down on the B side (2026-10-12). The Liron idles WR
  at about 4.0 V through about 1.9k; a 10k pull-down dragged it to 3.40 V,
  under the 8T245's 3.5 V input-high at VCCB = 5 V. With 100k it idles at
  3.96 V. **[measured]**
- Host WRPROT/ACK: 10k pull-up to +5 on the B side. With it, the Liron reads
  ACK as "ready" and starts sending a command packet. **[measured]**
- VCCB (bench +5) at 5.00 V, not higher: U3 drives RD and ACK at VCCB and the
  Liron runs on about 4.73 V.

## Open items

1. ~~Shifter board specifics~~ Resolved: 8 ch, DIR + OE broken out.
2. ~~/PWM pin 20~~ Resolved: tied to +5V on the original board. Copy that.
3. Does the mechanism tri-state RD when /ENBL is high? (ERS 3.4.x RDDATA
   timing; check on a dead-board drive.)
4. Daisy-chain out (rear D-sub): pass-through of PH/WR/RD/enable to a second
   drive. Out of scope for the first prototype.
5. Front LED + eject button wiring (CN105-108 on IF-83).
