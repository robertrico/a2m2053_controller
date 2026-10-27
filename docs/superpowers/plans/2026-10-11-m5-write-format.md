# M5 Board-side Write and Format Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The RP2350 writes 800K GCR itself: `writeblk` rewrites one block's data field right after its header and verifies it, and `format` lays down the drive ROM's track layout on every track side and verifies it.

**Architecture:** A second PIO1 state machine (`flux_tx`) acts as the IWM's write side: it shifts a DMA-fed byte image out one bit per 2.0425 us, toggling WRTDATA on each 1 and holding /WRTGATE low around the stream. Pure C builders (`wtrack`) produce the exact byte images the drive ROM writes. Core 1's mechanism server decodes live (batch 1 while armed), fires the TX from a new decoder `header` callback, then verifies through the M4 read path. Everything except the PIO/DMA driver runs on the Mac with a fake drive that splices written bits into its own track images.

**Tech Stack:** C11, Pico SDK 2.2.0 (`pico_stdlib`, `hardware_pio`, `hardware_dma`, `pico_multicore`), CMake + Ninja, host tests with `cc` + `firmware/test/test.h`, Python 3.14 + pytest for the host tool and fixtures.

**Spec:** `docs/superpowers/specs/2026-10-11-m5-write-format-design.md` (parent: `docs/superpowers/specs/2026-10-07-a2m2053-controller-design.md`; M4: `docs/superpowers/specs/2026-10-10-m4-board-read-design.md`).

## Global Constraints

- Scope: Apple IIe + 800K double-sided ProDOS disks. 1600 blocks; zones 12/11/10/9/8 sectors for tracks 0-15/16-31/32-47/48-63/64-79; .po block order as `gcr_block_to_tss`.
- Track layout = drive ROM (`liron.asm`): preamble 200 x `FF 3F CF F3 FC` (1000 bytes); per sector 763 bytes = 7 x `FF 3F CF F3 FC`, `FF`, address field (10), `FF`, data-field image (716). Data-field image = `FF 3F CF F3 FC FF` + data field (709: `D5 AA AD`, sector, 699 + 4 nibbles, `DE AA`) + `FF`. Format byte `$24`. Interleave 4:1 (slot walk as `synth.physical_order`). Data of a formatted sector: 524 zero bytes.
- WRTDATA: a 1 bit toggles the line, a 0 leaves it (ERS86 3.2.6). Cell 2.04248366 us, same in every zone. No write precompensation.
- /WRTGATE timing (ERS86 p24, p29): READY low before gate low; SEL settled >= 100 us before gate low; >= 1.8 us gate low to first edge; >= 2 us last edge to gate high; >= 620 us after gate high before reading or stepping. No status read and no SEL/CA change while the gate is low.
- /WRTGATE (GP18) is referenced only in `pins.h`, the `hw.c` idle table, the `hw.c` ADDR_MASK `_Static_assert`, and `flux_tx.c`. `firmware/test/check_wrtgate.sh` must pass. The PIO program raises the gate itself; `flux_tx_abort()` returns both pins to SIO with the gate high.
- Retries: block write 3 writes (verify after each, 2 revolutions max); format 10 write + verify attempts per track side (ROM `$0A`); header search = M4 fill budget; 5000 ms cap per block-write request; 30000 ms cap per format request.
- Reply status bytes: `0x00` ok, `0xE7` ok after soft errors, `0xA7` I/O error, `0xAF` no disk, `0xAD` bad block / bad argument, `0xAB` write-protected (new). Soft flags as M4.
- Never write REF800K. It stays write-protected.
- Every wait has a timeout. No allocation in firmware code (static buffers only).
- No hardware action (power, flash, plug/unplug, writing a disk) without the operator saying go. Bench rule: `oe off` and mechanism supply off before any Pico USB unplug/replug.
- Commits: Conventional Commits, ending with the two trailer lines given by the controller.

## Review Focus

1. **Read a block, write it, read it again.** Expected: the second read returns the new data, never the cached old sector. Pinned by Task 5 `test_write_then_read_not_stale`.
2. **Read a track side, format it, read it again.** Expected: all-zero sectors, never the cached old data. Pinned by Task 6 `test_format_then_read_not_stale`.
3. **A status read or head select while the gate is low.** Expected: never; a SEL change mid-write would switch heads and a status read changes SEL. Pinned by `D.addr_while_writing == 0` checks in Task 5 and Task 6.
4. **Writing a block on a track side where another sector is unreadable.** Expected: the write succeeds without waiting for the bad sector. Pinned by Task 5 `test_write_ignores_bad_neighbour`.
5. **512 payload bytes that look like console lines (`\nERR ...`, `\nOK\n`).** Expected: the host tool sends them only after `SEND 512` and parses the real `BLK` reply. Pinned by Task 9 `test_writeblk_sends_payload_after_prompt`.

---

## File Structure

```
firmware/src/
  wtrack.h, wtrack.c        NEW  byte images: one data field, one track side (ROM layout), interleave order
  flux_tx.pio               NEW  TX state machine: byte image -> WRTDATA toggles, /WRTGATE by set pins
  flux_tx.h, flux_tx.c      NEW  PIO1 SM1 + one DMA channel; the only code that touches GP18
  flux_dec.h, flux_dec.c    MOD  + header callback at an address field's DE AA
  ipc.h                     MOD  + REQ_WRITE, REQ_FORMAT_TS, req data, reply tries/fire_us, ST_WRPROT
  mech_server.h, .c         MOD  + flux_tx_t, armed fill, mech_server_write, mech_server_format_ts
  console.h, console.c      MOD  + writeblk, format
  main.c                    MOD  core 1 dispatch, hardware TX table, do_writeblk, do_format
  pins.h                    MOD  comment on GP18
firmware/test/
  test_wtrack.c             NEW
  fake_drive.h              MOD  bit model, stored written track sides, fake TX, write-protect
  test_flux_dec.c, test_mech_server.c, test_trackbuf_ipc.c, test_console.c  MOD
  check_wrtgate.sh, Makefile MOD
  fixtures/rom_*            NEW  generated by tools/mkfixtures.py --rom-only
firmware/CMakeLists.txt     MOD  sources, PIO header, core 1 stack 8 KB
tools/a2m/synth.py          MOD  + ROM layout (SYNC_PACKED, rom_data_field, rom_sector, rom_track_bytes)
tools/mkfixtures.py         MOD  + --rom-only
tools/a2m/proto.py, serialio.py, cli.py  MOD  + writeblk, writedisk, format
tools/tests/test_synth_rom.py NEW; tools/tests/test_proto_cli.py MOD
docs/build_log/m5.md        NEW (Task 10)
```

Run host tests: `make -C firmware test`. Build firmware: `make -C firmware`. Python tests: `cd tools && .venv/bin/python -m pytest -q`.

---

### Task 1: ROM track layout in Python, plus C fixtures

**Files:**
- Modify: `tools/a2m/synth.py`, `tools/mkfixtures.py`
- Create: `tools/tests/test_synth_rom.py`, `firmware/test/fixtures/rom_track_t17s1.bin`, `rom_track_t70s0.bin`, `rom_dfield_s5.pay`, `rom_dfield_s5.bin` (generated)

**Interfaces:**
- Consumes: `gcr.encode_address(track, sector, side)`, `gcr.encode_data(sector, payload)`, `synth.physical_order(spt, interleave)`, `synth.bytes_bits`, `synth.SYNC`.
- Produces: `synth.SYNC_PACKED: bytes` (`FF 3F CF F3 FC`), `synth.rom_data_field(sector: int, payload: bytes) -> bytes` (716), `synth.rom_sector(track, side, sector, payload) -> bytes` (763), `synth.rom_track_bytes(track, side, payloads: dict | None = None, interleave=4) -> bytes`. Fixture files used by Task 2.

- [ ] **Step 1: Write the failing tests**

Create `tools/tests/test_synth_rom.py`:

```python
import random
from a2m import gcr, synth, flux, track


def test_packed_sync_is_four_ten_bit_groups():
    assert synth.bytes_bits(synth.SYNC_PACKED) == synth.SYNC * 4


def test_rom_data_field_layout_zero_payload():
    f = synth.rom_data_field(3, bytes(gcr.SECTOR_LEN))
    assert len(f) == 716
    assert f[:9] == bytes.fromhex("ff3fcff3fcffd5aaad")
    assert f[9] == gcr.NIBBLES[3]
    assert f[10:713] == b"\x96" * 703          # liron.asm Se3b9: zero data and zero checksum
    assert f[713:] == b"\xde\xaa\xff"


def test_rom_track_lengths():
    for t, n in ((0, 12), (16, 11), (32, 10), (48, 9), (64, 8), (79, 8)):
        assert len(synth.rom_track_bytes(t, 1)) == 1000 + n * 763


def test_rom_track_first_sector():
    b = synth.rom_track_bytes(70, 1)
    assert b[:1000] == synth.SYNC_PACKED * 200
    first = 1000 + 35
    assert b[1000:first] == synth.SYNC_PACKED * 7
    assert b[first] == 0xFF
    assert b[first + 1:first + 11] == gcr.encode_address(70, 0, 1)   # slot 0 holds sector 0
    assert b[first + 11] == 0xFF
    assert b[first + 12:first + 12 + 716] == synth.rom_data_field(0, bytes(gcr.SECTOR_LEN))


def test_rom_track_decodes():
    rng = random.Random(5)
    p = {s: bytes(rng.randrange(256) for _ in range(gcr.SECTOR_LEN)) for s in range(11)}
    bits = synth.bytes_bits(synth.rom_track_bytes(17, 1, p))
    nibs = flux.bits_to_nibbles(flux.intervals_to_bits(flux.encode_flux(bits)))
    res = track.decode_track(nibs, 17, 1)
    assert sorted(res.sectors) == list(range(11))
    assert all(res.sectors[s].payload == p[s] for s in p)
```

- [ ] **Step 2: Run to verify they fail**

Run: `cd tools && .venv/bin/python -m pytest -q tests/test_synth_rom.py`
Expected: FAIL with `AttributeError: module 'a2m.synth' has no attribute 'SYNC_PACKED'`.

- [ ] **Step 3: Implement**

Append to `tools/a2m/synth.py`:

```python
# Drive ROM layout (liron.asm write_trk_actual / Se3b9 / write_data_actual).
SYNC_PACKED = bytes([0xFF, 0x3F, 0xCF, 0xF3, 0xFC])   # four 10-bit sync groups, byte-packed for the IWM
ROM_PREAMBLE = SYNC_PACKED * 200                        # 1000 bytes at the start of a format write
ROM_GAP = SYNC_PACKED * 7                               # 35 bytes before each header


def rom_data_field(sector: int, payload: bytes) -> bytes:
    """What the ROM writes for one sector's data (716 bytes), from just after the header's pad byte."""
    return SYNC_PACKED + b"\xff" + gcr.encode_data(sector, payload) + b"\xff"


def rom_sector(track: int, side: int, sector: int, payload: bytes) -> bytes:
    """One formatted sector (763 bytes)."""
    return ROM_GAP + b"\xff" + gcr.encode_address(track, sector, side) + b"\xff" + rom_data_field(sector, payload)


def rom_track_bytes(track: int, side: int, payloads: dict | None = None, interleave: int = 4) -> bytes:
    """A whole format write for one track side; payloads default to 524 zero bytes."""
    out = bytearray(ROM_PREAMBLE)
    for s in physical_order(gcr.sectors_per_track(track), interleave):
        out += rom_sector(track, side, s, payloads[s] if payloads else bytes(gcr.SECTOR_LEN))
    return bytes(out)
```

In `tools/mkfixtures.py`, add above `main()`:

```python
def write_rom_fixtures():
    """M5: the drive ROM's byte images, for firmware/test/test_wtrack.c."""
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "rom_track_t17s1.bin").write_bytes(synth.rom_track_bytes(17, 1))
    (OUT / "rom_track_t70s0.bin").write_bytes(synth.rom_track_bytes(70, 0))
    p = payloads_for(17, 1)[5]
    (OUT / "rom_dfield_s5.pay").write_bytes(p)
    (OUT / "rom_dfield_s5.bin").write_bytes(synth.rom_data_field(5, p))
```

and change the start and end of `main()`:

```python
def main():
    if sys.argv[1:] == ["--rom-only"]:
        write_rom_fixtures()
        print(f"rom fixtures -> {OUT}")
        return
    ref = Path(sys.argv[1]).read_bytes()
    ...
    (OUT / "real_expected.bin").write_bytes(bytes(blocks))
    write_rom_fixtures()
    print(f"fixtures -> {OUT}")
```

Update the usage docstring: add `.venv/bin/python mkfixtures.py --rom-only   (M5 fixtures only, no REF800K needed)`.

- [ ] **Step 4: Run tests, generate fixtures**

Run: `cd tools && .venv/bin/python -m pytest -q && .venv/bin/python mkfixtures.py --rom-only && git status --short ../firmware/test/fixtures`
Expected: all tests pass; four new `rom_*` files, no other fixture changed.

- [ ] **Step 5: Commit**

```bash
git add tools/a2m/synth.py tools/mkfixtures.py tools/tests/test_synth_rom.py firmware/test/fixtures/rom_*
git commit -m "feat(tools): drive ROM track layout and M5 fixtures"
```

---

### Task 2: `wtrack` byte images in C

