# M4 Board-side Block Read Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The RP2350 decodes 800K GCR itself and returns ProDOS blocks; a USB `readdisk` gives 1600/1600 blocks byte-identical to REF800K.

**Architecture:** Core 1 runs a mechanism server: it owns the drive, streams RD flux through PIO1 + a DMA ring buffer into a streaming C decoder (a port of `tools/a2m/flux.py` + `track.py`), fills a one-side track buffer, and applies the drive ROM's retry policy. Core 0 runs the USB console and talks to core 1 only through two lock-free queues. Everything except the PIO/DMA ring and the core wiring is plain C, tested on the Mac with fakes and with M3's real flux captures.

**Tech Stack:** C11, Pico SDK 2.2.0 (`pico_stdlib`, `hardware_pio`, `hardware_dma`, `pico_multicore`), CMake + Ninja, host tests with `cc` + `firmware/test/test.h`, Python 3.14 + pytest for the host tool and fixture generation.

**Spec:** `docs/superpowers/specs/2026-10-10-m4-board-read-design.md` (parent: `docs/superpowers/specs/2026-10-07-a2m2053-controller-design.md`).

## Global Constraints

- Scope: Apple IIe + 800K double-sided ProDOS disks. 1600 blocks; zones 12/11/10/9/8 sectors for tracks 0-15/16-31/32-47/48-63/64-79; .po block order = track, then side 0 then side 1, then sector (same as `tools/a2m/gcr.py block_number`).
- No disk writes: `/WRTGATE` (GP18) never referenced outside the `hw.c` idle table and the ADDR_MASK `_Static_assert`; `firmware/test/check_wrtgate.sh` must pass. Core 1 never issues EJECT in M4.
- GCR byte order is GROUP_ORDER = CHECKSUM_ORDER = (0, 1, 2) (confirmed on media in M3). Address prolog `D5 AA 96`, data prolog `D5 AA AD`, epilog `DE AA`. Data field = 1 sector nibble + 699 data nibbles + 4 checksum nibbles.
- Flux counts: 40 ns per count; the decoder adds back 2.0 counts per interval (PIO overhead, M3). Nominal cell 1e9/489600 ns = 51.0621 counts. PLL alpha 0.05, adapt only on 1-3 cell intervals, clamp at 8 cells.
- Retry policy (spec section 6): 12 revolutions per fill, recal + re-seek after 6; wrong-track header -> resync + re-seek, budget 4, recal on the 3rd; data prolog must appear within 25 nibbles after the address epilog; 5000 ms hard cap per request; motor idle-off 1000 ms.
- Reply status bytes: `0x00` ok, `0xE7` ok after soft errors, `0xA7` I/O error, `0xAF` no disk, `0xAD` bad block. Soft flags: RECAL 0x01, FOREIGN 0x02, BADDATA 0x04, BADADDR 0x08, OVERRUN 0x10.
- Every wait has a timeout. No allocation in firmware code (static buffers only).
- No hardware action (power, flash, plug/unplug) without the operator saying go. Bench rule: `oe off` and mechanism supply off before any Pico USB unplug/replug.
- Commits: Conventional Commits, ending with the two trailer lines given by the controller.

## Review Focus

1. **Disk swapped while the motor idles.** Expected: a different disk never returns the previous disk's cached sectors. Pinned by Task 5 `test_idle_off_invalidates` (ruling: the track buffer is invalidated at idle-off and whenever CSTIN reads "no disk"; this tightens spec section 4 step 8).
2. **Disk pulled mid-request or no flux at all.** Expected: the request ends with `0xA7` within the 5000 ms cap, nothing hangs. Pinned by Task 5 `test_no_flux_times_out`.
3. **Stream noise around a head switch or seek.** Expected: noise never produces a sector; only fully checked address + data fields land in the buffer, and a foreign header never overwrites a slot. Pinned by Task 3 `test_wrong_side_is_foreign` and `test_corrupt_sector_dropped`.
4. **DMA ring overrun (core 1 stalls).** Expected: the partial revolution is dropped, `SOFT_OVERRUN` set, data still correct. Pinned by Task 5 `test_overrun_recovers`.
5. **USB host stops reading mid-`readdisk`, or the console sees a long line.** Expected: the board stays responsive; the host tool reports a framing/CRC error instead of writing a bad image. Pinned by Task 8 `test_parse_disk_bad_crc` and `test_parse_disk_truncated`.

---

## File Structure

```
firmware/src/
  gcr.h, gcr.c            NEW  nibble table, mangle/unmangle, address/data decode+encode, geometry
  flux_dec.h, flux_dec.c  NEW  streaming decoder: interval -> bit -> nibble -> fields -> sectors
  trackbuf.h, trackbuf.c  NEW  one track side of sectors
  ipc.h, ipc.c            NEW  two SPSC queues between the cores (C11 atomics, no SDK)
  mech_server.h, mech_server.c NEW  core 1 request handling + retry policy over mech_bus_t + flux_src_t
  flux.h, flux.c          MOD  + continuous stream mode (ring DMA), flux_stream_*()
  console.h, console.c    MOD  + server, readblk, readdisk
  main.c                  MOD  core 1 launch, server control, readblk/readdisk output
firmware/test/
  test_gcr.c, test_flux_dec.c, test_trackbuf_ipc.c, test_mech_server.c NEW
  fake_drive.h            NEW  fake mechanism (bus) + fake flux source for the server tests
  fixtures/               NEW  generated by tools/mkfixtures.py (synthetic tracks + expected blocks)
  Makefile                MOD
firmware/CMakeLists.txt   MOD
tools/mkfixtures.py       NEW
tools/a2m/proto.py        MOD  + parse_disk
tools/a2m/serialio.py     MOD  + readdisk, readblk
tools/a2m/cli.py          MOD  + readdisk, readblk subcommands
tools/tests/test_proto_cli.py MOD
docs/build_log/m4.md      NEW (Task 9)
```

---

### Task 1: GCR codec and geometry in C

**Files:**
- Create: `firmware/src/gcr.h`, `firmware/src/gcr.c`, `firmware/test/test_gcr.c`
- Modify: `firmware/test/Makefile`

**Interfaces:**
- Consumes: nothing.
- Produces (exact):
  - constants `GCR_TAG_LEN 12`, `GCR_DATA_LEN 512`, `GCR_SECTOR_LEN 524`, `GCR_TRACKS 80`, `GCR_SIDES 2`, `GCR_BLOCKS 1600`, `GCR_ADDR_SIXES 5`, `GCR_DATA_SIXES 704`, `GCR_ADDR_FIELD_LEN 10`, `GCR_DATA_FIELD_LEN 709`, `GCR_FORMAT_UNIDISK 0x24`
  - `extern const uint8_t gcr_nibbles[64];`
  - `int gcr_decode6(uint8_t nibble);` (0-63, or -1 if not a data nibble)
  - `int gcr_sectors_per_track(int track);` (-1 if out of range)
  - `bool gcr_block_to_tss(int block, int *track, int *side, int *sector);`
  - `int gcr_tss_to_block(int track, int side, int sector);` (-1 if invalid)
  - `void gcr_mangle(const uint8_t in[524], uint8_t out[524], uint8_t csum[3]);`
  - `void gcr_unmangle(const uint8_t in[524], uint8_t out[524], uint8_t csum[3]);`
  - `typedef struct { int track, sector, side, fmt; bool ok; } gcr_addr_t;`
  - `gcr_addr_t gcr_decode_address(const uint8_t sixes[5]);`
  - `bool gcr_decode_data(const uint8_t sixes[704], int *sector, uint8_t payload[524]);`
  - `void gcr_encode_address(int track, int sector, int side, uint8_t out[10]);`
  - `void gcr_encode_data(int sector, const uint8_t payload[524], uint8_t out[709]);`

- [ ] **Step 1: Write the failing test**

`firmware/test/test_gcr.c`:
```c
#include <string.h>
#include "test.h"
#include "gcr.h"

static void test_nibble_table(void) {
    int seen[256] = {0};
    for (int i = 0; i < 64; i++) {
        CHECK(gcr_nibbles[i] >= 0x96);
        CHECK(seen[gcr_nibbles[i]]++ == 0);
        CHECK(gcr_decode6(gcr_nibbles[i]) == i);
    }
    CHECK(gcr_nibbles[0] == 0x96 && gcr_nibbles[63] == 0xFF);
    CHECK(gcr_decode6(0xD5) == -1 && gcr_decode6(0xAA) == -1 && gcr_decode6(0x00) == -1);
}

static void test_geometry(void) {
    CHECK(gcr_sectors_per_track(0) == 12 && gcr_sectors_per_track(15) == 12);
    CHECK(gcr_sectors_per_track(16) == 11 && gcr_sectors_per_track(47) == 10);
    CHECK(gcr_sectors_per_track(79) == 8 && gcr_sectors_per_track(80) == -1);
    int t, s, sec, total = 0;
    for (int tr = 0; tr < 80; tr++) total += 2 * gcr_sectors_per_track(tr);
    CHECK(total == GCR_BLOCKS);
    CHECK(gcr_block_to_tss(0, &t, &s, &sec) && t == 0 && s == 0 && sec == 0);
    CHECK(gcr_block_to_tss(12, &t, &s, &sec) && t == 0 && s == 1 && sec == 0);
    CHECK(gcr_block_to_tss(24, &t, &s, &sec) && t == 1 && s == 0 && sec == 0);
    CHECK(gcr_block_to_tss(1599, &t, &s, &sec) && t == 79 && s == 1 && sec == 7);
    CHECK(!gcr_block_to_tss(1600, &t, &s, &sec) && !gcr_block_to_tss(-1, &t, &s, &sec));
    for (int b = 0; b < GCR_BLOCKS; b++) {
        CHECK(gcr_block_to_tss(b, &t, &s, &sec));
        CHECK(gcr_tss_to_block(t, s, sec) == b);
    }
    CHECK(gcr_tss_to_block(79, 0, 8) == -1);
}

// tashnotes gcr_checksum.md: "TAGS" x3 + "DATA" x128.
static void test_mangle_vector(void) {
    uint8_t in[524], out[524], csum[3], back[524], csum2[3];
    for (int i = 0; i < 12; i++) in[i] = "TAGS"[i % 4];
    for (int i = 12; i < 524; i++) in[i] = "DATA"[(i - 12) % 4];
    gcr_mangle(in, out, csum);
    static const uint8_t head[16] = {0x54,0x15,0x06,0xDD,0xF3,0xD4,0xD8,0xBC,0xBC,0xA6,0x76,0x63,0x32,0x34,0x25,0xD4};
    static const uint8_t tail[12] = {0x0A,0x4B,0x9B,0xE3,0xD2,0x52,0xB0,0x94,0x11,0x69,0x7A,0xE8};
    CHECK(memcmp(out, head, 16) == 0);
    CHECK(memcmp(out + 0x200, tail, 12) == 0);
    CHECK(csum[0] == 0xA9 && csum[1] == 0x69 && csum[2] == 0x2E);
    gcr_unmangle(out, back, csum2);
    CHECK(memcmp(back, in, 524) == 0 && memcmp(csum, csum2, 3) == 0);
}

static void test_address_round_trip(void) {
    static const int cases[][3] = {{0,0,0},{17,10,1},{70,7,1},{79,7,0}};
    for (int k = 0; k < 4; k++) {
        uint8_t f[10], sx[5];
        gcr_encode_address(cases[k][0], cases[k][1], cases[k][2], f);
        CHECK(f[0] == 0xD5 && f[1] == 0xAA && f[2] == 0x96 && f[8] == 0xDE && f[9] == 0xAA);
        for (int i = 0; i < 5; i++) sx[i] = (uint8_t)gcr_decode6(f[3 + i]);
        gcr_addr_t a = gcr_decode_address(sx);
        CHECK(a.ok && a.track == cases[k][0] && a.sector == cases[k][1] && a.side == cases[k][2]);
        CHECK(a.fmt == GCR_FORMAT_UNIDISK);
        sx[4] ^= 1;
        CHECK(!gcr_decode_address(sx).ok);
    }
}

static void test_data_round_trip(void) {
    uint8_t p[524], f[709], sx[704], out[524];
    for (int i = 0; i < 524; i++) p[i] = (uint8_t)(i * 37 + 11);
    gcr_encode_data(9, p, f);
    CHECK(f[0] == 0xD5 && f[1] == 0xAA && f[2] == 0xAD && f[707] == 0xDE && f[708] == 0xAA);
    for (int i = 0; i < 704; i++) sx[i] = (uint8_t)gcr_decode6(f[3 + i]);
    int sec = -1;
    CHECK(gcr_decode_data(sx, &sec, out) && sec == 9 && memcmp(out, p, 524) == 0);
    sx[100] ^= 1;
    CHECK(!gcr_decode_data(sx, &sec, out));
}

int main(void) {
    test_nibble_table();
    test_geometry();
    test_mangle_vector();
    test_address_round_trip();
    test_data_round_trip();
    TEST_END();
}
```

Add to `firmware/test/Makefile` (extend `TESTS`, add the rule):
```make
TESTS  := test_crc32 test_mech test_console test_gcr

$(OUT)/test_gcr: test_gcr.c ../src/gcr.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $^
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd firmware/test && make`
Expected: FAIL, `gcr.h: No such file or directory`.

