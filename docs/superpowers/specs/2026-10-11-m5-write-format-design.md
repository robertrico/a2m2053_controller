# M5: writing and formatting on the board (design)

Status: draft for review, 2026-10-11.
Parent spec: `2026-10-07-a2m2053-controller-design.md` (sections 2.4, 2.5,
2.6, 4.2). Builds on M4 (`2026-10-10-m4-board-read-design.md`,
`docs/build_log/m4.md`).

## 1. Goal

The RP2350 writes 800K GCR itself: one block at a time (find the header,
write the data field, verify), and a whole-disk low-level format.

**Pass (parent spec M5):**
- Sector write + verify on a scratch disk: after one `writeblk`, a
  `readdisk` differs from the pre-write snapshot in the target block only.
- A disk formatted by our board, then filled with REF800K's blocks by our
  board, boots or catalogs in the working UniDisk like REF800K.

What carries forward: `REQ_WRITE` and `REQ_FORMAT_TS` are the requests the
SmartPort WRITE BLOCK and FORMAT commands (M6/M7) will call. A whole-disk
format is 160 `REQ_FORMAT_TS` requests issued by core 0. M5 drives them
from the USB console.

Scope of "format" (decision 2026-10-11): low-level only, as the drive ROM's
`cmd_format_actual`. ProDOS (and DOS 3.3 via ADTPro) volume structures are
written by the host tool afterwards as ordinary block writes. The firmware
never builds a ProDOS volume.

Out of scope: SmartPort (M6/M7), eject on format failure (compare with the
oracle in M7), disk-swap reporting (M9), multi-block or whole-track write
batching.

## 2. Constraints

- The mechanism records only when /WRTGATE=0, /ENBL=0, /READY=0 and the disk
  is not write-protected (ERS86 sec. 3.2.5). A /WRTGATE stuck low erases the
  track under the head, so gate control is fenced (section 3, "Safety").
- Never write REF800K. It stays write-protected (tab); the drive refuses to
  record on a protected disk even if the firmware has a bug.
- No write precompensation. None of the docs mention it and the ROM applies
  none (IWM mode `$1F`, no write-related drive command).
- Practical over period-correct: the ROM is the oracle for layout, byte
  counts, retry counts and error codes. We do not copy its structure.
- Every wait has a timeout (M4 rule).
- Bench power rule unchanged: `oe off` and mechanism supply off before any
  Pico USB unplug or replug.

### Electrical and timing facts used

| Fact | Value | Source |
|---|---|---|
| WRTDATA encoding | each level change writes a flux transition; a 1 bit = toggle, 0 = no toggle; idle high | ERS86 p21 sec. 3.2.6; IWM spec |
| Cell | 2.0425 us (489.6 kbit/s +-0.1 %), same in every zone (drive changes RPM) | ERS86 sec. 2.3.2; `Mac_v_IIgs_sectors.pdf` p1 |
| /READY low before /WRTGATE low (T1) | >= 0.5 us | ERS86 p29 sec. 3.4.5.1 |
| /WRTGATE low to first WRTDATA edge (T3) | >= 1.8 us | ERS86 p29 |
| Last WRTDATA edge to /WRTGATE high (T4) | >= 2 us | ERS86 p29 |
| /ENBL low after /WRTGATE high (T5) | >= 0.5 us | ERS86 p29 |
| Erase tail after /WRTGATE high (T7) | 480-590 us | ERS86 p29 |
| SEL settled before /WRTGATE low | >= 100 us | ERS86 p24 (T6) |
| RD valid / /STEP allowed after /WRTGATE high | >= 620 us | ERS86 p24, p27 |
| Edge rise/fall | <= 50 ns | ERS86 p30 sec. 3.4.5.2 |
| WRTDATA, /WRTGATE pull-ups in the drive | 3.3k to +5 V (ours: 10k on the shifter B side) | ERS86 sec. 3.3.2 |

## 3. Structure