**Files:**
- Create: `firmware/src/wtrack.h`, `firmware/src/wtrack.c`, `firmware/test/test_wtrack.c`
- Modify: `firmware/test/Makefile`

**Interfaces:**
- Consumes: `gcr_encode_address(int track, int sector, int side, uint8_t out[10])`, `gcr_encode_data(int sector, const uint8_t payload[524], uint8_t out[709])`, `gcr_sectors_per_track(int)`, `gcr_nibbles`, fixtures from Task 1, `flux_dec_*` (test only).
- Produces:
  - `WT_SYNC_LEN 5`, `WT_PREAMBLE_LEN 1000`, `WT_GAP_SYNCS 7`, `WT_DATA_FIELD_LEN 716`, `WT_SECTOR_LEN 763`, `WT_INTERLEAVE 4`, `WT_TRACK_MAX 10156`
  - `size_t wtrack_data_field(uint8_t out[WT_DATA_FIELD_LEN], int sector, const uint8_t payload[GCR_SECTOR_LEN]);` returns 716
  - `size_t wtrack_track(uint8_t *out, size_t cap, int cyl, int side);` returns the length, or 0 if `cap` is too small or cyl/side is out of range
  - `void wtrack_order(int nsect, int interleave, int order[12]);` order[slot] = sector

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_wtrack.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "wtrack.h"
#include "flux_dec.h"

#define FIX "fixtures/"

static uint8_t *load(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("missing %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*len);
    if (fread(b, 1, (size_t)*len, f) != (size_t)*len) { printf("short read %s\n", path); exit(1); }
    fclose(f);
    return b;
}

static void test_data_field_layout(void) {
    static const uint8_t zero[GCR_SECTOR_LEN];
    static const uint8_t head[9] = { 0xFF, 0x3F, 0xCF, 0xF3, 0xFC, 0xFF, 0xD5, 0xAA, 0xAD };
    uint8_t f[WT_DATA_FIELD_LEN];
    CHECK(wtrack_data_field(f, 3, zero) == 716);
    CHECK(memcmp(f, head, 9) == 0);
    CHECK(f[9] == gcr_nibbles[3]);
    int all96 = 1;
    for (int i = 10; i < 713; i++) if (f[i] != 0x96) all96 = 0;
    CHECK(all96);                                   // ROM Se3b9: 703 x $96 for zero data
    CHECK(f[713] == 0xDE && f[714] == 0xAA && f[715] == 0xFF);
}

static void test_data_field_matches_python(void) {
    long pl, bl;
    uint8_t *pay = load(FIX "rom_dfield_s5.pay", &pl), *exp = load(FIX "rom_dfield_s5.bin", &bl);
    CHECK(pl == GCR_SECTOR_LEN && bl == WT_DATA_FIELD_LEN);
    uint8_t f[WT_DATA_FIELD_LEN];
    wtrack_data_field(f, 5, pay);
    CHECK(memcmp(f, exp, WT_DATA_FIELD_LEN) == 0);
    free(pay); free(exp);
}

static void track_matches(const char *path, int cyl, int side) {
    static uint8_t img[WT_TRACK_MAX];
    long el;
    uint8_t *exp = load(path, &el);
    size_t n = wtrack_track(img, sizeof img, cyl, side);
    CHECK((long)n == el && memcmp(img, exp, n) == 0);
    free(exp);
}

static void test_track_matches_python(void) {
    track_matches(FIX "rom_track_t17s1.bin", 17, 1);
    track_matches(FIX "rom_track_t70s0.bin", 70, 0);
}

static void test_track_lengths_and_limits(void) {
    static uint8_t img[WT_TRACK_MAX];
    static const int cyl[6] = { 0, 16, 32, 48, 64, 79 }, ns[6] = { 12, 11, 10, 9, 8, 8 };
    for (int i = 0; i < 6; i++) CHECK(wtrack_track(img, sizeof img, cyl[i], 1) == (size_t)(1000 + ns[i] * 763));
    CHECK(WT_TRACK_MAX == 1000 + 12 * 763);
    CHECK(wtrack_track(img, 100, 0, 0) == 0);
    CHECK(wtrack_track(img, sizeof img, 80, 0) == 0);
    CHECK(wtrack_track(img, sizeof img, -1, 0) == 0);
    CHECK(wtrack_track(img, sizeof img, 0, 2) == 0);
}

static void test_order(void) {
    static const int o12[12] = { 0, 3, 6, 9, 1, 4, 7, 10, 2, 5, 8, 11 };   // synth.physical_order(12, 4)
    static const int o8[8] = { 0, 2, 4, 6, 1, 3, 5, 7 };                   // synth.physical_order(8, 4)
    int o[12];
    wtrack_order(12, 4, o);
    for (int i = 0; i < 12; i++) CHECK(o[i] == o12[i]);
    wtrack_order(8, 4, o);
    for (int i = 0; i < 8; i++) CHECK(o[i] == o8[i]);
}

static int seen[12];
static void on_sec(void *ctx, int sec, const uint8_t *p) {
    (void)ctx;
    for (int i = 0; i < GCR_SECTOR_LEN; i++) if (p[i]) return;
    if (sec >= 0 && sec < 12) seen[sec]++;
}

static void test_track_decodes(void) {
    static uint8_t img[WT_TRACK_MAX];
    static uint16_t iv[WT_TRACK_MAX * 8];
    static flux_dec_t d;
    size_t len = wtrack_track(img, sizeof img, 70, 0);
    int n = 0, run = 0;
    for (size_t k = 0; k < len * 8; k++) {
        run++;
        if ((img[k >> 3] >> (7 - (k & 7))) & 1) {
            iv[n++] = (uint16_t)((float)run * FLUX_DEC_CELL_COUNTS - FLUX_DEC_OVERHEAD + 0.5f);
            run = 0;
        }
    }
    memset(seen, 0, sizeof seen);
    flux_dec_sink_t k = { .sector = on_sec };
    flux_dec_reset(&d, 70, 0, &k);
    flux_dec_push_many(&d, iv, n);
    for (int s = 0; s < 8; s++) CHECK(seen[s] == 1);
}

int main(void) {
    test_data_field_layout();
    test_data_field_matches_python();
    test_track_matches_python();
    test_track_lengths_and_limits();
    test_order();
    test_track_decodes();
    TEST_END();
}
```

In `firmware/test/Makefile`, add `test_wtrack` to `TESTS` and the rule:

```make
$(OUT)/test_wtrack: test_wtrack.c ../src/wtrack.c ../src/gcr.c ../src/flux_dec.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)
```

- [ ] **Step 2: Run to verify it fails**

Run: `make -C firmware test`
Expected: FAIL to compile, `wtrack.h: No such file or directory`.

- [ ] **Step 3: Implement**

Create `firmware/src/wtrack.h`:

```c
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "gcr.h"

// Byte images the TX state machine streams, in the drive ROM's layout (liron.asm
// write_trk_actual, Se3b9, write_data_actual). Bits go out MSB first; the packed
// sync FF 3F CF F3 FC lays down four 10-bit self-sync groups.
#define WT_SYNC_LEN        5
#define WT_PREAMBLE_LEN    1000     // 200 sync blocks at the start of a format write
#define WT_GAP_SYNCS       7        // 35 bytes before each header
#define WT_DATA_FIELD_LEN  716      // FF 3F CF F3 FC FF + data field (709) + FF
#define WT_SECTOR_LEN      763      // gap 35 + FF + address 10 + FF + data-field image 716
#define WT_INTERLEAVE      4        // ROM zero-page $62 on the UniDisk
#define WT_TRACK_MAX       (WT_PREAMBLE_LEN + 12 * WT_SECTOR_LEN)

size_t wtrack_data_field(uint8_t out[WT_DATA_FIELD_LEN], int sector, const uint8_t payload[GCR_SECTOR_LEN]);
size_t wtrack_track(uint8_t *out, size_t cap, int cyl, int side);
void wtrack_order(int nsect, int interleave, int order[12]);
```

Create `firmware/src/wtrack.c`:

```c
#include "wtrack.h"
#include <string.h>

static const uint8_t SYNC[WT_SYNC_LEN] = { 0xFF, 0x3F, 0xCF, 0xF3, 0xFC };
static const uint8_t ZERO[GCR_SECTOR_LEN];

size_t wtrack_data_field(uint8_t out[WT_DATA_FIELD_LEN], int sector, const uint8_t payload[GCR_SECTOR_LEN]) {
    memcpy(out, SYNC, WT_SYNC_LEN);
    out[WT_SYNC_LEN] = 0xFF;
    gcr_encode_data(sector, payload, out + WT_SYNC_LEN + 1);
    out[WT_DATA_FIELD_LEN - 1] = 0xFF;
    return WT_DATA_FIELD_LEN;
}

// ROM Se3f0: walk the slots `interleave` apart, taking the next free slot on a collision.
void wtrack_order(int nsect, int interleave, int order[12]) {
    for (int i = 0; i < nsect; i++) order[i] = -1;
    int pos = 0;
    for (int sec = 0; sec < nsect; sec++) {
        while (order[pos] >= 0) pos = (pos + 1) % nsect;
        order[pos] = sec;
        pos = (pos + interleave) % nsect;
    }
}

size_t wtrack_track(uint8_t *out, size_t cap, int cyl, int side) {
    int nsect = gcr_sectors_per_track(cyl);
    if (nsect < 0 || side < 0 || side >= GCR_SIDES) return 0;
    size_t len = WT_PREAMBLE_LEN + (size_t)nsect * WT_SECTOR_LEN;
    if (cap < len) return 0;
    uint8_t *p = out;
    for (int i = 0; i < WT_PREAMBLE_LEN / WT_SYNC_LEN; i++) { memcpy(p, SYNC, WT_SYNC_LEN); p += WT_SYNC_LEN; }
    int order[12];
    wtrack_order(nsect, WT_INTERLEAVE, order);
    for (int k = 0; k < nsect; k++) {
        for (int i = 0; i < WT_GAP_SYNCS; i++) { memcpy(p, SYNC, WT_SYNC_LEN); p += WT_SYNC_LEN; }
        *p++ = 0xFF;
        gcr_encode_address(cyl, order[k], side, p);
        p += GCR_ADDR_FIELD_LEN;
        *p++ = 0xFF;
        p += wtrack_data_field(p, order[k], ZERO);
    }
    return (size_t)(p - out);
}
```

- [ ] **Step 4: Run tests**

Run: `make -C firmware test`
Expected: every test binary passes, including `test_wtrack.c: N checks, 0 failed`, and `check_wrtgate: ok`.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/wtrack.h firmware/src/wtrack.c firmware/test/test_wtrack.c firmware/test/Makefile
git commit -m "feat(fw): wtrack builds the ROM's data-field and track images"
```

---

### Task 3: Decoder `header` callback

**Files:**
- Modify: `firmware/src/flux_dec.h`, `firmware/src/flux_dec.c`, `firmware/test/test_flux_dec.c`

**Interfaces:**
- Consumes: existing decoder.
- Produces: `flux_dec_sink_t` gains `void (*header)(void *ctx, int track, int side, int sector);` as its **last** field (after `ctx`). Called once per address field whose checksum and `DE AA` epilog are good, with the header's own track, side and sector, before the foreign/range checks and before that sector's data field is decoded.

- [ ] **Step 1: Write the failing tests**

In `firmware/test/test_flux_dec.c`, change the two positional sink initializers to designated ones (a fourth field would otherwise trip `-Wmissing-field-initializers`):

```c
    flux_dec_sink_t k = { .sector = on_sector, .event = on_event, .ctx = s };    // in decode()
    flux_dec_sink_t k = { .sector = on_sector, .event = on_event, .ctx = &s };   // in test_pll_recovers_from_noise()
```

Add above `main()`:

```c
typedef struct { int n, nsec, track, side, sec[16], nsec_at[16]; } hdr_log_t;

static void hl_hdr(void *ctx, int track, int side, int sector) {
    hdr_log_t *h = ctx;
    if (h->n < 16) { h->sec[h->n] = sector; h->nsec_at[h->n] = h->nsec; }
    h->n++; h->track = track; h->side = side;
}
static void hl_sec(void *ctx, int sector, const uint8_t *p) { (void)sector; (void)p; ((hdr_log_t *)ctx)->nsec++; }

static void hdr_run(const char *path, int track, int side, hdr_log_t *h) {
    int n;
    uint16_t *v = load_u16(path, &n);
    memset(h, 0, sizeof *h);
    flux_dec_t d;
    flux_dec_sink_t k = { .sector = hl_sec, .ctx = h, .header = hl_hdr };
    flux_dec_reset(&d, track, side, &k);
    flux_dec_push_many(&d, v, n);
    free(v);
}

static void test_header_callback(void) {
    hdr_log_t h;
    hdr_run(FIX "synth_clean_t00s0.u16", 0, 0, &h);
    CHECK(h.n == 12 && h.track == 0 && h.side == 0);
    int seen = 0;
    for (int i = 0; i < 12; i++) seen |= 1 << h.sec[i];
    CHECK(seen == 0xFFF);
    for (int i = 0; i < 12; i++) CHECK(h.nsec_at[i] == i);   // fires before its own data field
}

static void test_header_callback_foreign_and_bad_data(void) {
    hdr_log_t h;
    hdr_run(FIX "synth_wrongside_t03s1.u16", 3, 0, &h);   // decoder expects side 0, disk says side 1
    CHECK(h.n == 12 && h.side == 1 && h.nsec == 0);       // reported with the header's own side
    hdr_run(FIX "synth_corrupt_t20s0.u16", 20, 0, &h);    // one bad data field, all headers good
    CHECK(h.n == 11 && h.nsec == 10);
}
```