- [ ] **Step 3: Write `gcr.h`**

```c
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Apple 3.5" 800K GCR (port of tools/a2m/gcr.py). Byte order (0,1,2) for the
// data groups and the checksum, confirmed on real media in M3.
#define GCR_TAG_LEN         12
#define GCR_DATA_LEN        512
#define GCR_SECTOR_LEN      524
#define GCR_TRACKS          80
#define GCR_SIDES           2
#define GCR_BLOCKS          1600
#define GCR_ADDR_SIXES      5
#define GCR_DATA_SIXES      704     // 1 sector + 699 data + 4 checksum
#define GCR_ADDR_FIELD_LEN  10      // prolog 3 + 5 + epilog 2
#define GCR_DATA_FIELD_LEN  709     // prolog 3 + 704 + epilog 2
#define GCR_FORMAT_UNIDISK  0x24

extern const uint8_t gcr_nibbles[64];

int gcr_decode6(uint8_t nibble);
int gcr_sectors_per_track(int track);
bool gcr_block_to_tss(int block, int *track, int *side, int *sector);
int gcr_tss_to_block(int track, int side, int sector);

void gcr_mangle(const uint8_t in[GCR_SECTOR_LEN], uint8_t out[GCR_SECTOR_LEN], uint8_t csum[3]);
void gcr_unmangle(const uint8_t in[GCR_SECTOR_LEN], uint8_t out[GCR_SECTOR_LEN], uint8_t csum[3]);

typedef struct { int track, sector, side, fmt; bool ok; } gcr_addr_t;
gcr_addr_t gcr_decode_address(const uint8_t sixes[GCR_ADDR_SIXES]);
bool gcr_decode_data(const uint8_t sixes[GCR_DATA_SIXES], int *sector, uint8_t payload[GCR_SECTOR_LEN]);

void gcr_encode_address(int track, int sector, int side, uint8_t out[GCR_ADDR_FIELD_LEN]);
void gcr_encode_data(int sector, const uint8_t payload[GCR_SECTOR_LEN], uint8_t out[GCR_DATA_FIELD_LEN]);
```

- [ ] **Step 4: Write `gcr.c`**

```c
#include "gcr.h"
#include <string.h>

const uint8_t gcr_nibbles[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

static int8_t dec_table[256];
static bool dec_built;

int gcr_decode6(uint8_t nibble) {
    if (!dec_built) {
        memset(dec_table, -1, sizeof dec_table);
        for (int i = 0; i < 64; i++) dec_table[gcr_nibbles[i]] = (int8_t)i;
        dec_built = true;
    }
    return dec_table[nibble];
}

int gcr_sectors_per_track(int track) {
    if (track < 0 || track >= GCR_TRACKS) return -1;
    return 12 - track / 16;
}

bool gcr_block_to_tss(int block, int *track, int *side, int *sector) {
    if (block < 0 || block >= GCR_BLOCKS) return false;
    int base = 0;
    for (int t = 0; t < GCR_TRACKS; t++) {
        int n = gcr_sectors_per_track(t);
        if (block < base + 2 * n) {
            *track = t;
            *side = (block - base) / n;
            *sector = (block - base) % n;
            return true;
        }
        base += 2 * n;
    }
    return false;
}

int gcr_tss_to_block(int track, int side, int sector) {
    int n = gcr_sectors_per_track(track);
    if (n < 0 || side < 0 || side > 1 || sector < 0 || sector >= n) return -1;
    int base = 0;
    for (int t = 0; t < track; t++) base += 2 * gcr_sectors_per_track(t);
    return base + side * n + sector;
}

void gcr_mangle(const uint8_t in[GCR_SECTOR_LEN], uint8_t out[GCR_SECTOR_LEN], uint8_t csum[3]) {
    unsigned a = 0, b = 0, c = 0, carry = 0, t;
    int i = 0, n = GCR_SECTOR_LEN;
    while (i < n) {
        uint8_t byte = in[i];
        t = a + byte + carry; a = t & 0xFF; carry = t >> 8;
        out[i++] = byte ^ (uint8_t)c;
        if (i >= n) break;
        byte = in[i];
        t = b + byte + carry; b = t & 0xFF; carry = t >> 8;
        out[i++] = byte ^ (uint8_t)a;
        if (i >= n) break;
        byte = in[i];
        t = c + byte + carry; c = t & 0xFF; carry = t >> 8;
        out[i++] = byte ^ (uint8_t)b;
        carry = (c & 0x80) ? 1 : 0;
        c = ((c << 1) & 0xFF) | carry;
    }
    csum[0] = (uint8_t)a; csum[1] = (uint8_t)b; csum[2] = (uint8_t)c;
}

void gcr_unmangle(const uint8_t in[GCR_SECTOR_LEN], uint8_t out[GCR_SECTOR_LEN], uint8_t csum[3]) {
    unsigned a = 0, b = 0, c = 0, carry, t;
    int i = 0, n = GCR_SECTOR_LEN;
    for (;;) {
        carry = (c & 0x80) ? 1 : 0;
        c = ((c << 1) & 0xFF) | carry;
        if (i >= n) break;
        uint8_t byte = in[i] ^ (uint8_t)c;
        out[i++] = byte;
        t = a + byte + carry; a = t & 0xFF; carry = t >> 8;
        if (i >= n) break;
        byte = in[i] ^ (uint8_t)a;
        out[i++] = byte;
        t = b + byte + carry; b = t & 0xFF; carry = t >> 8;
        if (i >= n) break;
        byte = in[i] ^ (uint8_t)b;
        out[i++] = byte;
        t = c + byte + carry; c = t & 0xFF; carry = t >> 8;
    }
    csum[0] = (uint8_t)a; csum[1] = (uint8_t)b; csum[2] = (uint8_t)c;
}

gcr_addr_t gcr_decode_address(const uint8_t s[GCR_ADDR_SIXES]) {
    gcr_addr_t a;
    a.ok = (s[0] ^ s[1] ^ s[2] ^ s[3]) == s[4];
    a.track = s[0] | ((s[2] & 1) << 6);
    a.sector = s[1];
    a.side = (s[2] >> 5) & 1;
    a.fmt = s[3];
    return a;
}

// Groups of up to 3 bytes: one six holding the top 2 bits of each byte
// (first byte in bits 5-4), then the low 6 bits of each byte.
static void decode_groups(const uint8_t *sx, uint8_t *out, int nbytes) {
    int i = 0, o = 0;
    while (o < nbytes) {
        int k = nbytes - o < 3 ? nbytes - o : 3;
        uint8_t hi = sx[i];
        for (int m = 0; m < k; m++)
            out[o + m] = (uint8_t)((((hi >> (4 - 2 * m)) & 3) << 6) | sx[i + 1 + m]);
        i += 1 + k;
        o += k;
    }
}

static int encode_groups(const uint8_t *raw, int nbytes, uint8_t *sx) {
    int i = 0;
    for (int o = 0; o < nbytes; o += 3) {
        int k = nbytes - o < 3 ? nbytes - o : 3;
        uint8_t hi = 0;
        for (int m = 0; m < k; m++) hi |= (uint8_t)((raw[o + m] >> 6) << (4 - 2 * m));
        sx[i++] = hi;
        for (int m = 0; m < k; m++) sx[i++] = raw[o + m] & 0x3F;
    }
    return i;
}

bool gcr_decode_data(const uint8_t s[GCR_DATA_SIXES], int *sector, uint8_t payload[GCR_SECTOR_LEN]) {
    uint8_t mangled[GCR_SECTOR_LEN], disk[3], calc[3];
    *sector = s[0];
    decode_groups(s + 1, mangled, GCR_SECTOR_LEN);
    decode_groups(s + 700, disk, 3);
    gcr_unmangle(mangled, payload, calc);
    return memcmp(disk, calc, 3) == 0;
}

void gcr_encode_address(int track, int sector, int side, uint8_t out[GCR_ADDR_FIELD_LEN]) {
    uint8_t v[5];
    v[0] = (uint8_t)(track & 0x3F);
    v[1] = (uint8_t)(sector & 0x3F);
    v[2] = (uint8_t)((side << 5) | (track >> 6));
    v[3] = GCR_FORMAT_UNIDISK & 0x3F;
    v[4] = v[0] ^ v[1] ^ v[2] ^ v[3];
    out[0] = 0xD5; out[1] = 0xAA; out[2] = 0x96;
    for (int i = 0; i < 5; i++) out[3 + i] = gcr_nibbles[v[i]];
    out[8] = 0xDE; out[9] = 0xAA;
}

void gcr_encode_data(int sector, const uint8_t payload[GCR_SECTOR_LEN], uint8_t out[GCR_DATA_FIELD_LEN]) {
    uint8_t mangled[GCR_SECTOR_LEN], csum[3], sx[GCR_DATA_SIXES];
    gcr_mangle(payload, mangled, csum);
    sx[0] = (uint8_t)(sector & 0x3F);
    int n = 1 + encode_groups(mangled, GCR_SECTOR_LEN, sx + 1);
    encode_groups(csum, 3, sx + n);
    out[0] = 0xD5; out[1] = 0xAA; out[2] = 0xAD;
    for (int i = 0; i < GCR_DATA_SIXES; i++) out[3 + i] = gcr_nibbles[sx[i]];
    out[707] = 0xDE; out[708] = 0xAA;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd firmware/test && make`
