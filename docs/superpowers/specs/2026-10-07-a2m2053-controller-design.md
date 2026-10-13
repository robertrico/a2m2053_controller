# A2M2053 (UniDisk 3.5) replacement controller: design

Status: **approved** by Rico on 2026-10-07 (written spec).

## 0. Goal and constraints

- Replace the dead controller board (IF-83, P/N 661-0307) in an Apple
  UniDisk 3.5. The new board talks SmartPort to the host (Liron card or IIc)
  and drives the stock Sony 800K mechanism directly.
- This is not a period-correct clone. The firmware is our own C and ARM code.
  The original drive ROM (`liron.asm`, Eric Smith disassembly) serves as an
  **oracle**: we check our sequencing, status, errors and timing against it,
  and match its behaviour at the protocol level only.
- Phase 1: perfboard prototype on a Pico 2 (RP2350A) with Rico's 8T245
  shifter boards. Phase 2: a low-cost, hand-solderable drop-in PCB (the
  only spare board on eBay has sat unsold at $150 for 7 months). PCB
  requirements and part choices: `FABRICATION.md`.
- Full parity for **Apple II use**: read, write, format, eject, LED, daisy
  chain out the rear port. ProDOS/Apple II 800K disks.
- **Out of scope: Macintosh disks** (524-byte blocks, Mac interleave). This
  is an Apple IIe drive. Apple's Tech Note UniDisk #4 describes Mac access,
  but we don't build or test it.
- **Out of scope: 400K single-sided disks.** Never used on the Apple IIe.
  800K double-sided only. (The ROM keeps single-sided format code, a
  Mac-compatibility leftover. Ignore it.)
- Move to an FPGA only if RP2350 timing fails.
- Test bench: 2 drives with dead boards and good mechanisms (probe-safe).
  The 1 working drive is **never probed**.

Reference docs: `docs/research_notes.md`, `docs/level_shifting.md`,
`docs/pin_budget.md`, `docs/iwm_docs_index.md`, `docs/private/bitsavers_sony/`.

## 1. Overall structure

```
 Host (Liron card)                                     Sony mechanism
   | CN102                                                  | CN104
 [U1 in] --> PIO0: SmartPort FM rx/tx -+       +- PIO1: GCR flux rx/tx <-> [U1 in / U2 out]
 [U3 out] <--   + daisy-chain copy SM  |       |   + CA/SEL/LSTRB via SIO
 [U3 ACK]                              v       v
                         CORE 0: SmartPort      CORE 1: mechanism server
                         - bus phases, packets  - status reads (CA/SEL -> RD)
                         - command dispatch     - seek / zone / motor / eject
                         - ID chain, LASTONE    - track buffer (one side in RAM)
                         - DIB / status         - GCR decode/encode, sector write
                                 |   block request queue    ^
                                 +--------------------------+
 [U4 out] PH3_OUT, ENBL_OUT, LED     [U5 in] LASTONE/, eject button
```

- **PIO0 (host):** SmartPort FM receive/transmit, ported from PicoPort
  (`project_byte_hamr/software/SMARTPORT`, proven on RP2040). Plus one
  state machine that copies host PH3 and /ENBL to the rear port while
  pass-through is enabled.