and call both from `main()`.

- [ ] **Step 2: Run to verify it fails**

Run: `make -C firmware test`
Expected: FAIL to compile, `flux_dec_sink_t has no member named 'header'`.

- [ ] **Step 3: Implement**

In `firmware/src/flux_dec.h`, change the sink:

```c
typedef struct {
    void (*sector)(void *ctx, int sector, const uint8_t payload[GCR_SECTOR_LEN]);
    void (*event)(void *ctx, flux_ev_t ev, int track, int side, int sector);
    void *ctx;
    // At a good address field's DE AA (M5 writes start here), before the foreign/range checks.
    void (*header)(void *ctx, int track, int side, int sector);
} flux_dec_sink_t;
```

In `firmware/src/flux_dec.c`, `case FD_ADDR_EPILOG`, right after `if (++d->n < 2) return;`:

```c
            if (d->sink && d->sink->header) d->sink->header(d->sink->ctx, d->addr.track, d->addr.side, d->addr.sector);
```

- [ ] **Step 4: Run tests**

Run: `make -C firmware test`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/flux_dec.h firmware/src/flux_dec.c firmware/test/test_flux_dec.c
git commit -m "feat(fw): flux_dec reports each good header at its epilog"
```

---

### Task 4: Server TX interface, armed fill, fake drive bit model

No behaviour change for reads. The fake drive moves to a bit model so later tasks can splice writes into it; all 18 existing server tests must pass unchanged in substance.

**Files:**
- Modify: `firmware/src/ipc.h`, `firmware/src/mech_server.h`, `firmware/src/mech_server.c`, `firmware/src/main.c` (init call and request struct only), `firmware/test/fake_drive.h`, `firmware/test/test_mech_server.c`, `firmware/test/test_trackbuf_ipc.c`, `firmware/test/Makefile`

**Interfaces:**
- Consumes: `flux_dec_sink_t.header` (Task 3), `WT_TRACK_MAX` (Task 2).
- Produces:
  - `ipc.h`: `ST_WRPROT 0xAB`; `typedef enum { REQ_READ, REQ_WRITE, REQ_FORMAT_TS } req_op_t;`; `req_t { req_op_t op; uint16_t block; uint8_t cyl, side; uint8_t data[GCR_SECTOR_LEN]; }`; `reply_t { uint16_t block; uint8_t status, soft, tries; uint16_t fire_us; uint8_t data[GCR_SECTOR_LEN]; }`
  - `mech_server.c` (static): `bool fill(mech_server_t *s, int track, int side, uint64_t t_req, uint16_t want, int arm, int revs_max)`, `bool motor_up(mech_server_t *s)`, `uint16_t all_sectors(int track)`
  - `mech_server.h`: `flux_tx_t { void (*load)(void *ctx, const uint8_t *buf, size_t len); void (*fire)(void *ctx); bool (*busy)(void *ctx); void (*abort)(void *ctx); void *ctx; }`; `void mech_server_init(mech_server_t *s, const mech_bus_t *bus, const flux_src_t *src, const flux_tx_t *tx);`; constants `SRV_SEL_SETTLE_US 100`, `SRV_ERASE_TAIL_US 620`, `SRV_TX_POLL_US 100`; server fields `tx, arm_sector, fired, read_us, fire_us`
  - `fake_drive.h`: `fake_flux_tx(fake_drive_t *)`, fields `wprot, tx_loads, tx_fires, tx_aborts, gate_low, tx_hang, fire_delay_us, corrupt_writes, idle_bit, addr_while_writing, wbits[80][2]`, `fd_rev_bits(int track)`

- [ ] **Step 1: Write the failing test**

In `firmware/test/test_mech_server.c`, add `static flux_tx_t T;` and change `setup()`:

```c
static void setup(void) {
    fake_drive_init(&D);
    B = fake_drive_bus(&D);
    S = fake_flux_src(&D);
    T = fake_flux_tx(&D);
    mech_server_init(&SRV, &B, &S, &T);
}
```

Add a test and call it from `main()`:

```c
static void test_reads_never_touch_tx(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    mech_server_read(&SRV, 1599, &r);
    CHECK(r.status == ST_OK && D.tx_loads == 0 && D.tx_fires == 0 && !D.gate_low);
}
```

In `firmware/test/test_trackbuf_ipc.c`, change `req_t r = {REQ_READ, 0};` to `req_t r = { .op = REQ_READ, .block = 0 };` and add after the existing reply checks:

```c
    memset(&a, 0, sizeof a); a.block = 0x8007; a.tries = 3; a.fire_us = 1234;
    CHECK(ipc_reply_push(&q, &a) && ipc_reply_pop(&q, &b));
    CHECK(b.block == 0x8007 && b.tries == 3 && b.fire_us == 1234);
    req_t w = { .op = REQ_WRITE, .block = 9 };
    w.data[0] = 0x11; w.data[523] = 0x22;
    CHECK(ipc_req_push(&q, &w) && ipc_req_pop(&q, &r));
    CHECK(r.op == REQ_WRITE && r.block == 9 && r.data[0] == 0x11 && r.data[523] == 0x22);
```

In `firmware/test/Makefile`, add `../src/wtrack.c` to the `test_mech_server` rule's sources.

- [ ] **Step 2: Run to verify it fails**

Run: `make -C firmware test`
Expected: FAIL to compile (`fake_flux_tx` undeclared, `tries` not a member).

- [ ] **Step 3: Implement `ipc.h`**

Replace the status block and the request/reply types:

```c
#define ST_OK       0x00
#define ST_SOFT     0xE7   // ok after soft errors (ROM: $E7)
#define ST_IOERR    0xA7   // SmartPort $27 with the ROM's $80 bit
#define ST_NODISK   0xAF   // $2F
#define ST_BADBLK   0xAD   // $2D (also a bad cyl/side on REQ_FORMAT_TS)
#define ST_WRPROT   0xAB   // $2B

typedef enum { REQ_READ, REQ_WRITE, REQ_FORMAT_TS } req_op_t;
typedef struct {
    req_op_t op;
    uint16_t block;                 // REQ_READ / REQ_WRITE; REQ_FORMAT_TS: a tag the reply echoes
    uint8_t cyl, side;              // REQ_FORMAT_TS
    uint8_t data[GCR_SECTOR_LEN];   // REQ_WRITE: 12 tag + 512 data
} req_t;
typedef struct {
    uint16_t block;
    uint8_t status;
    uint8_t soft;
    uint8_t tries;                  // REQ_WRITE / REQ_FORMAT_TS: writes used
    uint16_t fire_us;               // REQ_WRITE: header interval read -> TX fired, last write
    uint8_t data[GCR_SECTOR_LEN];   // REQ_READ: 12 tag + 512 data; zeros on error
} reply_t;
```

In `firmware/src/main.c`, `request()`: change `req_t rq = { REQ_READ, block };` to `req_t rq = { .op = REQ_READ, .block = block };`. (Task 8 reworks `request()`.)

- [ ] **Step 4: Implement the server changes**

In `firmware/src/mech_server.h`: add `#include <stddef.h>` and `#include "wtrack.h"`, and below `flux_src_t`:

```c
#define SRV_SEL_SETTLE_US     100     // ERS86 p24 T6: SEL settled before /WRTGATE low
#define SRV_ERASE_TAIL_US     620     // ERS86 p24/p27: after /WRTGATE high, RD valid and /STEP allowed
#define SRV_TX_POLL_US        100

// The write side. The PIO program raises /WRTGATE itself at the end of the stream;
// abort() stops it and returns the pins to SIO with the gate high (also called after
// every normal finish).
typedef struct {
    void (*load)(void *ctx, const uint8_t *buf, size_t len);   // armed, not running; gate high
    void (*fire)(void *ctx);                                   // start: gate low, stream
    bool (*busy)(void *ctx);                                   // false once the gate is back high
    void (*abort)(void *ctx);
    void *ctx;
} flux_tx_t;
```

Add to `mech_server_t` (after `src`): `const flux_tx_t *tx;` and (after `foreign_track`):

```c
    int arm_sector;         // >= 0: fire the TX at this sector's header (fill)
    bool fired;
    uint64_t read_us;       // when the interval being decoded was read
    uint16_t fire_us;
```

Change the prototype: `void mech_server_init(mech_server_t *s, const mech_bus_t *bus, const flux_src_t *src, const flux_tx_t *tx);`

In `firmware/src/mech_server.c`:
- Move `static uint64_t now_us(const mech_server_t *s)` to just below `rev_ms`.
- Add after `on_event`:

```c
static void on_header(void *ctx, int track, int side, int sector) {
    mech_server_t *s = ctx;
    if (s->arm_sector < 0 || s->fired) return;
    if (track != s->tb.track || side != s->tb.side || sector != s->arm_sector) return;
    s->tx->fire(s->tx->ctx);                           // first: every us here moves the splice
    s->fired = true;
    uint64_t dt = now_us(s) - s->read_us;
    s->fire_us = dt > 0xFFFFu ? 0xFFFFu : (uint16_t)dt;
}

static uint16_t all_sectors(int track) { return (uint16_t)((1u << gcr_sectors_per_track(track)) - 1u); }

static bool motor_up(mech_server_t *s) {
    if (s->motor_on) return true;
    if (mech_motor(s->bus, &s->ms, true) != MECH_OK) return false;
    s->motor_on = true;
    return true;
}
```

- In `mech_server_init`: add parameter `tx`, set `s->tx = tx; s->arm_sector = -1; s->sink.header = on_header;`.
- Replace `fill()` with:

```c
// Read loop on (track, side). Ends true once every sector in `want` is in the track buffer, or,
// with arm >= 0, once the TX has fired at sector `arm`'s header (one interval per read while armed).
// Gives up after revs_max revolutions (reads and header search: SRV_REVS_MAX).
static bool fill(mech_server_t *s, int track, int side, uint64_t t_req, uint16_t want, int arm, int revs_max) {
    s->stats.fills++;
    trackbuf_start(&s->tb, track, side);
    s->arm_sector = arm;
    s->fired = false;
    if (!motor_up(s)) return false;
    int batch = arm >= 0 ? 1 : SRV_BATCH;
    int seeks = 0, revs = 0;
    bool recal_done = false;
    for (;;) {                                         // one pass per (re)seek
        if (now_us(s) - t_req > (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u) return false;
        if (seeks >= SRV_SEEK_TRIES_MAX) return false;
        if (seeks == 2) {
            mech_recal(s->bus, &s->ms); s->soft |= SOFT_RECAL;
            if (now_us(s) - t_req > (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u) return false;
        }
        if (mech_seek(s->bus, &s->ms, track) != MECH_OK) { seeks++; s->ms.track_known = false; continue; }
        if (now_us(s) - t_req > (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u) return false;
        if (arm >= 0 && mech_status(s->bus, &s->ms, MREG_READY)) { seeks++; continue; }   // ERS86 T1
        mech_select_head(s->bus, side);
        if (arm >= 0) s->bus->delay_us(s->bus->ctx, SRV_SEL_SETTLE_US);                  // ERS86 T6
        s->src->start(s->src->ctx);
        flux_dec_reset(&s->dec, track, side, &s->sink);
        s->foreign_seen = false;
        uint64_t rev_t = now_us(s);
        uint32_t rev_len = rev_ms[track / 16] * 1000u;
        bool reseek = false;
        while (!reseek) {
            int n = s->src->read(s->src->ctx, s->batch, batch);
            if (n < 0) { s->soft |= SOFT_OVERRUN; flux_dec_reset(&s->dec, track, side, &s->sink); }
            if (n > 0) {
                uint64_t d0 = now_us(s);
                s->read_us = d0;
                flux_dec_push_many(&s->dec, s->batch, n);
                uint64_t d1 = now_us(s);
                s->stats.decode_us += d1 - d0;
                s->stats.intervals += (uint32_t)n;
            }
            if (s->fired) { s->src->stop(s->src->ctx); return true; }
            if (want && (s->tb.present & want) == want) { s->src->stop(s->src->ctx); return true; }
            uint64_t now = now_us(s);
            if (now - t_req > (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u) { s->src->stop(s->src->ctx); return false; }
            if (s->foreign_seen) {                     // ROM: resync position to the header, re-seek
                s->src->stop(s->src->ctx);
                s->ms.track = s->foreign_track;
                s->ms.track_known = true;
                seeks++;
                reseek = true;
                continue;
            }
            if (now - rev_t >= rev_len) {
                revs++;
                rev_t = now;
                if (revs >= revs_max) { s->src->stop(s->src->ctx); return false; }
                if (revs == SRV_RECAL_AFTER_REVS && !recal_done) {
                    recal_done = true;
                    s->src->stop(s->src->ctx);
                    mech_recal(s->bus, &s->ms);
                    s->soft |= SOFT_RECAL;
                    if (now_us(s) - t_req > (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u) return false;
                    reseek = true;
                }
            }
        }
    }
}
```

- In `mech_server_read`: `fill(s, track, side, t_req, all_sectors(track), -1, SRV_REVS_MAX);`
- In `mech_server_start` and `mech_server_stop`: call `s->tx->abort(s->tx->ctx);` first.