```
core 0 (console; SmartPort later)            core 1 (mechanism server)
  writeblk N / format  --req queue-->   REQ_WRITE: encode -> load TX
           <--reply queue-- status           seek, SEL, arm decoder (batch 1)
                                             on_header(target) -> fire TX
                                             wait done + 620 us -> verify read
                                           REQ_FORMAT_TS: one track side
                                             build image -> fire TX -> verify
  PIO1 SM0 flux_rx (M4)  RD -> DMA ring -> flux_dec
  PIO1 SM1 flux_tx (new) DMA -> TX FIFO -> WRTDATA, /WRTGATE (set pin)
```

### Units

| Unit | Files | Job | Host-tested |
|---|---|---|---|
| flux_tx | `src/flux_tx.pio`, `src/flux_tx.c/.h` | TX state machine on PIO1 SM1 plus one DMA channel. Acts as the IWM's write side: shifts bytes MSB first, one bit per cell, toggles WRTDATA on each 1; /WRTGATE by a `set pins` instruction (side-set would leave 3 delay bits, too few for the 32-cycle bit loop). Only file allowed to touch GP18. | no (hardware); timing on the scope |
| wtrack | `src/wtrack.c/.h` | Pure builders of the byte images the TX streams: one data field, one whole track side. Uses `gcr_encode_address` / `gcr_encode_data`. | yes |
| flux_dec | `src/flux_dec.c/.h` (existing) | New `header` sink callback, called at the header's `DE AA`. | yes |
| mech_server | `src/mech_server.c/.h` (existing) | New `REQ_WRITE` and `REQ_FORMAT_TS` handling, verify, retries, trackbuf update. Talks to TX through a `flux_tx_ops_t` table (load, fire, busy, abort) so it runs on the host with fakes. | yes, with fake bus, fake flux source and fake TX |
| ipc | `src/ipc.c/.h` (existing) | New request ops and fields (below). | yes |
| console | `src/console.c`, `src/main.c` (existing) | New commands `writeblk`, `format` (section 7). | parser yes |
| host tool | `tools/a2m/` | `writeblk`, `writedisk`, `format` subcommands; `synth.py` gets the ROM track layout. | yes (pytest) |

### Interfaces

```c
// ipc.h (additions)
#define ST_WRPROT   0xAB   // $2B with the ROM's $80 bit
typedef enum { REQ_READ, REQ_WRITE, REQ_FORMAT_TS } req_op_t;
typedef struct {
    req_op_t op;
    uint16_t block;                 // REQ_READ / REQ_WRITE
    uint8_t  cyl, side;             // REQ_FORMAT_TS (one track side, bench aid)
    uint8_t  data[GCR_SECTOR_LEN];  // REQ_WRITE: 12 tag + 512 data
} req_t;
// reply_t gains: uint8_t tries;      // REQ_WRITE / REQ_FORMAT_TS: writes used
//                uint16_t fire_us;   // REQ_WRITE: header read -> fire, last write
// reply.block echoes req.block (core 0 tags format requests 0x8000 | cyl << 1 | side).

// flux_dec.h (addition to flux_dec_sink_t)
void (*header)(void *ctx, int track, int side, int sector);  // at DE AA

// flux_tx.h
void flux_tx_init(void);                   // claims SM1 + DMA, pins idle high
void flux_tx_load(const uint8_t *buf, size_t len);  // DMA armed, SM stopped
void flux_tx_fire(void);                   // one register write: SM enable
bool flux_tx_busy(void);                   // false once the gate is back high
void flux_tx_abort(void);                  // SM off, pins back to SIO high

// wtrack.h
#define WT_DATA_FIELD_LEN  716
#define WT_SECTOR_LEN      763
#define WT_PREAMBLE_LEN    1000
size_t wtrack_data_field(uint8_t out[WT_DATA_FIELD_LEN], int sector,
                         const uint8_t payload[GCR_SECTOR_LEN]);
size_t wtrack_track(uint8_t *out, size_t cap, int cyl, int side);
```

### Safety (/WRTGATE)

- Only `flux_tx.c` references `PIN_MECH_WRTGATE` outside `pins.h` and the
  idle table. `check_wrtgate.sh` is changed to allow that one file; every
  other file is still checked. The ADDR_MASK `_Static_assert` stays (the
  mechanism address bus never drives GP18).
- The PIO program raises the gate itself at the end of the stream, so the
  CPU never has to remember to.
