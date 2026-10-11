# M4: reading blocks on the board (design)

Status: draft for review, 2026-10-10.
Parent spec: `2026-10-07-a2m2053-controller-design.md` (sections 1, 2.3, 2.6, 4.2).
Builds on M0-M3 (`docs/build_log/m0.md` .. `m3.md`).

## 1. Goal

The RP2350 decodes 800K GCR itself and returns ProDOS blocks. The Mac asks
for block N and gets 512 data bytes (plus the 12 tag bytes and a status),
with no host-side decoding.

**Pass (parent spec M4):** a whole-disk read over a USB test command
(`readdisk`) gives 1600/1600 blocks byte-identical to REF800K, on Drive A
and on Drive B.

What carries forward: the block-read request interface between core 0 and
core 1 is the one SmartPort (M6/M7) will call. M4 builds and tests it with
the USB console standing in for SmartPort.

Out of scope: writing and formatting (M5), SmartPort (M6/M7), the eject
button, the LED, disk-swap reporting to a host (M9). M4 reads only.

## 2. Constraints

- /WRTGATE (GP18) stays high. `check_wrtgate.sh` and the ADDR_MASK
  `_Static_assert` stay. Core 1 never issues EJECT in M4.
- Streaming decode (decision 2026-10-10): flux is decoded as it arrives,
  because M5 writes must find a sector header live and open the write gate
  right after it. M4 builds that decoder once.
- Two cores now (decision 2026-10-10): core 1 owns the mechanism; core 0
  talks to it only through queues.
- The retry policy follows the drive ROM (decision 2026-10-10), adapted to
  a whole-track buffer (section 6).
- Every wait has a timeout. The ROM has unbounded loops (step-done, READY,
  nibble waits); ours do not.
- Bench power rule from M0/M1 stays: `oe off` and mechanism supply off
  before any Pico USB unplug or replug.

## 3. Structure

```
core 0 (console; SmartPort later)          core 1 (mechanism server)
  readblk N / readdisk  --req queue-->    block -> track/side/sector
          <--reply queue-- 524 B + status   seek (recal if position unknown)
                                             PIO1 -> DMA ring buffer (continuous)
                                             stream decoder: intervals -> bits
                                               -> nibbles -> address field
                                               -> data field -> 524 B
                                             track buffer: one side, <= 12 sectors
```

### Units

| Unit | Files | Job | Host-tested |
|---|---|---|---|
| gcr | `src/gcr.c/.h` | 64-entry nibble table and inverse, mangle/unmangle (tashnotes), address field decode, data field decode with GROUP_ORDER/CHECKSUM_ORDER = (0,1,2) (confirmed on media in M3), `gcr_block_to_tss()` and `gcr_sectors_per_track()` | yes |
| flux_dec | `src/flux_dec.c/.h` | Streaming decoder. `flux_dec_reset(dec, track, side)`, `flux_dec_push(dec, count)` per interval. Holds the cell PLL, the nibble latch and the field state machine. Calls back with each good sector, each bad header/data event and each foreign header (track, side, sector). Pure C, no hardware, no allocation. | yes |
| trackbuf | `src/trackbuf.c/.h` | One track side: which sectors are present, their 524 bytes, valid flag, invalidate. | yes |
| flux_rx | `src/flux.c/.h` (existing) | Adds continuous mode: PIO1 SM0 + two chained DMA channels into a ring buffer, a reader index, overrun detection. The one-shot `flux_capture()` stays for the `capture` command. | no (hardware) |
| mech_server | `src/mech_server.c/.h` | Core 1 loop: take a request, check disk and range, seek, fill the track buffer through flux_rx + flux_dec, apply the retry policy, reply, idle motor-off. Talks to hardware only through `mech_bus_t` (existing) and a flux source interface, so it runs on the host with fakes. | yes, with fake bus and fake flux source |
| ipc | `src/ipc.c/.h` | Request and reply queues (Pico SDK `queue_t` on target, a plain ring on host). | yes |
| console | `src/console.c`, `src/main.c` | New commands `server on|off`, `readblk <n>`, `readdisk`. Old mechanism commands (`status`, `cmd`, `motor`, `recal`, `seek`, `head`, `eject`, `tach`, `capture`) work only while the server is off; with it on they return `ERR server on`. | parser yes |

### Interfaces