In `firmware/src/main.c` (temporary until Task 8): add a do-nothing table so the build links, and pass it:

```c
static void nop_load(void *c, const uint8_t *b, size_t n) { (void)c; (void)b; (void)n; }
static void nop_v(void *c) { (void)c; }
static bool nop_busy(void *c) { (void)c; return false; }
static const flux_tx_t no_tx = { nop_load, nop_v, nop_busy, nop_v, NULL };
...
    mech_server_init(&srv, hw_mech_bus(), &hw_src, &no_tx);
```

- [ ] **Step 5: Implement the fake drive bit model**

Replace `firmware/test/fake_drive.h` with:

```c
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "mech.h"
#include "mech_server.h"
#include "gcr.h"
#include "wtrack.h"

// A fake Sony mechanism: decodes command strobes, answers status reads, serves a
// GCR flux stream for the current track and selected head, and records writes.
// Bit model: a revolution is a bit string (1 = flux transition). A track side that
// was never written is generated each revolution (6-sync gaps, coprime stride, the
// fault knobs apply). A written track side is a stored image one revolution long;
// the fake TX splices the streamed bits into it (fault knobs no longer apply there).
#define FD_REV_BITS_MAX 80000

// One revolution in bit cells: 489600 bit/s x 60 s / rpm (394/429/472/525/590 by zone).
static int fd_rev_bits(int track) {
    static const int rpm[5] = { 394, 429, 472, 525, 590 };
    return 29376000 / rpm[track / 16];
}

typedef struct {
    // mechanism state
    int pos, dir;                 // physical track, +1 inward / -1 outward
    bool motor, disk_in, wprot;   // wprot: tab set, WRPROT reads low, nothing is recorded
    uint8_t addr;
    int side;                     // last head selected via RDDATA0/1
    int step_strobes, starts;     // counters for the tests
    uint32_t step_cost_us;        // extra time each STEP strobe takes (slow mechanism)
    bool ready_stuck;             // READY reads not-ready once a step happened since motor-on
    int steps_since_motor;
    uint64_t now_us;              // shared clock (bus delays + flux time)
    int addr_while_writing;       // CA/SEL changes while the gate was low (must stay 0)
    // flux stream
    bool streaming, no_flux;
    int skew;                     // headers report pos + skew (positioning fault)
    int foreign_always;           // >=0: every header claims this track
    int bad_sector, bad_revs;     // corrupt this sector's data for the first bad_revs revolutions
    int overrun_once;             // next read returns -1 once
    bool overrun_always;          // every read returns -1 (1 ms passes per call)
    int overrun_after, reads;     // >0: read number overrun_after returns -1 once
    uint8_t cur[FD_REV_BITS_MAX]; // this revolution, one byte per bit
    int ncur;
    uint16_t ivs[65536];
    int ivend[65536];             // bit index just past interval i's transition
    int nivs, rd, revs;
    // written track sides: packed bits, wbits = 0 if never written
    uint8_t wimg[GCR_TRACKS][GCR_SIDES][FD_REV_BITS_MAX / 8];
    int wbits[GCR_TRACKS][GCR_SIDES];
    // fake TX
    const uint8_t *tx_buf;
    size_t tx_len;
    int tx_loads, tx_fires, tx_aborts;
    bool gate_low;                // between fire and abort
    uint64_t tx_end_us;
    bool tx_hang;                 // busy never clears
    uint32_t fire_delay_us;       // the write lands this long after the fire call
    int corrupt_writes;           // the next N writes land with the nibble at tx_len/2 changed
    int idle_bit;                 // where a write starts when the stream is not running
} fake_drive_t;

static void fd_set_addr(void *c, uint8_t a) {
    fake_drive_t *d = c; d->addr = a;
    if (d->gate_low) d->addr_while_writing++;
    if (a == MREG_RDDATA0) d->side = 0;
    if (a == MREG_RDDATA1) d->side = 1;
}
static void fd_set_lstrb(void *c, bool high) {
    fake_drive_t *d = c;
    if (!high) return;
    switch (d->addr) {
    case MCMD_DIR_IN:    d->dir = +1; break;
    case MCMD_DIR_OUT:   d->dir = -1; break;
    case MCMD_STEP:
        d->step_strobes++;
        d->steps_since_motor++;
        d->now_us += d->step_cost_us;
        d->pos += d->dir;
        if (d->pos < 0) d->pos = 0;
        if (d->pos > 79) d->pos = 79;
        break;
    case MCMD_MOTOR_ON:  d->motor = true; d->steps_since_motor = 0; break;
    case MCMD_MOTOR_OFF: d->motor = false; break;
    default: break;
    }
}
static bool fd_read_rd(void *c) {
    fake_drive_t *d = c;
    switch (d->addr) {
    case MREG_CSTIN:   return !d->disk_in;
    case MREG_STEP:    return true;
    case MREG_WRPROT:  return !d->wprot;
    case MREG_MOTORON: return !d->motor;
    case MREG_TK0:     return d->pos != 0;
    case MREG_READY:   if (d->ready_stuck && d->steps_since_motor > 0) return true;
                       return !(d->motor && d->disk_in);
    case MREG_DRVIN:   return false;
    default:           return true;
    }
}
static void fd_delay(void *c, uint32_t us) { ((fake_drive_t *)c)->now_us += us; }

static mech_bus_t fake_drive_bus(fake_drive_t *d) {
    mech_bus_t b = { fd_set_addr, fd_set_lstrb, fd_read_rd, fd_delay, d };
    return b;
}

static uint8_t fd_payload_byte(int t, int s, int sec, int i) { return (uint8_t)(t * 7 + s * 3 + sec + i); }

static void fd_put_bits(fake_drive_t *d, const uint8_t *bytes, int n) {
    for (int k = 0; k < n; k++)
        for (int b = 7; b >= 0; b--)
            if (d->ncur < FD_REV_BITS_MAX) d->cur[d->ncur++] = (uint8_t)((bytes[k] >> b) & 1);
}
static void fd_put_sync(fake_drive_t *d) {         // 10-bit self-sync byte
    static const uint8_t ff = 0xFF;
    fd_put_bits(d, &ff, 1);
    for (int i = 0; i < 2 && d->ncur < FD_REV_BITS_MAX; i++) d->cur[d->ncur++] = 0;
}

// The generated revolution for (pos, side); rev 0.. lets bad_revs corrupt early revolutions.
static void fd_generate(fake_drive_t *d) {
    d->ncur = 0;
    int t = d->pos, hdr_t = d->foreign_always >= 0 ? d->foreign_always : d->pos + d->skew;
    int spt = gcr_sectors_per_track(t);
    int stride = 3;                                // any stride coprime to spt gives a permutation
    while (spt % stride == 0) stride += 2;
    for (int k = 0; k < spt; k++) {
        int sec = (k * stride) % spt;              // fixed interleave; must visit every sector
        uint8_t a[GCR_ADDR_FIELD_LEN], p[GCR_SECTOR_LEN], f[GCR_DATA_FIELD_LEN];
        for (int i = 0; i < GCR_SECTOR_LEN; i++) p[i] = fd_payload_byte(hdr_t, d->side, sec, i);   // content belongs to the track the header claims
        gcr_encode_address(hdr_t < 0 ? 0 : (hdr_t > 127 ? 127 : hdr_t), sec, d->side, a);
        gcr_encode_data(sec, p, f);
        if (sec == d->bad_sector && d->revs < d->bad_revs) f[60] = gcr_nibbles[(gcr_decode6(f[60]) + 1) % 64];
        for (int i = 0; i < 6; i++) fd_put_sync(d);
        fd_put_bits(d, a, GCR_ADDR_FIELD_LEN);
        for (int i = 0; i < 6; i++) fd_put_sync(d);
        fd_put_bits(d, f, GCR_DATA_FIELD_LEN);
    }
    for (int i = 0; i < 6; i++) fd_put_sync(d);
}

static int fd_wget(const fake_drive_t *d, int t, int s, int i) { return (d->wimg[t][s][i >> 3] >> (7 - (i & 7))) & 1; }
static void fd_wset(fake_drive_t *d, int t, int s, int i, int v) {
    uint8_t m = (uint8_t)(0x80u >> (i & 7));
    if (v) d->wimg[t][s][i >> 3] |= m; else d->wimg[t][s][i >> 3] &= (uint8_t)~m;
}

static void fd_to_ivs(fake_drive_t *d) {
    float cell = 1e9f / 489600.0f / 40.0f;
    int run = 0;
    d->nivs = 0;
    for (int i = 0; i < d->ncur; i++) {
        run++;
        if (d->cur[i]) {
            float t = (float)run * cell - 2.0f;
            if (d->nivs < 65536) { d->ivs[d->nivs] = (uint16_t)(t + 0.5f); d->ivend[d->nivs] = i + 1; d->nivs++; }
            run = 0;
        }
    }
}

static void fd_build(fake_drive_t *d) {
    int t = d->pos, s = d->side;
    if (d->wbits[t][s]) {
        d->ncur = d->wbits[t][s];
        for (int i = 0; i < d->ncur; i++) d->cur[i] = (uint8_t)fd_wget(d, t, s, i);
    } else {
        fd_generate(d);
    }
    fd_to_ivs(d);
}

// First write to (pos, side): store what is there now, padded with sync to one revolution.
static void fd_materialize(fake_drive_t *d) {
    int t = d->pos, s = d->side;
    if (d->wbits[t][s]) return;
    if (!d->streaming) fd_generate(d);             // streaming: cur already holds this revolution
    int n = fd_rev_bits(t);
    for (int i = 0; i < n; i++) fd_wset(d, t, s, i, i < d->ncur ? d->cur[i] : ((i - d->ncur) % 10) < 8);
    d->wbits[t][s] = n;
}

static void fd_splice(fake_drive_t *d, int start, const uint8_t *buf, size_t len) {
    int t = d->pos, s = d->side;
    fd_materialize(d);
    int n = d->wbits[t][s];
    for (size_t k = 0; k < len * 8; k++)
        fd_wset(d, t, s, (int)(((size_t)start + k) % (size_t)n), (buf[k >> 3] >> (7 - (k & 7))) & 1);
}

static void fs_start(void *c) { fake_drive_t *d = c; d->streaming = true; d->starts++; d->reads = 0; d->rd = 0; d->revs = 0; fd_build(d); }
static void fs_stop(void *c)  { ((fake_drive_t *)c)->streaming = false; }
static int fs_read(void *c, uint16_t *buf, int max) {
    fake_drive_t *d = c;
    if (!d->streaming) return 0;
    if (d->overrun_always) { d->now_us += 1000; return -1; }
    d->reads++;
    if (d->overrun_after && d->reads == d->overrun_after) return -1;
    if (d->overrun_once) { d->overrun_once = 0; return -1; }
    if (d->no_flux || !d->disk_in || !d->motor) { d->now_us += 1000; return 0; }
    int n = 0;
    while (n < max) {
        if (d->rd >= d->nivs) { d->revs++; d->rd = 0; fd_build(d); }
        uint16_t v = d->ivs[d->rd++];
        buf[n++] = v;
        d->now_us += (uint64_t)v * 40u / 1000u + 1u;   // ~ interval length in us
    }
    return n;
}
static uint64_t fs_now_us(void *c) { return ((fake_drive_t *)c)->now_us; }

static flux_src_t fake_flux_src(fake_drive_t *d) {
    flux_src_t s = { fs_start, fs_stop, fs_read, fs_now_us, d };
    return s;
}

static void ft_load(void *c, const uint8_t *buf, size_t len) {
    fake_drive_t *d = c; d->tx_buf = buf; d->tx_len = len; d->tx_loads++;
}
static void ft_fire(void *c) {
    fake_drive_t *d = c;
    d->tx_fires++;
    d->gate_low = true;
    d->tx_end_us = d->now_us + d->fire_delay_us + (uint64_t)d->tx_len * 16340u / 1000u;
    if (d->wprot || !d->motor || !d->disk_in) return;          // the drive records only then
    static uint8_t w[WT_TRACK_MAX];
    memcpy(w, d->tx_buf, d->tx_len);
    if (d->corrupt_writes > 0) {
        d->corrupt_writes--;
        size_t m = d->tx_len / 2;
        int v = gcr_decode6(w[m]);
        if (v >= 0) w[m] = gcr_nibbles[(v + 1) % 64];
    }
    int delay_bits = (int)((double)d->fire_delay_us * 489600.0 / 1e6);
    int start = (d->streaming && d->rd > 0 ? d->ivend[d->rd - 1] : d->idle_bit) + delay_bits;
    fd_splice(d, start, w, d->tx_len);
}
static bool ft_busy(void *c) { fake_drive_t *d = c; return d->tx_hang || d->now_us < d->tx_end_us; }
static void ft_abort(void *c) { fake_drive_t *d = c; d->tx_aborts++; d->gate_low = false; }

static flux_tx_t fake_flux_tx(fake_drive_t *d) {
    flux_tx_t t = { ft_load, ft_fire, ft_busy, ft_abort, d };
    return t;
}

static void fake_drive_init(fake_drive_t *d) {
    memset(d, 0, sizeof *d);
    d->disk_in = true;
    d->pos = 37;                  // somewhere unknown
    d->bad_sector = -1;
    d->foreign_always = -1;
    d->idle_bit = 1234;
}
```

- [ ] **Step 6: Run tests**