Expected: `test_gcr.c: <N> checks, 0 failed`, the other suites unchanged, `check_wrtgate: ok`.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/gcr.h firmware/src/gcr.c firmware/test/test_gcr.c firmware/test/Makefile
git commit -m "feat(fw): GCR codec and 800K geometry in C"
```

---

### Task 2: Test fixtures from the proven Python decoder and M3 captures

**Files:**
- Create: `tools/mkfixtures.py`, `firmware/test/fixtures/*` (generated, committed)

**Interfaces:**
- Consumes: `tools/a2m/gcr.py`, `synth.py`, `flux.py`, `proto.py` (committed); M3 captures `docs/captures/m3/scan7_driveA_caps/tTTsS.flux`; REF800K at `/Applications/ADTPro-v.r.m/disks/REF800K.po` (SHA-256 `bfed064fef52e30578723b3c9fcc6768ab52907f4bb1aa4670f70823657742ab`).
- Produces (all little-endian; read by Task 3's tests from `firmware/test/fixtures/`):
  - `synth_clean_t00s0.u16` / `.exp`: clean track 0 side 0; `.exp` = 12 x 524 payloads in sector order.
  - `synth_drift_t70s1.u16` / `.exp`: track 70 side 1, jitter 0.06, drift 0.12, seed 3; 8 x 524.
  - `synth_wrongside_t03s1.u16`: track 3 side 1 (the test decodes it as side 0).
  - `synth_corrupt_t20s0.u16` / `.exp`: track 20 side 0 with one data nibble of the first physical sector changed; `.exp` = 11 x 524 in sector order (the test knows which sector is corrupt from `synth_corrupt_t20s0.bad`, one byte).
  - `real_expected.bin`: for (track, side) in `[(0,0),(0,1),(5,0),(5,1),(20,0),(20,1),(40,0),(40,1),(60,0),(60,1),(79,0),(79,1)]`, for each sector in order, REF800K's 512-byte block = 124 x 512 = 63488 bytes.
  - `.u16` = raw interval counts, uint16 each, no header.

- [ ] **Step 1: Write `tools/mkfixtures.py`**

```python
#!/usr/bin/env python3
"""Generate firmware/test/fixtures from the Python decoder's synthetic tracks and REF800K.

Usage (from tools/): .venv/bin/python mkfixtures.py /Applications/ADTPro-v.r.m/disks/REF800K.po
"""
import hashlib
import random
import struct
import sys
from pathlib import Path

from a2m import gcr, synth, flux

OUT = Path(__file__).resolve().parent.parent / "firmware" / "test" / "fixtures"
REF_SHA256 = "bfed064fef52e30578723b3c9fcc6768ab52907f4bb1aa4670f70823657742ab"
REAL_TS = [(0, 0), (0, 1), (5, 0), (5, 1), (20, 0), (20, 1), (40, 0), (40, 1), (60, 0), (60, 1), (79, 0), (79, 1)]


def payloads_for(trk, side, seed=0):
    rng = random.Random(seed * 1000 + trk * 2 + side)
    return {s: bytes(rng.randrange(256) for _ in range(gcr.SECTOR_LEN))
            for s in range(gcr.sectors_per_track(trk))}


def write_u16(name, values):
    (OUT / name).write_bytes(b"".join(struct.pack("<H", min(v, 65535)) for v in values))


def write_exp(name, payloads, skip=None):
    (OUT / name).write_bytes(b"".join(payloads[s] for s in sorted(payloads) if s != skip))


def corrupt_track_bits(trk, side, payloads):
    """synth.track_bits with one data nibble of the first physical sector changed."""
    order = synth.physical_order(gcr.sectors_per_track(trk), 4)
    bits = []
    for idx, s in enumerate(order):
        bits += synth.SYNC * 6 + synth.bytes_bits(gcr.encode_address(trk, s, side))
        enc = bytearray(gcr.encode_data(s, payloads[s]))
        if idx == 0:
            enc[50] = gcr.NIBBLES[(gcr.DECODE[enc[50]] + 1) % 64]
        bits += synth.SYNC * 6 + synth.bytes_bits(bytes(enc))
    bits += synth.SYNC * 6
    return bits, order[0]


def main():
    ref = Path(sys.argv[1]).read_bytes()
    if hashlib.sha256(ref).hexdigest() != REF_SHA256:
        sys.exit("REF800K hash mismatch")
    OUT.mkdir(parents=True, exist_ok=True)

    p = payloads_for(0, 0)
    write_u16("synth_clean_t00s0.u16", flux.encode_flux(synth.track_bits(0, 0, p)))
    write_exp("synth_clean_t00s0.exp", p)

    p = payloads_for(70, 1)
    write_u16("synth_drift_t70s1.u16",
              flux.encode_flux(synth.track_bits(70, 1, p), jitter=0.06, drift=0.12, seed=3))
    write_exp("synth_drift_t70s1.exp", p)

    p = payloads_for(3, 1)
    write_u16("synth_wrongside_t03s1.u16", flux.encode_flux(synth.track_bits(3, 1, p)))

    p = payloads_for(20, 0)
    bits, bad = corrupt_track_bits(20, 0, p)
    write_u16("synth_corrupt_t20s0.u16", flux.encode_flux(bits))
    write_exp("synth_corrupt_t20s0.exp", p, skip=bad)
    (OUT / "synth_corrupt_t20s0.bad").write_bytes(bytes([bad]))

    blocks = bytearray()
    for t, s in REAL_TS:
        for sec in range(gcr.sectors_per_track(t)):
            b = gcr.block_number(t, s, sec)
            blocks += ref[b * 512:(b + 1) * 512]
    (OUT / "real_expected.bin").write_bytes(bytes(blocks))
    print(f"fixtures -> {OUT}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Generate and check**

Run: `cd tools && .venv/bin/python mkfixtures.py /Applications/ADTPro-v.r.m/disks/REF800K.po && ls -l ../firmware/test/fixtures`
Expected: `fixtures -> ...`; `real_expected.bin` is 63488 bytes; `synth_clean_t00s0.exp` is 6288 bytes (12 x 524); `synth_drift_t70s1.exp` 4192; `synth_corrupt_t20s0.exp` 5240; the `.u16` files are tens of KB each.

- [ ] **Step 3: Sanity-check the synthetic fixtures with the Python decoder**

Run:
```bash
cd tools && .venv/bin/python - <<'EOF'
import struct
from pathlib import Path
from a2m import flux, track
F = Path("../firmware/test/fixtures")
def ivs(n): d = (F / n).read_bytes(); return list(struct.unpack(f"<{len(d)//2}H", d))
for name, t, s, want in [("synth_clean_t00s0.u16", 0, 0, 12), ("synth_drift_t70s1.u16", 70, 1, 8),
                         ("synth_corrupt_t20s0.u16", 20, 0, 10), ("synth_wrongside_t03s1.u16", 3, 0, 0)]:
    res = track.decode_track(flux.bits_to_nibbles(flux.intervals_to_bits(ivs(name))), t, s)
    print(name, len(res.sectors), "want", want)
    assert len(res.sectors) == want
EOF
```
Expected: four lines, each count equal to `want`.

- [ ] **Step 4: Commit**

```bash
git add tools/mkfixtures.py firmware/test/fixtures
git commit -m "test(fw): decoder fixtures from synthetic tracks and REF800K"
```

---

### Task 3: Streaming flux decoder

**Files:**
- Create: `firmware/src/flux_dec.h`, `firmware/src/flux_dec.c`, `firmware/test/test_flux_dec.c`
- Modify: `firmware/test/Makefile`

**Interfaces:**
- Consumes: Task 1 `gcr_*`; Task 2 fixtures in `firmware/test/fixtures/`; M3 captures `docs/captures/m3/scan7_driveA_caps/tTTsS.flux` (format: text up to `FLUX BEGIN <n> <tick>\r\n`, then n uint16 LE, then `\r\nFLUX END ...`).
- Produces (exact):
  - `typedef enum { FLUX_EV_BAD_ADDR, FLUX_EV_FOREIGN, FLUX_EV_NO_DATA, FLUX_EV_BAD_DATA, FLUX_EV_RANGE } flux_ev_t;`
  - `typedef struct { void (*sector)(void *ctx, int sector, const uint8_t payload[524]); void (*event)(void *ctx, flux_ev_t ev, int track, int side, int sector); void *ctx; } flux_dec_sink_t;`
  - `flux_dec_t` (opaque-by-convention struct, fields below)
  - `void flux_dec_reset(flux_dec_t *d, int track, int side, const flux_dec_sink_t *sink);`
  - `void flux_dec_push(flux_dec_t *d, uint16_t count);`
  - `void flux_dec_push_many(flux_dec_t *d, const uint16_t *counts, int n);`
  - Event arguments: BAD_ADDR passes (-1,-1,-1); FOREIGN/RANGE pass the header's (track, side, sector); NO_DATA/BAD_DATA pass the expected (track, side) and the header's sector.

- [ ] **Step 1: Write the failing test**

`firmware/test/test_flux_dec.c`:
```c
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "flux_dec.h"

#define FIX "fixtures/"
#define M3  "../../docs/captures/m3/scan7_driveA_caps/"

typedef struct {
    uint8_t data[12][524];
    int got[12];
    int nsect, events[5], foreign_side1;
} sink_t;

static void on_sector(void *ctx, int sector, const uint8_t *p) {
    sink_t *s = ctx;
    if (sector >= 0 && sector < 12) { memcpy(s->data[sector], p, 524); s->got[sector]++; }
}
static void on_event(void *ctx, flux_ev_t ev, int track, int side, int sector) {
    sink_t *s = ctx; (void)track; (void)sector;
    s->events[ev]++;
    if (ev == FLUX_EV_FOREIGN && side == 1) s->foreign_side1++;
}

static uint8_t *load(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("missing fixture %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)*len);
    if (fread(buf, 1, (size_t)*len, f) != (size_t)*len) exit(2);
    fclose(f);
    return buf;
}

static uint16_t *load_u16(const char *path, int *n) {
    long len; uint8_t *b = load(path, &len);
    *n = (int)(len / 2);
    uint16_t *v = malloc(sizeof(uint16_t) * (size_t)*n);
    for (int i = 0; i < *n; i++) v[i] = (uint16_t)(b[2 * i] | (b[2 * i + 1] << 8));
    free(b);
    return v;
}

// Parse an M3 capture (FLUX BEGIN <n> <tick>\r\n + n x u16). Drops the first (partial) interval.
static uint16_t *load_flux(const char *path, int *n) {
    long len; uint8_t *b = load(path, &len);
    char *p = strstr((char *)b, "FLUX BEGIN ");
    if (!p) exit(2);
    int count = atoi(p + 11);
    char *nl = strchr(p, '\n');
    const uint8_t *d = (const uint8_t *)nl + 1;
    uint16_t *v = malloc(sizeof(uint16_t) * (size_t)count);
    for (int i = 1; i < count; i++) v[i - 1] = (uint16_t)(d[2 * i] | (d[2 * i + 1] << 8));
    *n = count - 1;
    free(b);
    return v;
}

static void decode(const uint16_t *v, int n, int track, int side, sink_t *s, int chunk) {
    memset(s, 0, sizeof *s);
    flux_dec_t d;
    flux_dec_sink_t k = { on_sector, on_event, s };
    flux_dec_reset(&d, track, side, &k);
    if (chunk <= 0) { for (int i = 0; i < n; i++) flux_dec_push(&d, v[i]); }
    else for (int i = 0; i < n; i += chunk) flux_dec_push_many(&d, v + i, n - i < chunk ? n - i : chunk);
    for (int i = 0; i < 12; i++) if (s->got[i]) s->nsect++;
}

static void test_clean(void) {
    int n; long el;
    uint16_t *v = load_u16(FIX "synth_clean_t00s0.u16", &n);
    uint8_t *exp = load(FIX "synth_clean_t00s0.exp", &el);
    sink_t s; decode(v, n, 0, 0, &s, 0);
    CHECK(s.nsect == 12);
    for (int i = 0; i < 12; i++) CHECK(memcmp(s.data[i], exp + 524 * i, 524) == 0);
    CHECK(s.events[FLUX_EV_BAD_DATA] == 0 && s.events[FLUX_EV_FOREIGN] == 0);
    free(v); free(exp);
}

static void test_jitter_and_drift(void) {
    int n; long el;
    uint16_t *v = load_u16(FIX "synth_drift_t70s1.u16", &n);
    uint8_t *exp = load(FIX "synth_drift_t70s1.exp", &el);
    sink_t s; decode(v, n, 70, 1, &s, 0);
    CHECK(s.nsect == 8);
    for (int i = 0; i < 8; i++) CHECK(memcmp(s.data[i], exp + 524 * i, 524) == 0);
    free(v); free(exp);
}

static void test_wrong_side_is_foreign(void) {
    int n;
    uint16_t *v = load_u16(FIX "synth_wrongside_t03s1.u16", &n);
    sink_t s; decode(v, n, 3, 0, &s, 0);
    CHECK(s.nsect == 0);
    CHECK(s.events[FLUX_EV_FOREIGN] == 12 && s.foreign_side1 == 12);
    free(v);
}

static void test_corrupt_sector_dropped(void) {
    int n; long el, bl;
    uint16_t *v = load_u16(FIX "synth_corrupt_t20s0.u16", &n);
    uint8_t *exp = load(FIX "synth_corrupt_t20s0.exp", &el);
    uint8_t *bad = load(FIX "synth_corrupt_t20s0.bad", &bl);
    sink_t s; decode(v, n, 20, 0, &s, 0);
    CHECK(s.nsect == 10 && s.got[bad[0]] == 0);
    CHECK(s.events[FLUX_EV_BAD_DATA] >= 1);
    int k = 0;
    for (int i = 0; i < 11; i++) {
        if (i == bad[0]) continue;
        CHECK(memcmp(s.data[i], exp + 524 * k, 524) == 0);
        k++;
    }
    free(v); free(exp); free(bad);
}

static void test_empty(void) {
    sink_t s; decode(NULL, 0, 0, 0, &s, 0);
    CHECK(s.nsect == 0);
    for (int e = 0; e < 5; e++) CHECK(s.events[e] == 0);
}

static const int real_ts[12][2] = {{0,0},{0,1},{5,0},{5,1},{20,0},{20,1},{40,0},{40,1},{60,0},{60,1},{79,0},{79,1}};

static void test_real_captures(void) {
    long el;
    uint8_t *exp = load(FIX "real_expected.bin", &el);
    CHECK(el == 124 * 512);
    long off = 0;
    for (int k = 0; k < 12; k++) {
        int t = real_ts[k][0], sd = real_ts[k][1], spt = gcr_sectors_per_track(t), n;
        char path[256];
        snprintf(path, sizeof path, M3 "t%02ds%d.flux", t, sd);
        uint16_t *v = load_flux(path, &n);
        sink_t s; decode(v, n, t, sd, &s, 0);
        CHECK(s.nsect == spt);
        for (int sec = 0; sec < spt; sec++)
            CHECK(memcmp(s.data[sec] + 12, exp + off + 512L * sec, 512) == 0);
        off += 512L * spt;
        free(v);
    }
    free(exp);
}

static void test_chunking_is_invisible(void) {
    int n;
    uint16_t *v = load_flux(M3 "t40s1.flux", &n);
    sink_t ref; decode(v, n, 40, 1, &ref, 0);
    static const int chunks[] = {1, 7, 500, 1 << 30};
    for (int c = 0; c < 4; c++) {
        sink_t s; decode(v, n, 40, 1, &s, chunks[c]);
        CHECK(s.nsect == ref.nsect);
        CHECK(memcmp(s.events, ref.events, sizeof s.events) == 0);
        for (int i = 0; i < 12; i++) CHECK(!ref.got[i] || memcmp(s.data[i], ref.data[i], 524) == 0);
    }
    free(v);
}

int main(void) {
    test_clean();
    test_jitter_and_drift();
    test_wrong_side_is_foreign();
    test_corrupt_sector_dropped();
    test_empty();
    test_real_captures();
    test_chunking_is_invisible();
    TEST_END();
}
```

Add to `firmware/test/Makefile`:
```make
TESTS  := test_crc32 test_mech test_console test_gcr test_flux_dec

$(OUT)/test_flux_dec: test_flux_dec.c ../src/flux_dec.c ../src/gcr.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $^
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd firmware/test && make`
Expected: FAIL, `flux_dec.h: No such file or directory`.

- [ ] **Step 3: Write `flux_dec.h`**

```c
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "gcr.h"

// Streaming 800K GCR decoder (C port of tools/a2m/flux.py + track.py).
// Feed raw PIO interval counts (40 ns units) one at a time or in batches.
#define FLUX_DEC_OVERHEAD      2.0f                          // counts lost per edge in the PIO loop
#define FLUX_DEC_CELL_COUNTS   (1e9f / 489600.0f / 40.0f)    // 51.0621 counts per bit cell
#define FLUX_DEC_ALPHA         0.05f
#define FLUX_DEC_MAX_CELLS     8
#define FLUX_DEC_DATA_WINDOW   25                            // nibbles after the address epilog (ROM)

typedef enum { FLUX_EV_BAD_ADDR, FLUX_EV_FOREIGN, FLUX_EV_NO_DATA, FLUX_EV_BAD_DATA, FLUX_EV_RANGE } flux_ev_t;

typedef struct {
    void (*sector)(void *ctx, int sector, const uint8_t payload[GCR_SECTOR_LEN]);
    void (*event)(void *ctx, flux_ev_t ev, int track, int side, int sector);
    void *ctx;
} flux_dec_sink_t;

typedef enum { FD_HUNT_ADDR, FD_ADDR, FD_ADDR_EPILOG, FD_HUNT_DATA, FD_DATA } flux_dec_state_t;

typedef struct {
    const flux_dec_sink_t *sink;
    int track, side;
    float cell;                  // current cell estimate in counts
    uint8_t reg;                 // nibble latch
    int nbits;
    uint32_t last3;              // last three nibbles
    flux_dec_state_t st;
    int n;                       // sixes collected / epilog bytes seen
    int window;                  // nibbles left to find the data prolog
    gcr_addr_t addr;
    uint8_t six[GCR_DATA_SIXES];
    uint8_t payload[GCR_SECTOR_LEN];
} flux_dec_t;

void flux_dec_reset(flux_dec_t *d, int track, int side, const flux_dec_sink_t *sink);
void flux_dec_push(flux_dec_t *d, uint16_t count);
void flux_dec_push_many(flux_dec_t *d, const uint16_t *counts, int n);
```

- [ ] **Step 4: Write `flux_dec.c`**

```c
#include "flux_dec.h"
#include <string.h>

void flux_dec_reset(flux_dec_t *d, int track, int side, const flux_dec_sink_t *sink) {
    memset(d, 0, sizeof *d);
    d->sink = sink;
    d->track = track;
    d->side = side;
    d->cell = FLUX_DEC_CELL_COUNTS;
    d->st = FD_HUNT_ADDR;
}

static void emit(flux_dec_t *d, flux_ev_t ev, int t, int s, int sec) {
    if (d->sink && d->sink->event) d->sink->event(d->sink->ctx, ev, t, s, sec);
}

static void nibble(flux_dec_t *d, uint8_t nb) {
    d->last3 = ((d->last3 << 8) | nb) & 0xFFFFFFu;
    int v;
    switch (d->st) {
    case FD_HUNT_ADDR:
        if (d->last3 == 0xD5AA96u) { d->st = FD_ADDR; d->n = 0; }
        return;
    case FD_ADDR:
        v = gcr_decode6(nb);
        if (v < 0) { emit(d, FLUX_EV_BAD_ADDR, -1, -1, -1); d->st = FD_HUNT_ADDR; return; }
        d->six[d->n++] = (uint8_t)v;
        if (d->n == GCR_ADDR_SIXES) {
            d->addr = gcr_decode_address(d->six);
            if (!d->addr.ok) { emit(d, FLUX_EV_BAD_ADDR, -1, -1, -1); d->st = FD_HUNT_ADDR; return; }
            d->st = FD_ADDR_EPILOG;
            d->n = 0;
        }
        return;
    case FD_ADDR_EPILOG:
        if ((d->n == 0 && nb == 0xDE) || (d->n == 1 && nb == 0xAA)) {
            if (++d->n < 2) return;
            if (d->addr.track != d->track || d->addr.side != d->side) {
                emit(d, FLUX_EV_FOREIGN, d->addr.track, d->addr.side, d->addr.sector);
                d->st = FD_HUNT_ADDR;
            } else if (d->addr.sector >= gcr_sectors_per_track(d->track)) {
                emit(d, FLUX_EV_RANGE, d->addr.track, d->addr.side, d->addr.sector);
                d->st = FD_HUNT_ADDR;
            } else {
                d->st = FD_HUNT_DATA;
                d->window = FLUX_DEC_DATA_WINDOW;
            }
            return;
        }
        emit(d, FLUX_EV_BAD_ADDR, -1, -1, -1);
        d->st = FD_HUNT_ADDR;
        return;
    case FD_HUNT_DATA:
        if (d->last3 == 0xD5AAADu) { d->st = FD_DATA; d->n = 0; return; }
        if (--d->window <= 0) {
            emit(d, FLUX_EV_NO_DATA, d->track, d->side, d->addr.sector);
            d->st = FD_HUNT_ADDR;
        }
        return;
    case FD_DATA: {
        v = gcr_decode6(nb);
        if (v < 0) {
            emit(d, FLUX_EV_BAD_DATA, d->track, d->side, d->addr.sector);
            d->st = FD_HUNT_ADDR;
            return;
        }
        d->six[d->n++] = (uint8_t)v;
        if (d->n < GCR_DATA_SIXES) return;
        int sec;
        bool ok = gcr_decode_data(d->six, &sec, d->payload);
        if (ok && sec == d->addr.sector) {
            if (d->sink && d->sink->sector) d->sink->sector(d->sink->ctx, sec, d->payload);
        } else {
            emit(d, FLUX_EV_BAD_DATA, d->track, d->side, d->addr.sector);
        }
        d->st = FD_HUNT_ADDR;
        return;
    }
    }
}

static inline void bit(flux_dec_t *d, int b) {
    if (d->nbits == 0 && !b) return;          // IWM latch: wait for a leading 1
    d->reg = (uint8_t)((d->reg << 1) | b);
    if (++d->nbits == 8) {
        uint8_t nb = d->reg;
        d->reg = 0;
        d->nbits = 0;
        nibble(d, nb);
    }
}

void flux_dec_push(flux_dec_t *d, uint16_t count) {
    float t = (float)count + FLUX_DEC_OVERHEAD;
    int n = (int)(t / d->cell + 0.5f);
    if (n < 1) n = 1;
    if (n > FLUX_DEC_MAX_CELLS) n = FLUX_DEC_MAX_CELLS;
    for (int i = 1; i < n; i++) bit(d, 0);
    bit(d, 1);
    if (n <= 3) d->cell += FLUX_DEC_ALPHA * (t / (float)n - d->cell);
}

void flux_dec_push_many(flux_dec_t *d, const uint16_t *counts, int n) {
    for (int i = 0; i < n; i++) flux_dec_push(d, counts[i]);
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd firmware/test && make`
Expected: `test_flux_dec.c: <N> checks, 0 failed`. If `test_real_captures` decodes fewer sectors than the Python decoder did on the same capture (12/12 etc. per `docs/build_log/m3.md` scan7), stop and report the per-track counts and event counts (the epilog check and the 25-nibble data window are the two places this decoder is stricter than the Python one); do not loosen the checks without a ruling.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/flux_dec.h firmware/src/flux_dec.c firmware/test/test_flux_dec.c firmware/test/Makefile
git commit -m "feat(fw): streaming GCR flux decoder, checked against real M3 captures"
```

---

### Task 4: Track buffer and inter-core queues

**Files:**
- Create: `firmware/src/trackbuf.h`, `firmware/src/trackbuf.c`, `firmware/src/ipc.h`, `firmware/src/ipc.c`, `firmware/test/test_trackbuf_ipc.c`
- Modify: `firmware/test/Makefile`

**Interfaces:**
- Consumes: Task 1 `GCR_SECTOR_LEN`, `gcr_sectors_per_track`.
- Produces (exact):
  - `typedef struct { int track, side, nsect; bool valid; uint16_t present; uint8_t data[12][GCR_SECTOR_LEN]; } trackbuf_t;`
  - `void trackbuf_init(trackbuf_t *b);` `void trackbuf_start(trackbuf_t *b, int track, int side);` `bool trackbuf_put(trackbuf_t *b, int sector, const uint8_t *payload);` (false if out of range or already present; first good copy wins) `bool trackbuf_has(const trackbuf_t *b, int sector);` `bool trackbuf_complete(const trackbuf_t *b);` `bool trackbuf_matches(const trackbuf_t *b, int track, int side);` `void trackbuf_invalidate(trackbuf_t *b);` `const uint8_t *trackbuf_get(const trackbuf_t *b, int sector);`
  - `typedef enum { REQ_READ } req_op_t;` `typedef struct { req_op_t op; uint16_t block; } req_t;`
  - `typedef struct { uint16_t block; uint8_t status; uint8_t soft; uint8_t data[GCR_SECTOR_LEN]; } reply_t;`
  - status/soft constants: `ST_OK 0x00`, `ST_SOFT 0xE7`, `ST_IOERR 0xA7`, `ST_NODISK 0xAF`, `ST_BADBLK 0xAD`; `SOFT_RECAL 0x01`, `SOFT_FOREIGN 0x02`, `SOFT_BADDATA 0x04`, `SOFT_BADADDR 0x08`, `SOFT_OVERRUN 0x10`
  - Ruling (spec section 3 said SDK `queue_t` on target and a plain ring on the host): one C11-atomics SPSC ring used on both, so the code under test is the code that runs. Cost if wrong: swap the two-function body for `queue_t` later.
  - `#define IPC_REQ_SLOTS 4`, `#define IPC_REPLY_SLOTS 2`; `ipc_t`; `void ipc_init(ipc_t *q);` `bool ipc_req_push(ipc_t *q, const req_t *r);` `bool ipc_req_pop(ipc_t *q, req_t *r);` `bool ipc_reply_push(ipc_t *q, const reply_t *r);` `bool ipc_reply_pop(ipc_t *q, reply_t *r);` (single producer, single consumer per direction)

- [ ] **Step 1: Write the failing test**

`firmware/test/test_trackbuf_ipc.c`:
```c
#include <string.h>
#include "test.h"
#include "trackbuf.h"
#include "ipc.h"

static void test_trackbuf(void) {
    static trackbuf_t b;
    uint8_t p[524], q[524];
    memset(p, 0x11, sizeof p); memset(q, 0x22, sizeof q);
    trackbuf_init(&b);
    CHECK(!trackbuf_matches(&b, 0, 0));
    trackbuf_start(&b, 70, 1);
    CHECK(b.nsect == 8 && trackbuf_matches(&b, 70, 1) && !trackbuf_matches(&b, 70, 0));
    CHECK(!trackbuf_complete(&b) && !trackbuf_has(&b, 3));
    CHECK(trackbuf_put(&b, 3, p));
    CHECK(!trackbuf_put(&b, 3, q));                 // first good copy wins
    CHECK(memcmp(trackbuf_get(&b, 3), p, 524) == 0);
    CHECK(!trackbuf_put(&b, 8, p) && !trackbuf_put(&b, -1, p));
    for (int s = 0; s < 8; s++) trackbuf_put(&b, s, p);
    CHECK(trackbuf_complete(&b));
    trackbuf_invalidate(&b);
    CHECK(!trackbuf_matches(&b, 70, 1) && !trackbuf_has(&b, 3));
}

static void test_ipc(void) {
    static ipc_t q;
    ipc_init(&q);
    req_t r = {REQ_READ, 0};
    for (int i = 0; i < IPC_REQ_SLOTS; i++) { r.block = (uint16_t)i; CHECK(ipc_req_push(&q, &r)); }
    CHECK(!ipc_req_push(&q, &r));                    // full
    for (int i = 0; i < IPC_REQ_SLOTS; i++) { CHECK(ipc_req_pop(&q, &r)); CHECK(r.block == i); }
    CHECK(!ipc_req_pop(&q, &r));                     // empty
    static reply_t a, b;
    memset(&a, 0, sizeof a); a.block = 7; a.status = ST_SOFT; a.soft = SOFT_RECAL; a.data[523] = 0x5A;
    CHECK(ipc_reply_push(&q, &a));
    CHECK(ipc_reply_pop(&q, &b));
    CHECK(b.block == 7 && b.status == 0xE7 && b.soft == 0x01 && b.data[523] == 0x5A);
    CHECK(!ipc_reply_pop(&q, &b));
}

int main(void) {
    test_trackbuf();
    test_ipc();
    TEST_END();
}
```

Add to `firmware/test/Makefile`:
```make
TESTS  := test_crc32 test_mech test_console test_gcr test_flux_dec test_trackbuf_ipc

$(OUT)/test_trackbuf_ipc: test_trackbuf_ipc.c ../src/trackbuf.c ../src/ipc.c ../src/gcr.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $^
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd firmware/test && make`
Expected: FAIL, `trackbuf.h: No such file or directory`.

- [ ] **Step 3: Write `trackbuf.h` / `trackbuf.c`**

```c
// trackbuf.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "gcr.h"

// One track side of decoded sectors (12 max). First good copy of a sector wins.
typedef struct {
    int track, side, nsect;
    bool valid;
    uint16_t present;            // bit per sector
    uint8_t data[12][GCR_SECTOR_LEN];
} trackbuf_t;

void trackbuf_init(trackbuf_t *b);
void trackbuf_start(trackbuf_t *b, int track, int side);
bool trackbuf_put(trackbuf_t *b, int sector, const uint8_t *payload);
bool trackbuf_has(const trackbuf_t *b, int sector);
bool trackbuf_complete(const trackbuf_t *b);
bool trackbuf_matches(const trackbuf_t *b, int track, int side);
void trackbuf_invalidate(trackbuf_t *b);
const uint8_t *trackbuf_get(const trackbuf_t *b, int sector);
```

```c
// trackbuf.c
#include "trackbuf.h"
#include <string.h>

void trackbuf_init(trackbuf_t *b) { memset(b, 0, sizeof *b); }

void trackbuf_start(trackbuf_t *b, int track, int side) {
    b->track = track;
    b->side = side;
    b->nsect = gcr_sectors_per_track(track);
    b->present = 0;
    b->valid = b->nsect > 0;
}

bool trackbuf_put(trackbuf_t *b, int sector, const uint8_t *payload) {
    if (!b->valid || sector < 0 || sector >= b->nsect || (b->present & (1u << sector))) return false;
    memcpy(b->data[sector], payload, GCR_SECTOR_LEN);
    b->present |= (uint16_t)(1u << sector);
    return true;
}

bool trackbuf_has(const trackbuf_t *b, int sector) {
    return b->valid && sector >= 0 && sector < b->nsect && (b->present & (1u << sector));
}

bool trackbuf_complete(const trackbuf_t *b) {
    return b->valid && b->present == (uint16_t)((1u << b->nsect) - 1);
}

bool trackbuf_matches(const trackbuf_t *b, int track, int side) {
    return b->valid && b->track == track && b->side == side;
}

void trackbuf_invalidate(trackbuf_t *b) { b->valid = false; b->present = 0; }

const uint8_t *trackbuf_get(const trackbuf_t *b, int sector) {
    return trackbuf_has(b, sector) ? b->data[sector] : NULL;
}
```

- [ ] **Step 4: Write `ipc.h` / `ipc.c`**

```c
// ipc.h
#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include "gcr.h"

#define ST_OK       0x00
#define ST_SOFT     0xE7   // ok after soft errors (ROM: $E7)
#define ST_IOERR    0xA7   // SmartPort $27 with the ROM's $80 bit
#define ST_NODISK   0xAF   // $2F
#define ST_BADBLK   0xAD   // $2D

#define SOFT_RECAL    0x01
#define SOFT_FOREIGN  0x02
#define SOFT_BADDATA  0x04
#define SOFT_BADADDR  0x08
#define SOFT_OVERRUN  0x10

typedef enum { REQ_READ } req_op_t;
typedef struct { req_op_t op; uint16_t block; } req_t;
typedef struct {
    uint16_t block;
    uint8_t status;
    uint8_t soft;
    uint8_t data[GCR_SECTOR_LEN];   // 12 tag + 512 data; zeros on error
} reply_t;

#define IPC_REQ_SLOTS   4
#define IPC_REPLY_SLOTS 2

// Two single-producer single-consumer rings: core 0 -> core 1 requests, core 1 -> core 0 replies.
typedef struct {
    req_t req[IPC_REQ_SLOTS];
    atomic_uint req_head, req_tail;
    reply_t rep[IPC_REPLY_SLOTS];
    atomic_uint rep_head, rep_tail;
} ipc_t;

void ipc_init(ipc_t *q);
bool ipc_req_push(ipc_t *q, const req_t *r);
bool ipc_req_pop(ipc_t *q, req_t *r);
bool ipc_reply_push(ipc_t *q, const reply_t *r);
bool ipc_reply_pop(ipc_t *q, reply_t *r);
```

```c
// ipc.c
#include "ipc.h"
#include <string.h>

void ipc_init(ipc_t *q) {
    memset(q, 0, sizeof *q);
    atomic_init(&q->req_head, 0); atomic_init(&q->req_tail, 0);
    atomic_init(&q->rep_head, 0); atomic_init(&q->rep_tail, 0);
}

// head = next write (producer), tail = next read (consumer); counts run freely.
#define PUSH(arr, slots, head, tail, item)                                         \
    do {                                                                           \
        unsigned h = atomic_load_explicit(&q->head, memory_order_relaxed);         \
        unsigned t = atomic_load_explicit(&q->tail, memory_order_acquire);         \
        if (h - t >= (slots)) return false;                                        \
        q->arr[h % (slots)] = *(item);                                             \
        atomic_store_explicit(&q->head, h + 1, memory_order_release);              \
        return true;                                                               \
    } while (0)

#define POP(arr, slots, head, tail, item)                                          \
    do {                                                                           \
        unsigned t = atomic_load_explicit(&q->tail, memory_order_relaxed);         \
        unsigned h = atomic_load_explicit(&q->head, memory_order_acquire);         \
        if (h == t) return false;                                                  \
        *(item) = q->arr[t % (slots)];                                             \
        atomic_store_explicit(&q->tail, t + 1, memory_order_release);              \
        return true;                                                               \
    } while (0)

bool ipc_req_push(ipc_t *q, const req_t *r)     { PUSH(req, IPC_REQ_SLOTS, req_head, req_tail, r); }
bool ipc_req_pop(ipc_t *q, req_t *r)            { POP(req, IPC_REQ_SLOTS, req_head, req_tail, r); }
bool ipc_reply_push(ipc_t *q, const reply_t *r) { PUSH(rep, IPC_REPLY_SLOTS, rep_head, rep_tail, r); }
bool ipc_reply_pop(ipc_t *q, reply_t *r)        { POP(rep, IPC_REPLY_SLOTS, rep_head, rep_tail, r); }
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd firmware/test && make`
Expected: `test_trackbuf_ipc.c: <N> checks, 0 failed`, all other suites unchanged.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/trackbuf.h firmware/src/trackbuf.c firmware/src/ipc.h firmware/src/ipc.c firmware/test/test_trackbuf_ipc.c firmware/test/Makefile
git commit -m "feat(fw): track buffer and lock-free inter-core queues"
```

---

### Task 5: Mechanism server (request handling and retry policy)

**Files:**
- Create: `firmware/src/mech_server.h`, `firmware/src/mech_server.c`, `firmware/test/fake_drive.h`, `firmware/test/test_mech_server.c`
- Modify: `firmware/test/Makefile`

**Interfaces:**
- Consumes: `mech_*` (`mech.h`: `mech_status`, `mech_motor`, `mech_seek`, `mech_recal`, `mech_select_head`, `mech_state_init`, `MREG_CSTIN`), Task 1 `gcr_*`, Task 3 `flux_dec_*`, Task 4 `trackbuf_*`, `reply_t`, `ST_*`, `SOFT_*`.
- Produces (exact):
  - `typedef struct { void (*start)(void *ctx); void (*stop)(void *ctx); int (*read)(void *ctx, uint16_t *buf, int max); uint64_t (*now_us)(void *ctx); void *ctx; } flux_src_t;` (`read` returns intervals copied, 0 if none yet, -1 on ring overrun)
  - constants `SRV_REVS_MAX 12`, `SRV_RECAL_AFTER_REVS 6`, `SRV_SEEK_TRIES_MAX 4`, `SRV_REQ_TIMEOUT_MS 5000`, `SRV_IDLE_OFF_MS 1000`, `SRV_BATCH 256`
  - `typedef struct { uint64_t decode_us; uint32_t intervals; uint32_t fills; } srv_stats_t;`
  - Ruling (spec section 5 said "worst cycles per interval"): measured as the worst nanoseconds per interval over full 256-interval batches, using the microsecond timer, because the M33 cycle counter needs debug-block setup this task doesn't otherwise touch. The pass threshold is under 2000 ns (one interval per 2 us). Same question answered.
  - `mech_server_t`; `void mech_server_init(mech_server_t *s, const mech_bus_t *bus, const flux_src_t *src);` `void mech_server_read(mech_server_t *s, uint16_t block, reply_t *r);` `void mech_server_idle(mech_server_t *s);` `void mech_server_stop(mech_server_t *s);` `const srv_stats_t *mech_server_stats(const mech_server_t *s);`
- Ruling (tightens spec section 4 step 8): the track buffer is invalidated when the motor idles off and whenever CSTIN reads "no disk", so a swapped disk never returns cached sectors.

- [ ] **Step 1: Write the fake drive and flux source**

`firmware/test/fake_drive.h`:
```c
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "mech.h"
#include "mech_server.h"
#include "gcr.h"

// A fake Sony mechanism: decodes command strobes, answers status reads, and
// serves a GCR flux stream for the current track and selected head.
typedef struct {
    // mechanism state
    int pos, dir;                 // physical track, +1 inward / -1 outward
    bool motor, disk_in;
    uint8_t addr;
    int side;                     // last head selected via RDDATA0/1
    int step_strobes, recal_like; // counters for the tests
    uint64_t now_us;              // shared clock (bus delays + flux time)
    // flux stream
    bool streaming, no_flux;
    int skew;                     // headers report pos + skew (positioning fault)
    int foreign_always;           // >=0: every header claims this track
    int bad_sector, bad_revs;     // corrupt this sector's data for the first bad_revs revolutions
    int overrun_once;             // next read returns -1 once
    uint16_t ivs[65536];
    int nivs, rd, revs;
} fake_drive_t;

static void fd_set_addr(void *c, uint8_t a) {
    fake_drive_t *d = c; d->addr = a;
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
        d->pos += d->dir;
        if (d->pos < 0) d->pos = 0;
        if (d->pos > 79) d->pos = 79;
        break;
    case MCMD_MOTOR_ON:  d->motor = true; break;
    case MCMD_MOTOR_OFF: d->motor = false; break;
    default: break;
    }
}
static bool fd_read_rd(void *c) {
    fake_drive_t *d = c;
    switch (d->addr) {
    case MREG_CSTIN:   return !d->disk_in;
    case MREG_STEP:    return true;
    case MREG_MOTORON: return !d->motor;
    case MREG_TK0:     return d->pos != 0;
    case MREG_READY:   return !(d->motor && d->disk_in);
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

static void fd_bits(fake_drive_t *d, const uint8_t *bytes, int n, int *run, float cell) {
    for (int k = 0; k < n; k++)
        for (int b = 7; b >= 0; b--) {
            (*run)++;
            if ((bytes[k] >> b) & 1) {
                float t = (float)*run * cell - 2.0f;
                if (d->nivs < 65536) d->ivs[d->nivs++] = (uint16_t)(t + 0.5f);
                *run = 0;
            }
        }
}
static void fd_sync(fake_drive_t *d, int *run, float cell) {
    static const uint8_t ff = 0xFF;
    fd_bits(d, &ff, 1, run, cell);
    *run += 2;                                     // 10-bit self-sync byte
}

// Build one revolution for (pos, side); rev 0.. lets bad_revs corrupt early revolutions.
static void fd_build(fake_drive_t *d) {
    d->nivs = 0;
    int t = d->pos, hdr_t = d->foreign_always >= 0 ? d->foreign_always : d->pos + d->skew;
    int spt = gcr_sectors_per_track(t);
    float cell = 1e9f / 489600.0f / 40.0f;
    int run = 0;
    for (int k = 0; k < spt; k++) {
        int sec = (k * 4) % spt;   // any fixed order works for the fake
        uint8_t a[GCR_ADDR_FIELD_LEN], p[GCR_SECTOR_LEN], f[GCR_DATA_FIELD_LEN];
        for (int i = 0; i < GCR_SECTOR_LEN; i++) p[i] = fd_payload_byte(t, d->side, sec, i);
        gcr_encode_address(hdr_t < 0 ? 0 : (hdr_t > 79 ? 79 : hdr_t), sec, d->side, a);
        gcr_encode_data(sec, p, f);
        if (sec == d->bad_sector && d->revs < d->bad_revs) f[60] = gcr_nibbles[(gcr_decode6(f[60]) + 1) % 64];
        for (int i = 0; i < 6; i++) fd_sync(d, &run, cell);
        fd_bits(d, a, GCR_ADDR_FIELD_LEN, &run, cell);
        for (int i = 0; i < 6; i++) fd_sync(d, &run, cell);
        fd_bits(d, f, GCR_DATA_FIELD_LEN, &run, cell);
    }
    for (int i = 0; i < 6; i++) fd_sync(d, &run, cell);
}

static void fs_start(void *c) { fake_drive_t *d = c; d->streaming = true; d->rd = 0; d->revs = 0; fd_build(d); }
static void fs_stop(void *c)  { ((fake_drive_t *)c)->streaming = false; }
static int fs_read(void *c, uint16_t *buf, int max) {
    fake_drive_t *d = c;
    if (!d->streaming) return 0;
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

static void fake_drive_init(fake_drive_t *d) {
    memset(d, 0, sizeof *d);
    d->disk_in = true;
    d->pos = 37;                  // somewhere unknown
    d->bad_sector = -1;
    d->foreign_always = -1;
}
```

- [ ] **Step 2: Write the failing test**

`firmware/test/test_mech_server.c`:
```c
#include <string.h>
#include "test.h"
#include "fake_drive.h"

static fake_drive_t D;
static mech_bus_t B;
static flux_src_t S;
static mech_server_t SRV;

static void setup(void) {
    fake_drive_init(&D);
    B = fake_drive_bus(&D);
    S = fake_flux_src(&D);
    mech_server_init(&SRV, &B, &S);
}

static bool data_ok(const reply_t *r) {
    int t, s, sec;
    if (!gcr_block_to_tss(r->block, &t, &s, &sec)) return false;
    for (int i = 0; i < GCR_SECTOR_LEN; i++)
        if (r->data[i] != fd_payload_byte(t, s, sec, i)) return false;
    return true;
}

static void test_miss_then_hit(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    CHECK(r.status == ST_OK && r.soft == 0 && r.block == 0 && data_ok(&r));
    CHECK(D.pos == 0);
    int strobes = D.step_strobes;
    mech_server_read(&SRV, 5, &r);                   // same track side: served from the buffer
    CHECK(r.status == ST_OK && data_ok(&r));
    CHECK(D.step_strobes == strobes && mech_server_stats(&SRV)->fills == 1);
    mech_server_read(&SRV, 1599, &r);               // track 79 side 1
    CHECK(r.status == ST_OK && data_ok(&r) && D.pos == 79 && D.side == 1);
}

static void test_no_disk(void) {
    setup();
    D.disk_in = false;
    reply_t r;
    mech_server_read(&SRV, 100, &r);
    CHECK(r.status == ST_NODISK && D.step_strobes == 0 && !D.motor);
}

static void test_bad_block(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 1600, &r);
    CHECK(r.status == ST_BADBLK && D.step_strobes == 0 && !D.motor);
}

static void test_retry_then_ok(void) {
    setup();
    D.bad_sector = 3; D.bad_revs = 2;
    reply_t r;
    mech_server_read(&SRV, 3, &r);                   // track 0 side 0 sector 3
    CHECK(r.status == ST_SOFT && (r.soft & SOFT_BADDATA) && data_ok(&r));
}

static void test_budget_exhausted(void) {
    setup();
    D.bad_sector = 3; D.bad_revs = 1000;
    reply_t r;
    uint64_t t0 = D.now_us;
    mech_server_read(&SRV, 3, &r);
    CHECK(r.status == ST_IOERR && (r.soft & SOFT_RECAL));
    for (int i = 0; i < GCR_SECTOR_LEN; i++) CHECK(r.data[i] == 0);
    CHECK(D.now_us - t0 <= (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u + 200000u);
    mech_server_read(&SRV, 4, &r);                   // other sectors of that track were good
    CHECK(r.status == ST_OK && data_ok(&r));
}

static void test_wrong_track_reseeks(void) {
    setup();
    D.skew = 1;                                      // head lands one track off, once
    reply_t r;
    mech_server_read(&SRV, 24 * 2 * 10, &r);         // a block in track 10..
    CHECK(r.status == ST_SOFT && (r.soft & SOFT_FOREIGN) && data_ok(&r));
}

static void test_foreign_forever_fails(void) {
    setup();
    D.foreign_always = 50;
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    CHECK(r.status == ST_IOERR && (r.soft & SOFT_FOREIGN) && (r.soft & SOFT_RECAL));
}

static void test_no_flux_times_out(void) {
    setup();
    D.no_flux = true;
    reply_t r;
    uint64_t t0 = D.now_us;
    mech_server_read(&SRV, 0, &r);
    CHECK(r.status == ST_IOERR);
    CHECK(D.now_us - t0 <= (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u + 2500000u);  // + recal time
}

static void test_overrun_recovers(void) {
    setup();
    D.overrun_once = 1;
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    CHECK(r.status == ST_SOFT && (r.soft & SOFT_OVERRUN) && data_ok(&r));
}

static void test_idle_off_invalidates(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    CHECK(D.motor);
    mech_server_idle(&SRV);
    CHECK(D.motor);                                  // not idle long enough
    D.now_us += (uint64_t)SRV_IDLE_OFF_MS * 1000u + 1000u;
    mech_server_idle(&SRV);
    CHECK(!D.motor);
    int strobes = D.step_strobes;
    mech_server_read(&SRV, 1, &r);                   // same track: buffer was dropped, so re-read
    CHECK(r.status == ST_OK && data_ok(&r) && D.motor);
    CHECK(D.step_strobes == strobes);                // already on the track: no steps, but a fresh fill
    CHECK(mech_server_stats(&SRV)->fills == 2);
}

static void test_no_disk_invalidates(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    D.disk_in = false;
    mech_server_read(&SRV, 1, &r);
    CHECK(r.status == ST_NODISK);
    D.disk_in = true;
    mech_server_read(&SRV, 1, &r);
    CHECK(r.status == ST_OK && mech_server_stats(&SRV)->fills == 2);
}

static void test_stop(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    mech_server_stop(&SRV);
    CHECK(!D.motor && !D.streaming);
}

int main(void) {
    test_miss_then_hit();
    test_no_disk();
    test_bad_block();
    test_retry_then_ok();
    test_budget_exhausted();
    test_wrong_track_reseeks();
    test_foreign_forever_fails();
    test_no_flux_times_out();
    test_overrun_recovers();
    test_idle_off_invalidates();
    test_no_disk_invalidates();
    test_stop();
    TEST_END();
}
```

Note on `test_wrong_track_reseeks`: the fake's `skew` stays 1, so after the server resyncs its position to the header's track (pos + 1) and seeks back by one, the head is physically at track - 1 and the headers read `track`. That is exactly the ROM's "resync to the header, re-seek" behaviour converging.

Add to `firmware/test/Makefile`:
```make
TESTS  := test_crc32 test_mech test_console test_gcr test_flux_dec test_trackbuf_ipc test_mech_server

$(OUT)/test_mech_server: test_mech_server.c ../src/mech_server.c ../src/mech.c ../src/mech_addr.c ../src/flux_dec.c ../src/trackbuf.c ../src/gcr.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $^
```

- [ ] **Step 3: Run to verify it fails**

Run: `cd firmware/test && make`
Expected: FAIL, `mech_server.h: No such file or directory`.

- [ ] **Step 4: Write `mech_server.h`**

```c
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "mech.h"
#include "flux_dec.h"
#include "trackbuf.h"
#include "ipc.h"

// Retry policy: drive ROM (liron.asm cmd_read_block, retry loop lines 1297-1317)
// translated from single-sector attempts to whole-track revolutions (M4 spec sec. 6).
#define SRV_REVS_MAX          12
#define SRV_RECAL_AFTER_REVS  6
#define SRV_SEEK_TRIES_MAX    4
#define SRV_REQ_TIMEOUT_MS    5000
#define SRV_IDLE_OFF_MS       1000
#define SRV_BATCH             256

typedef struct {
    void (*start)(void *ctx);                       // stream RD (head already selected)
    void (*stop)(void *ctx);
    int (*read)(void *ctx, uint16_t *buf, int max); // >=0 copied, -1 ring overrun
    uint64_t (*now_us)(void *ctx);
    void *ctx;
} flux_src_t;

typedef struct {
    uint32_t intervals;
    uint32_t worst_ns_per_interval;   // worst full batch of decode time / intervals
    uint32_t fills;
} srv_stats_t;

typedef struct {
    const mech_bus_t *bus;
    const flux_src_t *src;
    mech_state_t ms;
    bool motor_on;
    uint64_t last_us;
    trackbuf_t tb;
    flux_dec_t dec;
    flux_dec_sink_t sink;
    uint8_t soft;
    bool foreign_seen;
    int foreign_track;
    srv_stats_t stats;
    uint16_t batch[SRV_BATCH];
} mech_server_t;

void mech_server_init(mech_server_t *s, const mech_bus_t *bus, const flux_src_t *src);
void mech_server_read(mech_server_t *s, uint16_t block, reply_t *r);
void mech_server_idle(mech_server_t *s);
void mech_server_stop(mech_server_t *s);
const srv_stats_t *mech_server_stats(const mech_server_t *s);
```

- [ ] **Step 5: Write `mech_server.c`**

```c
#include "mech_server.h"
#include <string.h>

// 1.2 x one revolution per speed zone (394/429/472/525/590 rpm).
static const uint32_t rev_ms[5] = { 183, 168, 153, 138, 123 };

static void on_sector(void *ctx, int sector, const uint8_t *payload) {
    mech_server_t *s = ctx;
    trackbuf_put(&s->tb, sector, payload);
}

static void on_event(void *ctx, flux_ev_t ev, int track, int side, int sector) {
    mech_server_t *s = ctx;
    (void)side; (void)sector;
    switch (ev) {
    case FLUX_EV_BAD_ADDR: case FLUX_EV_RANGE: s->soft |= SOFT_BADADDR; break;
    case FLUX_EV_NO_DATA:  case FLUX_EV_BAD_DATA: s->soft |= SOFT_BADDATA; break;
    case FLUX_EV_FOREIGN:
        s->soft |= SOFT_FOREIGN;
        if (track != s->tb.track) { s->foreign_seen = true; s->foreign_track = track; }
        break;
    }
}

void mech_server_init(mech_server_t *s, const mech_bus_t *bus, const flux_src_t *src) {
    memset(s, 0, sizeof *s);
    s->bus = bus;
    s->src = src;
    mech_state_init(&s->ms);
    trackbuf_init(&s->tb);
    s->sink.sector = on_sector;
    s->sink.event = on_event;
    s->sink.ctx = s;
}

static uint64_t now_us(const mech_server_t *s) { return s->src->now_us(s->src->ctx); }

static void motor_off(mech_server_t *s) {
    mech_motor(s->bus, &s->ms, false);
    s->motor_on = false;
    trackbuf_invalidate(&s->tb);
}

// Fill the track buffer for (track, side). Returns true if every sector is present.
static bool fill(mech_server_t *s, int track, int side, uint64_t t_req) {
    s->stats.fills++;
    trackbuf_start(&s->tb, track, side);
    if (!s->motor_on) {
        if (mech_motor(s->bus, &s->ms, true) != MECH_OK) return false;
        s->motor_on = true;
    }
    int seeks = 0, revs = 0;
    bool recal_done = false;
    for (;;) {                                         // one pass per (re)seek
        if (seeks >= SRV_SEEK_TRIES_MAX) return false;
        if (seeks == 2) { mech_recal(s->bus, &s->ms); s->soft |= SOFT_RECAL; }
        if (mech_seek(s->bus, &s->ms, track) != MECH_OK) { seeks++; s->ms.track_known = false; continue; }
        mech_select_head(s->bus, side);
        s->src->start(s->src->ctx);
        flux_dec_reset(&s->dec, track, side, &s->sink);
        s->foreign_seen = false;
        uint64_t rev_t = now_us(s);
        uint32_t rev_len = rev_ms[track / 16] * 1000u;
        bool reseek = false;
        while (!reseek) {
            uint64_t b0 = now_us(s);
            int n = s->src->read(s->src->ctx, s->batch, SRV_BATCH);
            if (n < 0) { s->soft |= SOFT_OVERRUN; flux_dec_reset(&s->dec, track, side, &s->sink); continue; }
            if (n > 0) {
                uint64_t d0 = now_us(s);
                flux_dec_push_many(&s->dec, s->batch, n);
                uint64_t d1 = now_us(s);
                s->stats.intervals += (uint32_t)n;
                if (n == SRV_BATCH) {
                    uint32_t ns = (uint32_t)((d1 - d0) * 1000u / (uint64_t)n);
                    if (ns > s->stats.worst_ns_per_interval) s->stats.worst_ns_per_interval = ns;
                }
            }
            (void)b0;
            if (trackbuf_complete(&s->tb)) { s->src->stop(s->src->ctx); return true; }
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
                if (revs >= SRV_REVS_MAX) { s->src->stop(s->src->ctx); return false; }
                if (revs == SRV_RECAL_AFTER_REVS && !recal_done) {
                    recal_done = true;
                    s->src->stop(s->src->ctx);
                    mech_recal(s->bus, &s->ms);
                    s->soft |= SOFT_RECAL;
                    reseek = true;
                }
            }
        }
    }
}

void mech_server_read(mech_server_t *s, uint16_t block, reply_t *r) {
    memset(r, 0, sizeof *r);
    r->block = block;
    int track, side, sector;
    if (!gcr_block_to_tss(block, &track, &side, &sector)) { r->status = ST_BADBLK; return; }
    if (mech_status(s->bus, &s->ms, MREG_CSTIN)) {     // CSTIN high = no disk
        trackbuf_invalidate(&s->tb);
        r->status = ST_NODISK;
        return;
    }
    uint64_t t_req = now_us(s);
    s->last_us = t_req;
    if (trackbuf_matches(&s->tb, track, side) && trackbuf_has(&s->tb, sector)) {
        memcpy(r->data, trackbuf_get(&s->tb, sector), GCR_SECTOR_LEN);
        r->status = ST_OK;
        return;
    }
    s->soft = 0;
    fill(s, track, side, t_req);
    s->last_us = now_us(s);
    r->soft = s->soft;
    if (trackbuf_has(&s->tb, sector)) {
        memcpy(r->data, trackbuf_get(&s->tb, sector), GCR_SECTOR_LEN);
        r->status = s->soft ? ST_SOFT : ST_OK;
    } else {
        r->status = ST_IOERR;
    }
}

void mech_server_idle(mech_server_t *s) {
    if (s->motor_on && now_us(s) - s->last_us > (uint64_t)SRV_IDLE_OFF_MS * 1000u) motor_off(s);
}

void mech_server_stop(mech_server_t *s) {
    s->src->stop(s->src->ctx);
    if (s->motor_on) motor_off(s);
    trackbuf_invalidate(&s->tb);
}

const srv_stats_t *mech_server_stats(const mech_server_t *s) { return &s->stats; }
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cd firmware/test && make`
Expected: `test_mech_server.c: <N> checks, 0 failed`; all other suites still pass; `check_wrtgate: ok`.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/mech_server.h firmware/src/mech_server.c firmware/test/fake_drive.h firmware/test/test_mech_server.c firmware/test/Makefile
git commit -m "feat(fw): mechanism server with the ROM's read retry policy"
```

---

### Task 6: Continuous flux stream (PIO1 + ring DMA)

**Files:**
- Modify: `firmware/src/flux.h`, `firmware/src/flux.c`

**Interfaces:**
- Consumes: existing `flux_init`, PIO program `flux_rx`, `FLUX_PIO`/`FLUX_SM`/`dma_ch` in `flux.c`.
- Produces (exact): `#define FLUX_RING_LEN 8192`; `void flux_stream_start(void);` `void flux_stream_stop(void);` `int flux_stream_read(uint16_t *buf, int max);` (same contract as `flux_src_t.read`: copied count, 0 if none, -1 on overrun).
- Ruling (simplifies spec section 5's "two chained DMA channels"): one DMA channel with a 16 KB write ring (`channel_config_set_ring(..., true, 14)` on a 16 KB-aligned buffer) and a 28-bit transfer count (268 million transfers, about 9 minutes at the worst-case rate) re-armed on every `flux_stream_start`. Same behaviour, one channel. If this proves wrong on the bench, two chained channels is the fallback.

This task is hardware-only (no host test). Verification is a clean firmware build here and the bench in Task 9.

- [ ] **Step 1: Add to `flux.h`**

```c
#define FLUX_RING_LEN 8192   // 16 KB ring of uint16 counts, about 16 ms at the worst-case rate

// Continuous capture for the mechanism server (core 1). The head must already be selected.
void flux_stream_start(void);
void flux_stream_stop(void);
int flux_stream_read(uint16_t *buf, int max);   // copied, 0 if none, -1 if the ring overran
```

- [ ] **Step 2: Add to `flux.c`** (after `flux_data`; reuse the existing `offset` and `dma_ch`)

```c
static uint16_t ring[FLUX_RING_LEN] __attribute__((aligned(FLUX_RING_LEN * 2)));
static const uint32_t STREAM_COUNT = 0x0FFFFFFFu;
static uint32_t consumed;

static uint32_t stream_written(void) {
    return STREAM_COUNT - (dma_channel_hw_addr(dma_ch)->transfer_count & 0x0FFFFFFFu);
}

void flux_stream_start(void) {
    flux_stream_stop();
    pio_sm_clear_fifos(FLUX_PIO, FLUX_SM);
    pio_sm_restart(FLUX_PIO, FLUX_SM);
    pio_sm_exec(FLUX_PIO, FLUX_SM, pio_encode_jmp(offset));
    dma_channel_config cfg = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_ring(&cfg, true, 14);                 // wrap writes every 16 KB
    channel_config_set_dreq(&cfg, pio_get_dreq(FLUX_PIO, FLUX_SM, false));
    dma_channel_configure(dma_ch, &cfg, ring, &FLUX_PIO->rxf[FLUX_SM], STREAM_COUNT, true);
    consumed = 0;
    pio_sm_set_enabled(FLUX_PIO, FLUX_SM, true);
}

void flux_stream_stop(void) {
    pio_sm_set_enabled(FLUX_PIO, FLUX_SM, false);
    if (dma_channel_is_busy(dma_ch)) dma_channel_abort(dma_ch);
}

int flux_stream_read(uint16_t *buf, int max) {
    uint32_t written = stream_written();
    uint32_t avail = written - consumed;
    if (avail > FLUX_RING_LEN) { consumed = written; return -1; }     // writer lapped us
    int n = avail < (uint32_t)max ? (int)avail : max;
    for (int i = 0; i < n; i++) buf[i] = ring[(consumed + (uint32_t)i) % FLUX_RING_LEN];
    consumed += (uint32_t)n;
    return n;
}
```

`flux_capture()` (the M3 `capture` command) keeps working unchanged: it reconfigures the same channel without a ring each time it runs, and `capture` is only allowed while the server is off (Task 7).

- [ ] **Step 3: Build**

Run: `cd firmware && make test && make`
Expected: host suites unchanged and passing, `check_wrtgate: ok`, firmware links, no warnings from `src/`.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/flux.h firmware/src/flux.c
git commit -m "feat(fw): continuous flux stream into a DMA ring buffer"
```

---

### Task 7: Two cores, server control, readblk and readdisk

**Files:**
- Modify: `firmware/src/console.h`, `firmware/src/console.c`, `firmware/test/test_console.c`, `firmware/src/main.c`, `firmware/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 4 `ipc_*`, `reply_t`, `ST_*`; Task 5 `mech_server_*`, `flux_src_t`; Task 6 `flux_stream_*`; existing `hw_*`, `mech_*`, `crc32_update`, `console_parse`.
- Produces: console ops `CON_SERVER` (arg on/off), `CON_READBLK` (0-1599), `CON_READDISK`; the wire protocol in spec section 7 (used by Task 8):
  - `readblk <n>` -> `BLK <n> <status 2 hex> <soft 2 hex>` then 33 lines of hex (524 bytes, 16 per line, last line 12) then `OK`.
  - `readdisk` -> `DISK BEGIN 1600`, then per track side `TS <t> <s> <nsect> <status 2 hex> <soft 2 hex>` + nsect x 512 binary bytes + `\nTS END <crc32 8 hex>`, then `DISK END <worst ns per interval> <total ms>` and `OK`. (Pico stdio turns `\n` into `\r\n` for text.)

- [ ] **Step 1: Write the failing parser tests** (append to `firmware/test/test_console.c` before `CHECK(strlen(console_help()) > 20);`)

```c
    CHECK(P("server on").op == CON_SERVER && P("server on").arg[0] == 1);
    CHECK(P("server off").arg[0] == 0);
    CHECK(P("server").op == CON_BAD);
    CHECK(P("readblk 0").op == CON_READBLK && P("readblk 1599").arg[0] == 1599);
    CHECK(P("readblk 1600").op == CON_BAD && P("readblk -1").op == CON_BAD);
    CHECK(P("readdisk").op == CON_READDISK && P("readdisk 1").op == CON_BAD);
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd firmware/test && make`
Expected: FAIL to compile, `CON_SERVER` undeclared.

- [ ] **Step 3: Extend the parser**

In `console.h`, extend the enum (append before the closing brace):
```c
    CON_SERVER, CON_READBLK, CON_READDISK,
```
In `console.c`, append to `specs[]`:
```c
    {"server",   CON_SERVER,   1, true,  {0},       {0}},
    {"readblk",  CON_READBLK,  1, false, {0},       {1599}},
    {"readdisk", CON_READDISK, 0, false, {0},       {0}},
```
Replace `console_help()`'s string:
```c
    return "help | oe on|off | enbl on|off | status [0-15] | cmd <0-15> | motor on|off\n"
           "recal | seek <0-79> | head <0|1> | eject | tach | settle <us> | capture <t> <s> <n>\n"
           "server on|off | readblk <0-1599> | readdisk";
```

- [ ] **Step 4: Run tests**

Run: `cd firmware/test && make`
Expected: all suites pass.

- [ ] **Step 5: Wire the cores in `main.c`**

Add includes:
```c
#include <stdatomic.h>
#include "pico/multicore.h"
#include "ipc.h"
#include "mech_server.h"
#include "gcr.h"
```

Add after the existing statics (`static mech_state_t ms; static bool oe_on, ...`):
```c
static ipc_t ipc;
static mech_server_t srv;
static atomic_bool srv_run, srv_running;

static void hw_src_start(void *c) { (void)c; flux_stream_start(); }
static void hw_src_stop(void *c)  { (void)c; flux_stream_stop(); }
static int  hw_src_read(void *c, uint16_t *b, int max) { (void)c; return flux_stream_read(b, max); }
static uint64_t hw_src_now(void *c) { (void)c; return time_us_64(); }
static const flux_src_t hw_src = { hw_src_start, hw_src_stop, hw_src_read, hw_src_now, NULL };

static void core1_main(void) {
    mech_server_init(&srv, hw_mech_bus(), &hw_src);
    for (;;) {
        if (!atomic_load(&srv_run)) {
            if (atomic_load(&srv_running)) { mech_server_stop(&srv); atomic_store(&srv_running, false); }
            sleep_ms(1);
            continue;
        }
        atomic_store(&srv_running, true);
        req_t rq;
        if (ipc_req_pop(&ipc, &rq)) {
            static reply_t rp;
            mech_server_read(&srv, rq.block, &rp);
            while (!ipc_reply_push(&ipc, &rp)) tight_loop_contents();
        } else {
            mech_server_idle(&srv);
        }
    }
}

static bool server_set(bool on) {
    if (on) {
        hw_mech_outputs_enable(true);
        hw_mech_enbl(true);
        oe_on = enbl_on = true;
        atomic_store(&srv_run, true);
        return true;
    }
    atomic_store(&srv_run, false);
    absolute_time_t until = make_timeout_time_ms(2000);
    while (atomic_load(&srv_running) && !time_reached(until)) sleep_ms(1);
    hw_mech_enbl(false);
    hw_mech_outputs_enable(false);
    oe_on = enbl_on = motor_on = false;
    hw_led(false);
    return !atomic_load(&srv_running);
}

static bool request(uint16_t block, reply_t *r) {
    req_t rq = { REQ_READ, block };
    if (!ipc_req_push(&ipc, &rq)) return false;
    absolute_time_t until = make_timeout_time_ms(SRV_REQ_TIMEOUT_MS + 3000);
    while (!ipc_reply_pop(&ipc, r)) {
        if (time_reached(until)) return false;
        tight_loop_contents();
    }
    return true;
}

static void do_readblk(uint16_t block) {
    static reply_t r;
    if (!request(block, &r)) { puts("ERR server timeout"); return; }
    printf("BLK %u %02x %02x\n", block, r.status, r.soft);
    for (int i = 0; i < GCR_SECTOR_LEN; i += 16) {
        int n = GCR_SECTOR_LEN - i < 16 ? GCR_SECTOR_LEN - i : 16;
        for (int k = 0; k < n; k++) printf("%02x", r.data[i + k]);
        printf("\n");
    }
    puts("OK");
}

static void do_readdisk(void) {
    static uint8_t ts[12 * GCR_DATA_LEN];
    static reply_t r;
    uint64_t t0 = time_us_64();
    printf("DISK BEGIN %d\n", GCR_BLOCKS);
    for (int t = 0; t < GCR_TRACKS; t++) {
        for (int s = 0; s < GCR_SIDES; s++) {
            int nsect = gcr_sectors_per_track(t);
            uint8_t status = ST_OK, soft = 0;
            for (int sec = 0; sec < nsect; sec++) {
                uint16_t b = (uint16_t)gcr_tss_to_block(t, s, sec);
                if (!request(b, &r)) { memset(&r, 0, sizeof r); r.status = ST_IOERR; }
                memcpy(ts + sec * GCR_DATA_LEN, r.data + GCR_TAG_LEN, GCR_DATA_LEN);
                if (r.status == ST_IOERR || r.status == ST_NODISK) status = r.status;
                else if (r.status == ST_SOFT && status == ST_OK) status = ST_SOFT;
                soft |= r.soft;
            }
            printf("TS %d %d %d %02x %02x\n", t, s, nsect, status, soft);
            stdio_flush();
            stdio_put_string((const char *)ts, nsect * GCR_DATA_LEN, false, false);
            printf("\nTS END %08lx\n", (unsigned long)crc32_update(0, ts, (size_t)nsect * GCR_DATA_LEN));
        }
    }
    printf("DISK END %lu %lu\n", (unsigned long)mech_server_stats(&srv)->worst_ns_per_interval,
           (unsigned long)((time_us_64() - t0) / 1000u));
    puts("OK");
}
```

In `execute()`, add at the very top of the function body (before the `switch`):
```c
    bool mech_cmd = c->op == CON_OE || c->op == CON_ENBL || c->op == CON_STATUS || c->op == CON_CMD ||
                    c->op == CON_MOTOR || c->op == CON_RECAL || c->op == CON_SEEK || c->op == CON_HEAD ||
                    c->op == CON_EJECT || c->op == CON_TACH || c->op == CON_CAPTURE;
    if (mech_cmd && atomic_load(&srv_run)) { puts("ERR server on"); return; }
```
and add these cases to the `switch`:
```c
    case CON_SERVER:
        if (server_set(c->arg[0])) puts("OK"); else puts("ERR server did not stop");
        return;
    case CON_READBLK:
        if (!atomic_load(&srv_run)) { puts("ERR server off"); return; }
        do_readblk((uint16_t)c->arg[0]);
        return;
    case CON_READDISK:
        if (!atomic_load(&srv_run)) { puts("ERR server off"); return; }
        do_readdisk();
        return;
```

In `main()`, after `flux_init();` add:
```c
    ipc_init(&ipc);
    atomic_init(&srv_run, false);
    atomic_init(&srv_running, false);
    multicore_launch_core1(core1_main);
```
Add `#include <string.h>` if `memset`/`memcpy` are not already visible.

- [ ] **Step 6: CMake**

In `firmware/CMakeLists.txt`, change `add_executable` and `target_link_libraries`:
```cmake
add_executable(a2m_bringup
    src/main.c src/hw.c src/crc32.c src/mech.c src/mech_addr.c src/console.c src/flux.c
    src/gcr.c src/flux_dec.c src/trackbuf.c src/ipc.c src/mech_server.c)
target_link_libraries(a2m_bringup pico_stdlib pico_multicore hardware_pio hardware_dma)
```

- [ ] **Step 7: Build and test**

Run: `cd firmware && make test && make`
Expected: all host suites `0 failed`, `check_wrtgate: ok`, firmware links with no warnings from `src/`.

- [ ] **Step 8: Commit**

```bash
git add firmware/
git commit -m "feat(fw): core 1 mechanism server, readblk and readdisk over USB"
```

---

### Task 8: Host tool: readdisk and readblk

**Files:**
- Modify: `tools/a2m/proto.py`, `tools/a2m/serialio.py`, `tools/a2m/cli.py`, `tools/tests/test_proto_cli.py`

**Interfaces:**
- Consumes: the Task 7 wire protocol; `gcr.sectors_per_track`, `gcr.block_number`.
- Produces: `proto.DiskError(CaptureError)`; `proto.parse_disk(stream: bytes) -> (image: bytes, tracks: list[tuple[int,int,int,int]], mean_ns: int, backlog_max: int, total_ms: int)` where `tracks` = `(track, side, status, soft)` per track side; `proto.parse_blk(lines: list[str]) -> (block, status, soft, data: bytes)`; `Console.readdisk(timeout=900.0) -> bytes`; `Console.readblk(n) -> list[str]`; CLI subcommands `readdisk --port P --out F [--compare REF]` and `readblk --port P --block N [--compare REF]`.

- [ ] **Step 1: Write the failing tests** (append to `tools/tests/test_proto_cli.py`)

```python
from a2m import proto as _proto


def make_disk(image: bytes, bad_crc_at=None, cut=0, fail=()):
    out = b"junk\r\nDISK BEGIN 1600\r\n"
    for t in range(80):
        for s in range(2):
            n = gcr.sectors_per_track(t)
            b0 = gcr.block_number(t, s, 0)
            payload = image[b0 * 512:(b0 + n) * 512]
            status = 0xA7 if (t, s) in fail else 0x00
            crc = zlib.crc32(payload)
            if bad_crc_at == (t, s):
                crc ^= 1
            out += b"TS %d %d %d %02x %02x\r\n" % (t, s, n, status, 0) + payload + b"\r\nTS END %08x\r\n" % crc
    out += b"DISK END 812 131000\r\nOK\r\n"
    return out[:len(out) - cut] if cut else out


def test_parse_disk_ok():
    img = bytes((i * 7) & 0xFF for i in range(1600 * 512))
    out, tracks, worst, ms = _proto.parse_disk(make_disk(img))
    assert out == img and len(tracks) == 160 and worst == 812 and ms == 131000
    assert all(st == 0 for (_, _, st, _) in tracks)


def test_parse_disk_bad_crc():
    img = bytes(1600 * 512)
    with pytest.raises(_proto.CaptureError):
        _proto.parse_disk(make_disk(img, bad_crc_at=(33, 1)))


def test_parse_disk_truncated():
    img = bytes(1600 * 512)
    with pytest.raises(_proto.CaptureError):
        _proto.parse_disk(make_disk(img, cut=5000))


def test_parse_disk_reports_failed_track():
    img = bytes(1600 * 512)
    _, tracks, _, _ = _proto.parse_disk(make_disk(img, fail={(12, 0)}))
    assert [(t, s) for (t, s, st, _) in tracks if st == 0xA7] == [(12, 0)]


def test_parse_blk():
    data = bytes(range(256)) * 2 + bytes(12)
    lines = ["readblk 7", "BLK 7 e7 04"] + [data[i:i + 16].hex() for i in range(0, 524, 16)]
    block, status, soft, out = _proto.parse_blk(lines)
    assert (block, status, soft, out) == (7, 0xE7, 0x04, data)
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd tools && .venv/bin/pytest tests/test_proto_cli.py -q`
Expected: FAIL with `AttributeError: module 'a2m.proto' has no attribute 'parse_disk'`.

- [ ] **Step 3: Implement in `proto.py`** (append)

```python
from . import gcr as _gcr

DISK_BEGIN_RE = re.compile(rb"DISK BEGIN (\d+)\r?\n")
TS_RE = re.compile(rb"TS (\d+) (\d+) (\d+) ([0-9a-f]{2}) ([0-9a-f]{2})\r?\n")
TS_END_RE = re.compile(rb"\r?\nTS END ([0-9a-f]{8})\r?\n")
DISK_END_RE = re.compile(rb"DISK END (\d+) (\d+)\r?\n")
BLK_RE = re.compile(r"BLK (\d+) ([0-9a-f]{2}) ([0-9a-f]{2})")


class DiskError(CaptureError):
    pass


def parse_disk(stream: bytes):
    m = DISK_BEGIN_RE.search(stream)
    if not m:
        raise DiskError("no DISK BEGIN in stream")
    pos = m.end()
    image = bytearray(_gcr.TOTAL_BLOCKS * 512)
    tracks = []
    for t in range(_gcr.TRACKS):
        for s in range(_gcr.SIDES):
            h = TS_RE.match(stream, pos)
            if not h or (int(h[1]), int(h[2])) != (t, s):
                raise DiskError(f"expected TS {t} {s} at byte {pos}")
            n = int(h[3])
            if n != _gcr.sectors_per_track(t):
                raise DiskError(f"TS {t} {s}: {n} sectors")
            start = h.end()
            end = start + n * 512
            if len(stream) < end:
                raise DiskError(f"truncated in TS {t} {s}")
            payload = stream[start:end]
            e = TS_END_RE.match(stream, end)
            if not e:
                raise DiskError(f"no TS END after TS {t} {s}")
            if int(e[1], 16) != zlib.crc32(payload):
                raise DiskError(f"crc mismatch in TS {t} {s}")
            b0 = _gcr.block_number(t, s, 0)
            image[b0 * 512:(b0 + n) * 512] = payload
            tracks.append((t, s, int(h[4], 16), int(h[5], 16)))
            pos = e.end()
    d = DISK_END_RE.match(stream, pos)
    if not d:
        raise DiskError("no DISK END")
    return bytes(image), tracks, int(d[1]), int(d[2])


def parse_blk(lines):
    for i, line in enumerate(lines):
        m = BLK_RE.fullmatch(line.strip())
        if m:
            data = bytes.fromhex("".join(l.strip() for l in lines[i + 1:]))
            if len(data) != _gcr.SECTOR_LEN:
                raise CaptureError(f"BLK data is {len(data)} bytes")
            return int(m[1]), int(m[2], 16), int(m[3], 16), data
    raise CaptureError("no BLK line")
```

- [ ] **Step 4: Implement in `serialio.py`** (add methods to `Console`)

```python
    def readblk(self, block: int, timeout: float = 10.0) -> list[str]:
        return self.cmd(f"readblk {block}", timeout=timeout)

    def readdisk(self, timeout: float = 900.0) -> bytes:
        self.ser.reset_input_buffer()
        self.ser.write(b"readdisk\n")
        deadline = time.monotonic() + timeout
        buf = b""
        while time.monotonic() < deadline:
            buf += self.ser.read(65536)
            if b"DISK BEGIN" not in buf:
                for raw in buf.split(b"\n")[:-1]:
                    if raw.strip().startswith(b"ERR"):
                        raise ConsoleError(raw.decode(errors="replace").strip())
            elif re.search(rb"DISK END \d+ \d+\r?\n", buf):
                tail_deadline = time.monotonic() + 1.0
                while not re.search(rb"\nOK\r?\n", buf[-16:]) and time.monotonic() < tail_deadline:
                    buf += self.ser.read(64)
                return buf
        raise ConsoleError("readdisk timed out")
```

(`DISK END` can't appear inside the binary payload by accident and still pass `parse_disk`: every track side is length-framed and CRC-checked, so a premature return is caught as `DiskError`.)

- [ ] **Step 5: Implement in `cli.py`**

Add subparsers in `main()` next to the others:
```python
    r = sub.add_parser("readdisk", help="read the whole disk on the board (M4)")
    r.add_argument("--port", required=True); r.add_argument("--out", required=True)
    r.add_argument("--compare")
    b = sub.add_parser("readblk", help="read one block on the board (M4)")
    b.add_argument("--port", required=True); b.add_argument("--block", type=int, required=True)
    b.add_argument("--compare")
```
and before `return _disk(args)`:
```python
    if args.cmd == "readdisk":
        from .serialio import Console
        con = Console(args.port)
        img, tracks, worst, ms = parse_disk(con.readdisk())
        Path(args.out).write_bytes(img)
        bad = [(t, s, st) for (t, s, st, _) in tracks if st not in (0x00, 0xE7)]
        soft = [(t, s) for (t, s, st, _) in tracks if st == 0xE7]
        print(f"readdisk: {ms} ms, worst decode {worst} ns/interval, "
              f"{len(bad)} failed track sides, {len(soft)} with soft errors -> {args.out}")
        if bad:
            print("failed:", bad[:20])
        ok = not bad
        if args.compare:
            ref = Path(args.compare).read_bytes()
            diff = [k for k in range(gcr.TOTAL_BLOCKS) if img[k * 512:(k + 1) * 512] != ref[k * 512:(k + 1) * 512]]
            print(f"compare vs {args.compare}: {len(diff)} blocks differ", diff[:40])
            ok = ok and not diff
        return 0 if ok else 1
    if args.cmd == "readblk":
        from .serialio import Console
        con = Console(args.port)
        block, status, soft, data = parse_blk(con.readblk(args.block))
        print(f"block {block}: status {status:02x} soft {soft:02x}")
        if args.compare:
            ref = Path(args.compare).read_bytes()
            same = data[gcr.TAG_LEN:] == ref[block * 512:(block + 1) * 512]
            print("matches reference" if same else "DIFFERS from reference")
            return 0 if same else 1
        return 0
```
and extend the import line at the top of `cli.py`:
```python
from .proto import parse_capture, CaptureError, parse_disk, parse_blk
```

- [ ] **Step 6: Run tests**

Run: `cd tools && .venv/bin/pytest -q`
Expected: all pass (previous 34 + 5 new = 39).

- [ ] **Step 7: Commit**

```bash
git add tools/a2m/proto.py tools/a2m/serialio.py tools/a2m/cli.py tools/tests/test_proto_cli.py
git commit -m "feat(tools): readdisk and readblk for the board-side reader"
```

---

### Task 9: [BENCH] M4: blocks read on the board

**Files:**
- Create: `docs/build_log/m4.md`, `docs/captures/m4/`

Bench safety: `oe off` (or `server off`) and mechanism supply off before any Pico USB unplug or replug. SWD flash and `make reset` are fine. Use `--port /dev/cu.usbmodem114401` (the Pico; the Debug Probe is the other usbmodem port). Close any console terminal before running `a2mflux.py`. REF800K disk in the drive, tab protected. Drive A first.

- [ ] **Step 1: Flash and regression check (server off)**

`cd firmware && make flash`. Console: `oe on`, `enbl on`, `status` (800K ID as in M1), `motor on`, `recal`, `seek 40`, `tach` (about 472 rpm), `motor off`, `oe off`. Expected: all `OK`, same values as M1/M2.

- [ ] **Step 2: Single blocks**

`server on`, then from `tools/`:
```bash
for b in 0 1 23 400 900 1300 1599; do .venv/bin/python a2mflux.py readblk --port /dev/cu.usbmodem114401 --block $b --compare /Applications/ADTPro-v.r.m/disks/REF800K.po; done
```
Expected: `status 00` and `matches reference` for every block. A status of `e7` with a match is acceptable; record it.

- [ ] **Step 3: Whole disk, Drive A (M4 pass)**

```bash
.venv/bin/python a2mflux.py readdisk --port /dev/cu.usbmodem114401 --out ../docs/private/ref/m4_driveA.po --compare /Applications/ADTPro-v.r.m/disks/REF800K.po
```
Pass: `0 failed track sides`, `0 blocks differ`, exit code 0. The `readdisk:` line reports the total ms, the mean decode ns/interval and the max ring backlog (of 8192). Record all three, and how many track sides reported soft errors. Compare the time with M3's 2 min 12 s.

- [ ] **Step 4: Decode headroom**

Pass: mean decode under 2000 ns/interval (one interval per 2 us is the worst-case rate) AND max ring backlog under 4096 intervals (half the ring). Record both numbers and the margin. If the mean is above about 1500 ns or the backlog above about 2048, note it as a risk for M5/M7.

- [ ] **Step 5: Drive B**

`server off`, mechanism supply off, swap to Drive B (CN1 continuity checks as in M0), supply on, `server on`, repeat Step 3 with `--out ../docs/private/ref/m4_driveB.po`. Same pass criteria.

- [ ] **Step 6: Error paths**

With the server on and no disk: `readblk 0` -> `status af`. Console `readblk 1600` -> `ERR bad command`. `server off` while idle -> `OK`, motor off, LED off.

- [ ] **Step 7: Record**

`docs/build_log/m4.md`: commands and results for each step, total time, mean ns/interval, max ring backlog, soft-error counts, anything surprising. Then:
```bash
git add docs/build_log/m4.md docs/captures/m4
git commit -m "docs(m4): blocks read on the board, whole disk byte-identical"
```
