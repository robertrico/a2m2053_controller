# M6: SmartPort on the RP2350, serving a RAM disk (design)

Status: implemented and passed on the bench 2026-10-12 (`docs/build_log/m6.md`).
Written without the owner in the loop (the brief was
spec, plan and build in one go). Design choices that are the owner's to overrule
are marked **[choice]**. The brief: functionality, timing, "does it
work"; the bench is the final test.

Parent spec: `2026-10-07-a2m2053-controller-design.md` (sec. 1, 3, 4.2).
Bench facts: `docs/build_log/m6.md`, `docs/research_notes.md` "Host bus on
the Liron card (M6)".

## 1. Goal

The board answers the Liron card as a SmartPort device and serves an 800K
ProDOS volume from flash with a RAM write overlay. The mechanism is not
used.

**Pass (parent spec M6):**
1. `PR#5` (or a cold boot with the card in slot 5) boots ProDOS from the
   board through the Liron card.
2. The volume catalogs; a file saved from BASIC reads back until power off.
3. Scope: /WRREQ rise (packet end) to ACK fall measured, with margin against
   the card's ACK window (sec. 4).

"`sp_asm` timing retuned" in the parent spec is replaced: no
cycle-counted assembly. Bit timing lives in PIO; the CPU only has the
deadlines in sec. 4.

Out of scope: the floppy back end (M7), daisy chain pass-through and
LASTONE/ wiring (M8; INIT still reads LASTONE/), eject on CONTROL `$04`,
write-protect, disk swap, persistence of RAM writes across power off.

## 2. Sources and how they are used

- `liron-if.asm` (card ROM): the **host** side. Every host timeout below is
  counted from it. Cycle counts are 65C02 at 1.023 MHz.
- `liron.asm` (drive ROM): the **device** side, as oracle for order of
  handshake steps, packet layout, status bytes, error codes, INIT.
- PicoPort (`project_byte_hamr/software/SMARTPORT`): proven packet
  encoder layout and the group-of-7 convention. Not its bus loop: it ran
  behind an FPGA IWM, ignored RX checksums and patched bit slips. Our host
  is a real IWM.

## 3. Bus protocol (from the two ROMs)

Signals at our pins: PH0-3, /WRREQ, WR in (U1); RD, ACK out (U3, both gated
by U3 /OE = ENBUS). ACK has a 10k pull-up on the B side, so with ENBUS off
the host reads ACK high. In the card ROM, ACK is IWM SENSE (status bit 7):
1 = deasserted ("ready"), 0 = asserted.

Bus states (PH3..PH0): `1010` idle/enabled, `1011` REQ, `0101` reset. REQ = PH0.

### 3.1 Host sends a packet (card `smartport_bus_write_packet`)

1. Bus enable (PH1, PH3), IWM mode `$07` (slow: 4 us cells), motor on.
2. Wait for ACK high, max 50 polls x 11 cycles = **about 540 us**. Fail ->
   the whole send is retried (3000x for commands and data, 1x for INIT).
3. REQ high. Write `3F CF F3 FC FF C3` (sync, PBEGIN), then header, data,
   checksum, `C8`. Wait for write underrun (/WRREQ goes high).
4. Wait for ACK low: max 9 polls x 12 cycles after underrun = **about
   105 us**. Fail -> REQ low, retry from 1.
5. REQ low.

### 3.2 Host reads a packet (card `smartport_bus_read_packet`)

1. Wait for ACK high, **no timeout**. This is how a device holds the host
   off while it works.
2. REQ high. Read nibbles; a `C3` must come within 30 nibbles. Nibble waits
   themselves have no timeout.
3. Read header, data, checksum, `C8`. Bad checksum or `C8` missing -> error
   with REQ still high; the card retries 5x (1 ms pause, REQ low, back to 1).
4. Good packet: wait for ACK low (no timeout), then REQ low.

### 3.3 Device side (drive ROM, in order)

Receive (`Sea29`): ENBUS off. Wait for REQ high. Hunt `C3`. If the
destination is not ours and we have an ID, ignore (wait REQ low). Without an
ID accept any destination (INIT). Wrong packet type (`$80` command, `$82`
data) -> ignore. Read the rest; bad checksum or no `C8` -> ignore (no ACK, so
the host retries). Good: **ACK low, then ENBUS on**, wait REQ low.

Command (`vector`): runs with ACK low (host held off at 3.2 step 1).
WRITEBLOCK and CONTROL first receive a data packet: ENBUS off (ACK goes high
via the pull-up), receive as above with type `$82`.