- `flux_tx_abort()` returns both pins to SIO, gate high. Core 1 calls it if
  `flux_tx_busy()` is still true after the expected stream time + 10 %.
- Power-on and reset: gate idle high (SIO idle table, pull-ups), as today.
- Write-protect is checked in firmware before anything moves; the drive also
  refuses.

## 4. TX state machine (flux_tx)

- The buffer holds exactly the bytes the drive ROM writes through the IWM.
  The packed `FF 3F CF F3 FC` already lays down four 10-bit sync groups
  (`1111111100`), so there is no special sync case.
- One bit per 2.0425 us: PIO1 SM1 with a fractional clock divider so the
  per-bit loop lands on the nominal cell (150 MHz system clock; target
  within +-0.05 %, half the ERS tolerance). Fractional-divider jitter is one
  PIO clock, well under the 250 ns read/write margin (ERS86 sec. 2.10.2).
- Sequence: `set pins` gate low; wait >= 1.8 us (T3); shift bits, toggle
  WRTDATA on 1; after the last bit hold >= 2 us (T4); `set pins` gate high;
  park in a self-loop (busy = program counter not on that loop). WRTDATA starts at its SIO idle level (0); only level changes matter.
- DMA: byte transfers from the image into the TX FIFO, paced by the SM's
  DREQ. `flux_tx_load` pre-fills the FIFO so `flux_tx_fire` starts shifting
  at once.
- The RX stream (SM0) is stopped once the TX fires and restarted for the
  verify, so nothing RD shows while writing is ever decoded.

## 5. Sector write (REQ_WRITE)

On core 1, in `mech_server`:

1. **Checks before any motion:** block >= 1600 -> `0xAD`; no disk -> `0xAF`;
   write-protected -> `0xAB`.
2. **Encode first:** `wtrack_data_field` builds the 716-byte image
   `FF 3F CF F3 FC FF D5 AA AD <sector> <699 nibbles> <4 checksum nibbles>
   DE AA FF` (ROM `write_data_actual`, liron.asm lines 2085-2156);
   `flux_tx_load` it. Nothing is computed after the header, as in the ROM.
3. **Position:** motor on, seek (M4 code), wait READY, select the head.
   At least 100 us between the head select and arming.
4. **Arm:** flush the RX ring, `flux_dec_reset`, read the ring one interval at
   a time (batch 1) while armed. The `header` callback compares track, side
   and sector. A wrong-track header re-seeks as in M4.
5. **Fire:** on the target header, `flux_tx_fire()`. Record `fire_us`: time from reading the
   interval that completed `DE AA` to the fire; the console also reports the
   ring backlog, so the true latency is that plus the unread intervals. The decoder only sees `DE AA`
   complete at the next edge; the byte after it is `FF`, so that adds at most
   about 2 us. Budget: under 60 us. Hard limit: about 160 us, the distance to
   the old data mark (`DE AA FF FF 3F CF F3 FC FF D5 AA AD`); a later start
   leaves the old mark in front of our data.
6. **After:** wait for `flux_tx_busy()` false (about 11.7 ms for 716 bytes),
   then >= 620 us (erase tail, RD valid). Flush the RX ring.
7. **Verify:** restart the track buffer for that track side and run the M4
   fill until the target sector is in (not the whole track, 2 revolutions
   max), then compare all 524 bytes.
8. **Retries:**
   - Header not found: M4 fill budget and recal escalation, 5 s cap, then
     `0xA7`.
   - Verify mismatch or bad checksum: rewrite, 3 writes in total, then `0xA7`.
   - The ROM does not verify block writes (`cmd_write_block` returns right
     after `write_data`, line 1369). Ours does on purpose for M5, since the
     write path is new; revisit for speed in M7.
9. **Track buffer:** after the verify it holds the freshly read target
   sector (other sectors of that side refill on demand); on failure it is
   invalidated.
10. **Tags:** `writeblk` takes 512 data bytes and zeroes the 12 tag bytes, as
    the ROM's WriteBlock (lines 1286-1288). `REQ_WRITE` itself carries all
    524 bytes, for tests and for the extended Write command later.

Success after soft events returns `0xE7` with soft flags, as reads do.

## 6. Format (REQ_FORMAT_TS; whole disk driven by core 0)