```c
// ipc.h
typedef enum { REQ_READ } req_op_t;
typedef struct { req_op_t op; uint16_t block; } req_t;
typedef struct {
    uint16_t block;
    uint8_t  status;      // SmartPort-style: 0x00 ok, 0xE7 ok after retries,
                          // 0xA7 I/O error, 0xAF no disk, 0xAD bad block
    uint8_t  soft;        // soft-error flags (section 6), 0 if clean
    uint8_t  data[524];   // 12 tag + 512 data; zeros on error
} reply_t;

// flux_dec.h
typedef struct {
    void (*sector)(void *ctx, int sector, const uint8_t payload[524]);
    void (*event)(void *ctx, flux_ev_t ev, int track, int side, int sector);
    void *ctx;
} flux_dec_sink_t;
void flux_dec_reset(flux_dec_t *d, int track, int side, const flux_dec_sink_t *sink);
void flux_dec_push(flux_dec_t *d, uint16_t count);
```

`flux_ev_t`: `EV_BAD_ADDR` (address checksum or epilog), `EV_FOREIGN`
(good header, wrong track or side), `EV_NO_DATA` (data prolog not within
25 nibbles), `EV_BAD_DATA` (data checksum), `EV_RANGE` (sector number
>= sectors per track).

## 4. Data flow for one request

1. Core 0 pushes `{REQ_READ, N}`.
2. Core 1: disk present? (status CSTIN) else reply `0xAF`. N < 1600? else
   `0xAD`. These checks come before any mechanism movement, as in the ROM
   (`Se5f0`, `Le73e`).
3. Map N to (track, side, sector) with the zone table (12/11/10/9/8 sectors
   for tracks 0-15/16-31/32-47/48-63/64-79), .po order: blocks run through
   side 0 then side 1 of each track. Same mapping as `gcr.block_number`,
   proven by the M3 full-disk compare.
4. **Track buffer hit** (same track and side, valid): reply from the buffer.
5. **Miss:** motor on if off; seek (recal first if the position is unknown);
   wait READY with a timeout; select the head; start or restart the flux
   stream; `flux_dec_reset`.
6. **Fill:** read intervals from the ring buffer up to the DMA write
   position and push them into the decoder. Each good sector for this track
   and side goes into the track buffer. Stop when all sectors of the track
   side are present or the retry budget runs out.
7. Reply with the requested sector (or the error).
8. Motor idle-off: after `MECH_IDLE_OFF_MS` (1000 ms) with no request,
   motor off and the track buffer stays valid (the disk is still in).

## 5. Flux stream and decoder

- PIO program and timing unchanged from M3: 40 ns counts, falling edge on
  GP20, overhead 2.0 counts added back by the decoder.
- Continuous mode: two DMA channels chained in a ping-pong over a ring of
  8192 x uint16 (16 KB). At the worst-case rate (one interval per 2 us)
  that is about 16 ms of slack. The reader compares its index with the DMA
  write position.
- Overrun (writer laps reader): drop the partial revolution, reset the
  decoder, set the `SOFT_OVERRUN` flag, carry on. Data is never silently
  lost.
- Decoder: C port of `tools/a2m/flux.py` + `track.py`. Cell PLL with the
  same constants (nominal 2.0425 us, alpha 0.05, adapt on 1-3 cell
  intervals only, clamp at 8 cells). IWM-style latch (skip zeros until a 1,
  then 8 bits). Field state machine: hunt `D5 AA 96`; 5 address nibbles;
  XOR check and `DE AA`; hunt `D5 AA AD` within 25 nibbles (ROM value);
  1 + 699 + 4 data nibbles; unmangle; checksum; sector number must equal the
  header's.
- CPU budget: about 300 M33 cycles per interval at 150 MHz at the worst-case
  rate. Measured on the board in the bench task (cycle counter around
  `flux_dec_push`, worst case reported by `readdisk`).

## 6. Retry and error policy

ROM read path (`liron.asm`, `cmd_read_block` line 1281, retry loop
lines 1297-1317), as found 2026-10-10:

| ROM | Value |
|---|---|
| Up-front | no disk -> `$2F`; block >= 1600 -> `$2D` |
| Seek / wrong-track budget (Z39) | 4; recal on the 3rd attempt |
| Header + data failure budget (Z3B) | 150 (any of: no address field, bad address checksum, wrong side, wrong sector, data prolog missing, bad data checksum); one recal at 75 left |
| Address search | about 1500 nibbles |
| Data prolog search | 25 nibbles |
| Give up | `$27` (reply byte `$A7`) |
| Success after soft errors | data sent, status `$E7`, soft flags recorded |

The ROM reads one sector at a time, so wrong-sector headers spend its
budget: 150 is about 12 revolutions. With a whole-track buffer:

| Ours | Value |
|---|---|
| Fill budget | 12 revolutions (`READ_REVS_MAX`); recal + re-seek after 6 |
| Revolution | counted as 1.2 x the zone's rotation time (152 ms at 394 rpm down to 102 ms at 590 rpm), so a missing sector gets at least one full pass |
| Wrong-track header | resync position to the header's track, re-seek; budget 4 (`SEEK_TRIES_MAX`), recal on the 3rd |
| Give up | reply `0xA7`, data zeroed |
| Success after any soft event | reply `0xE7`, data returned, `soft` flags set |
| No disk / bad block | `0xAF` / `0xAD`, no mechanism movement |
| Timeouts | seek step 50 ms, READY 1 s (existing), whole request 5 s hard cap |

Soft flags (bit per cause, mirroring the ROM's Z57): `SOFT_RECAL`
(a recal happened), `SOFT_FOREIGN` (wrong track header), `SOFT_BADDATA`,
`SOFT_BADADDR`, `SOFT_OVERRUN`.

## 7. Console protocol (test commands)

- `server on` / `server off` -> `OK`. Server off stops core 1's loop, flux
  stream off, motor off.
- `readblk <n>` (0-1599) -> `BLK <n> <status 2 hex> <soft 2 hex>`, then
  the 524 bytes as hex, 16 bytes per line, then `OK` (or `ERR <reason>` if
  the server is off).
- `readdisk` -> `DISK BEGIN 1600\r\n`, then for each track side in order:
  `TS <t> <s> <nsect> <status> <soft>\r\n` + nsect x 512 data bytes (binary)
  + `\r\nTS END <crc32 8 hex>\r\n`; then `DISK END <worst cycles per
  interval> <total ms>\r\n` and `OK`. CRC-32 (zlib) over that track side's
  binary bytes. A failed track side still sends nsect x 512 bytes (zeros
  for missing sectors) with its error status, so the stream stays framed.
- Host side: `a2mflux.py readdisk --port ... --out img.po --compare ref.po`
  parses the stream, checks each CRC, writes the image and compares.
  `a2mflux.py readblk` prints one block and its match against a reference.

## 8. Testing

Host (Mac), `firmware/test`:

| Test | Checks against |
|---|---|
| test_gcr | Nibble table, tashnotes mangle vector, address/data round trips (cases from `tools/tests/test_gcr.py`) |
| test_flux_dec_synth | Synthetic tracks exported by `tools/a2m/synth.py` as fixture files: clean, 6 % jitter + 12 % drift, wrong side, corrupt sector, empty input. Same results as the Python decoder. |
| test_flux_dec_real | M3 captures (`docs/captures/m3/scan7_driveA_caps`, `scan5_drive2`: tracks 0/5/20/40/60/79, both sides): every decoded sector equals REF800K's block. The expected blocks for exactly those 12 track sides (124 blocks, about 62 KB) are extracted once from REF800K into `firmware/test/fixtures/` and committed, so the test does not depend on a file outside the repo. (REF800K can't be regenerated from its seeds alone: its directory, bitmap and index blocks come from AppleCommander.) |
| test_flux_dec_chunks | The same capture pushed in chunks of 1, 7, 500 and all at once gives identical sectors and events |
| test_mech_server | Fake bus + fake flux source: hit, miss, fill on revolution 1, fill after retries -> `0xE7`, budget exhausted -> `0xA7`, wrong-track -> re-seek -> recal on the 3rd, no disk -> `0xAF` without moving, block 1600 -> `0xAD`, idle motor-off, every wait bounded |
| test_ipc | Full/empty, ordering |
| test_console | New commands, ranges, `ERR server on` for mechanism commands |
| check_wrtgate | Unchanged |

Python side: tests for the `readdisk` stream parser (good, bad CRC,
truncated, failed track side).

Bench (Rico; numbered steps in the plan):
1. Flash. Server off: `status`, `seek`, `capture` still work (M1-M3
   regression).
2. `server on`; `readblk` 0, 1599 and a few per zone; each matches REF800K.
3. `readdisk` on Drive A: 1600/1600 byte-identical (M4 pass). Record total
   time and worst cycles per interval.
4. Same on Drive B.
5. No disk -> `0xAF`. `readblk 1600` is rejected by the console parser
   (`ERR bad command`); the server's own `0xAD` path is covered by
   test_mech_server and becomes reachable from the host in M7. Server off
   while idle; power rule respected throughout.

## 9. Risks and open questions

1. Decode headroom on core 1 is estimated, not measured. Mitigation: the
   cycle counter in step 3; if it is tight, move the cell classification
   into PIO (a later change, not M4).
2. Motor idle-off time (1 s) is a rough reading of the ROM's counter; refine
   against the working drive (motor sound) in M7.
3. Head settle after a head switch is not in the ROM read path; the decoder
   simply rejects foreign or bad headers until the stream is clean.
4. The 5 s request cap must stay under the Liron card's ACK timeout; that
   number is still owed (parent spec risk 1, M7).