Run: `make -C firmware test && make -C firmware`
Expected: all host tests pass (the 18 existing server tests plus `test_reads_never_touch_tx`); the firmware builds.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/ipc.h firmware/src/mech_server.h firmware/src/mech_server.c firmware/src/main.c firmware/test/
git commit -m "refactor(fw): server TX interface, armed fill, fake drive bit model"
```

---

### Task 5: Block write (`mech_server_write`)

**Files:**
- Modify: `firmware/src/mech_server.h`, `firmware/src/mech_server.c`, `firmware/test/test_mech_server.c`

**Interfaces:**
- Consumes: `fill(..., want, arm)`, `motor_up`, `on_header`, `flux_tx_t`, `wtrack_data_field`, `WT_DATA_FIELD_LEN`, fake drive (Task 4).
- Produces: `void mech_server_write(mech_server_t *s, uint16_t block, const uint8_t data[GCR_SECTOR_LEN], reply_t *r);`, `SRV_WRITE_TRIES 3`, `SRV_VERIFY_REVS 2`, `SRV_TX_CAP_US(len)`, server field `uint8_t wbuf[WT_DATA_FIELD_LEN]`, static `tx_finish(s, len)` (used by Task 6).

- [ ] **Step 1: Write the failing tests**

Add to `firmware/test/test_mech_server.c`:

```c
static void pattern(uint8_t *p, int seed) { for (int i = 0; i < GCR_SECTOR_LEN; i++) p[i] = (uint8_t)(i * 13 + seed); }

static void test_write_then_read_not_stale(void) {
    setup();
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN];
    pattern(p, 1);
    mech_server_read(&SRV, 30, &r);                  // t1 s0 sector 6: cached now
    CHECK(r.status == ST_OK && data_ok(&r));
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_OK && r.block == 30 && r.tries == 1 && r.soft == 0);
    CHECK(D.tx_fires == 1 && !D.gate_low && D.addr_while_writing == 0);
    CHECK(r.fire_us < 100);
    mech_server_read(&SRV, 30, &r);
    CHECK(r.status == ST_OK && memcmp(r.data, p, GCR_SECTOR_LEN) == 0);
    mech_server_read(&SRV, 31, &r);                  // neighbour untouched
    CHECK(r.status == ST_OK && data_ok(&r));
    mech_server_read(&SRV, 29, &r);
    CHECK(r.status == ST_OK && data_ok(&r));
}

static void test_write_protected(void) {
    setup();
    D.wprot = true;
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN] = {0};
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_WRPROT && D.tx_fires == 0 && D.step_strobes == 0 && !D.motor);
}

static void test_write_no_disk_and_range(void) {
    setup();
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN] = {0};
    mech_server_write(&SRV, 1600, p, &r);
    CHECK(r.status == ST_BADBLK && D.tx_fires == 0);
    D.disk_in = false;
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_NODISK && D.tx_fires == 0 && D.step_strobes == 0);
}

static void test_write_retry_after_bad_verify(void) {
    setup();
    D.corrupt_writes = 1;
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN];
    pattern(p, 2);
    mech_server_write(&SRV, 100, p, &r);
    CHECK(r.status == ST_SOFT && (r.soft & SOFT_BADDATA) && r.tries == 2 && D.tx_fires == 2);
    mech_server_read(&SRV, 100, &r);
    CHECK(r.status == ST_OK && memcmp(r.data, p, GCR_SECTOR_LEN) == 0);
}

static void test_write_gives_up(void) {
    setup();
    D.corrupt_writes = 100;
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN];
    pattern(p, 3);
    mech_server_write(&SRV, 100, p, &r);
    CHECK(r.status == ST_IOERR && r.tries == SRV_WRITE_TRIES && D.tx_fires == SRV_WRITE_TRIES);
    CHECK(!SRV.tb.valid && !D.gate_low);
}

static void test_write_header_missing(void) {
    setup();
    D.no_flux = true;
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN] = {0};
    uint64_t t0 = D.now_us;
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_IOERR && D.tx_fires == 0 && D.tx_aborts >= 1 && !D.gate_low);
    CHECK(D.now_us - t0 <= (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u + 200000u);
}

static void test_write_tx_hang(void) {
    setup();
    D.tx_hang = true;
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN] = {0};
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_IOERR && D.tx_fires == 1 && !D.gate_low && r.tries == 1);
}

static void test_write_late_fire(void) {
    setup();
    D.fire_delay_us = 40;                            // well inside the gap: fine
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN];
    pattern(p, 4);
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_OK);
    setup();
    D.fire_delay_us = 300;                           // past the old data mark: never verifies
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_IOERR && r.tries == SRV_WRITE_TRIES);
}

static void test_write_ignores_bad_neighbour(void) {
    setup();
    D.bad_sector = 7; D.bad_revs = 1000;             // t1 s0 sector 7 never reads
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN];
    pattern(p, 5);
    uint64_t t0 = D.now_us;
    mech_server_write(&SRV, 30, p, &r);              // sector 6 of the same track side
    CHECK(r.status == ST_OK && D.now_us - t0 < 1500000u);
}
```

Call all of them from `main()`.

- [ ] **Step 2: Run to verify it fails**

Run: `make -C firmware test`
Expected: FAIL to compile, `implicit declaration of function 'mech_server_write'`.

- [ ] **Step 3: Implement**

In `firmware/src/mech_server.h` add:

```c
#define SRV_WRITE_TRIES       3       // block write: writes per request, each verified
#define SRV_VERIFY_REVS       2       // verify reads after a write or format (ROM verify: one pass)
// Cap on one TX stream: nominal (16.34 us per byte) + 10 % + 1 ms.
#define SRV_TX_CAP_US(len)    ((uint64_t)(len) * 16340u / 1000u * 11u / 10u + 1000u)
```

the field `uint8_t wbuf[WT_DATA_FIELD_LEN];` in `mech_server_t`, and the prototype:

```c
void mech_server_write(mech_server_t *s, uint16_t block, const uint8_t data[GCR_SECTOR_LEN], reply_t *r);
```

In `firmware/src/mech_server.c`, after `fill()`:

```c
// After fire: wait for the PIO to raise the gate (or abort at the cap), then the erase tail.
static bool tx_finish(mech_server_t *s, size_t len) {
    uint64_t t0 = now_us(s);
    while (s->tx->busy(s->tx->ctx)) {
        if (now_us(s) - t0 > SRV_TX_CAP_US(len)) { s->tx->abort(s->tx->ctx); return false; }
        s->bus->delay_us(s->bus->ctx, SRV_TX_POLL_US);
    }
    s->tx->abort(s->tx->ctx);                          // pins back to SIO, gate high
    s->bus->delay_us(s->bus->ctx, SRV_ERASE_TAIL_US);  // ERS86 T7 erase tail; RD valid after
    return true;
}

void mech_server_write(mech_server_t *s, uint16_t block, const uint8_t data[GCR_SECTOR_LEN], reply_t *r) {
    memset(r, 0, sizeof *r);
    r->block = block;
    int track, side, sector;
    if (mech_status(s->bus, &s->ms, MREG_CSTIN)) {     // CSTIN high = no disk
        trackbuf_invalidate(&s->tb);
        r->status = ST_NODISK;
        return;
    }
    if (!gcr_block_to_tss(block, &track, &side, &sector)) { r->status = ST_BADBLK; return; }
    if (!mech_status(s->bus, &s->ms, MREG_WRPROT)) { r->status = ST_WRPROT; return; }   // WRPROT low = protected
    wtrack_data_field(s->wbuf, sector, data);          // encode before the search, as the ROM
    uint64_t t_req = now_us(s);
    s->soft = 0;
    bool ok = false;
    for (int w = 0; w < SRV_WRITE_TRIES && !ok; w++) {
        r->tries = (uint8_t)(w + 1);
        s->tx->load(s->tx->ctx, s->wbuf, WT_DATA_FIELD_LEN);
        if (!fill(s, track, side, t_req, 0, sector, SRV_REVS_MAX)) { s->tx->abort(s->tx->ctx); break; }   // header never seen
        r->fire_us = s->fire_us;
        if (!tx_finish(s, WT_DATA_FIELD_LEN)) break;   // TX stuck: aborted, gate high
        uint16_t want = (uint16_t)(1u << sector);
        if (fill(s, track, side, t_req, want, -1, SRV_VERIFY_REVS) && memcmp(trackbuf_get(&s->tb, sector), data, GCR_SECTOR_LEN) == 0)
            ok = true;
        else
            s->soft |= SOFT_BADDATA;
    }
    s->arm_sector = -1;
    s->last_us = now_us(s);
    r->soft = s->soft;
    if (ok) {
        r->status = s->soft ? ST_SOFT : ST_OK;
    } else {
        trackbuf_invalidate(&s->tb);
        r->status = ST_IOERR;
    }
}
```

- [ ] **Step 4: Run tests**

Run: `make -C firmware test`
Expected: all pass. If `test_write_late_fire`'s 40 us case fails, print `D.ivend[D.rd - 1]` at fire and the old data mark position before changing any timing constant; the fake's gap after `DE AA` is 60 bits (6 syncs), so 40 us (about 20 bits) must fit.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/mech_server.h firmware/src/mech_server.c firmware/test/test_mech_server.c
git commit -m "feat(fw): block write with header-triggered TX and verify"
```

---

### Task 6: Format one track side (`mech_server_format_ts`)

**Files:**
- Modify: `firmware/src/mech_server.h`, `firmware/src/mech_server.c`, `firmware/test/test_mech_server.c`

**Interfaces:**
- Consumes: `tx_finish`, `fill`, `motor_up`, `all_sectors`, `wtrack_track`, `WT_TRACK_MAX`.
- Produces: `void mech_server_format_ts(mech_server_t *s, int cyl, int side, reply_t *r);`, `SRV_FORMAT_TRIES 10`, `SRV_FORMAT_TS_TIMEOUT_MS 30000`, server field `uint8_t timg[WT_TRACK_MAX]`. Reply: `status`, `soft`, `tries`; `block` is left 0 (core 0 sets the tag).

- [ ] **Step 1: Write the failing tests**

Add to `firmware/test/test_mech_server.c`:

```c
static bool ts_zero(int t, int s) {
    reply_t r;
    for (int sec = 0; sec < gcr_sectors_per_track(t); sec++) {
        mech_server_read(&SRV, (uint16_t)gcr_tss_to_block(t, s, sec), &r);
        if (r.status != ST_OK) return false;
        for (int i = 0; i < GCR_SECTOR_LEN; i++) if (r.data[i]) return false;
    }
    return true;
}

static void test_format_ts_ok(void) {
    setup();
    reply_t r;
    mech_server_format_ts(&SRV, 5, 1, &r);
    CHECK(r.status == ST_OK && r.tries == 1 && D.tx_fires == 1 && !D.gate_low && D.addr_while_writing == 0);
    CHECK(D.wbits[5][1] == fd_rev_bits(5));
    CHECK(ts_zero(5, 1));
    mech_server_format_ts(&SRV, 79, 0, &r);           // zone 4: shortest revolution, still wraps
    CHECK(r.status == ST_OK && ts_zero(79, 0));
}

static void test_format_starts_anywhere(void) {
    setup();
    D.idle_bit = 50000;                              // no index: the write wraps over its own preamble
    reply_t r;
    mech_server_format_ts(&SRV, 3, 0, &r);
    CHECK(r.status == ST_OK && ts_zero(3, 0));
}

static void test_format_then_read_not_stale(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 24, &r);                  // t1 s0 cached
    CHECK(r.status == ST_OK && data_ok(&r));
    mech_server_format_ts(&SRV, 1, 0, &r);
    CHECK(r.status == ST_OK && ts_zero(1, 0));
}

static void test_format_protected_and_args(void) {
    setup();
    reply_t r;
    mech_server_format_ts(&SRV, 80, 0, &r);
    CHECK(r.status == ST_BADBLK);
    mech_server_format_ts(&SRV, 0, 2, &r);
    CHECK(r.status == ST_BADBLK);
    D.wprot = true;
    mech_server_format_ts(&SRV, 0, 0, &r);
    CHECK(r.status == ST_WRPROT && D.tx_fires == 0 && D.step_strobes == 0);
    D.wprot = false; D.disk_in = false;
    mech_server_format_ts(&SRV, 0, 0, &r);
    CHECK(r.status == ST_NODISK && D.tx_fires == 0);
}

static void test_format_gives_up_after_10(void) {
    setup();
    D.corrupt_writes = 1000;                         // the middle of every image is a data nibble
    reply_t r;
    mech_server_format_ts(&SRV, 20, 1, &r);
    CHECK(r.status == ST_IOERR && r.tries == SRV_FORMAT_TRIES && D.tx_fires == SRV_FORMAT_TRIES);
    CHECK(!SRV.tb.valid && !D.gate_low);
}

static void test_format_retry_then_ok(void) {
    setup();
    D.corrupt_writes = 2;
    reply_t r;
    mech_server_format_ts(&SRV, 40, 0, &r);
    CHECK(r.status == ST_SOFT && r.tries == 3 && (r.soft & SOFT_BADDATA) && ts_zero(40, 0));
}

static void test_format_tx_hang(void) {
    setup();
    D.tx_hang = true;
    reply_t r;
    mech_server_format_ts(&SRV, 0, 0, &r);
    CHECK(r.status == ST_IOERR && D.tx_fires == 1 && !D.gate_low);
}

static void test_write_on_board_formatted_track(void) {
    setup();
    reply_t r;
    mech_server_format_ts(&SRV, 1, 0, &r);
    CHECK(r.status == ST_OK);
    uint8_t p[GCR_SECTOR_LEN];
    pattern(p, 9);
    mech_server_write(&SRV, 30, p, &r);              // ROM gaps: DE AA FF | FF 3F CF F3 FC FF D5 AA AD
    CHECK(r.status == ST_OK && r.tries == 1);
    mech_server_read(&SRV, 30, &r);
    CHECK(r.status == ST_OK && memcmp(r.data, p, GCR_SECTOR_LEN) == 0);
    mech_server_read(&SRV, 31, &r);
    CHECK(r.status == ST_OK);
    for (int i = 0; i < GCR_SECTOR_LEN; i++) CHECK(r.data[i] == 0);
}
```