Reply (`spb_send_packet`): ACK high (driven), wait REQ high (ROM: about
0.5 s, then give up). Send `FF` + `3F CF F3 FC FF 3F CF F3 FC FF C3` + header
+ data + checksum + `C8`. ACK low. Wait REQ low (ROM: about 1 ms, else send
again). Then ENBUS off before the next receive.

Bus reset (`0101`): the original drive is reset by hardware (RSTCAP). We
treat it as: abort, ENBUS off, ID cleared.

### 3.4 Wire format

FM, 4 us class cells (Liron IWM: 28 FCLK at 7.159 MHz = **3.911 us**). A 1 is
a level change on WR (host -> us). On RD (us -> host) a 1 is a **falling
edge**: the IWM spec counts falling transitions only ("a falling transition
within a bit cell window is considered to be a one"; RDDATA high and low time
>= 300 ns). We send a 1 as a 1 us low pulse from an idle-high line.

Byte framing is the IWM's: skip 0 bits until a 1, then take 8 bits. The
sync bytes collapse to `FF`s and `C3` frames the packet. Every byte after
`C3` has bit 7 set.

Packet after `C3`: DEST, SRC, TYPE, AUX, STAT, ODDCNT, GRP7CNT (all with bit 7
set), then if ODDCNT > 0 an odd-MSB byte and the odd bytes, then per group an
MSB byte and 7 bytes, then checksum `c | $AA`, `(c >> 1) | $AA`, then `C8`.
The MSB of data byte i of a run sits in bit 6-i of its MSB byte. Checksum c =
XOR of the seven header wire bytes XOR every decoded data byte. Decode:
`((ck2 << 1) | 1) & ck1`.

Command packet data (9 bytes = 2 odd + 1 group): `cmd, pcount, p0..p6`.
p2 = status or control code = block low byte; p3, p4 = block mid, high.

## 4. Timing budget