- **PIO1 (mechanism):** flux interval capture on RD (DMA into a ring buffer)
  and flux transmit on WRTDATA (DMA'd timing stream).
- **Core 0:** SmartPort device. No floppy knowledge. Asks core 1 for
  block N and gets back 512 or 524 bytes.
- **Core 1:** owns the mechanism. Keeps a whole track side in a RAM buffer
  (12 sectors max, about 6.5 KB). Writes are single-sector
  read-modify-write.
- **Queue between the cores:** keeps mechanism latency (seek plus zone change,
  up to about 150 ms) out of the bus timing code. Core 0 holds off the host
  with the SmartPort ACK handshake while core 1 works.

Pins and shifters: `docs/pin_budget.md` (25 of 26 GPIO, 5x 8T245; ACK
rides U3 with RD, released with it by U3 /OE).

### Risks carried

1. ~~**How long the host waits for ACK.**~~ Resolved (M6/M7): the card
   gives about 105 us for the ACK after its write underrun (ours: 4-7 us),
   and waits for the reply with no timeout, so seeks fit (M7 bench: up to
   844 ms). `docs/research_notes.md` "SmartPort handshake from the two ROMs".
2. ~~**PicoPort `sp_asm.S` timing.**~~ Moot: the M6 bus code is new; bit
   timing is in PIO and the hot path runs from RAM (M6 spec).

## 2. Mechanism driver (core 1 + PIO1)

Command/status names are from `private/bitsavers_sony/Apple_drive_command_and_status_codes.pdf`.
Mechanism connector: ERS 699-0321 sec. 3.6 (`docs/level_shifting.md`).

### 2.1 Mechanism access (software, SIO)

- **Status read:** /ENBL low. Set CA0-2 and SEL. Wait >= 5 us. Sample RD.
  (ROM oracle: `Se640` selects, then reads the IWM status about 2-4 us later
  at 2 MHz.)
- **Command:** set CA0-2 and SEL. LSTRB high for >= 2 us, then low. The command
  acts on the rising edge (ERS 3.2.3). (ROM oracle: `Se64a`, ph3 on/off.)
- CA0, CA1, CA2, LSTRB and SEL sit on contiguous GPIO (GP12-16), so one write
  sets them all.

### 2.2 Seek

- Software tracks the current track. Recalibrate by stepping outward until
  the track-0 status bit (rNotTrack0Adr) reads low.
- Move: set the direction (wDirNext/wDirPrev), send step (wStepOnAdr), poll
  the step-complete status (rStepOffAdr) rather than waiting a fixed delay.
- After any move that changes speed zone, wait for READY (rNotReadyAdr). ERS:
  36 ms per step without a zone change, 152 ms max with one.
- Head select = SEL.

### 2.3 Read (track buffer)

- PIO1 measures the time between flux transitions on RD (falling edge =
  transition) at a high sample clock and DMA's the intervals into a ring buffer.
- Software sorts each interval into 1, 2 or 3 bit cells (nominal 2.04 us at
  489.6 kbit/s) with an adaptive window that tracks speed drift.
  Reassembles GCR bytes.
- Sector header `D5 AA 96` + track, sector, side, format, checksum + `DE AA`.
  Data `D5 AA AD` + sector + 12 tag + 512 data bytes (6-and-2 encoding) +
  checksum + `DE AA`.
- Fill the track buffer in one revolution when possible. A bad checksum
  retries on the next revolution.
- CPU budget: at most one transition per 2 us, about 300 M33 cycles each.

### 2.4 Write (per sector)

- Decode the flux live until the target sector's header passes.
- After the header's `DE AA` and the gap, assert /WRTGATE. PIO1 streams the
  encoded data field from a DMA buffer to WRTDATA. Deassert /WRTGATE.
- 3.5" GCR has no index pulse, so normal writes are per sector, never a
  whole-track rewrite.
- Verify by reading back on the next revolution.

### 2.5 Format

- Write every sector (header + data) of each track. Zone sector counts are
  12/11/10/9/8 (tracks 0-15 / 16-31 / 32-47 / 48-63 / 64-79).
- Default: 4:1 interleave, double-sided (ROM zero-page defaults `$62`=`$04`,
  `$63`=`$80`, set on INIT).

### 2.6 Housekeeping

- **Disk in / swap:** poll rNoDiskInPlAdr. On a swap, invalidate the track
  buffer and report it to the host the way the ROM does (oracle: error code and
  which call reports it).
- **Write protect:** rNoWrProtectAdr. Writes return `$2B`.
- **Eject button:** U5 input, latched in software (the original gate array
  had a button latch per drive, BLATCH1/2). Eject only when idle (motor off,
  LED off), as the original does: wEjectOnAdr, about 1.5 s. Then clear the
  latch (wNoDiskInPlAdr).
- **Motor and LED:** motor on per request (wMotorOnAdr). Off after an idle
  timeout matching the ROM. The LED follows the motor ("in use").
- **Errors:** retry the read for N revolutions, then recalibrate and retry,
  then return `$27` (I/O error). N and the escalation order come from the ROM.

### 2.7 Bench items (dead-board drive)

- Status settle time after a CA/SEL change.
- Real step timing and step-complete behaviour.
- Whether RD is stable or tri-stated while /ENBL is high.

## 3. SmartPort side and daisy chain (core 0 + PIO0)

Approved 2026-10-07.

Base: PicoPort bus loop, FM PIO programs, packet encode/decode and command
dispatch, ported to the RP2350. Its SD-card back end is replaced by the block
request queue to core 1.

### 3.1 Commands (ROM `cmd_tab`)

| Code | Command | Behaviour |
|---|---|---|
| $00 | STATUS | sec. 3.2 |
| $01 | READBLOCK | 512 bytes |
| $02 | WRITEBLOCK | 512 bytes |
| $03 | FORMAT | 800K, 4:1 interleave (sec. 2.5) |
| $04 | CONTROL | sec. 3.3 |
| $05 | INIT | ID assignment, sec. 3.5 |
| $06, $07 | Open, Close | bad command (`$01`), as the ROM does |
| $08, $09 | READ, WRITE | **v1: bad command (`$01`).** In the ROM these move 524-byte blocks for Mac disks, which are out of scope. |

### 3.2 STATUS codes (ROM `status_tab`)

| Code | Returns |
|---|---|
| $00 | General status: block device, write allowed, read allowed, online, write-protect bit (Tech Note UniDisk #3 layout) + block count |
| $03 | DIB, copied from the ROM (`Ded81`): name length 8, "DISK 3.5" padded with spaces to 16, type `$01`, subtype `$00`, version `$00 $10` |
| $05 | UniDisk status (`Led96`). Exact contents to be transcribed from the ROM. |
| $01, $02, $04 | error `$21` |

PicoPort's DIB currently says "PICOPORT", type `$02`. It must change.

### 3.3 CONTROL codes (ROM `control_tab`)

| Code | Behaviour |
|---|---|
| $00 | reset device |
| $04 | eject (sec. 2.6) |
| $05 | execute: error. We don't run downloaded 65C02 code (later emulator option). |
| $06, $07 | set download address, download: **v1: error `$21`.** Only used for downloaded code and Mac format setup. |
| $01-$03 | error `$21` |

### 3.4 Holding off the host

On a read, write or format, core 0 posts the request to core 1 and holds the
host off with the normal SmartPort ACK handshake until the result is ready.
Budget:

- Measure the Liron card ROM's ACK wait (`liron-if.asm`, around `Lc938`).
- Bound our own worst case: recalibrate + seek + zone change + retries.
- If the worst case can exceed the host's wait, answer with `$27` (I/O
  error) before the host times out. Never let the host time out.

### 3.5 Daisy chain

Model of the ROM's gate-array use (PH3EN, ENBUS, LASTONE/):

- **Bus reset** (SmartPort reset state on the PH lines): clear our ID.
  Pass-through off: PH3_OUT held low, ENBL_OUT held high.
- **INIT while we have no ID:** take the destination ID. Read LASTONE/
  (rear pin 16; high = nothing behind us). If we're last, the INIT reply says
  so (ROM sets `$C0`). Then turn pass-through on.
- **Pass-through on:** a PIO0 state machine copies host PH3 -> PH3_OUT and
  host /ENBL -> ENBL_OUT.
- **INIT once we have an ID:** it's for a drive behind us. Ignore it.
- **Bus drive:** we drive RD and ACK (both on U3, gated by U3 /OE) only while
  answering a packet addressed to us. This is the ROM's ENBUS on (`$88`)
  and off (`$08`).
- **Unverified (bench, with a real drive behind us):** that /ENBL
  pass-through follows the same rule as PH3. The continuity check shows
  rear pin 17 comes from the CXD1013 (pins 12 and 13), but not the logic.

## 4. Bring-up and test plan

### 4.1 Oracle

The working UniDisk is used **normally only**: cabled to the Apple, or
cabled behind our board. Never opened, never probed.

- A small Apple II test program sends identical SmartPort calls to both
  drives (STATUS `$00`/`$03`/`$05`, read, write, error cases: no disk,
  write-protected, bad block) and dumps the replies side by side.
- Reference disks are written on the working UniDisk and imaged (ADTPro or a
  ProDOS copy). Our board must read them back byte for byte.
- `liron.asm` answers what a test can't: retry counts, timeouts, the motor
  idle time, the `$05` status layout.

### 4.2 Milestones

| # | Milestone | Pass |
|---|---|---|
| M0 | Bench rig: Pico 2 + 5x 8T245 + mechanism on a current-limited +5/+12 bench supply | Rails correct. Shifter /OE high through RP2350 boot (scope). |
| M1 | Mechanism status reads | All 16 status bits read sensibly; disk-in and write-protect follow when a disk is inserted or the tab flipped; settle time measured |
| M2 | Mechanism commands | Motor on + READY; tach pulses; recalibrate to track 0; step + step-complete; eject |
| M3 | Raw flux capture | Every track side dumped over USB, decoded on the dev computer in Python; all 1600 sector headers and checksums good on a reference disk |
| M4 | Reading on the board | GCR decode on core 1; the whole disk read over a USB test command matches the reference image byte for byte |
| M5 | Write + format | Sector write + verify on a scratch disk; a disk formatted by our board reads correctly in the working UniDisk |
| M6 | SmartPort on the RP2350 (alongside M1-M5) | PicoPort ported, serving a RAM disk; ProDOS boots through the Liron card; `sp_asm` timing retuned |
| M7 | Integration | ProDOS boots from a real 800K floppy through our board; comparison program matches the working UniDisk; worst-case ACK wait measured against the host timeout |
| M8 | Daisy chain | Our board in front of the working UniDisk, then behind it. INIT assigns both, ProDOS sees both drives, /ENBL pass-through rule confirmed |
| M9 | Robustness | Disk swap mid-session, write-protected disk, empty drive, long copy loops |

### 4.3 Tools

- Python host tool over USB CDC: flux dumps, offline decoding, test runner.
- Logic Hamr / sigrok: PH, CA, LSTRB, RD and WR lines.
- Scope (oscope MCP): timing.