Call all of them from `main()`.

- [ ] **Step 2: Run to verify it fails**

Run: `make -C firmware test`
Expected: FAIL to compile, `implicit declaration of function 'mech_server_format_ts'`.

- [ ] **Step 3: Implement**

In `firmware/src/mech_server.h` add:

```c
#define SRV_FORMAT_TRIES          10      // ROM cmd_format_actual: spb_packet_type = $0A
#define SRV_FORMAT_TS_TIMEOUT_MS  30000   // one track side, all tries
```

the field `uint8_t timg[WT_TRACK_MAX];` in `mech_server_t`, and the prototype:

```c
void mech_server_format_ts(mech_server_t *s, int cyl, int side, reply_t *r);
```

In `firmware/src/mech_server.c`, after `mech_server_write`:

```c
static bool track_all_zero(const mech_server_t *s) {
    for (int sec = 0; sec < s->tb.nsect; sec++) {
        const uint8_t *p = trackbuf_get(&s->tb, sec);
        if (!p) return false;
        for (int i = 0; i < GCR_SECTOR_LEN; i++) if (p[i]) return false;
    }
    return true;
}

// ROM cmd_format_actual for one track side: one write of the whole image (longer than a
// revolution; the tail lands in the preamble), then a verify read. No index: starts anywhere.
void mech_server_format_ts(mech_server_t *s, int cyl, int side, reply_t *r) {
    memset(r, 0, sizeof *r);
    if (cyl < 0 || cyl >= GCR_TRACKS || side < 0 || side >= GCR_SIDES) { r->status = ST_BADBLK; return; }
    if (mech_status(s->bus, &s->ms, MREG_CSTIN)) {
        trackbuf_invalidate(&s->tb);
        r->status = ST_NODISK;
        return;
    }
    if (!mech_status(s->bus, &s->ms, MREG_WRPROT)) { r->status = ST_WRPROT; return; }
    trackbuf_invalidate(&s->tb);
    size_t len = wtrack_track(s->timg, sizeof s->timg, cyl, side);
    uint64_t t0 = now_us(s);
    s->soft = 0;
    bool ok = false;
    for (int k = 0; k < SRV_FORMAT_TRIES && !ok; k++) {
        if (now_us(s) - t0 > (uint64_t)SRV_FORMAT_TS_TIMEOUT_MS * 1000u) break;
        r->tries = (uint8_t)(k + 1);
        if (!motor_up(s)) break;
        if (mech_seek(s->bus, &s->ms, cyl) != MECH_OK) { s->ms.track_known = false; s->soft |= SOFT_RECAL; continue; }
        if (mech_status(s->bus, &s->ms, MREG_READY)) continue;          // ERS86 T1: READY low first
        mech_select_head(s->bus, side);
        s->bus->delay_us(s->bus->ctx, SRV_SEL_SETTLE_US);             // ERS86 T6
        s->tx->load(s->tx->ctx, s->timg, len);
        s->tx->fire(s->tx->ctx);
        if (!tx_finish(s, len)) break;
        if (fill(s, cyl, side, now_us(s), all_sectors(cyl), -1, SRV_VERIFY_REVS) && track_all_zero(s)) ok = true;
        else s->soft |= SOFT_BADDATA;
    }
    if (!ok) trackbuf_invalidate(&s->tb);
    s->last_us = now_us(s);
    r->soft = s->soft;
    r->status = ok ? (s->soft ? ST_SOFT : ST_OK) : ST_IOERR;
}
```

- [ ] **Step 4: Run tests**

Run: `make -C firmware test`
Expected: all pass. If `test_format_gives_up_after_10` passes with fewer than 10 fires, check that byte `len / 2` of each zone's image is a data nibble (it is: offset 260 into a sector image for every zone).

- [ ] **Step 5: Commit**

```bash
git add firmware/src/mech_server.h firmware/src/mech_server.c firmware/test/test_mech_server.c
git commit -m "feat(fw): format one track side in the ROM layout, with verify"
```

---

### Task 7: `flux_tx` PIO driver and the /WRTGATE fence

Hardware code: no host test. The build, `check_wrtgate.sh` and the bench (Task 10) check it.

**Files:**
- Create: `firmware/src/flux_tx.pio`, `firmware/src/flux_tx.h`, `firmware/src/flux_tx.c`
- Modify: `firmware/CMakeLists.txt`, `firmware/test/check_wrtgate.sh`, `firmware/src/pins.h` (comment)

**Interfaces:**
- Consumes: `PIN_MECH_WRTGATE` (18), `PIN_MECH_WRTDATA` (19), PIO1 (SM0 is `flux_rx`).
- Produces: `void flux_tx_init(void); void flux_tx_load(const uint8_t *buf, size_t len); void flux_tx_fire(void); bool flux_tx_busy(void); void flux_tx_abort(void);`

- [ ] **Step 1: Tighten the fence first (fails until flux_tx.c exists)**

Replace `firmware/test/check_wrtgate.sh`:

```sh
#!/bin/sh
# /WRTGATE low erases the track under the head. Allowed references: the define in pins.h,
# the idle table and the ADDR_MASK static_assert in hw.c, and flux_tx.c (the only writer, M5).
cd "$(dirname "$0")/../src" || exit 1
hits=$(grep -n "PIN_MECH_WRTGATE" ./*.c ./*.h | grep -v "^./pins.h:" | grep -v "^./flux_tx.c:" | grep -v "^./hw.c:.*{PIN_MECH_WRTGATE, 1}" | grep -v "^./hw.c:.*_Static_assert((ADDR_MASK & (1u << PIN_MECH_WRTGATE))")
if [ -n "$hits" ]; then
    echo "FAIL: /WRTGATE referenced outside pins.h, the hw.c idle table and flux_tx.c:"
    echo "$hits"
    exit 1
fi
if [ ! -f ./flux_tx.pio ] || ! grep -q "set pins, 1" ./flux_tx.pio; then
    echo "FAIL: flux_tx.pio must raise /WRTGATE itself (set pins, 1)"
    exit 1
fi
echo "check_wrtgate: ok"
```

Run: `make -C firmware test`
Expected: FAIL with `flux_tx.pio must raise /WRTGATE itself`.

- [ ] **Step 2: Write the PIO program**

Create `firmware/src/flux_tx.pio`:

```
; Write side of the IWM for the mechanism's WRTDATA and /WRTGATE.
; One bit cell = 32 PIO cycles (clkdiv sets 2.0425 us). A 1 bit toggles WRTDATA
; (ERS86 3.2.6: each level change writes a flux transition); a 0 bit leaves it.
; The CPU puts (bit count - 1) in the TX FIFO, then DMA feeds bytes, MSB first.
; /WRTGATE goes low one cell before the first possible edge (T3 >= 1.8 us) and
; high one cell after the last cell (T4 >= 2 us), then the SM parks.
; /WRTGATE uses `set pins` (side-set would leave 3 delay bits, too few).
.program flux_tx
    pull block                ; bit count - 1
    mov y, osr
    out null, 32              ; empty the OSR: autopull loads the first data byte
    set pins, 0 [31]          ; /WRTGATE low, one cell (T3)
bitloop:
    out x, 1                  ; 1
    jmp !x skip               ; 1
    mov pins, ~pins [28]      ; 29: 1 bit, toggle WRTDATA
    jmp y-- bitloop           ; 1  -> 32 cycles per cell
    jmp tail
skip:
    nop [28]                  ; 29: 0 bit
    jmp y-- bitloop           ; 1  -> 32 cycles per cell
tail:
    nop [31]                  ; T4: one more cell after the last
    set pins, 1               ; /WRTGATE high
public park:
    jmp park
```

- [ ] **Step 3: Write the driver**

Create `firmware/src/flux_tx.h`:

```c
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// TX state machine on PIO1 SM1 (M5). The PIO owns GP18/GP19 only from flux_tx_load()
// to flux_tx_abort(); otherwise they are SIO outputs at the hw.c idle levels (gate high).
void flux_tx_init(void);
void flux_tx_load(const uint8_t *buf, size_t len);   // armed, SM stopped, gate high
void flux_tx_fire(void);                              // one register write: SM enable
bool flux_tx_busy(void);                              // false once the program has raised the gate
void flux_tx_abort(void);                             // SM off, DMA off, pins back to SIO, gate high
```

Create `firmware/src/flux_tx.c`:

```c
#include "flux_tx.h"
#include "pins.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "flux_tx.pio.h"

// The only file allowed to touch /WRTGATE (firmware/test/check_wrtgate.sh).
#define TX_PIO              pio1
#define TX_SM               1
#define TX_CELL_US          2.04248366f     // 489.6 kbit/s (ERS86 2.3.2)
#define TX_CYCLES_PER_CELL  32
#define TX_PINS             ((1u << PIN_MECH_WRTGATE) | (1u << PIN_MECH_WRTDATA))

static uint offset;
static int dma_ch;
static pio_sm_config cfg;

void flux_tx_init(void) {
    offset = pio_add_program(TX_PIO, &flux_tx_program);
    pio_sm_claim(TX_PIO, TX_SM);
    cfg = flux_tx_program_get_default_config(offset);
    sm_config_set_out_pins(&cfg, PIN_MECH_WRTDATA, 1);
    sm_config_set_in_pins(&cfg, PIN_MECH_WRTDATA);              // mov pins, ~pins reads it back
    sm_config_set_set_pins(&cfg, PIN_MECH_WRTGATE, 1);
    sm_config_set_out_shift(&cfg, false, true, 8);              // MSB first, autopull per byte
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&cfg, (float)clock_get_hz(clk_sys) * TX_CELL_US / 1e6f / TX_CYCLES_PER_CELL);
    dma_ch = dma_claim_unused_channel(true);
}

void flux_tx_abort(void) {
    pio_sm_set_enabled(TX_PIO, TX_SM, false);
    if (dma_channel_is_busy(dma_ch)) dma_channel_abort(dma_ch);
    gpio_put(PIN_MECH_WRTGATE, 1);                              // SIO latch high before taking the pin back
    gpio_set_function(PIN_MECH_WRTGATE, GPIO_FUNC_SIO);
    gpio_set_function(PIN_MECH_WRTDATA, GPIO_FUNC_SIO);
}

void flux_tx_load(const uint8_t *buf, size_t len) {
    flux_tx_abort();
    pio_sm_init(TX_PIO, TX_SM, offset, &cfg);                  // stopped, FIFOs cleared, PC at start
    uint32_t level = (1u << PIN_MECH_WRTGATE) | ((uint32_t)gpio_get_out_level(PIN_MECH_WRTDATA) << PIN_MECH_WRTDATA);
    pio_sm_set_pins_with_mask(TX_PIO, TX_SM, level, TX_PINS);  // PIO outputs match the SIO levels
    pio_sm_set_pindirs_with_mask(TX_PIO, TX_SM, TX_PINS, TX_PINS);
    pio_gpio_init(TX_PIO, PIN_MECH_WRTGATE);                   // pad follows the PIO from here: gate high
    pio_gpio_init(TX_PIO, PIN_MECH_WRTDATA);
    pio_sm_put(TX_PIO, TX_SM, (uint32_t)(len * 8u - 1u));
    dma_channel_config c = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);     // byte lands in all lanes; MSB-first shift takes it
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(TX_PIO, TX_SM, true));
    dma_channel_configure(dma_ch, &c, &TX_PIO->txf[TX_SM], buf, len, true);   // pre-fills the FIFO now
}

void flux_tx_fire(void) { pio_sm_set_enabled(TX_PIO, TX_SM, true); }

bool flux_tx_busy(void) {
    bool enabled = (TX_PIO->ctrl & (1u << (PIO_CTRL_SM_ENABLE_LSB + TX_SM))) != 0;
    return enabled && pio_sm_get_pc(TX_PIO, TX_SM) != offset + flux_tx_offset_park;
}
```

In `firmware/src/pins.h`, change the GP18 comment to `// /WRTGATE, active low. Driven only by flux_tx.c (M5).`

- [ ] **Step 4: Build wiring**

In `firmware/CMakeLists.txt`:
- add `src/wtrack.c src/flux_tx.c` to `add_executable`
- add `pico_generate_pio_header(a2m_bringup ${CMAKE_CURRENT_LIST_DIR}/src/flux_tx.pio)`
- add `target_compile_definitions(a2m_bringup PRIVATE PICO_CORE1_STACK_SIZE=0x2000)` with the comment `# core 1: gcr_encode_data alone uses ~1.25 KB of stack (M4 review item)`

- [ ] **Step 5: Verify**