| Deadline | Host limit | Our design | Margin |
|---|---|---|---|
| ACK high before host sends | 540 us (then retry) | ENBUS off at idle: ACK always high | n/a |
| First WR edge after REQ high | none for us: PIO arms on /WRREQ low | RX SM waits on /WRREQ itself; DMA catches every bit | no CPU latency |
| Packet end to ACK low | about 105 us after underrun | fm_rx starts on the first edge (FF's MSB) and every byte is 8 cells, so `C8` always closes a DMA chunk, pushed half a cell **before** the host's last cell ends; then decode + checksum on the CPU (< 10 us), IRQs off. ACK lands about at the underrun (simulated: 1 us before) | **about 100 us** [model; scope it] |
| Reply start after REQ high | none (host loops on nibbles) | 20 us pause after REQ high, then TX (PicoPort: the card flips Q6 about 6 us after REQ; edges before that are lost) | n/a |
| Host holds REQ after reply | none | wait up to 50 ms, release; a card re-read (REQ high, no write, 200 us) gets the reply again (ROM: re-sends after about 1 ms) | n/a |
| Late packet (main loop stalled) | card retries after 105 us | ACK only while REQ is high and less than 90 us after the seen underrun; otherwise no ACK, the card's retry is served | n/a |

The critical number is the second-to-last row. Bench check: CH1 /WRREQ,
CH2 ACK (J1.20), trigger /WRREQ rising, `measure_delay` rise to fall.

## 5. Structure

```
core 0                                        PIO0
  main loop: sp_bus_poll() + console poll       SM0 fm_rx: WR -> cell bits -> DMA -> rx buffer
  sp_bus  (handshake engine, timeouts)          SM1 fm_tx: DMA -> bits -> RD pulses
  sp_dev  (commands, status, INIT, ID)
  sp_wire (framing, decode, encode, checksum)  GPIO: ACK (GP9), U3 /OE (GP11), PH0-3, /WRREQ
  ramdisk (flash image + RAM overlay)
core 1: mechanism server (M5), idle in M6
```

| Unit | Files | Job | Host-tested |
|---|---|---|---|
| sp_wire | `sp_wire.c/.h` | IWM framer over a bit stream, packet decode with checksum, packet encode | yes |
| sp_dev | `sp_dev.c/.h` | command dispatch, STATUS/DIB/`$05`, INIT and ID, error codes, which commands take a data packet | yes |
| ramdisk | `ramdisk.c/.h` | base image (may be absent) + overlay of written blocks, FORMAT | yes |
| sp_bus | `sp_bus.c/.h` | sec. 3.3 sequencing over a HAL; timeouts; bus reset; re-send on re-read | yes, against a simulated card (sec. 7) |
| sp_hw | `sp_hw.c`, `sp_fm.pio` | HAL on the RP2350: PIO RX/TX + DMA, ACK, ENBUS, phases | PIO programs by a cycle model in `tools/` |
| image | `sp_image.S` | `.incbin` of the ProDOS image into flash | build |

### 5.1 PIO receive (`fm_rx`)

Cell-synchronous decoder, PIO clock 48 cycles per cell (clkdiv 12.22 at
150 MHz). The SM waits for /WRREQ low (`wait 0 gpio 6`), reads the WR level
and waits for the first edge. From each edge it polls WR in 3-cycle loops:
an edge before 1.5 cells pushes a 1 and restarts the window; no edge by 1.5
cells pushes a 0, then a 0 per further cell. Autopush every 8 bits; DMA
copies each push byte into a linear buffer. The CPU re-arms the SM and DMA
at the end of every transaction (SM back at the /WRREQ wait, FIFO and
buffer cleared).

Blanking: WR is not polled for 6 cycles (0.49 us) after an edge, so ringing or
a slow threshold crossing is not a second edge (the loop counts are cut to
keep 72 and 48 cycles).

Tolerance: an edge-to-edge gap reads as n cells anywhere in (n - 0.5,
n + 0.5) cells, i.e. +-1.95 us between consecutive edges, so +-0.97 us per
edge. The host IWM's edges come off its crystal (ns jitter). Edge detection
jitter is 1 loop (0.25 us).

### 5.2 PIO transmit (`fm_tx`)

32 cycles per cell (clkdiv 18.33). CPU puts the bit count - 1 in the FIFO,
DMA feeds bytes MSB first. A 1 drives RD low for 8 cycles (0.98 us), then
high. A 0 keeps RD high. RD idles high and parks high. The same pattern as
M5's `flux_tx`.

### 5.3 ACK and ENBUS

ACK (GP9) and RD (GP10) are push-pull through U3; U3 /OE (GP11) is ENBUS.
Before ENBUS goes on, ACK and RD are set to the level wanted on the bus
(ROM order: ACK low, then ENBUS on). `hw_init_safe` idles RD high
(was low in M0-M5; harmless then because U3 stayed off).

## 6. Device behaviour (`sp_dev`, from the drive ROM)

**[choice]** Follow the drive ROM where it is cheap; where the ROM depends
on unwritten Apple behaviour (e.g. ignoring a CONTROL data packet for codes
>= 8) we always take the safe path.

| Cmd | Data packet from host | Reply |
|---|---|---|
| `$00` STATUS | no | code `$00`: 4 bytes `[gen, $40, $06, $00]` (1600 blocks); gen `$F8` online R/W/format (ROM: `$E8` no disk, `$FC` write-protected). `$03`: those 4 + DIB `08 "DISK 3.5" + 8 spaces, $01 $00 $00 $10` (25 bytes). `$05`: 8 bytes, all zero for the RAM disk. Other codes: `$21`. |
| `$01` READBLOCK | no | 512 bytes, or `$2D` if block >= 1600 |
| `$02` WRITEBLOCK | yes, 512 bytes | `$00`, `$2D` bad block, `$27` overlay full |
| `$03` FORMAT | no | `$00`; every block reads zero afterwards |
| `$04` CONTROL | yes, any length up to 895 bytes (format limit: 127 groups + 6) | `$00` for code `$00` (reset) and `$04` (eject: no-op in M6); `$21` otherwise |
| `$05` INIT | no | sec. 6.1 |
| `$06`, `$07`, `$08` | no | `$01` |
| `$09` WRITE | yes | `$01` (data swallowed) |
| >= `$0A` | no | `$01` |

Parameter count checked first, as the ROM table: `$03 $03 $03 $01 $03 $02
$01 $01 $04 $04` for `$00-$09`; mismatch -> `$04` (data packet still
swallowed for `$02`, `$04`, `$09`).

Reply packet: type `$81` for every reply, data or not (the ROM does this;
the card does not check the type). STAT = `$80 | code`. SRC = our ID.

### 6.1 INIT and ID

- No ID: INIT to any destination is accepted (the card walks the chain) and
  takes the destination as our ID.
  LASTONE/ (GP1 via U5, fitted 2026-10-15; internal pull-up as a fallback) high -> status
  `$40` ("last device"), so the card stops its INIT loop. Then pass-through
  would turn on (ROM `$44`); in M6 it only sets a flag shown by `sp`.
- No ID and not INIT: ignore (no ACK). The ROM answers these; we do not,
  so a board reset under a running IIe can never answer for a drive behind
  it (M8). The card recovers by itself: 3000 failed tries, then bus reset
  and INIT.
- ID set: only packets to our ID; INIT to our ID replies `$00` and keeps
  the ID.
- Bus reset clears the ID.

### 6.2 RAM disk **[choice]**

- Base: an 800K ProDOS image linked into flash (`.incbin`). CMake variable
  `A2M_DISK_IMAGE`, default `docs/private/ref/m6_boot800k.po` (git-ignored,
  never committed: it holds Apple's ProDOS). Built from
  `ProDOS_2_4_1.po` with AppleCommander (`mkprodos` skill, `-f` copies the
  boot blocks and system files). Without the file the build still links and
  the disk reads as zeros.
- Overlay: 384 RAM slots of 512 bytes (192 KB, fits beside M5's 258 KB of
  static RAM). A block map points written blocks to slots. Full -> `$27`.
  Lost at power off. FORMAT clears the map and hides the base image.
- An image shorter than 800K reads zero past its end; longer is cut.

## 7. Tests (host, before any bench)

- `test_sp_wire`: framer on the measured PR#5 start (`FF 3F CF F3 FC FF C3
  81`) with leading junk; encode/decode round trip for 0, 4, 9, 25, 512
  bytes; checksum against a hand-built packet from the card ROM algorithm;
  corrupted checksum and missing `C8` rejected.
- `test_sp_dev`: every row of sec. 6, param counts, INIT with LASTONE/ high
  and low, ID filtering, bus reset.
- `test_ramdisk`: base, overlay, full, FORMAT, short image.
- `test_sp_bus`: a simulated Liron card stepped in simulated time, following
  sec. 3.1/3.2 with its timeouts: boot sequence (reset, INIT, STATUS, READ
  block 0), WRITEBLOCK with its data packet, a corrupted command (no ACK,
  card retries), packet to another ID, bus reset mid-transaction, the
  ACK-low deadline (fails the test if ACK comes later than 105 us after
  /WRREQ rises), a re-read after a corrupted reply.
- `tools/pio_model.py` + `tools/tests/test_fm_pio.py`: executes the
  assembled `fm_rx`/`fm_tx` instruction words cycle by cycle. RX: WR edge
  trains with +-0.8 us jitter per edge and +-1 % clock error decode to the
  sent bits; the window edges sit at 1.5, 2.5, 3.5 cells.
  TX: cell period 3.911 us, low pulse 0.98 us, idle and park high.

## 8. Console

- `sp` prints: enabled, ID, last-device flag, counters (packets, ACKs,
  checksum errors, ignored, resets, INITs, per-command), overlay slots used,
  image size, and the host input levels (PH0-3, /WRREQ, /HSTEN, WR,
  LASTONE/).
- `sp on|off`: service the bus or stay off it (ENBUS off). On at boot.
- `splog on|off`: one line per transaction (command, block, status,
  ACK latency in us), printed only between transactions. On at boot.
- M5 mechanism commands stay. While one runs, the bus is not serviced; the
  card retries or reports no device.

## 9. Bench plan

Setup as at the end of 2026-10-12: J1 wired, bench +5 at 5.00 V, mechanism
ribbon off, U3 present with the ACK pull-up.

1. Build with the image, flash (`make flash`). Console: `sp` shows ID 0,
   inputs idle (PH1+PH3 high).
2. IIe on with the card in slot 5. Expect INIT in the log (ID 1, status
   `$40`), STATUS, then READ block 0, 1, 2... and the ProDOS banner.
3. Scope: CH1 /WRREQ (J1.10), CH2 ACK (J1.20), CH3 RD (J1.16). Trigger CH1
   rising. Measure packet end to ACK low; capture a reply on RD (1 us pulses,
   3.91 us cells).
4. BASIC: `CATALOG`, `SAVE HELLO`, `CATALOG`, `LOAD HELLO`. `sp` shows one
   overlay slot used per written block.
5. Fail paths worth a look if time allows: unplug the Pico USB while the IIe
   runs (card should report I/O or no device, never a hung bus), `sp off`
   then `PR#5` (no device).

## 10. Risks

1. **ACK latency** (sec. 4): the model says ACK comes within a few us of the
   underrun. If the scope shows more than about 50 us, look for CPU stalls
   (flash XIP misses, IRQs) between DMA and the ACK write.
2. **Reply not seen by the card**: the 20 us pause and the preamble length
   are from PicoPort and the drive ROM; if the card never finds `C3`, scope RD
   against REQ and adjust the pause.
3. **WR idle margin**: WR idles at 3.96 V against a 3.5 V threshold. PIO only
   looks at WR after /WRREQ falls, so idle noise does not create packets.
4. **Pico boot after the IIe**: the card sends INIT once, about 90 ms into
   its boot. Power the Pico (USB) before the IIe, or `PR#5` after.
5. **USB IRQs**: IRQs are off from the first received bit to ACK (up to
   about 20 ms for a 512-byte data packet). USB tolerates this.