Whole disk, in the ROM's order (`cmd_format_actual`, lines 449-483):

1. Each request checks: no disk -> `0xAF`; write-protected -> `0xAB`. The
   first seek after `server on` recalibrates (position unknown).
2. Core 0 issues cyl 0..79: side 0, then side 1, and stops at the first
   failure. After a zone change, poll READY
   (ROM: fixed 150 / 25 ms waits).

One track side:

1. **Image:** `wtrack_track(cyl, side)` into a static buffer:
   - 1000-byte preamble: 200 x `FF 3F CF F3 FC`.
   - Then each sector in interleave order, 763 bytes: 35 bytes of sync
     (7 x `FF 3F CF F3 FC`), `FF D5 AA 96`, track, sector, side, format,
     checksum, `DE AA FF`, `FF 3F CF F3 FC`, `FF D5 AA AD`, sector, the
     encoded data field of 524 zero bytes (= 703 x `$96`, as the ROM), `DE AA
     FF`.
   - Header fields as the ROM (`Se3f0`, lines 619-658): track = cyl & `$3F`;
     side = (cyl bit 6) | `$20` for side 1; format = `$24` (4:1 interleave,
     double-sided); checksum = track ^ sector ^ side ^ format.
   - Interleave: start at slot 0, step 4 slots per sector mod nsect, move to
     the next free slot on a collision (lines 595-618; matches
     `synth.physical_order`).
   - Sectors per track: 12 - cyl / 16. Zone 0 image: 1000 + 12 x 763 =
     10,156 bytes, about 166 ms against a 152 ms revolution. The tail
     overwrites the front of the preamble, as with the ROM. No index pulse:
     the write starts wherever the head is.
2. **Write:** head select, >= 100 us, `flux_tx_load` + `flux_tx_fire`, wait
   for done (cap: expected time + 10 %), then >= 620 us.
3. **Verify** with the M4 track read (2 revolutions max): every sector 0..nsect-1 seen exactly
   once, header and data checksums good, data all zero (ROM `verify_actual`,
   lines 660-680).
4. **Retries:** up to 10 write + verify attempts per track side (ROM
   `spb_packet_type` = `$0A`). Final failure replies `0xA7`; core 0 stops
   the format there and reports cyl and side. The ROM also ejects the disk (`Se51b`); we skip
   that in M5.
5. The track buffer is invalidated when a format starts.

`format <cyl> <side>` issues one `REQ_FORMAT_TS`, for the first bench tests.

Expected time: about 166 ms write + 1-2 revolutions verify + a step, per
track side; 160 track sides, so about 70-80 s per disk.

## 7. Console protocol (test commands)

The console line limit (80 chars) stays; block data travels as binary.

- `writeblk <n>` (0-1599) -> `SEND 512`; the host sends 512 binary bytes;
  the board replies `BLK <n> <status> <soft> <crc32> <tries> <fire_us> <backlog>`
  and `OK` (backlog = largest unread ring count during the request). CRC-32 (zlib) over the 512 bytes received, so the host can check the
  transfer. If the 512 bytes do not arrive within 2 s: `ERR timeout`, nothing
  is written.
- `format` -> one line per track side as it finishes,
  `FMT <cyl> <side> <tries> <status>`, then `FORMAT END <status> <total ms>`
  and `OK`. Stops at the first failed track side.
- `format <cyl> <side>` -> the same, for one track side.
- `ERR server off` if the server is not running, as for reads.
- Host side (`a2mflux.py`):
  - `writeblk <n> --data file|--pattern <byte>`: one block, prints status
    and latency.
  - `writedisk <image.po>`: loops `writeblk` over blocks 0-1599, prints a
    progress line per track side and the total time. No separate stream
    protocol: per-block writes are what SmartPort will send.
  - `format [cyl side]`: prints the progress lines.

## 8. Testing

Host (Mac), `firmware/test`:

| Test | Checks |
|---|---|
| test_wtrack | Data field image is byte-for-byte the ROM layout. Track image length per zone = 1000 + nsect x 763. Interleave order equals `synth.physical_order`. Header checksums. Round trip: image -> bit cells -> intervals -> `flux_dec` decodes every sector, payload all zero. |
| test_flux_dec | New `header` callback fires once per good header, at `DE AA`, with the right track/side/sector; not on a bad checksum. |
| test_mech_server (write) | fake_drive learns to write: when the fake TX fires, it splices the TX bit cells into its track at the current position, with a configurable latency. Cases: write + verify OK; `0xAB` protected and `0xAF` no disk without moving; header never found -> recal -> `0xA7`; corrupt-write fault knob -> 3 writes -> `0xA7`; TX never finishes -> abort, gate high, `0xA7`; latency past the old data mark -> verify fails; track buffer updated after a write and invalidated after a failure. |
| test_mech_server (format) | One track side format + verify OK and reads back all-zero sectors; format fails 10 times -> `0xA7`, tries = 10; protected -> `0xAB` without writing; a sector write on a board-formatted track; track buffer invalidated. |
| test_ipc, test_console | New ops and fields; `writeblk` / `format` parsing and ranges. |
| check_wrtgate | GP18 only in `pins.h`, the idle table, the static assert and `flux_tx.c`. |

Python side: `synth.py` gets the ROM track layout (preamble, 35-byte gaps,
`FF` before marks) next to the existing one; encoder parity between
`gcr.py` and `gcr.c` via fixtures; parsers for the `BLK`, `FMT` and
`FORMAT END` replies.

Bench (Rico; numbered steps in the plan). Disk 1 and disk 2 are scratch
disks; REF800K is never written.

| Step | What | Done when |
|---|---|---|
| 1 | Format disk 1 on the IIe + working UniDisk. Board `readdisk` as the snapshot. | 1600 blocks read |
| 2 | `writeblk` one block. Scope /WRTGATE and WRTDATA against RD. Meter CN104 pin 11 (+5 V) during a write loop. | Latency logged and under budget; gate timing matches T3/T4; +5 V in spec under write load |
| 3 | `readdisk`, diff against the snapshot | Only the target block changed (M5 pass, part 1) |
| 4 | `capture` the written track | Cell histogram of our sector matches the UniDisk-written sectors |
| 5 | `writedisk REF800K` onto disk 1 | `readdisk` byte-identical to REF800K; time recorded |
| 6 | Board `format 0 0`, then a full `format` on disk 2 | All verifies pass; `readdisk` all zero; time recorded |
| 7 | `writedisk REF800K` onto disk 2 | Byte-identical |
| 8 | Disk 2 in the working UniDisk on the IIe (normal cable use) | Boots or catalogs like REF800K (M5 pass, part 2). Optional: ADTPro image, diff against REF800K |

Captures go to `docs/captures/m5/`, the build note to
`docs/build_log/m5.md`.

## 9. Risks and open questions

1. **Drive A has never written.** Write current (CX20185) and erase on a
   dead-board drive are untested. First writes go to a scratch disk with a
   flux capture straight after; Drive B is the backup.
2. **+5 V drop at CN104 under write load.** Read failures were traced to IR
   drop before; writing adds write and erase current. Meter pin 11 in bench
   step 2 before trusting results.
3. **Fire latency is estimated.** Batch-1 polling should give a few us
   against a 160 us limit. Measured by the latency stat and the scope in
   step 2. If it is too close, a PIO header matcher that triggers TX by IRQ
   is the fallback; flux_tx stays the same.
4. **Our reader may be more tolerant than the UniDisk's.** A verify can pass
   on our board while the CXD1013/IWM rejects the sector. Covered by bench
   steps 4 and 8.
5. **Speed drift between format and rewrite** can stretch a data field into
   the next header's sync. The 35-byte gap is there for that, as in the ROM.
6. **The first WRTDATA toggle at a splice** can leave one odd flux cell. The
   IWM does the same; sync absorbs it.
7. **Per-block verify costs about a revolution.** `writedisk` will take
   several minutes. Fine for M5; the verify policy and its effect on the
   Liron ACK timeout are revisited in M7.
8. **Core 1 stack.** The SDK default is 2 KB and `gcr_encode_data` uses about
   1.25 KB. Raise `PICO_CORE1_STACK_SIZE` to 8 KB and make the encode
   buffers and the track image static (deferred from the M4 reviews).