Run: `make -C firmware test && make -C firmware`
Expected: `check_wrtgate: ok`; the firmware links. Then `grep -c "flux_tx_offset_park" firmware/build/flux_tx.pio.h` prints at least 1.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/flux_tx.pio firmware/src/flux_tx.h firmware/src/flux_tx.c firmware/src/pins.h firmware/CMakeLists.txt firmware/test/check_wrtgate.sh
git commit -m "feat(fw): flux_tx PIO write driver; /WRTGATE fenced to flux_tx.c"
```

---

### Task 8: Console commands and core wiring

**Files:**
- Modify: `firmware/src/console.h`, `firmware/src/console.c`, `firmware/test/test_console.c`, `firmware/src/main.c`

**Interfaces:**
- Consumes: `mech_server_write`, `mech_server_format_ts`, `flux_tx_*`, `req_t`/`reply_t` (Task 4), `crc32_update`.
- Produces: `CON_WRITEBLK`, `CON_FORMAT`; console lines `SEND 512`, `BLK <n> <st> <soft> <crc32> <tries> <fire_us> <backlog>`, `FMT <cyl> <side> <tries> <st>`, `FORMAT END <st> <ms>`, `ERR timeout` (Task 9 parses these).

- [ ] **Step 1: Write the failing parser tests**

Add to `firmware/test/test_console.c` before `TEST_END()`:

```c
    CHECK(P("writeblk 0").op == CON_WRITEBLK && P("writeblk 1599").arg[0] == 1599);
    CHECK(P("writeblk 1600").op == CON_BAD && P("writeblk").op == CON_BAD);
    CHECK(P("format").op == CON_FORMAT && P("format").argc == 0);
    con_cmd_t f = P("format 79 1");
    CHECK(f.op == CON_FORMAT && f.argc == 2 && f.arg[0] == 79 && f.arg[1] == 1);
    CHECK(P("format 80 0").op == CON_BAD && P("format 3 2").op == CON_BAD);
    CHECK(P("format 3").op == CON_BAD && P("format 1 1 1").op == CON_BAD);
```

Run: `make -C firmware test`
Expected: FAIL to compile, `CON_WRITEBLK undeclared`.

- [ ] **Step 2: Implement the parser**

`console.h`: append `CON_WRITEBLK, CON_FORMAT,` to `con_op_t`.

`console.c`:
- comment on `nargs`: `// -1 = zero or one argument, -2 = zero or two`
- add specs:

```c
    {"writeblk", CON_WRITEBLK, 1, false, {0},       {1599}},
    {"format",   CON_FORMAT,  -2, false, {0, 0},    {79, 1}},
```

- the count check in `console_parse` becomes:

```c
        bool count_ok = sp->nargs == -1 ? given <= 1
                      : sp->nargs == -2 ? (given == 0 || given == 2)
                      : given == sp->nargs;
        if (!count_ok) return c;
```

- help text: add a line `"writeblk <0-1599> (then 512 raw bytes) | format [<0-79> <0|1>]"`.

Run: `make -C firmware test`
Expected: all pass.

- [ ] **Step 3: Wire core 1 and the commands in `main.c`**

- `#include "flux_tx.h"` and `#include "wtrack.h"`.
- Replace the temporary `no_tx` table with:

```c
static void hw_tx_load(void *c, const uint8_t *b, size_t n) { (void)c; flux_tx_load(b, n); }
static void hw_tx_fire(void *c)  { (void)c; flux_tx_fire(); }
static bool hw_tx_busy(void *c)  { (void)c; return flux_tx_busy(); }
static void hw_tx_abort(void *c) { (void)c; flux_tx_abort(); }
static const flux_tx_t hw_tx = { hw_tx_load, hw_tx_fire, hw_tx_busy, hw_tx_abort, NULL };
```

- `core1_main`: `mech_server_init(&srv, hw_mech_bus(), &hw_src, &hw_tx);` and replace the request handling with:

```c
        static req_t rq;
        if (ipc_req_pop(&ipc, &rq)) {
            static reply_t rp;
            switch (rq.op) {
            case REQ_READ:      mech_server_read(&srv, rq.block, &rp); break;
            case REQ_WRITE:     mech_server_write(&srv, rq.block, rq.data, &rp); break;
            case REQ_FORMAT_TS: mech_server_format_ts(&srv, rq.cyl, rq.side, &rp); rp.block = rq.block; break;
            }
            while (!ipc_reply_push(&ipc, &rp)) tight_loop_contents();
        } else {
```

- Replace `request()`:

```c
static bool request(const req_t *rq, reply_t *r, uint32_t timeout_ms) {
    if (!ipc_req_push(&ipc, rq)) return false;
    absolute_time_t until = make_timeout_time_ms(timeout_ms);
    for (;;) {
        if (ipc_reply_pop(&ipc, r)) {
            if (r->block == rq->block) return true;
            continue;                   // stale reply from an earlier timed-out request
        }
        if (time_reached(until)) { srv_wedged = true; return false; }
        tight_loop_contents();
    }
}

static bool read_request(uint16_t block, reply_t *r) {
    static req_t rq;
    memset(&rq, 0, sizeof rq);
    rq.op = REQ_READ;
    rq.block = block;
    return request(&rq, r, SRV_REQ_TIMEOUT_MS + 3000);
}
```

and change the two existing callers (`do_readblk`, `do_readdisk`) from `request(block, &r)` / `request(b, &r)` to `read_request(...)`.

- Add:

```c
// writeblk: 512 raw bytes follow the SEND line (no echo); tags are zeroed as the ROM's WriteBlock.
static void do_writeblk(uint16_t block) {
    static req_t rq;
    static reply_t r;
    if (srv_wedged) { puts("ERR server wedged (server off, then on)"); return; }
    memset(&rq, 0, sizeof rq);
    rq.op = REQ_WRITE;
    rq.block = block;
    puts("SEND 512");
    stdio_flush();
    absolute_time_t until = make_timeout_time_ms(2000);
    for (int i = 0; i < GCR_DATA_LEN; i++) {
        int ch;
        do ch = getchar_timeout_us(1000); while (ch == PICO_ERROR_TIMEOUT && !time_reached(until));
        if (ch == PICO_ERROR_TIMEOUT) { puts("ERR timeout"); return; }
        rq.data[GCR_TAG_LEN + i] = (uint8_t)ch;
    }
    uint32_t crc = crc32_update(0, rq.data + GCR_TAG_LEN, GCR_DATA_LEN);
    flux_stream_backlog_reset();
    if (!request(&rq, &r, SRV_REQ_TIMEOUT_MS + 3000)) { puts("ERR server timeout"); return; }
    printf("BLK %u %02x %02x %08lx %u %u %lu\n", block, r.status, r.soft, (unsigned long)crc,
           r.tries, r.fire_us, (unsigned long)flux_stream_backlog_max());
    puts("OK");
}

static void do_format(const con_cmd_t *c) {
    static req_t rq;
    static reply_t r;
    if (srv_wedged) { puts("ERR server wedged (server off, then on)"); return; }
    uint64_t t0 = time_us_64();
    int t_lo = c->argc ? (int)c->arg[0] : 0, t_hi = c->argc ? (int)c->arg[0] : GCR_TRACKS - 1;
    int s_lo = c->argc ? (int)c->arg[1] : 0, s_hi = c->argc ? (int)c->arg[1] : GCR_SIDES - 1;
    uint8_t status = ST_OK;
    for (int t = t_lo; t <= t_hi && (status == ST_OK || status == ST_SOFT); t++) {
        for (int s = s_lo; s <= s_hi && (status == ST_OK || status == ST_SOFT); s++) {
            memset(&rq, 0, sizeof rq);
            rq.op = REQ_FORMAT_TS;
            rq.block = (uint16_t)(0x8000u | (unsigned)(t << 1) | (unsigned)s);
            rq.cyl = (uint8_t)t;
            rq.side = (uint8_t)s;
            if (!request(&rq, &r, SRV_FORMAT_TS_TIMEOUT_MS + 3000)) { puts("ERR server timeout"); return; }
            printf("FMT %d %d %u %02x\n", t, s, r.tries, r.status);
            stdio_flush();
            if (r.status == ST_SOFT && status == ST_OK) status = ST_SOFT;
            else if (r.status != ST_OK && r.status != ST_SOFT) status = r.status;
        }
    }
    printf("FORMAT END %02x %lu\n", status, (unsigned long)((time_us_64() - t0) / 1000u));
    puts("OK");
}
```

- In `execute()`, next to `CON_READDISK`:

```c
    case CON_WRITEBLK:
        if (!atomic_load(&srv_run)) { puts("ERR server off"); return; }
        do_writeblk((uint16_t)c->arg[0]);
        return;
    case CON_FORMAT:
        if (!atomic_load(&srv_run)) { puts("ERR server off"); return; }
        do_format(c);
        return;
```

- In `main()`, after `flux_init();`: `flux_tx_init();`

- [ ] **Step 4: Verify**

Run: `make -C firmware test && make -C firmware`
Expected: all host tests pass; the firmware builds with no new warnings (`grep -i warning` on the build output is empty for `main.c`, `flux_tx.c`, `wtrack.c`, `mech_server.c`).

- [ ] **Step 5: Commit**

```bash
git add firmware/src/console.h firmware/src/console.c firmware/test/test_console.c firmware/src/main.c
git commit -m "feat(fw): writeblk and format console commands"
```

---

### Task 9: Host tool: writeblk, writedisk, format

**Files:**
- Modify: `tools/a2m/proto.py`, `tools/a2m/serialio.py`, `tools/a2m/cli.py`, `tools/tests/test_proto_cli.py`

**Interfaces:**
- Consumes: the console lines from Task 8.
- Produces: `proto.WriteResult` (fields `block, status, soft, crc, tries, fire_us, backlog`), `proto.parse_wblk(lines) -> WriteResult`, `proto.parse_fmt(line) -> tuple | None`, `proto.parse_format_end(line) -> tuple | None`; `Console.writeblk(block, data, timeout=15.0) -> list[str]`; `Console.cmd(line, timeout=5.0, on_line=None)`; CLI subcommands `writeblk`, `writedisk`, `format`.

- [ ] **Step 1: Write the failing tests**

Append to `tools/tests/test_proto_cli.py`:

```python
class RecordingSerial(FakeSerial):
    def __init__(self, chunks):
        super().__init__(chunks)
        self.sent = []

    def write(self, data):
        self.sent.append(bytes(data))


def _console(chunks):
    con = serialio.Console.__new__(serialio.Console)
    con.ser = RecordingSerial(chunks)
    return con


def test_parse_wblk():
    r = proto.parse_wblk(["writeblk 5", "SEND 512", "BLK 5 e7 04 1a2b3c4d 2 12 3"])
    assert (r.block, r.status, r.soft, r.crc, r.tries, r.fire_us, r.backlog) == (5, 0xE7, 4, 0x1A2B3C4D, 2, 12, 3)
    with pytest.raises(proto.CaptureError):
        proto.parse_wblk(["SEND 512"])


def test_parse_format_lines():
    assert proto.parse_fmt("FMT 3 1 2 e7") == (3, 1, 2, 0xE7)
    assert proto.parse_fmt("FORMAT END 00 81234") is None
    assert proto.parse_format_end("FORMAT END a7 500") == (0xA7, 500)
    assert proto.parse_format_end("FMT 3 1 2 e7") is None


def test_writeblk_sends_payload_after_prompt():
    data = (b"\nERR x\nOK\n" * 60)[:512]           # binary that looks like console lines
    crc = zlib.crc32(data)
    con = _console([b"writeblk 7\r\nSE", b"ND 512\r\n", b"BLK 7 00 00 %08x 1 9 0\r\nOK\r\n" % crc])
    lines = con.writeblk(7, data, timeout=2.0)
    assert con.ser.sent == [b"writeblk 7\n", data]
    assert proto.parse_wblk(lines).crc == crc


def test_writeblk_err_and_length():
    con = _console([b"SEND 512\r\n", b"ERR timeout\r\n"])
    with pytest.raises(serialio.ConsoleError):
        con.writeblk(7, bytes(512), timeout=2.0)
    with pytest.raises(ValueError):
        _console([]).writeblk(7, bytes(100), timeout=2.0)


def test_cmd_streams_lines():
    seen = []
    con = _console([b"format 3 1\r\nFMT 3 1 1 00\r\n", b"FORMAT END 00 410\r\nOK\r\n"])
    lines = con.cmd("format 3 1", timeout=2.0, on_line=seen.append)
    assert "FMT 3 1 1 00" in seen and "FORMAT END 00 410" in lines


class FakeWriteConsole:
    instances = []

    def __init__(self, port, fail_block=None, bad_crc_block=None):
        self.blocks = []
        self.fail_block, self.bad_crc_block = fail_block, bad_crc_block
        FakeWriteConsole.instances.append(self)

    def writeblk(self, block, data, timeout=15.0):
        self.blocks.append(block)
        st = "a7" if block == self.fail_block else "00"
        crc = zlib.crc32(data) ^ (1 if block == self.bad_crc_block else 0)
        return [f"BLK {block} {st} 00 {crc:08x} 1 10 0"]

    def cmd(self, line, timeout=5.0, on_line=None):
        out = ["FMT 0 0 1 00", "FMT 0 1 2 e7", "FORMAT END e7 900"]
        for s in out:
            if on_line:
                on_line(s)
        return out


def _writedisk(tmp_path, **kw):
    img = tmp_path / "x.po"
    img.write_bytes(bytes(range(256)) * 3200)
    FakeWriteConsole.instances = []
    with patch("a2m.serialio.Console", lambda port: FakeWriteConsole(port, **kw)):
        return cli.main(["writedisk", "--port", "p", "--image", str(img)])


def test_writedisk_ok(tmp_path):
    assert _writedisk(tmp_path) == 0
    assert FakeWriteConsole.instances[0].blocks == list(range(1600))


def test_writedisk_stops_on_error(tmp_path):
    assert _writedisk(tmp_path, fail_block=30) == 1
    assert FakeWriteConsole.instances[0].blocks[-1] == 30


def test_writedisk_crc_mismatch(tmp_path):
    assert _writedisk(tmp_path, bad_crc_block=5) == 2


def test_writedisk_wrong_size(tmp_path):
    img = tmp_path / "x.po"
    img.write_bytes(bytes(1000))
    assert cli.main(["writedisk", "--port", "p", "--image", str(img)]) == 2


def test_format_cli(capsys):
    with patch("a2m.serialio.Console", FakeWriteConsole):
        assert cli.main(["format", "--port", "p"]) == 0
    out = capsys.readouterr().out
    assert "FMT 0 1 2 e7" in out and "status e7" in out
    assert cli.main(["format", "--port", "p", "--cyl", "3"]) == 2
```

- [ ] **Step 2: Run to verify they fail**

Run: `cd tools && .venv/bin/python -m pytest -q tests/test_proto_cli.py`
Expected: FAIL with `AttributeError: module 'a2m.proto' has no attribute 'parse_wblk'`.

- [ ] **Step 3: Implement**

`tools/a2m/proto.py`, add near the other regexes and functions:

```python
from dataclasses import dataclass

WBLK_RE = re.compile(r"BLK (\d+) ([0-9a-f]{2}) ([0-9a-f]{2}) ([0-9a-f]{8}) (\d+) (\d+) (\d+)")
FMT_RE = re.compile(r"FMT (\d+) (\d+) (\d+) ([0-9a-f]{2})")
FMT_END_RE = re.compile(r"FORMAT END ([0-9a-f]{2}) (\d+)")


@dataclass
class WriteResult:
    block: int
    status: int
    soft: int
    crc: int
    tries: int
    fire_us: int
    backlog: int


def parse_wblk(lines) -> WriteResult:
    for line in lines:
        m = WBLK_RE.fullmatch(line.strip())
        if m:
            return WriteResult(int(m[1]), int(m[2], 16), int(m[3], 16), int(m[4], 16),
                               int(m[5]), int(m[6]), int(m[7]))
    raise CaptureError("no BLK line")


def parse_fmt(line: str):
    m = FMT_RE.fullmatch(line.strip())
    return (int(m[1]), int(m[2]), int(m[3]), int(m[4], 16)) if m else None


def parse_format_end(line: str):
    m = FMT_END_RE.fullmatch(line.strip())
    return (int(m[1], 16), int(m[2])) if m else None
```

`tools/a2m/serialio.py`:
- `cmd()` gains `on_line=None`; where it appends a line: `lines.append(s)` then `if on_line: on_line(s)`.
- Add:

```python
    def writeblk(self, block: int, data: bytes, timeout: float = 15.0) -> list[str]:
        """writeblk: send the 512 bytes only after the board's SEND 512 line."""
        if len(data) != 512:
            raise ValueError("writeblk needs exactly 512 bytes")
        self.ser.reset_input_buffer()
        self.ser.write(f"writeblk {block}\n".encode())
        deadline = time.monotonic() + timeout
        lines, buf, sent = [], b"", False
        while time.monotonic() < deadline:
            buf += self.ser.read(4096)
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                s = raw.decode(errors="replace").strip()
                if s.startswith("ERR"):
                    raise ConsoleError(f"writeblk {block}: {s}")
                if s == "SEND 512" and not sent:
                    self.ser.write(data)
                    sent = True
                elif s == "OK":
                    if not sent:
                        raise ConsoleError(f"writeblk {block}: OK before SEND")
                    return lines
                elif s:
                    lines.append(s)
        raise ConsoleError(f"writeblk {block} timed out")
```

`tools/a2m/cli.py`:
- imports: `import time`, `import zlib`, and `from .proto import parse_capture, CaptureError, parse_disk, parse_blk, parse_wblk, parse_format_end`
- subparsers in `main()`:

```python
    w = sub.add_parser("writeblk", help="write one block on the board (M5)")
    w.add_argument("--port", required=True); w.add_argument("--block", type=int, required=True)
    g = w.add_mutually_exclusive_group(required=True)
    g.add_argument("--data", help="file with exactly 512 bytes")
    g.add_argument("--pattern", type=lambda x: int(x, 0), help="fill byte, e.g. 0xA5")
    wd = sub.add_parser("writedisk", help="write a whole .po image block by block (M5)")
    wd.add_argument("--port", required=True); wd.add_argument("--image", required=True)
    f = sub.add_parser("format", help="low-level format on the board (M5)")
    f.add_argument("--port", required=True)
    f.add_argument("--cyl", type=int); f.add_argument("--side", type=int)
```

- handlers, before `return _disk(args)`:

```python
    if args.cmd == "writeblk":
        from .serialio import Console, ConsoleError
        data = Path(args.data).read_bytes() if args.data else bytes([args.pattern & 0xFF]) * 512
        if len(data) != 512:
            print("writeblk: data must be exactly 512 bytes")
            return 2
        try:
            r = parse_wblk(Console(args.port).writeblk(args.block, data))
        except (CaptureError, ConsoleError) as e:
            print(f"writeblk failed: {e}")
            return 2
        if r.block != args.block or r.crc != zlib.crc32(data):
            print("writeblk: transfer check failed (block or CRC)")
            return 2
        print(f"block {r.block}: status {r.status:02x} soft {r.soft:02x} tries {r.tries} "
              f"fire {r.fire_us} us backlog {r.backlog}")
        return 0 if r.status in (0x00, 0xE7) else 1
    if args.cmd == "writedisk":
        from .serialio import Console, ConsoleError
        img = Path(args.image).read_bytes()
        if len(img) != gcr.TOTAL_BLOCKS * 512:
            print(f"writedisk: {args.image} is {len(img)} bytes, expected {gcr.TOTAL_BLOCKS * 512}")
            return 2
        con = Console(args.port)
        t0, soft, fire_max = time.monotonic(), 0, 0
        try:
            for t in range(gcr.TRACKS):
                for s in range(gcr.SIDES):
                    for sec in range(gcr.sectors_per_track(t)):
                        b = gcr.block_number(t, s, sec)
                        data = img[b * 512:(b + 1) * 512]
                        r = parse_wblk(con.writeblk(b, data))
                        if r.block != b or r.crc != zlib.crc32(data):
                            print(f"writedisk: block {b}: transfer check failed")
                            return 2
                        if r.status not in (0x00, 0xE7):
                            print(f"writedisk: block {b} (t{t} s{s} sec{sec}): status {r.status:02x} after {r.tries} writes")
                            return 1
                        soft += r.status == 0xE7
                        fire_max = max(fire_max, r.fire_us)
                    print(f"TS {t} {s} ok")
        except (CaptureError, ConsoleError) as e:
            print(f"writedisk failed: {e}")
            return 2
        print(f"writedisk: {gcr.TOTAL_BLOCKS} blocks in {time.monotonic() - t0:.1f} s, "
              f"{soft} with soft errors, fire max {fire_max} us")
        return 0
    if args.cmd == "format":
        from .serialio import Console, ConsoleError
        if (args.cyl is None) != (args.side is None):
            print("format: give both --cyl and --side, or neither")
            return 2
        line = "format" if args.cyl is None else f"format {args.cyl} {args.side}"
        try:
            lines = Console(args.port).cmd(line, timeout=900.0,
                                           on_line=lambda s: print(s) if s.startswith("FMT") else None)
        except ConsoleError as e:
            print(f"format failed: {e}")
            return 2
        end = next((e for e in map(parse_format_end, lines) if e), None)
        if end is None:
            print("format: no FORMAT END line")
            return 2
        print(f"format: status {end[0]:02x}, {end[1]} ms")
        return 0 if end[0] in (0x00, 0xE7) else 1
```

- [ ] **Step 4: Run tests**

Run: `cd tools && .venv/bin/python -m pytest -q`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add tools/a2m/proto.py tools/a2m/serialio.py tools/a2m/cli.py tools/tests/test_proto_cli.py
git commit -m "feat(tools): writeblk, writedisk and format subcommands"
```

---

### Task 10: [BENCH] M5: write and format on the board

Every hardware step needs the operator's go. Disk 1 and disk 2 are scratch 800K DS disks; REF800K stays write-protected and is never inserted while writes are possible unless a step says "read only". Host tool prefix: `cd tools && PYTHONPATH=. .venv/bin/python -m a2m.cli`. Console port `/dev/cu.usbmodem114401`. Power-down order: mechanism supply off before any Pico USB unplug.

**Files:**
- Create: `docs/build_log/m5.md`, `docs/captures/m5/` (screenshots, flux captures, console logs with setup notes)
- Modify: `docs/research_notes.md` (new facts with tags), `HANDOFF.md`

- [ ] **Step 1: Shot list (CLAUDE.local.md).** Before the operator builds: write + verify console; scratch disk (tab open) next to REF800K (tab protected); scope of /WRTGATE + WRTDATA over one sector write; board-formatted disk catalogued in the working UniDisk; bench overview with the scratch disk in Drive A. Names `docs/photos/m05/2026-10-DD_short-desc.jpg`.

- [ ] **Step 2: Flash (the operator says go).** `make -C firmware flash`. If it fails with `failed to exit flash XIP mode`, power cycle the Pico first. Then `server off` regression: `status`, `seek 10`, `capture 0 0 1000` still answer as in M4.

- [ ] **Step 3: Disk 1 snapshot.** The operator formats disk 1 on the IIe + working UniDisk (normal cable use; Filer or ADTPro). Insert in Drive A, tab open. `server on`, then `readdisk --port ... --out docs/captures/m5/disk1_before.po`. Expected 1600 blocks, no failed track sides.

- [ ] **Step 4: First sector write, scope and meter.** Scope: CH1 /WRTGATE at CN104, CH2 WRTDATA at CN104, CH3 RD; trigger CH1 falling, single. Meter CN104 pin 11 (+5 V). Run `writeblk --port ... --block 100 --pattern 0xA5`.
  Record: status, tries, fire_us, backlog; scope: gate low to first WRTDATA edge (>= 1.8 us), last edge to gate high (>= 2 us), cell spacing (2.04 us +- 0.1 %), gate-low length (~11.7 ms); +5 V under write load. Save screenshots + setup notes in `docs/captures/m5/`.

- [ ] **Step 5: Collateral check.** `readdisk --out docs/captures/m5/disk1_after1.po`. Python diff against `disk1_before.po`: only block 100 differs, and it equals 512 x `A5`. (M5 pass, part 1.)

- [ ] **Step 6: Flux of our sector.** `capture --track <t of block 100> --side <s> --out docs/captures/m5/written_track.flux`; `hist` and `decode` it. Our sector's cell histogram peaks match the UniDisk-written sectors on the same track.

- [ ] **Step 7: Whole-disk write.** `writedisk --image /Applications/ADTPro-v.r.m/disks/REF800K.po` onto disk 1 (REF800K is only read from the Mac here). Then `readdisk --out ... --compare /Applications/ADTPro-v.r.m/disks/REF800K.po`: 0 blocks differ. Record time and fire max.

- [ ] **Step 8: Board format.** Disk 2 in Drive A, tab open. `format --cyl 0 --side 0`, then `capture 0 0` + `decode` (12 sectors, all zero). Then full `format`: every `FMT` line status `00`/`e7`; record total time. `readdisk`: all blocks zero.

- [ ] **Step 9: Fill disk 2.** `writedisk --image REF800K.po`, then `readdisk --compare REF800K.po`: 0 blocks differ.

- [ ] **Step 10: UniDisk check (normal cable use only).** The operator moves disk 2 to the working UniDisk on the IIe: it boots or catalogs like REF800K. Optional: ADTPro image of disk 2, diff against REF800K. (M5 pass, part 2.)

- [ ] **Step 11: Error paths.** Disk 1 with the tab protected: `writeblk` -> status `ab`, `format 0 0` -> `FMT 0 0 0 ab`, and Drive A's scope shows no gate activity. No disk: `af`.

- [ ] **Step 12: Write it up.** `docs/build_log/m5.md` (what we tried, measured, broke, next; numbers from steps 4-10). Add to `docs/research_notes.md` with tags: WRTDATA level-change encoding [doc ERS86 3.2.6], /WRTGATE T1-T7 [doc ERS86 p24/p29], ROM write and format layout [fw liron.asm lines], measured gate timing and fire latency [measured]. Update `HANDOFF.md`. Commit:

```bash
git add docs/build_log/m5.md docs/captures/m5 docs/research_notes.md HANDOFF.md
git commit -m "docs(m5): board writes and formats; UniDisk reads our disk"
```

- [ ] **Step 13: Close-out nudges.** bytehamr: one line with the last log's date and title from `get_project` (never guessed). Hackaday: offer a project-log draft from `m5.md`; never post.
