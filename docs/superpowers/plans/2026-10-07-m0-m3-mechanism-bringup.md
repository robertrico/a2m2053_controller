# M0-M3 Mechanism Bring-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drive the Sony 800K mechanism from a Pico 2 and read every sector of a ProDOS 800K disk into a byte-exact `.po` image on the dev computer.

**Architecture:** RP2350 bring-up firmware (`firmware/`) exposes a line-based USB console: mechanism status reads, mechanism commands, seek/recal/eject, and raw flux capture (PIO1 interval timer + DMA, dumped as binary with CRC). A Python tool (`tools/`) does all the GCR decoding offline: flux intervals -> bits -> nibbles -> address/data fields -> 524-byte sectors -> disk image. Mechanism sequencing logic is pure C behind a small bus interface, so it is unit-tested on the host with a fake bus. Firmware-side GCR decode (M4) comes in a later plan.

**Tech Stack:** C11, Raspberry Pi Pico SDK 2.2.0 (`~/Development/pico-sdk`), Arm GNU Toolchain 15.2 (`arm-none-eabi-gcc`, `arm-none-eabi-gdb`), CMake + Ninja, PIO + DMA on RP2350A (Pico 2). Load and debug over SWD: Raspberry Pi Debug Probe / picoprobe (CMSIS-DAP) + OpenOCD + GDB, the usual flow. Python 3.14, pytest, pyserial.

**Spec:** `docs/superpowers/specs/2026-10-07-a2m2053-controller-design.md` (sections 1, 2, 4.2 M0-M3). Pin map: `docs/pin_budget.md`. Signal directions and shifters: `docs/level_shifting.md`. Mechanism facts: `docs/research_notes.md` sections 7, 8.

## Global Constraints

- Scope: Apple IIe + 800K double-sided ProDOS disks. No Mac disks, no 400K disks.
- The working UniDisk is never opened or probed. Test beds: the two dead-board drives.
- **No disk writes in M0-M3.** `/WRTGATE` (GP18) is held high by firmware at all times; the only code reference allowed is the idle table in `hw.c`.
- GPIO map is exactly `docs/pin_budget.md` (CA0-2 = GP12-14, LSTRB = GP15, SEL = GP16, /ENBL = GP17, /WRTGATE = GP18, WRTDATA = GP19, mech RD = GP20, mech shifter /OE = GP21).
- 8T245 rule: one DIR and one /OE per 8-channel board; all channels on a board share them.
- The original drive ROM (`~/Development/project_byte_hamr/gateware/rev2/smart_hamr/liron.asm`) is the oracle for sequences and timing. Do not copy its structure.
- Proven sources: `~/Development/project_byte_hamr` only. Never use `~/Development/ironsteel-fpga-disk-controller`.
- No hardware action (power, flash, connect, plug/unplug) without the operator saying go. Bench tasks are run by the operator; the agent prepares, reads results, and records.
- Mechanism connector CN104 pin 20 (/PWM) is tied to +5V. Pin 9 is N/C.

## Execution order and milestone gates

Tasks are numbered by subsystem, but they run **one milestone at a time**.
Each phase ends at a hard stop: the operator runs the bench task, the results get
documented (build note + the operator's own photo/log routine), and the operator says go
before the next phase starts. An executor never starts the next phase on its
own.

| Phase | Tasks, in this order | Ends with |
|---|---|---|
| **M0** | Task 5 (firmware scaffold, safe pin init, flash targets) -> **Task 9 [BENCH]** | `docs/build_log/m0.md`, then **STOP** |
| M1 | Task 6 (mechanism logic) -> Task 7 (console) -> **Task 10 [BENCH]** | `docs/build_log/m1.md`, then **STOP** |
| M2 | **Task 11 [BENCH]** (firmware from M1 already covers it) | `docs/build_log/m2.md`, then **STOP** |
| M3 | Tasks 1 -> 2 -> 3 -> 4 (Python decoder) -> Task 8 (flux capture) -> **Task 12 [BENCH]** | `docs/build_log/m3.md`, then **STOP** |

M0 blocker to clear first: an OpenOCD with `target/rp2350.cfg` (see Task 9
step 3). The operator chooses and installs it.

## Review Focus

1. **Boot/reset glitches on mechanism lines.** Expected: the mechanism never steps, ejects or spins when the Pico resets, boots or is flashed. Pinned by: Task 6 test `test_no_addr_change_while_strobe_high` and `test_lstrb_low_after_every_op`; Task 9 bench step scoping GP21 and LSTRB through a reset.
2. **Accidental write gate.** Expected: `/WRTGATE` never goes low in this firmware. Pinned by: Task 5 `check_wrtgate.sh`, run by `make test`.
3. **No disk / no drive / motor never ready.** Expected: every console command returns `ERR ...` within its timeout; nothing hangs. Pinned by: Task 6 tests `test_step_timeout`, `test_recal_fail`, `test_motor_on_not_ready`; Task 4 test `test_parse_empty_capture`; Task 3 test `test_decode_empty`.
4. **Noisy flux, speed drift, wrong head/track.** Expected: decoder recovers all sectors under ±6 % jitter and 6 % drift, and never files a sector under the wrong track/side. Pinned by: Task 3 tests `test_track_decodes_with_jitter_and_drift` and `test_wrong_side_is_misplaced`.
5. **Truncated or corrupted USB dump.** Expected: CRC/length mismatch raises `CaptureError`; `disk` retries the track instead of writing garbage. Pinned by: Task 4 tests `test_parse_bad_crc`, `test_parse_truncated`.

---

## File Structure

```
.gitignore                          (modify: firmware test binaries)
tools/
  requirements.txt                  pytest, pyserial
  pytest.ini                        pythonpath = . ; testpaths = tests
  a2mflux.py                        CLI shim -> a2m.cli.main
  a2m/__init__.py
  a2m/gcr.py                        nibble table, geometry, mangle/unmangle, address/data fields
  a2m/synth.py                      synthetic track bitstreams (tests now, format/write later)
  a2m/flux.py                       intervals <-> bits, bits -> nibbles, histogram
  a2m/track.py                      find + decode sectors in a nibble stream
  a2m/proto.py                      parse the firmware's FLUX BEGIN/END capture block
  a2m/serialio.py                   pyserial console wrapper (hardware only, not unit-tested)
  a2m/cli.py                        hist / decode / probe / capture / disk
  tests/test_gcr.py
  tests/test_flux_track.py
  tests/test_proto_cli.py
firmware/
  CMakeLists.txt
  Makefile                          build, test, uf2 copy
  pico_sdk_import.cmake             copied from the SDK
  src/pins.h                        GPIO map
  src/hw.h, src/hw.c                safe init, OE/ENBL/LED, real mechanism bus
  src/mech_addr.h, src/mech_addr.c  status/command register numbers + ROM code mapping
  src/mech.h, src/mech.c            mechanism sequences over mech_bus_t (host-testable)
  src/crc32.h, src/crc32.c          CRC-32 (zlib-compatible)
  src/console.h, src/console.c      command-line parser (host-testable)
  src/flux_rx.pio                   PIO flux interval timer
  src/flux.h, src/flux.c            PIO + DMA capture
  src/main.c                        USB console loop + dispatch
  test/Makefile                     host unit tests
  test/test.h                       CHECK macro
  test/fake_bus.h                   fake mechanism bus
  test/test_mech.c, test/test_crc32.c, test/test_console.c
  test/check_wrtgate.sh
docs/build_log/m0.md .. m3.md       bench results (created in Tasks 9-12)
```

Register numbering used everywhere: a 4-bit **address** `(CA2<<3) | (CA1<<2) | (CA0<<1) | SEL`, matching the tashnotes MCI tables and Apple's command/status table.

---

### Task 1: Python env, GCR tables, geometry, mangle

**Files:**
- Create: `tools/requirements.txt`, `tools/pytest.ini`, `tools/a2m/__init__.py`, `tools/a2m/gcr.py`
- Test: `tools/tests/test_gcr.py`

**Interfaces:**
- Produces: `gcr.NIBBLES: bytes` (64), `gcr.DECODE: dict[int,int]`, `gcr.sectors_per_track(track) -> int`, `gcr.block_number(track, side, sector) -> int`, `gcr.mangle(data) -> (bytes, bytes)`, `gcr.unmangle(data) -> (bytes, bytes)`, constants `TAG_LEN=12, DATA_LEN=512, SECTOR_LEN=524, TRACKS=80, SIDES=2, TOTAL_BLOCKS=1600`.

- [ ] **Step 1: Create the Python environment**

```bash
cd /Users/hambook/Development/a2m2053_controller/tools
printf 'pytest>=8\npyserial>=3.5\n' > requirements.txt
printf '[pytest]\npythonpath = .\ntestpaths = tests\n' > pytest.ini
python3 -m venv .venv && .venv/bin/pip install -q -r requirements.txt
mkdir -p a2m tests && touch a2m/__init__.py
```
Expected: no errors. `.venv/` is already git-ignored.

- [ ] **Step 2: Write the failing tests**

`tools/tests/test_gcr.py`:
```python
import random
import pytest
from a2m import gcr


def test_nibble_table():
    assert len(gcr.NIBBLES) == 64
    assert len(set(gcr.NIBBLES)) == 64
    assert all(b >= 0x96 for b in gcr.NIBBLES)
    assert all(gcr.DECODE[b] == i for i, b in enumerate(gcr.NIBBLES))


def test_sectors_per_track_zones():
    assert gcr.sectors_per_track(0) == 12
    assert gcr.sectors_per_track(15) == 12
    assert gcr.sectors_per_track(16) == 11
    assert gcr.sectors_per_track(47) == 10
    assert gcr.sectors_per_track(79) == 8
    with pytest.raises(ValueError):
        gcr.sectors_per_track(80)
    with pytest.raises(ValueError):
        gcr.sectors_per_track(-1)


def test_block_numbers_cover_800k():
    total = sum(gcr.SIDES * gcr.sectors_per_track(t) for t in range(gcr.TRACKS))
    assert total == gcr.TOTAL_BLOCKS == 1600
    assert gcr.block_number(0, 0, 0) == 0
    assert gcr.block_number(0, 1, 0) == 12
    assert gcr.block_number(1, 0, 0) == 24
    assert gcr.block_number(79, 1, 7) == 1599
    with pytest.raises(ValueError):
        gcr.block_number(79, 0, 8)


# Test vector: tashnotes gcr_checksum.md ("TAGS" x3 + "DATA" x128).
VECTOR_IN = b"TAGS" * 3 + b"DATA" * 128


def test_mangle_matches_tashnotes_vector():
    mangled, csum = gcr.mangle(VECTOR_IN)
    assert len(mangled) == 524
    assert mangled[:16] == bytes.fromhex("541506DDF3D4D8BCBCA67663323425D4")
    assert mangled[0x200:] == bytes.fromhex("0A4B9BE3D252B09411697AE8")
    assert csum == bytes.fromhex("A9692E")


def test_unmangle_inverts_mangle():
    rng = random.Random(1)
    for data in (VECTOR_IN, bytes(rng.randrange(256) for _ in range(524)), bytes(524), b"\xff" * 524):
        mangled, csum = gcr.mangle(data)
        plain, csum2 = gcr.unmangle(mangled)
        assert plain == data
        assert csum2 == csum
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cd tools && .venv/bin/pytest tests/test_gcr.py -q`
Expected: FAIL with `ImportError` / `AttributeError` (module `a2m.gcr` missing).

- [ ] **Step 4: Write the implementation**

`tools/a2m/gcr.py`:
```python
"""Apple 3.5-inch GCR helpers for 800K (UniDisk 3.5) disks.

Sources: tashnotes gcr_checksum.md (mangle/unmangle + test vector),
Apple 800K ERS 699-0321 (speed zones), IWM docs (6-and-2 nibble table).
"""

NIBBLES = bytes([
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
    0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
    0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
    0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
    0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
])
DECODE = {b: i for i, b in enumerate(NIBBLES)}

TAG_LEN = 12
DATA_LEN = 512
SECTOR_LEN = TAG_LEN + DATA_LEN  # 524
TRACKS = 80
SIDES = 2
TOTAL_BLOCKS = 1600


def sectors_per_track(track: int) -> int:
    """12/11/10/9/8 sectors for tracks 0-15/16-31/32-47/48-63/64-79."""
    if not 0 <= track < TRACKS:
        raise ValueError(f"track {track} out of range")
    return 12 - track // 16


def block_number(track: int, side: int, sector: int) -> int:
    """ProDOS block for a (track, side, logical sector), .po order."""
    spt = sectors_per_track(track)
    if side not in (0, 1) or not 0 <= sector < spt:
        raise ValueError(f"bad side/sector {side}/{sector} on track {track}")
    base = sum(SIDES * sectors_per_track(t) for t in range(track))
    return base + side * spt + sector


def mangle(data: bytes) -> tuple[bytes, bytes]:
    """Encode sector bytes before writing; returns (mangled, checksum A,B,C)."""
    a = b = c = 0
    carry = 0
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        byte = data[i]
        i += 1
        carry, a = divmod(a + byte + carry, 256)
        out.append(byte ^ c)
        if i >= n:
            break
        byte = data[i]
        i += 1
        carry, b = divmod(b + byte + carry, 256)
        out.append(byte ^ a)
        if i >= n:
            break
        byte = data[i]
        i += 1
        carry, c = divmod(c + byte + carry, 256)
        out.append(byte ^ b)
        carry = 1 if c & 0x80 else 0
        c = ((c << 1) & 0xFF) | carry
    return bytes(out), bytes((a, b, c))


def unmangle(data: bytes) -> tuple[bytes, bytes]:
    """Inverse of mangle(); returns (plain, checksum A,B,C computed while decoding)."""
    a = b = c = 0
    out = bytearray()
    i, n = 0, len(data)
    while True:
        carry = 1 if c & 0x80 else 0
        c = ((c << 1) & 0xFF) | carry
        if i >= n:
            break
        byte = data[i] ^ c
        i += 1
        out.append(byte)
        carry, a = divmod(a + byte + carry, 256)
        if i >= n:
            break
        byte = data[i] ^ a
        i += 1
        out.append(byte)
        carry, b = divmod(b + byte + carry, 256)
        if i >= n:
            break
        byte = data[i] ^ b
        i += 1
        out.append(byte)
        carry, c = divmod(c + byte + carry, 256)
    return bytes(out), bytes((a, b, c))
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd tools && .venv/bin/pytest tests/test_gcr.py -q`
Expected: `5 passed`.

- [ ] **Step 6: Commit**

```bash
git add tools/requirements.txt tools/pytest.ini tools/a2m/__init__.py tools/a2m/gcr.py tools/tests/test_gcr.py
git commit -m "feat(tools): GCR nibble table, 800K geometry, mangle/unmangle"
```

---

### Task 2: GCR address and data fields

**Files:**
- Modify: `tools/a2m/gcr.py` (append)
- Test: `tools/tests/test_gcr.py` (append)

**Interfaces:**
- Consumes: Task 1 `NIBBLES`, `DECODE`, `mangle`, `unmangle`, `SECTOR_LEN`.
- Produces:
  - constants `ADDR_PROLOG=b"\xD5\xAA\x96"`, `DATA_PROLOG=b"\xD5\xAA\xAD"`, `EPILOG=b"\xDE\xAA"`, `DATA_NIBBLES=699`, `CHECKSUM_NIBBLES=4`, `FORMAT_UNIDISK=0x24`, `GROUP_ORDER=(0,1,2)`, `CHECKSUM_ORDER=(0,1,2)`
  - `Address` frozen dataclass `(track:int, sector:int, side:int, fmt:int, ok:bool)`
  - `encode_address(track, sector, side, fmt=FORMAT_UNIDISK) -> bytes` (prolog + 5 nibbles + epilog)
  - `decode_address(sixes: list[int]) -> Address` (takes the 5 decoded 6-bit values)
  - `encode_data(sector, payload, group_order=GROUP_ORDER, checksum_order=CHECKSUM_ORDER) -> bytes` (prolog + 1 + 699 + 4 nibbles + epilog = 709 bytes)
  - `decode_data(sixes: list[int], group_order=..., checksum_order=...) -> (sector:int, payload:bytes, ok:bool)` (takes 704 decoded 6-bit values that follow the data prolog)

- [ ] **Step 1: Write the failing tests** (append to `tools/tests/test_gcr.py`)

```python
def _sixes(encoded: bytes, prolog_len=3, epilog_len=2):
    return [gcr.DECODE[b] for b in encoded[prolog_len:len(encoded) - epilog_len]]


@pytest.mark.parametrize("track,sector,side", [(0, 0, 0), (17, 10, 1), (70, 7, 1), (79, 7, 0)])
def test_address_round_trip(track, sector, side):
    enc = gcr.encode_address(track, sector, side)
    assert enc[:3] == gcr.ADDR_PROLOG and enc[-2:] == gcr.EPILOG and len(enc) == 10
    addr = gcr.decode_address(_sixes(enc))
    assert (addr.track, addr.sector, addr.side, addr.ok) == (track, sector, side, True)


def test_address_bad_checksum():
    sixes = _sixes(gcr.encode_address(5, 3, 0))
    sixes[4] ^= 1
    assert gcr.decode_address(sixes).ok is False


def test_data_round_trip_and_length():
    rng = random.Random(7)
    payload = bytes(rng.randrange(256) for _ in range(gcr.SECTOR_LEN))
    enc = gcr.encode_data(9, payload)
    assert len(enc) == 3 + 1 + gcr.DATA_NIBBLES + gcr.CHECKSUM_NIBBLES + 2 == 709
    sector, out, ok = gcr.decode_data(_sixes(enc))
    assert (sector, out, ok) == (9, payload, True)


def test_data_corruption_detected():
    payload = bytes(range(256)) * 2 + bytes(12)
    sixes = _sixes(gcr.encode_data(1, payload))
    sixes[100] ^= 0x01
    assert gcr.decode_data(sixes)[2] is False


def test_orders_round_trip_and_mismatch_fails():
    payload = bytes((i * 7) & 0xFF for i in range(gcr.SECTOR_LEN))
    enc = gcr.encode_data(4, payload, group_order=(2, 0, 1), checksum_order=(1, 2, 0))
    assert gcr.decode_data(_sixes(enc), (2, 0, 1), (1, 2, 0))[2] is True
    assert gcr.decode_data(_sixes(enc))[2] is False
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd tools && .venv/bin/pytest tests/test_gcr.py -q`
Expected: FAIL with `AttributeError: module 'a2m.gcr' has no attribute 'encode_address'`.

- [ ] **Step 3: Write the implementation** (append to `tools/a2m/gcr.py`)

```python
from dataclasses import dataclass

ADDR_PROLOG = b"\xD5\xAA\x96"
DATA_PROLOG = b"\xD5\xAA\xAD"
EPILOG = b"\xDE\xAA"
DATA_NIBBLES = 699        # 174 groups of 4 + 1 group of 3
CHECKSUM_NIBBLES = 4

# Address-field format byte: 0x20 = double-sided, low bits = interleave.
# The decoder records it but never checks it. UniDisk value is a [guess].
FORMAT_UNIDISK = 0x24

# On-disk order of the 3 mangled bytes inside each 4-nibble group, and of the
# 3 checksum bytes. Not yet confirmed on real media: Task 12 runs
# `a2mflux.py probe` on a real capture and changes these only if it says so.
GROUP_ORDER = (0, 1, 2)
CHECKSUM_ORDER = (0, 1, 2)


@dataclass(frozen=True)
class Address:
    track: int
    sector: int
    side: int
    fmt: int
    ok: bool


def encode_address(track: int, sector: int, side: int, fmt: int = FORMAT_UNIDISK) -> bytes:
    t_lo = track & 0x3F
    side_b = (side << 5) | (track >> 6)
    vals = [t_lo, sector & 0x3F, side_b, fmt & 0x3F]
    chk = vals[0] ^ vals[1] ^ vals[2] ^ vals[3]
    vals.append(chk)
    return ADDR_PROLOG + bytes(NIBBLES[v] for v in vals) + EPILOG


def decode_address(sixes: list[int]) -> Address:
    t_lo, sector, side_b, fmt, chk = sixes[:5]
    ok = (t_lo ^ sector ^ side_b ^ fmt) == chk
    return Address(track=t_lo | ((side_b & 1) << 6), sector=sector,
                   side=(side_b >> 5) & 1, fmt=fmt, ok=ok)


def _encode_groups(raw: bytes, order) -> list[int]:
    sixes = []
    for i in range(0, len(raw), 3):
        chunk = raw[i:i + 3]
        vals = [chunk[j] for j in order if j < len(chunk)]
        hi = 0
        for k, v in enumerate(vals):
            hi |= (v >> 6) << (4 - 2 * k)
        sixes.append(hi)
        sixes.extend(v & 0x3F for v in vals)
    return sixes


def _decode_groups(sixes: list[int], nbytes: int, order) -> bytes:
    out = bytearray()
    i = 0
    while len(out) < nbytes:
        k = min(3, nbytes - len(out))
        hi = sixes[i]
        vals = [(((hi >> (4 - 2 * m)) & 3) << 6) | sixes[i + 1 + m] for m in range(k)]
        i += 1 + k
        present = [j for j in order if j < k]
        chunk = [0] * k
        for pos, j in enumerate(present):
            chunk[j] = vals[pos]
        out.extend(chunk)
    return bytes(out)


def encode_data(sector: int, payload: bytes, group_order=GROUP_ORDER,
                checksum_order=CHECKSUM_ORDER) -> bytes:
    if len(payload) != SECTOR_LEN:
        raise ValueError("payload must be 524 bytes (12 tag + 512 data)")
    mangled, csum = mangle(payload)
    sixes = [sector & 0x3F] + _encode_groups(mangled, group_order) + _encode_groups(csum, checksum_order)
    return DATA_PROLOG + bytes(NIBBLES[s] for s in sixes) + EPILOG


def decode_data(sixes: list[int], group_order=GROUP_ORDER,
                checksum_order=CHECKSUM_ORDER) -> tuple[int, bytes, bool]:
    need = 1 + DATA_NIBBLES + CHECKSUM_NIBBLES
    if len(sixes) < need:
        raise ValueError(f"need {need} values, got {len(sixes)}")
    sector = sixes[0]
    mangled = _decode_groups(sixes[1:1 + DATA_NIBBLES], SECTOR_LEN, group_order)
    disk_csum = _decode_groups(sixes[1 + DATA_NIBBLES:need], 3, checksum_order)
    payload, calc = unmangle(mangled)
    return sector, payload, calc == disk_csum
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd tools && .venv/bin/pytest tests/test_gcr.py -q`
Expected: `13 passed`.

- [ ] **Step 5: Commit**

```bash
git add tools/a2m/gcr.py tools/tests/test_gcr.py
git commit -m "feat(tools): GCR address and data field encode/decode"
```

---

### Task 3: Synthetic tracks, flux to nibbles, track decoder

**Files:**
- Create: `tools/a2m/synth.py`, `tools/a2m/flux.py`, `tools/a2m/track.py`
- Test: `tools/tests/test_flux_track.py`

**Interfaces:**
- Consumes: Task 2 `encode_address`, `encode_data`, `decode_address`, `decode_data`, prologs, `DECODE`, `GROUP_ORDER`, `CHECKSUM_ORDER`, `DATA_NIBBLES`, `CHECKSUM_NIBBLES`.
- Produces:
  - `synth.track_bits(track, side, payloads: dict[int, bytes], interleave=4, gap_syncs=6, group_order=..., checksum_order=...) -> list[int]`, `synth.bytes_bits(data) -> list[int]`, `synth.SYNC`
  - `flux.TICK_NS = 40.0`, `flux.PIO_EDGE_OVERHEAD_TICKS = 1.5`, `flux.NOMINAL_CELL_NS = 1e9 / 489_600`
  - `flux.encode_flux(bits, cell_ticks=..., jitter=0.0, drift=0.0, seed=0) -> list[int]` (raw PIO counts)
  - `flux.intervals_to_bits(intervals, tick_ns=TICK_NS, cell_ns=NOMINAL_CELL_NS, alpha=0.05) -> list[int]`
  - `flux.bits_to_nibbles(bits) -> bytes`
  - `flux.histogram_lines(intervals, tick_ns=TICK_NS, bin_ns=100) -> list[str]`
  - `track.Sector` dataclass `(track, side, sector, fmt, payload: bytes)` (payload = 524 bytes)
  - `track.TrackResult` dataclass `(sectors: dict[int, Sector], bad_headers: int, bad_data: int, misplaced: list[tuple[int,int,int]])`
  - `track.decode_track(nibbles: bytes, track, side, group_order=..., checksum_order=...) -> TrackResult`

- [ ] **Step 1: Write the failing tests**

`tools/tests/test_flux_track.py`:
```python
import random
from a2m import gcr, synth, flux, track


def payloads_for(trk, side, seed=0):
    rng = random.Random(seed * 1000 + trk * 2 + side)
    return {s: bytes(rng.randrange(256) for _ in range(gcr.SECTOR_LEN))
            for s in range(gcr.sectors_per_track(trk))}


def test_bits_to_nibbles_self_syncs():
    bits = [0, 0, 1, 0] + synth.SYNC * 5 + synth.bytes_bits(gcr.ADDR_PROLOG)
    nibs = flux.bits_to_nibbles(bits)
    assert gcr.ADDR_PROLOG in nibs


def test_exact_flux_round_trip():
    bits = synth.SYNC * 4 + synth.bytes_bits(bytes([0xD5, 0xAA, 0x96, 0xFF, 0xB5]))
    ivs = flux.encode_flux(bits)
    out = flux.intervals_to_bits(ivs)
    # leading zeros before the first 1 are not represented by any interval
    first_one = bits.index(1)
    assert out == bits[first_one:]


def _decode(bits):
    return flux.bits_to_nibbles(flux.intervals_to_bits(flux.encode_flux(bits)))


def test_clean_track_decodes_all_sectors():
    p = payloads_for(0, 0)
    res = track.decode_track(_decode(synth.track_bits(0, 0, p)), 0, 0)
    assert sorted(res.sectors) == list(range(12))
    assert all(res.sectors[s].payload == p[s] for s in p)
    assert res.misplaced == []


def test_track_decodes_with_jitter_and_drift():
    p = payloads_for(70, 1)
    ivs = flux.encode_flux(synth.track_bits(70, 1, p), jitter=0.06, drift=0.06, seed=3)
    nibs = flux.bits_to_nibbles(flux.intervals_to_bits(ivs))
    res = track.decode_track(nibs, 70, 1)
    assert sorted(res.sectors) == list(range(8))
    assert all(res.sectors[s].payload == p[s] for s in p)


def test_wrong_side_is_misplaced():
    p = payloads_for(3, 1)
    res = track.decode_track(_decode(synth.track_bits(3, 1, p)), 3, 0)
    assert res.sectors == {}
    assert sorted(s for (_, _, s) in res.misplaced) == list(range(12))
    assert all((t, sd) == (3, 1) for (t, sd, _) in res.misplaced)


def test_corrupt_sector_is_dropped_others_kept():
    p = payloads_for(20, 0)
    nibs = bytearray(_decode(synth.track_bits(20, 0, p)))
    first_data = nibs.find(gcr.DATA_PROLOG)
    nibs[first_data + 50] = gcr.NIBBLES[(gcr.DECODE[nibs[first_data + 50]] + 1) % 64]
    res = track.decode_track(bytes(nibs), 20, 0)
    assert len(res.sectors) == 10
    assert res.bad_data >= 1


def test_decode_empty():
    res = track.decode_track(b"", 0, 0)
    assert res.sectors == {} and res.bad_headers == 0 and res.bad_data == 0


def test_histogram_has_three_peaks():
    p = payloads_for(0, 0)
    lines = flux.histogram_lines(flux.encode_flux(synth.track_bits(0, 0, p)))
    text = "\n".join(lines)
    assert " 2.0 us" in text and " 4.0 us" in text and " 6.1 us" in text
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd tools && .venv/bin/pytest tests/test_flux_track.py -q`
Expected: FAIL with `ImportError: cannot import name 'synth'`.

- [ ] **Step 3: Write `tools/a2m/synth.py`**

```python
"""Synthetic 800K track bitstreams for tests (and later, format/write)."""
from . import gcr

SYNC = [1, 1, 1, 1, 1, 1, 1, 1, 0, 0]   # self-sync 0xFF, 10 bit cells


def bytes_bits(data: bytes) -> list[int]:
    out = []
    for b in data:
        out.extend((b >> (7 - k)) & 1 for k in range(8))
    return out


def physical_order(spt: int, interleave: int) -> list[int]:
    slots = [None] * spt
    pos = 0
    for s in range(spt):
        while slots[pos] is not None:
            pos = (pos + 1) % spt
        slots[pos] = s
        pos = (pos + interleave) % spt
    return slots


def track_bits(track: int, side: int, payloads: dict[int, bytes], interleave: int = 4,
               gap_syncs: int = 6, group_order=gcr.GROUP_ORDER,
               checksum_order=gcr.CHECKSUM_ORDER) -> list[int]:
    bits = []
    for s in physical_order(gcr.sectors_per_track(track), interleave):
        bits += SYNC * gap_syncs + bytes_bits(gcr.encode_address(track, s, side))
        bits += SYNC * gap_syncs + bytes_bits(
            gcr.encode_data(s, payloads[s], group_order, checksum_order))
    bits += SYNC * gap_syncs
    return bits
```

- [ ] **Step 4: Write `tools/a2m/flux.py`**

```python
"""Flux intervals <-> bit cells <-> GCR nibbles.

Firmware PIO counts in 40 ns ticks (Task 8: 150 MHz / clkdiv 3 / 2 cycles
per count). Each interval loses 3 PIO cycles (1.5 ticks) at the edge, so we
add them back. 800K data rate is 489.6 kbit/s (ERS 699-0321): 2.0425 us cells.
"""
import random

TICK_NS = 40.0
PIO_EDGE_OVERHEAD_TICKS = 1.5
NOMINAL_CELL_NS = 1e9 / 489_600


def encode_flux(bits, cell_ticks: float = NOMINAL_CELL_NS / TICK_NS, jitter: float = 0.0,
                drift: float = 0.0, seed: int = 0) -> list[int]:
    """Bit cells -> raw PIO counts between transitions ('1' = transition)."""
    rng = random.Random(seed)
    out = []
    run = 0
    total = max(1, len(bits))
    for i, b in enumerate(bits):
        run += 1
        if b:
            scale = 1.0 + drift * (i / total)
            t = run * cell_ticks * scale * (1.0 + rng.uniform(-jitter, jitter))
            out.append(max(1, int(round(t - PIO_EDGE_OVERHEAD_TICKS))))
            run = 0
    return out


def intervals_to_bits(intervals, tick_ns: float = TICK_NS, cell_ns: float = NOMINAL_CELL_NS,
                      alpha: float = 0.05) -> list[int]:
    """Raw PIO counts -> bit cells, with a cell estimate that follows speed drift."""
    cell = cell_ns / tick_ns
    bits = []
    for raw in intervals:
        t = raw + PIO_EDGE_OVERHEAD_TICKS
        n = max(1, min(int(t / cell + 0.5), 8))
        bits.extend([0] * (n - 1))
        bits.append(1)
        if n <= 3:
            cell += alpha * (t / n - cell)
    return bits


def bits_to_nibbles(bits) -> bytes:
    """IWM-style latch: skip zeros until a 1, then take 8 bits as one byte."""
    out = bytearray()
    reg = 0
    count = 0
    for b in bits:
        if count == 0 and b == 0:
            continue
        reg = ((reg << 1) | b) & 0xFF
        count += 1
        if count == 8:
            out.append(reg)
            reg = 0
            count = 0
    return bytes(out)


def histogram_lines(intervals, tick_ns: float = TICK_NS, bin_ns: int = 100) -> list[str]:
    counts: dict[int, int] = {}
    for v in intervals:
        k = int((v + PIO_EDGE_OVERHEAD_TICKS) * tick_ns // bin_ns)
        counts[k] = counts.get(k, 0) + 1
    if not counts:
        return ["(no intervals)"]
    peak = max(counts.values())
    lines = []
    for k in sorted(counts):
        if counts[k] * 200 < peak:
            continue
        bar = "#" * max(1, counts[k] * 60 // peak)
        lines.append(f"{k * bin_ns / 1000:5.1f} us {counts[k]:7d} {bar}")
    return lines
```

- [ ] **Step 5: Write `tools/a2m/track.py`**

```python
"""Find and decode 800K GCR sectors in a nibble stream."""
from dataclasses import dataclass, field
from . import gcr


@dataclass
class Sector:
    track: int
    side: int
    sector: int
    fmt: int
    payload: bytes          # 524 bytes: 12 tag + 512 data


@dataclass
class TrackResult:
    sectors: dict = field(default_factory=dict)       # sector number -> Sector (good only)
    bad_headers: int = 0
    bad_data: int = 0
    misplaced: list = field(default_factory=list)      # (track, side, sector) seen but not expected


def _sixes(nibbles: bytes, start: int, count: int):
    if start + count > len(nibbles):
        return None
    out = []
    for b in nibbles[start:start + count]:
        v = gcr.DECODE.get(b)
        if v is None:
            return None
        out.append(v)
    return out


def decode_track(nibbles: bytes, track: int, side: int, group_order=gcr.GROUP_ORDER,
                 checksum_order=gcr.CHECKSUM_ORDER) -> TrackResult:
    res = TrackResult()
    need = 1 + gcr.DATA_NIBBLES + gcr.CHECKSUM_NIBBLES
    i = 0
    while True:
        i = nibbles.find(gcr.ADDR_PROLOG, i)
        if i < 0:
            return res
        i += len(gcr.ADDR_PROLOG)
        sx = _sixes(nibbles, i, 5)
        if sx is None:
            res.bad_headers += 1
            continue
        addr = gcr.decode_address(sx)
        if not addr.ok:
            res.bad_headers += 1
            continue
        if (addr.track, addr.side) != (track, side):
            res.misplaced.append((addr.track, addr.side, addr.sector))
            continue
        d = nibbles.find(gcr.DATA_PROLOG, i + 5, i + 5 + 64)
        if d < 0:
            res.bad_data += 1
            continue
        d += len(gcr.DATA_PROLOG)
        sx = _sixes(nibbles, d, need)
        if sx is None:
            res.bad_data += 1
            continue
        sec, payload, ok = gcr.decode_data(sx, group_order, checksum_order)
        if not ok or sec != addr.sector:
            res.bad_data += 1
            continue
        res.sectors.setdefault(addr.sector, Sector(track, side, addr.sector, addr.fmt, payload))
        i = d + need
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cd tools && .venv/bin/pytest -q`
Expected: all pass (`21 passed`). Peak bins: 1, 2 and 3 cells of 2.0425 us fall in the 2.0, 4.0 and 6.1 us bins (100 ns bins).

- [ ] **Step 7: Commit**

```bash
git add tools/a2m/synth.py tools/a2m/flux.py tools/a2m/track.py tools/tests/test_flux_track.py
git commit -m "feat(tools): flux-to-nibble decoder and 800K track parser"
```

---

### Task 4: Capture protocol, CLI, serial console

**Files:**
- Create: `tools/a2m/proto.py`, `tools/a2m/serialio.py`, `tools/a2m/cli.py`, `tools/a2mflux.py`
- Test: `tools/tests/test_proto_cli.py`

**Interfaces:**
- Consumes: Task 3 `flux.*`, `track.decode_track`, `track.TrackResult`; Task 1-2 `gcr.*`.
- Produces:
  - `proto.CaptureError(Exception)`, `proto.parse_capture(stream: bytes) -> (list[int], int tick_ns)`, `proto.END_RE`
  - `serialio.Console(port)` with `.cmd(line, timeout=5.0) -> list[str]`, `.capture(track, side, count, timeout=10.0) -> bytes`, `serialio.ConsoleError`
  - `cli.decode_capture(stream: bytes, track, side, group_order=..., checksum_order=...) -> (TrackResult, bytes nibbles)`
  - `cli.probe(nibbles, track, side) -> list[(good:int, group_order, checksum_order)]` best first
  - `cli.assemble_image(results: dict[(track, side)] -> TrackResult) -> (bytes image, int good, list missing)`
  - `cli.main(argv=None) -> int`
- Firmware wire format this parser expects (Task 8 produces it):
  `FLUX BEGIN <count> <tick_ns>\r\n` + `count` little-endian uint16 + `\r\nFLUX END <crc32 as 8 lowercase hex>\r\n` + `OK\r\n`. CRC-32 is zlib's, over the binary bytes only.

- [ ] **Step 1: Write the failing tests**

`tools/tests/test_proto_cli.py`:
```python
import struct
import zlib
import pytest
from a2m import gcr, synth, flux, proto, cli
from a2m.track import TrackResult, Sector


def make_stream(values, tick=40, crc=None, cut=0):
    payload = b"".join(struct.pack("<H", v) for v in values)
    c = zlib.crc32(payload) if crc is None else crc
    s = b"junk\r\nFLUX BEGIN %d %d\r\n" % (len(values), tick) + payload + b"\r\nFLUX END %08x\r\nOK\r\n" % c
    return s[:len(s) - cut] if cut else s


def test_parse_ok():
    vals, tick = proto.parse_capture(make_stream([51, 102, 65535]))
    assert vals == [51, 102, 65535] and tick == 40


def test_parse_empty_capture():
    assert proto.parse_capture(make_stream([])) == ([], 40)


def test_parse_bad_crc():
    with pytest.raises(proto.CaptureError):
        proto.parse_capture(make_stream([1, 2, 3], crc=0x12345678))


def test_parse_truncated():
    with pytest.raises(proto.CaptureError):
        proto.parse_capture(make_stream(list(range(100)), cut=150))


def test_parse_missing_begin():
    with pytest.raises(proto.CaptureError):
        proto.parse_capture(b"ERR motor off\r\n")


def full_track(trk, side):
    return TrackResult(sectors={s: Sector(trk, side, s, 0x24, bytes([s]) * 12 + bytes([trk]) * 512)
                                for s in range(gcr.sectors_per_track(trk))})


def test_assemble_full_disk():
    results = {(t, s): full_track(t, s) for t in range(80) for s in range(2)}
    img, good, missing = cli.assemble_image(results)
    assert len(img) == 1600 * 512 and good == 1600 and missing == []
    blk = gcr.block_number(33, 1, 4)
    assert img[blk * 512:(blk + 1) * 512] == bytes([33]) * 512


def test_assemble_reports_missing():
    results = {(t, s): full_track(t, s) for t in range(80) for s in range(2)}
    del results[(5, 0)].sectors[3]
    img, good, missing = cli.assemble_image(results)
    assert good == 1599 and missing == [(5, 0, 3)]
    blk = gcr.block_number(5, 0, 3)
    assert img[blk * 512:(blk + 1) * 512] == bytes(512)


def test_probe_finds_orders():
    payloads = {s: bytes((s + i) & 0xFF for i in range(524)) for s in range(12)}
    bits = synth.track_bits(0, 0, payloads, group_order=(2, 0, 1), checksum_order=(1, 2, 0))
    nibs = flux.bits_to_nibbles(flux.intervals_to_bits(flux.encode_flux(bits)))
    best = cli.probe(nibs, 0, 0)[0]
    assert best == (12, (2, 0, 1), (1, 2, 0))


def test_decode_capture_end_to_end():
    payloads = {s: bytes((s * 3 + i) & 0xFF for i in range(524)) for s in range(12)}
    ivs = flux.encode_flux(synth.track_bits(0, 0, payloads))
    res, _ = cli.decode_capture(make_stream([min(v, 65535) for v in ivs]), 0, 0)
    assert len(res.sectors) == 12
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd tools && .venv/bin/pytest tests/test_proto_cli.py -q`
Expected: FAIL with `ImportError: cannot import name 'proto'`.

- [ ] **Step 3: Write `tools/a2m/proto.py`**

```python
"""Parse the firmware's flux capture block (see firmware/src/main.c, capture)."""
import re
import zlib

BEGIN_RE = re.compile(rb"FLUX BEGIN (\d+) (\d+)\r?\n")
END_RE = re.compile(rb"\r?\nFLUX END ([0-9a-f]{8})\r?\n")


class CaptureError(Exception):
    pass


def parse_capture(stream: bytes) -> tuple[list[int], int]:
    m = BEGIN_RE.search(stream)
    if not m:
        raise CaptureError("no FLUX BEGIN in stream")
    count, tick_ns = int(m[1]), int(m[2])
    start = m.end()
    end = start + 2 * count
    if len(stream) < end:
        raise CaptureError(f"truncated: want {2 * count} bytes, have {len(stream) - start}")
    payload = stream[start:end]
    e = END_RE.match(stream, end)
    if not e:
        raise CaptureError("no FLUX END right after payload")
    if int(e[1], 16) != zlib.crc32(payload):
        raise CaptureError("crc mismatch")
    return [int.from_bytes(payload[k:k + 2], "little") for k in range(0, len(payload), 2)], tick_ns
```

- [ ] **Step 4: Write `tools/a2m/serialio.py`** (hardware-only, no unit test)

```python
"""USB CDC console to the bring-up firmware. Every command ends in OK or ERR."""
import re
import time
import serial

from .proto import END_RE


class ConsoleError(Exception):
    pass


class Console:
    def __init__(self, port: str, baud: int = 115200):
        self.ser = serial.Serial(port, baud, timeout=0.2)

    def cmd(self, line: str, timeout: float = 5.0) -> list[str]:
        self.ser.reset_input_buffer()
        self.ser.write((line + "\n").encode())
        deadline = time.monotonic() + timeout
        lines, buf = [], b""
        while time.monotonic() < deadline:
            buf += self.ser.read(4096)
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                s = raw.decode(errors="replace").strip()
                if s == "OK":
                    return lines
                if s.startswith("ERR"):
                    raise ConsoleError(f"{line!r}: {s}")
                if s:
                    lines.append(s)
        raise ConsoleError(f"timeout on {line!r}")

    def capture(self, track: int, side: int, count: int, timeout: float = 10.0) -> bytes:
        self.ser.reset_input_buffer()
        self.ser.write(f"capture {track} {side} {count}\n".encode())
        deadline = time.monotonic() + timeout
        buf = b""
        while time.monotonic() < deadline:
            buf += self.ser.read(65536)
            if b"FLUX BEGIN" not in buf:
                for raw in buf.split(b"\n")[:-1]:
                    if raw.strip().startswith(b"ERR"):
                        raise ConsoleError(raw.decode(errors="replace").strip())
            elif END_RE.search(buf):
                tail_deadline = time.monotonic() + 1.0
                while not re.search(rb"\nOK\r?\n", buf[-16:]) and time.monotonic() < tail_deadline:
                    buf += self.ser.read(64)
                return buf
        raise ConsoleError(f"capture {track} {side} timed out")
```

- [ ] **Step 5: Write `tools/a2m/cli.py` and the shim**

`tools/a2m/cli.py`:
```python
"""a2mflux: capture and decode UniDisk 800K flux from the bring-up firmware."""
import argparse
import itertools
import sys
from pathlib import Path

from . import gcr
from .flux import intervals_to_bits, bits_to_nibbles, histogram_lines
from .proto import parse_capture, CaptureError
from .track import decode_track, TrackResult


def decode_capture(stream: bytes, track: int, side: int, group_order=gcr.GROUP_ORDER,
                   checksum_order=gcr.CHECKSUM_ORDER):
    ivs, tick = parse_capture(stream)
    nibs = bits_to_nibbles(intervals_to_bits(ivs[1:], tick_ns=tick))  # first interval is partial
    return decode_track(nibs, track, side, group_order, checksum_order), nibs


def probe(nibbles: bytes, track: int, side: int):
    out = []
    for go in itertools.permutations((0, 1, 2)):
        for co in itertools.permutations((0, 1, 2)):
            out.append((len(decode_track(nibbles, track, side, go, co).sectors), go, co))
    out.sort(key=lambda x: -x[0])
    return out


def assemble_image(results):
    img = bytearray(gcr.TOTAL_BLOCKS * gcr.DATA_LEN)
    good, missing = 0, []
    for t in range(gcr.TRACKS):
        for s in range(gcr.SIDES):
            r = results.get((t, s))
            for sec in range(gcr.sectors_per_track(t)):
                if r is not None and sec in r.sectors:
                    blk = gcr.block_number(t, s, sec)
                    img[blk * 512:(blk + 1) * 512] = r.sectors[sec].payload[gcr.TAG_LEN:]
                    good += 1
                else:
                    missing.append((t, s, sec))
    return bytes(img), good, missing


def _print_track(res: TrackResult, track: int, side: int):
    want = gcr.sectors_per_track(track)
    print(f"track {track} side {side}: {len(res.sectors)}/{want} sectors good, "
          f"bad headers {res.bad_headers}, bad data {res.bad_data}, misplaced {len(res.misplaced)}")
    print("  good:", sorted(res.sectors))
    if res.misplaced:
        print("  misplaced (track, side, sector):", sorted(set(res.misplaced))[:12])


def _disk(args) -> int:
    from .serialio import Console, ConsoleError
    con = Console(args.port)
    for line in ("oe on", "enbl on", "motor on", "recal"):
        con.cmd(line, timeout=10.0)
    results = {}
    try:
        for t in range(gcr.TRACKS):
            for s in range(gcr.SIDES):
                merged = TrackResult()
                for _ in range(args.retries):
                    try:
                        res, _ = decode_capture(con.capture(t, s, args.count), t, s)
                    except (CaptureError, ConsoleError) as e:
                        print(f"  T{t:02d} S{s} retry: {e}")
                        continue
                    for k, v in res.sectors.items():
                        merged.sectors.setdefault(k, v)
                    if len(merged.sectors) == gcr.sectors_per_track(t):
                        break
                results[(t, s)] = merged
                print(f"T{t:02d} S{s} {len(merged.sectors)}/{gcr.sectors_per_track(t)}", flush=True)
    finally:
        con.cmd("motor off", timeout=5.0)
    img, good, missing = assemble_image(results)
    Path(args.out).write_bytes(img)
    print(f"{good}/{gcr.TOTAL_BLOCKS} blocks good -> {args.out}")
    if missing:
        print("missing (track, side, sector):", missing[:40])
    ok = good == gcr.TOTAL_BLOCKS
    if args.compare:
        ref = Path(args.compare).read_bytes()
        diff = [b for b in range(gcr.TOTAL_BLOCKS) if img[b * 512:(b + 1) * 512] != ref[b * 512:(b + 1) * 512]]
        print(f"compare vs {args.compare}: {len(diff)} blocks differ", diff[:40])
        ok = ok and not diff
    return 0 if ok else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="a2mflux")
    sub = ap.add_subparsers(dest="cmd", required=True)
    h = sub.add_parser("hist", help="interval histogram of a saved capture")
    h.add_argument("file")
    d = sub.add_parser("decode", help="decode a saved capture")
    d.add_argument("file"); d.add_argument("--track", type=int, required=True); d.add_argument("--side", type=int, required=True)
    p = sub.add_parser("probe", help="try all group/checksum byte orders on a saved capture")
    p.add_argument("file"); p.add_argument("--track", type=int, required=True); p.add_argument("--side", type=int, required=True)
    c = sub.add_parser("capture", help="capture one track side to a file")
    c.add_argument("--port", required=True); c.add_argument("--track", type=int, required=True)
    c.add_argument("--side", type=int, required=True); c.add_argument("--count", type=int, default=100000)
    c.add_argument("--out", required=True)
    k = sub.add_parser("disk", help="read the whole disk into a .po image")
    k.add_argument("--port", required=True); k.add_argument("--out", required=True)
    k.add_argument("--compare"); k.add_argument("--count", type=int, default=100000)
    k.add_argument("--retries", type=int, default=3)
    args = ap.parse_args(argv)

    if args.cmd == "hist":
        ivs, tick = parse_capture(Path(args.file).read_bytes())
        print("\n".join(histogram_lines(ivs[1:], tick_ns=tick)))
        return 0
    if args.cmd == "decode":
        res, _ = decode_capture(Path(args.file).read_bytes(), args.track, args.side)
        _print_track(res, args.track, args.side)
        return 0 if len(res.sectors) == gcr.sectors_per_track(args.track) else 1
    if args.cmd == "probe":
        _, nibs = decode_capture(Path(args.file).read_bytes(), args.track, args.side)
        for good, go, co in probe(nibs, args.track, args.side)[:6]:
            print(f"{good:2d} good  GROUP_ORDER={go}  CHECKSUM_ORDER={co}")
        return 0
    if args.cmd == "capture":
        from .serialio import Console
        con = Console(args.port)
        raw = con.capture(args.track, args.side, args.count)
        Path(args.out).write_bytes(raw)
        ivs, _ = parse_capture(raw)
        print(f"{len(ivs)} intervals -> {args.out}")
        return 0
    return _disk(args)


if __name__ == "__main__":
    sys.exit(main())
```

`tools/a2mflux.py`:
```python
#!/usr/bin/env python3
import sys
from a2m.cli import main

sys.exit(main())
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cd tools && .venv/bin/pytest -q`
Expected: all pass (`30 passed`).

- [ ] **Step 7: Commit**

```bash
git add tools/a2m/proto.py tools/a2m/serialio.py tools/a2m/cli.py tools/a2mflux.py tools/tests/test_proto_cli.py
git commit -m "feat(tools): capture protocol parser and a2mflux CLI"
```

---

### Task 5: Firmware scaffold, safe pin init, host test harness

**Files:**
- Create: `firmware/CMakeLists.txt`, `firmware/Makefile`, `firmware/pico_sdk_import.cmake` (copy), `firmware/src/pins.h`, `firmware/src/hw.h`, `firmware/src/hw.c`, `firmware/src/crc32.h`, `firmware/src/crc32.c`, `firmware/src/main.c`, `firmware/test/Makefile`, `firmware/test/test.h`, `firmware/test/test_crc32.c`, `firmware/test/check_wrtgate.sh`
- Modify: `.gitignore`

**Interfaces:**
- Produces: `hw_init_safe(void)`, `hw_mech_outputs_enable(bool on)`, `hw_mech_enbl(bool on)`, `hw_led(bool on)`, `hw_mech_rd(void) -> bool`; `crc32_update(uint32_t crc, const uint8_t *p, size_t n) -> uint32_t` (zlib-compatible; start with 0); all `PIN_*` macros; host test macro `CHECK(cond)` and `TEST_END()`.

- [ ] **Step 1: Write the failing host test and the write-gate check**

`firmware/test/test.h`:
```c
#pragma once
#include <stdio.h>
static int t_run, t_fail;
#define CHECK(cond) do { t_run++; if (!(cond)) { t_fail++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define TEST_END() do { printf("%s: %d checks, %d failed\n", __FILE__, t_run, t_fail); \
    return t_fail ? 1 : 0; } while (0)
```

`firmware/test/test_crc32.c`:
```c
#include <string.h>
#include "test.h"
#include "crc32.h"

int main(void) {
    const char *s = "123456789";
    CHECK(crc32_update(0, (const uint8_t *)s, 9) == 0xCBF43926u);
    CHECK(crc32_update(0, (const uint8_t *)s, 0) == 0);
    uint32_t c = crc32_update(0, (const uint8_t *)s, 4);
    c = crc32_update(c, (const uint8_t *)s + 4, 5);
    CHECK(c == 0xCBF43926u);
    TEST_END();
}
```

`firmware/test/check_wrtgate.sh`:
```sh
#!/bin/sh
# M0-M3: /WRTGATE must stay high. Only allowed reference: the idle table in hw.c.
cd "$(dirname "$0")/../src" || exit 1
hits=$(grep -n "PIN_MECH_WRTGATE" ./*.c | grep -v "^./hw.c:.*{PIN_MECH_WRTGATE, 1}")
if [ -n "$hits" ]; then
    echo "FAIL: /WRTGATE referenced outside the hw.c idle table:"
    echo "$hits"
    exit 1
fi
echo "check_wrtgate: ok"
```

`firmware/test/Makefile`:
```make
CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -I../src -Ibuild
OUT    := build

TESTS  := test_crc32

all: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do ./$(OUT)/$$t || exit 1; done
	@sh ./check_wrtgate.sh

$(OUT)/test_crc32: test_crc32.c ../src/crc32.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $^

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)

.PHONY: all clean
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd firmware/test && make`
Expected: FAIL, `crc32.h: No such file or directory`.

- [ ] **Step 3: Write `crc32`**

`firmware/src/crc32.h`:
```c
#pragma once
#include <stddef.h>
#include <stdint.h>
// zlib-compatible CRC-32. Start with crc = 0; chain calls for streaming.
uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n);
```

`firmware/src/crc32.c`:
```c
#include "crc32.h"

uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
```

- [ ] **Step 4: Write pins, hw and a minimal main**

`firmware/src/pins.h`:
```c
#pragma once
// GPIO map: docs/pin_budget.md. Shifters: U1 (B->A, always on),
// U2 + U4 (A->B, /OE = PIN_MECH_OE), U3 (A->B, /OE = PIN_HOST_RD_OE), U5 (B->A).
#define PIN_PH3_OUT       0
#define PIN_LASTONE       1
#define PIN_PH0           2
#define PIN_PH1           3
#define PIN_PH2           4
#define PIN_PH3           5
#define PIN_HOST_WRREQ    6
#define PIN_HOST_HSTEN    7
#define PIN_HOST_WR       8
#define PIN_ACK           9   // via 74LVC1G07: high = released
#define PIN_HOST_RD      10
#define PIN_HOST_RD_OE   11   // U3 /OE, active low
#define PIN_CA0          12
#define PIN_CA1          13
#define PIN_CA2          14
#define PIN_LSTRB        15
#define PIN_SEL          16
#define PIN_MECH_ENBL    17   // /ENBL, active low
#define PIN_MECH_WRTGATE 18   // /WRTGATE, active low. Held high in M0-M3.
#define PIN_MECH_WRTDATA 19
#define PIN_MECH_RD      20   // read data + status (CA0-2/SEL select)
#define PIN_MECH_OE      21   // U2 + U4 /OE, active low, 10k pull-up to 3V3
#define PIN_EJECT_BTN    22
#define PIN_LED          26
#define PIN_ENBL_OUT     27
```

`firmware/src/hw.h`:
```c
#pragma once
#include <stdbool.h>

// Call first thing in main(): mechanism shifter disabled, every output at its idle level.
void hw_init_safe(void);
void hw_mech_outputs_enable(bool on);   // U2 + U4 /OE
void hw_mech_enbl(bool on);             // mechanism /ENBL
void hw_led(bool on);
bool hw_mech_rd(void);
```

`firmware/src/hw.c`:
```c
#include <stddef.h>
#include "hw.h"
#include "pins.h"
#include "hardware/gpio.h"

typedef struct { unsigned pin; bool level; } pin_level_t;

static const pin_level_t idle_outputs[] = {
    {PIN_CA0, 0}, {PIN_CA1, 0}, {PIN_CA2, 0}, {PIN_LSTRB, 0}, {PIN_SEL, 0},
    {PIN_MECH_ENBL, 1},
    {PIN_MECH_WRTGATE, 1},
    {PIN_MECH_WRTDATA, 0},
    {PIN_PH3_OUT, 0}, {PIN_ENBL_OUT, 1}, {PIN_LED, 0},
    {PIN_HOST_RD, 0}, {PIN_ACK, 1},
};

static const unsigned input_pins[] = {
    PIN_MECH_RD, PIN_PH0, PIN_PH1, PIN_PH2, PIN_PH3,
    PIN_HOST_WRREQ, PIN_HOST_HSTEN, PIN_HOST_WR, PIN_LASTONE,
};

static void out_pin(unsigned pin, bool level) {
    gpio_init(pin);            // input, latch 0, SIO function
    gpio_put(pin, level);      // set the latch before driving: no glitch
    gpio_set_dir(pin, GPIO_OUT);
}

void hw_init_safe(void) {
    out_pin(PIN_MECH_OE, 1);       // U2 + U4 stay Hi-Z (the 10k pull-up held it through reset)
    out_pin(PIN_HOST_RD_OE, 1);    // U3 Hi-Z
    for (size_t i = 0; i < sizeof idle_outputs / sizeof idle_outputs[0]; i++)
        out_pin(idle_outputs[i].pin, idle_outputs[i].level);
    for (size_t i = 0; i < sizeof input_pins / sizeof input_pins[0]; i++) {
        gpio_init(input_pins[i]);
        gpio_disable_pulls(input_pins[i]);   // driven push-pull by an 8T245
    }
    gpio_init(PIN_EJECT_BTN);
    gpio_pull_up(PIN_EJECT_BTN);
}

void hw_mech_outputs_enable(bool on) { gpio_put(PIN_MECH_OE, !on); }
void hw_mech_enbl(bool on) { gpio_put(PIN_MECH_ENBL, !on); }
void hw_led(bool on) { gpio_put(PIN_LED, on); }
bool hw_mech_rd(void) { return gpio_get(PIN_MECH_RD); }
```

`firmware/src/main.c` (minimal; replaced in Task 7):
```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "hw.h"

int main(void) {
    hw_init_safe();
    stdio_init_all();
    for (;;) {
        int ch = getchar_timeout_us(100000);
        if (ch == '\n' || ch == '\r') puts("OK a2m_bringup (scaffold)");
    }
}
```

- [ ] **Step 5: Write the build files**

```bash
cp ~/Development/pico-sdk/external/pico_sdk_import.cmake firmware/
chmod +x firmware/test/check_wrtgate.sh
```

`firmware/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico2)
include(pico_sdk_import.cmake)
project(a2m_bringup C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(a2m_bringup
    src/main.c src/hw.c src/crc32.c)
target_include_directories(a2m_bringup PRIVATE src)
target_compile_options(a2m_bringup PRIVATE -Wall -Wextra)
target_link_libraries(a2m_bringup pico_stdlib)
pico_enable_stdio_usb(a2m_bringup 1)
pico_enable_stdio_uart(a2m_bringup 0)
pico_add_extra_outputs(a2m_bringup)
```

`firmware/Makefile`:
```make
PICO_SDK_PATH ?= $(HOME)/Development/pico-sdk
export PICO_SDK_PATH
BUILD   := build
ELF     := $(BUILD)/a2m_bringup.elf

# SWD via picoprobe / Debug Probe (CMSIS-DAP). OpenOCD must know the RP2350
# (target/rp2350.cfg): Raspberry Pi's OpenOCD fork or upstream newer than 0.12.
# The oss-cad-suite build on PATH today only has rp2040.cfg (checked 2026-10-07).
OPENOCD ?= openocd
OCD_CFG := -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000"
GDB     ?= arm-none-eabi-gdb

all: $(BUILD)/build.ninja
	cmake --build $(BUILD)

$(BUILD)/build.ninja:
	cmake -B $(BUILD) -S . -G Ninja

test:
	$(MAKE) -C test

# Operator only (hardware actions):
flash: all
	$(OPENOCD) $(OCD_CFG) -c "program $(ELF) verify reset exit"

reset:
	$(OPENOCD) $(OCD_CFG) -c "init; reset run; exit"

openocd:
	$(OPENOCD) $(OCD_CFG)

gdb: all
	$(GDB) $(ELF) -ex "target extended-remote :3333" -ex "monitor reset halt"

clean:
	rm -rf $(BUILD)
	$(MAKE) -C test clean

.PHONY: all test flash reset openocd gdb clean
```

Append to `.gitignore`:
```
firmware/build/
firmware/test/build/
```

- [ ] **Step 6: Run host tests and the firmware build**

Run: `cd firmware && make test && make`
Expected: `test_crc32.c: 3 checks, 0 failed`, `check_wrtgate: ok`, then a successful link producing `build/a2m_bringup.uf2` (`ls -la firmware/build/a2m_bringup.uf2` shows the file).

- [ ] **Step 7: Commit**

```bash
git add .gitignore firmware/
git commit -m "feat(fw): RP2350 scaffold with safe pin init and host tests"
```

---

### Task 6: Mechanism register map and sequences (host TDD)

**Files:**
- Create: `firmware/src/mech_addr.h`, `firmware/src/mech_addr.c`, `firmware/src/mech.h`, `firmware/src/mech.c`, `firmware/test/fake_bus.h`, `firmware/test/test_mech.c`
- Modify: `firmware/test/Makefile` (add `test_mech`), `firmware/CMakeLists.txt` (add sources)

**Interfaces:**
- Consumes: none (pure C).
- Produces:
  - `mech_reg_t` (`MREG_DIRTN=0x0 .. MREG_REVISED=0xF`), `mech_cmd_t` (`MCMD_DIR_IN=0x0, MCMD_STEP=0x2, MCMD_MOTOR_ON=0x4, MCMD_DIR_OUT=0x8, MCMD_RESET_SWITCHED=0x9, MCMD_MOTOR_OFF=0xC, MCMD_EJECT=0xE`), `mech_reg_names[16]`, `mech_addr_from_rom(uint8_t) -> uint8_t`
  - `mech_bus_t { set_addr(ctx, uint8_t addr); set_lstrb(ctx, bool); read_rd(ctx) -> bool; delay_us(ctx, uint32_t); ctx }`
  - `mech_state_t { int track; bool track_known; uint32_t settle_us; }`, `mech_state_init(mech_state_t*)`
  - `mech_err_t` (`MECH_OK, MECH_ERR_STEP_TIMEOUT, MECH_ERR_TRACK0, MECH_ERR_NOT_READY, MECH_ERR_EJECT, MECH_ERR_RANGE`), `mech_err_str(mech_err_t) -> const char*`
  - `mech_status(b, s, reg) -> bool`, `mech_status_all(b, s) -> uint16_t`, `mech_command(b, cmd)`, `mech_step(b, s, bool dir_in, int count) -> mech_err_t`, `mech_recal(b, s)`, `mech_seek(b, s, int track)`, `mech_motor(b, s, bool on)`, `mech_eject(b, s)`, `mech_select_head(b, int side)`

- [ ] **Step 1: Write the fake bus and failing tests**

`firmware/test/fake_bus.h`:
```c
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "mech.h"

typedef enum { EV_ADDR, EV_LSTRB, EV_READ, EV_DELAY } ev_type_t;
typedef struct { ev_type_t t; uint32_t v; } ev_t;

typedef struct fake {
    ev_t ev[64]; int n;                 // first 64 events only
    bool lstrb; int violations;         // set_addr while LSTRB high
    uint8_t addr;
    bool (*rd)(struct fake *f, uint8_t addr);
    int reads[16];
    int strobes[16];
    int seq[256]; int nseq;             // strobed addresses in order
    uint64_t now_us;
} fake_t;

static void f_push(fake_t *f, ev_type_t t, uint32_t v) {
    if (f->n < 64) { f->ev[f->n].t = t; f->ev[f->n].v = v; f->n++; }
}
static void f_set_addr(void *c, uint8_t a) {
    fake_t *f = c; if (f->lstrb) f->violations++; f->addr = a; f_push(f, EV_ADDR, a);
}
static void f_set_lstrb(void *c, bool h) {
    fake_t *f = c;
    if (h && !f->lstrb) { f->strobes[f->addr]++; if (f->nseq < 256) f->seq[f->nseq++] = f->addr; }
    f->lstrb = h; f_push(f, EV_LSTRB, h);
}
static bool f_read(void *c) {
    fake_t *f = c; f->reads[f->addr]++; f_push(f, EV_READ, f->addr); return f->rd(f, f->addr);
}
static void f_delay(void *c, uint32_t us) { fake_t *f = c; f->now_us += us; f_push(f, EV_DELAY, us); }

static mech_bus_t fake_bus(fake_t *f, bool (*rd)(fake_t *, uint8_t)) {
    memset(f, 0, sizeof *f);
    f->rd = rd;
    mech_bus_t b = { f_set_addr, f_set_lstrb, f_read, f_delay, f };
    return b;
}
```

`firmware/test/test_mech.c`:
```c
#include "test.h"
#include "fake_bus.h"
#include "mech.h"
#include "mech_addr.h"

// --- read-data behaviours -------------------------------------------------
static bool rd_all_high(fake_t *f, uint8_t a) { (void)f; (void)a; return true; }
static bool rd_drvin_low(fake_t *f, uint8_t a) { (void)f; return a != MREG_DRVIN; }
static bool rd_step_stuck(fake_t *f, uint8_t a) { (void)f; return a != MREG_STEP; }
static bool rd_tk0_on_third(fake_t *f, uint8_t a) {
    if (a == MREG_TK0) return f->reads[MREG_TK0] < 3;
    return a != MREG_READY;   // READY low, everything else high
}
static bool rd_tk0_never(fake_t *f, uint8_t a) { (void)f; return a != MREG_READY; }
static bool rd_ready_low(fake_t *f, uint8_t a) {
    if (a == MREG_TK0) return false;
    (void)f; return a != MREG_READY;
}
static bool rd_eject_ok(fake_t *f, uint8_t a) {
    if (a == MREG_CSTIN) return f->strobes[MCMD_EJECT] > 0 && f->reads[MREG_CSTIN] >= 5;
    return true;
}
static bool rd_eject_stuck(fake_t *f, uint8_t a) { (void)f; return a != MREG_CSTIN; }

static void test_rom_mapping(void) {
    // liron.asm call sites: Se640/Se64a argument -> our (CA2 CA1 CA0 SEL) address
    CHECK(mech_addr_from_rom(0x02) == MREG_CSTIN);       // Se51b disk-in check
    CHECK(mech_addr_from_rom(0x0a) == MREG_TK0);         // Le4f7 recal poll
    CHECK(mech_addr_from_rom(0x0b) == MREG_READY);       // Se56a
    CHECK(mech_addr_from_rom(0x04) == MCMD_STEP);        // Se4cf
    CHECK(mech_addr_from_rom(0x01) == MCMD_DIR_OUT);     // Le4f7, Le4b3
    CHECK(mech_addr_from_rom(0x00) == MCMD_DIR_IN);      // Le4b3
    CHECK(mech_addr_from_rom(0x09) == MCMD_MOTOR_OFF);   // Se51b
    CHECK(mech_addr_from_rom(0x0d) == MCMD_EJECT);       // Le534
}

static void test_command_strobe_sequence(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_all_high);
    mech_command(&b, MCMD_MOTOR_ON);
    CHECK(f.n == 6);
    CHECK(f.ev[0].t == EV_ADDR && f.ev[0].v == MCMD_MOTOR_ON);
    CHECK(f.ev[1].t == EV_DELAY && f.ev[1].v >= 1);
    CHECK(f.ev[2].t == EV_LSTRB && f.ev[2].v == 1);
    CHECK(f.ev[3].t == EV_DELAY && f.ev[3].v >= 2);
    CHECK(f.ev[4].t == EV_LSTRB && f.ev[4].v == 0);
    CHECK(f.strobes[MCMD_MOTOR_ON] == 1);
}

static void test_status_all(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_drvin_low);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_status_all(&b, &s) == (uint16_t)~(1u << MREG_DRVIN));
    for (int r = 0; r < 16; r++) CHECK(f.reads[r] == 1);
}

static void test_step_timeout(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_step_stuck);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_step(&b, &s, true, 1) == MECH_ERR_STEP_TIMEOUT);
    CHECK(f.now_us <= MECH_STEP_TIMEOUT_US + 2 * (MECH_STEP_POLL_US + s.settle_us) + 100);
}

static void test_recal_success(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_tk0_on_third);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_recal(&b, &s) == MECH_OK);
    CHECK(s.track == 0 && s.track_known);
    CHECK(f.strobes[MCMD_DIR_OUT] == 1);
    CHECK(f.strobes[MCMD_STEP] == MECH_TRACKS);
}

static void test_recal_fail(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_tk0_never);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_recal(&b, &s) == MECH_ERR_TRACK0);
    CHECK(!s.track_known);
    CHECK(f.reads[MREG_TK0] == MECH_RECAL_POLLS);
}

static void test_seek_range(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_ready_low);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_seek(&b, &s, -1) == MECH_ERR_RANGE);
    CHECK(mech_seek(&b, &s, MECH_TRACKS) == MECH_ERR_RANGE);
    CHECK(f.n == 0);
}

static void test_seek_counts_steps(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_ready_low);
    mech_state_t s; mech_state_init(&s);
    s.track = 10; s.track_known = true;
    CHECK(mech_seek(&b, &s, 13) == MECH_OK);
    CHECK(s.track == 13);
    CHECK(f.strobes[MCMD_DIR_IN] == 1 && f.strobes[MCMD_STEP] == 3);
    CHECK(mech_seek(&b, &s, 11) == MECH_OK);
    CHECK(s.track == 11);
    CHECK(f.strobes[MCMD_DIR_OUT] == 1 && f.strobes[MCMD_STEP] == 5);
    CHECK(mech_seek(&b, &s, 11) == MECH_OK);
    CHECK(f.strobes[MCMD_STEP] == 5);
}

static void test_seek_unknown_recals_first(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_ready_low);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_seek(&b, &s, 5) == MECH_OK);
    CHECK(f.strobes[MCMD_STEP] == MECH_TRACKS + 5);
    CHECK(s.track == 5 && s.track_known);
}

static void test_motor_on_not_ready(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_all_high);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_motor(&b, &s, true) == MECH_ERR_NOT_READY);
    CHECK(f.strobes[MCMD_MOTOR_ON] == 1);
    CHECK(f.now_us <= MECH_SPINUP_TIMEOUT_US + 2 * (MECH_READY_POLL_US + s.settle_us) + 100);
}

static void test_eject_no_disk(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_all_high);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_eject(&b, &s) == MECH_OK);
    CHECK(f.nseq == 0);
}

static void test_eject_success(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_eject_ok);
    mech_state_t s; mech_state_init(&s);
    s.track = 40; s.track_known = true;
    CHECK(mech_eject(&b, &s) == MECH_OK);
    CHECK(f.nseq == 2 && f.seq[0] == MCMD_MOTOR_OFF && f.seq[1] == MCMD_EJECT);
    CHECK(!s.track_known);
}

static void test_eject_fail(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_eject_stuck);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_eject(&b, &s) == MECH_ERR_EJECT);
    CHECK(f.strobes[MCMD_EJECT] == MECH_EJECT_TRIES);
}

static void test_no_addr_change_while_strobe_high(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_ready_low);
    mech_state_t s; mech_state_init(&s);
    mech_seek(&b, &s, 30);
    mech_motor(&b, &s, true);
    mech_motor(&b, &s, false);
    mech_select_head(&b, 1);
    CHECK(f.violations == 0);
    fake_t g; mech_bus_t b2 = fake_bus(&g, rd_eject_ok);
    mech_eject(&b2, &s);
    CHECK(g.violations == 0);
}

static void test_lstrb_low_after_every_op(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_ready_low);
    mech_state_t s; mech_state_init(&s);
    mech_command(&b, MCMD_STEP);        CHECK(!f.lstrb);
    mech_recal(&b, &s);                 CHECK(!f.lstrb);
    mech_seek(&b, &s, 12);              CHECK(!f.lstrb);
    mech_motor(&b, &s, true);           CHECK(!f.lstrb);
    mech_select_head(&b, 0);            CHECK(!f.lstrb);
}

static void test_select_head(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_all_high);
    mech_select_head(&b, 1);
    CHECK(f.addr == MREG_RDDATA1 && f.nseq == 0);
    mech_select_head(&b, 0);
    CHECK(f.addr == MREG_RDDATA0);
}

int main(void) {
    test_rom_mapping();
    test_command_strobe_sequence();
    test_status_all();
    test_step_timeout();
    test_recal_success();
    test_recal_fail();
    test_seek_range();
    test_seek_counts_steps();
    test_seek_unknown_recals_first();
    test_motor_on_not_ready();
    test_eject_no_disk();
    test_eject_success();
    test_eject_fail();
    test_no_addr_change_while_strobe_high();
    test_lstrb_low_after_every_op();
    test_select_head();
    TEST_END();
}
```

Add to `firmware/test/Makefile` (extend `TESTS` and add the rule):
```make
TESTS  := test_crc32 test_mech

$(OUT)/test_mech: test_mech.c ../src/mech.c ../src/mech_addr.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $^
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd firmware/test && make`
Expected: FAIL, `mech.h: No such file or directory`.

- [ ] **Step 3: Write `mech_addr`**

`firmware/src/mech_addr.h`:
```c
#pragma once
#include <stdint.h>

// 4-bit register address: (CA2 << 3) | (CA1 << 2) | (CA0 << 1) | SEL.
// Status (sampled on RD): tashnotes MCI 800K table / Apple drive status codes.
// Names without "!" but most are active low: CSTIN low = disk in, TK0 low =
// at track 0, READY low = ready, DRVIN low = drive present, WRPROT low = protected.
typedef enum {
    MREG_DIRTN = 0x0, MREG_CSTIN = 0x1, MREG_STEP = 0x2, MREG_WRPROT = 0x3,
    MREG_MOTORON = 0x4, MREG_TK0 = 0x5, MREG_SWITCHED = 0x6, MREG_TACH = 0x7,
    MREG_RDDATA0 = 0x8, MREG_RDDATA1 = 0x9, MREG_SUPERDRIVE = 0xA, MREG_REG_B = 0xB,
    MREG_SIDES = 0xC, MREG_READY = 0xD, MREG_DRVIN = 0xE, MREG_REVISED = 0xF,
} mech_reg_t;

// Commands (act on the LSTRB rising edge, ERS 3.2.3).
typedef enum {
    MCMD_DIR_IN = 0x0,          // toward center, track number increases
    MCMD_STEP = 0x2,
    MCMD_MOTOR_ON = 0x4,
    MCMD_DIR_OUT = 0x8,         // toward rim
    MCMD_RESET_SWITCHED = 0x9,  // clears the disk-switched / eject latch
    MCMD_MOTOR_OFF = 0xC,
    MCMD_EJECT = 0xE,
} mech_cmd_t;

extern const char *const mech_reg_names[16];

// liron.asm Se654 packs the address as b0=CA2 (ph2), b1=SEL (HDSEL), b2=CA0 (ph0),
// b3=CA1 (ph1). Converts a ROM argument to our address. Oracle cross-check only.
uint8_t mech_addr_from_rom(uint8_t rom_arg);
```

`firmware/src/mech_addr.c`:
```c
#include "mech_addr.h"

const char *const mech_reg_names[16] = {
    "DIRTN", "CSTIN", "STEP", "WRPROT", "MOTORON", "TK0", "SWITCHED", "TACH",
    "RDDATA0", "RDDATA1", "SUPERDRIVE", "REG_B", "SIDES", "READY", "DRVIN", "REVISED",
};

uint8_t mech_addr_from_rom(uint8_t a) {
    uint8_t ca2 = a & 1u, sel = (a >> 1) & 1u, ca0 = (a >> 2) & 1u, ca1 = (a >> 3) & 1u;
    return (uint8_t)((ca2 << 3) | (ca1 << 2) | (ca0 << 1) | sel);
}
```

- [ ] **Step 4: Write `mech`**

`firmware/src/mech.h`:
```c
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "mech_addr.h"

// Timing. "ROM" = liron.asm drive firmware; delay(A) there is ~A ms at ~2 MHz.
#define MECH_TRACKS              80
#define MECH_SETTLE_US_DEFAULT   10        // ROM Se640 samples ~20 us after select
#define MECH_SETUP_US            1
#define MECH_STROBE_US           2         // ROM Se64d: ph3 on, ph3 off
#define MECH_HOLD_US             1
#define MECH_STEP_POLL_US        100
#define MECH_STEP_TIMEOUT_US     50000     // ROM has none; ERS: 36 ms/step
#define MECH_STEP_SETTLE_US      150       // ROM Se4cf tail loop
#define MECH_RECAL_POLLS         80        // ROM Le4f7: $50 polls
#define MECH_RECAL_POLL_US       7000      // ROM: delay(7)
#define MECH_READY_POLL_US       1000
#define MECH_READY_TIMEOUT_US    500000    // ERS: 152 ms max with zone change
#define MECH_SPINUP_TIMEOUT_US   2000000
#define MECH_EJECT_MOTOR_OFF_US  200000    // ROM Se51b: delay($C8)
#define MECH_EJECT_TRIES         3         // ROM Z39
#define MECH_EJECT_POLLS         150       // ROM Z3B = $96
#define MECH_EJECT_POLL_US       10000     // ROM: delay($0A)

typedef struct {
    void (*set_addr)(void *ctx, uint8_t addr);   // CA2..CA0 + SEL only
    void (*set_lstrb)(void *ctx, bool high);
    bool (*read_rd)(void *ctx);
    void (*delay_us)(void *ctx, uint32_t us);
    void *ctx;
} mech_bus_t;

typedef struct {
    int track;
    bool track_known;
    uint32_t settle_us;
} mech_state_t;

typedef enum {
    MECH_OK = 0,
    MECH_ERR_STEP_TIMEOUT,
    MECH_ERR_TRACK0,
    MECH_ERR_NOT_READY,
    MECH_ERR_EJECT,
    MECH_ERR_RANGE,
} mech_err_t;

void mech_state_init(mech_state_t *s);
const char *mech_err_str(mech_err_t e);
bool mech_status(const mech_bus_t *b, const mech_state_t *s, mech_reg_t reg);
uint16_t mech_status_all(const mech_bus_t *b, const mech_state_t *s);
void mech_command(const mech_bus_t *b, mech_cmd_t cmd);
mech_err_t mech_step(const mech_bus_t *b, const mech_state_t *s, bool dir_in, int count);
mech_err_t mech_recal(const mech_bus_t *b, mech_state_t *s);
mech_err_t mech_seek(const mech_bus_t *b, mech_state_t *s, int track);
mech_err_t mech_motor(const mech_bus_t *b, const mech_state_t *s, bool on);
mech_err_t mech_eject(const mech_bus_t *b, mech_state_t *s);
void mech_select_head(const mech_bus_t *b, int side);
```

`firmware/src/mech.c`:
```c
#include "mech.h"

void mech_state_init(mech_state_t *s) {
    s->track = 0;
    s->track_known = false;
    s->settle_us = MECH_SETTLE_US_DEFAULT;
}

const char *mech_err_str(mech_err_t e) {
    switch (e) {
    case MECH_OK: return "ok";
    case MECH_ERR_STEP_TIMEOUT: return "step timeout";
    case MECH_ERR_TRACK0: return "track 0 not found";
    case MECH_ERR_NOT_READY: return "not ready";
    case MECH_ERR_EJECT: return "eject failed";
    case MECH_ERR_RANGE: return "track out of range";
    }
    return "unknown";
}

bool mech_status(const mech_bus_t *b, const mech_state_t *s, mech_reg_t reg) {
    b->set_addr(b->ctx, (uint8_t)reg);
    b->delay_us(b->ctx, s->settle_us);
    return b->read_rd(b->ctx);
}

uint16_t mech_status_all(const mech_bus_t *b, const mech_state_t *s) {
    uint16_t v = 0;
    for (int r = 0; r < 16; r++)
        if (mech_status(b, s, (mech_reg_t)r)) v |= (uint16_t)(1u << r);
    return v;
}

void mech_command(const mech_bus_t *b, mech_cmd_t cmd) {
    b->set_addr(b->ctx, (uint8_t)cmd);
    b->delay_us(b->ctx, MECH_SETUP_US);
    b->set_lstrb(b->ctx, true);
    b->delay_us(b->ctx, MECH_STROBE_US);
    b->set_lstrb(b->ctx, false);
    b->delay_us(b->ctx, MECH_HOLD_US);
}

static bool wait_level(const mech_bus_t *b, const mech_state_t *s, mech_reg_t reg,
                       bool level, uint32_t poll_us, uint32_t timeout_us) {
    uint32_t waited = 0;
    for (;;) {
        if (mech_status(b, s, reg) == level) return true;
        if (waited >= timeout_us) return false;
        b->delay_us(b->ctx, poll_us);
        waited += poll_us + s->settle_us;
    }
}

mech_err_t mech_step(const mech_bus_t *b, const mech_state_t *s, bool dir_in, int count) {
    mech_command(b, dir_in ? MCMD_DIR_IN : MCMD_DIR_OUT);
    for (int i = 0; i < count; i++) {
        mech_command(b, MCMD_STEP);
        if (!wait_level(b, s, MREG_STEP, true, MECH_STEP_POLL_US, MECH_STEP_TIMEOUT_US))
            return MECH_ERR_STEP_TIMEOUT;
        b->delay_us(b->ctx, MECH_STEP_SETTLE_US);
    }
    return MECH_OK;
}

mech_err_t mech_recal(const mech_bus_t *b, mech_state_t *s) {
    s->track_known = false;
    mech_err_t e = mech_step(b, s, false, MECH_TRACKS);   // ROM Le4f7: 80 steps outward
    if (e != MECH_OK) return e;
    for (int i = 0; i < MECH_RECAL_POLLS; i++) {
        b->delay_us(b->ctx, MECH_RECAL_POLL_US);
        if (!mech_status(b, s, MREG_TK0)) {
            s->track = 0;
            s->track_known = true;
            return MECH_OK;
        }
    }
    return MECH_ERR_TRACK0;
}

mech_err_t mech_seek(const mech_bus_t *b, mech_state_t *s, int track) {
    if (track < 0 || track >= MECH_TRACKS) return MECH_ERR_RANGE;
    if (!s->track_known) {
        mech_err_t e = mech_recal(b, s);
        if (e != MECH_OK) return e;
    }
    int delta = track - s->track;
    if (delta == 0) return MECH_OK;
    mech_err_t e = mech_step(b, s, delta > 0, delta > 0 ? delta : -delta);
    if (e != MECH_OK) {
        s->track_known = false;
        return e;
    }
    s->track = track;
    if (!wait_level(b, s, MREG_READY, false, MECH_READY_POLL_US, MECH_READY_TIMEOUT_US))
        return MECH_ERR_NOT_READY;
    return MECH_OK;
}

mech_err_t mech_motor(const mech_bus_t *b, const mech_state_t *s, bool on) {
    if (!on) {
        mech_command(b, MCMD_MOTOR_OFF);
        return MECH_OK;
    }
    mech_command(b, MCMD_MOTOR_ON);
    if (!wait_level(b, s, MREG_READY, false, MECH_READY_POLL_US, MECH_SPINUP_TIMEOUT_US))
        return MECH_ERR_NOT_READY;
    return MECH_OK;
}

mech_err_t mech_eject(const mech_bus_t *b, mech_state_t *s) {
    if (mech_status(b, s, MREG_CSTIN)) return MECH_OK;       // no disk: nothing to do
    mech_command(b, MCMD_MOTOR_OFF);
    b->delay_us(b->ctx, MECH_EJECT_MOTOR_OFF_US);
    for (int t = 0; t < MECH_EJECT_TRIES; t++) {
        mech_command(b, MCMD_EJECT);
        for (int p = 0; p < MECH_EJECT_POLLS; p++) {
            b->delay_us(b->ctx, MECH_EJECT_POLL_US);
            if (mech_status(b, s, MREG_CSTIN)) {
                s->track_known = false;
                return MECH_OK;
            }
        }
    }
    return MECH_ERR_EJECT;
}

void mech_select_head(const mech_bus_t *b, int side) {
    b->set_addr(b->ctx, side ? MREG_RDDATA1 : MREG_RDDATA0);
}
```

Add `src/mech.c src/mech_addr.c` to `add_executable` in `firmware/CMakeLists.txt`:
```cmake
add_executable(a2m_bringup
    src/main.c src/hw.c src/crc32.c src/mech.c src/mech_addr.c)
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd firmware && make test && make`
Expected: `test_mech.c: <N> checks, 0 failed`, `check_wrtgate: ok`, firmware still builds.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/mech_addr.h firmware/src/mech_addr.c firmware/src/mech.h firmware/src/mech.c \
        firmware/test/fake_bus.h firmware/test/test_mech.c firmware/test/Makefile firmware/CMakeLists.txt
git commit -m "feat(fw): mechanism register map and ROM-oracle sequences"
```

---

### Task 7: Console parser, real mechanism bus, console main

**Files:**
- Create: `firmware/src/console.h`, `firmware/src/console.c`, `firmware/test/test_console.c`
- Modify: `firmware/src/hw.h`, `firmware/src/hw.c` (append bus), `firmware/src/main.c` (replace), `firmware/test/Makefile`, `firmware/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 6 `mech_*`, Task 5 `hw_*`, `PIN_*`.
- Produces:
  - `con_op_t` (`CON_NONE, CON_BAD, CON_HELP, CON_OE, CON_ENBL, CON_MOTOR, CON_STATUS, CON_CMD, CON_RECAL, CON_SEEK, CON_HEAD, CON_EJECT, CON_TACH, CON_SETTLE, CON_CAPTURE`), `con_cmd_t { con_op_t op; int argc; long arg[3]; }`, `console_parse(const char*) -> con_cmd_t`, `console_help(void) -> const char*`, `CON_LINE_MAX = 80`, `CON_CAPTURE_MAX = 100000`
  - `hw_mech_bus(void) -> const mech_bus_t*`
  - Console protocol: every command prints zero or more info lines, then exactly one `OK` or `ERR <reason>` line.

- [ ] **Step 1: Write the failing test**

`firmware/test/test_console.c`:
```c
#include <string.h>
#include "test.h"
#include "console.h"

static con_cmd_t P(const char *s) { return console_parse(s); }

int main(void) {
    CHECK(P("").op == CON_NONE);
    CHECK(P("   \r\n").op == CON_NONE);
    CHECK(P("bogus").op == CON_BAD);
    CHECK(P("help").op == CON_HELP);
    CHECK(P("oe on").op == CON_OE && P("oe on").arg[0] == 1);
    CHECK(P("oe off").arg[0] == 0);
    CHECK(P("oe maybe").op == CON_BAD);
    CHECK(P("enbl on").op == CON_ENBL);
    CHECK(P("motor off").op == CON_MOTOR);
    CHECK(P("status").op == CON_STATUS && P("status").argc == 0);
    CHECK(P("status 14").argc == 1 && P("status 14").arg[0] == 14);
    CHECK(P("status 16").op == CON_BAD);
    CHECK(P("cmd 0xE").op == CON_CMD && P("cmd 0xE").arg[0] == 14);
    CHECK(P("cmd 16").op == CON_BAD);
    CHECK(P("seek 12").op == CON_SEEK && P("seek 12").arg[0] == 12);
    CHECK(P("seek 80").op == CON_BAD);
    CHECK(P("seek -1").op == CON_BAD);
    CHECK(P("seek 1 2").op == CON_BAD);
    CHECK(P("seek x").op == CON_BAD);
    CHECK(P("head 1").op == CON_HEAD);
    CHECK(P("head 2").op == CON_BAD);
    CHECK(P("recal").op == CON_RECAL);
    CHECK(P("recal 3").op == CON_BAD);
    CHECK(P("eject").op == CON_EJECT);
    CHECK(P("tach").op == CON_TACH);
    CHECK(P("settle 25").op == CON_SETTLE && P("settle 25").arg[0] == 25);
    CHECK(P("settle 0").op == CON_BAD);
    con_cmd_t c = P("capture 3 1 50000");
    CHECK(c.op == CON_CAPTURE && c.argc == 3 && c.arg[0] == 3 && c.arg[1] == 1 && c.arg[2] == 50000);
    CHECK(P("capture 3 2 5").op == CON_BAD);
    CHECK(P("capture 3 1 100001").op == CON_BAD);
    CHECK(P("capture 3 1").op == CON_BAD);
    char longline[200];
    memset(longline, 'a', sizeof longline - 1);
    longline[sizeof longline - 1] = 0;
    CHECK(P(longline).op == CON_BAD);
    CHECK(strlen(console_help()) > 20);
    TEST_END();
}
```

Add to `firmware/test/Makefile`:
```make
TESTS  := test_crc32 test_mech test_console

$(OUT)/test_console: test_console.c ../src/console.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $^
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd firmware/test && make`
Expected: FAIL, `console.h: No such file or directory`.

- [ ] **Step 3: Write the parser**

`firmware/src/console.h`:
```c
#pragma once
#include <stdbool.h>

#define CON_LINE_MAX     80
#define CON_CAPTURE_MAX  100000

typedef enum {
    CON_NONE, CON_BAD, CON_HELP, CON_OE, CON_ENBL, CON_MOTOR, CON_STATUS, CON_CMD,
    CON_RECAL, CON_SEEK, CON_HEAD, CON_EJECT, CON_TACH, CON_SETTLE, CON_CAPTURE,
} con_op_t;

typedef struct {
    con_op_t op;
    int argc;
    long arg[3];
} con_cmd_t;

con_cmd_t console_parse(const char *line);
const char *console_help(void);
```

`firmware/src/console.c`:
```c
#include "console.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    con_op_t op;
    int nargs;          // -1 = zero or one argument
    bool onoff;
    long lo[3], hi[3];
} con_spec_t;

static const con_spec_t specs[] = {
    {"help",    CON_HELP,     0, false, {0},       {0}},
    {"oe",      CON_OE,       1, true,  {0},       {0}},
    {"enbl",    CON_ENBL,     1, true,  {0},       {0}},
    {"motor",   CON_MOTOR,    1, true,  {0},       {0}},
    {"status",  CON_STATUS,  -1, false, {0},       {15}},
    {"cmd",     CON_CMD,      1, false, {0},       {15}},
    {"recal",   CON_RECAL,    0, false, {0},       {0}},
    {"seek",    CON_SEEK,     1, false, {0},       {79}},
    {"head",    CON_HEAD,     1, false, {0},       {1}},
    {"eject",   CON_EJECT,    0, false, {0},       {0}},
    {"tach",    CON_TACH,     0, false, {0},       {0}},
    {"settle",  CON_SETTLE,   1, false, {1},       {1000}},
    {"capture", CON_CAPTURE,  3, false, {0, 0, 1}, {79, 1, CON_CAPTURE_MAX}},
};

static bool parse_long(const char *s, long lo, long hi, long *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 0);
    if (errno || end == s || *end || v < lo || v > hi) return false;
    *out = v;
    return true;
}

static bool parse_onoff(const char *s, long *out) {
    if (strcmp(s, "on") == 0) { *out = 1; return true; }
    if (strcmp(s, "off") == 0) { *out = 0; return true; }
    return false;
}

con_cmd_t console_parse(const char *line) {
    con_cmd_t c = { CON_BAD, 0, {0, 0, 0} };
    char buf[CON_LINE_MAX];
    if (strlen(line) >= sizeof buf) return c;
    strcpy(buf, line);
    char *tok[5];
    int nt = 0;
    for (char *t = strtok(buf, " \t\r\n"); t; t = strtok(NULL, " \t\r\n")) {
        if (nt == 5) return c;
        tok[nt++] = t;
    }
    if (nt == 0) { c.op = CON_NONE; return c; }
    for (size_t i = 0; i < sizeof specs / sizeof specs[0]; i++) {
        const con_spec_t *sp = &specs[i];
        if (strcmp(tok[0], sp->name) != 0) continue;
        int given = nt - 1;
        if (sp->nargs == -1 ? given > 1 : given != sp->nargs) return c;
        for (int k = 0; k < given; k++) {
            bool ok = sp->onoff ? parse_onoff(tok[k + 1], &c.arg[k])
                                : parse_long(tok[k + 1], sp->lo[k], sp->hi[k], &c.arg[k]);
            if (!ok) return c;
        }
        c.op = sp->op;
        c.argc = given;
        return c;
    }
    return c;
}

const char *console_help(void) {
    return "help | oe on|off | enbl on|off | status [0-15] | cmd <0-15> | motor on|off\n"
           "recal | seek <0-79> | head <0|1> | eject | tach | settle <us> | capture <t> <s> <n>";
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware/test && make`
Expected: `test_console.c: <N> checks, 0 failed` plus the earlier suites and `check_wrtgate: ok`.

- [ ] **Step 5: Add the real bus to `hw`**

Append to `firmware/src/hw.h`:
```c
#include "mech.h"
// GPIO-backed mechanism bus (CA0-2 = GP12-14, SEL = GP16, LSTRB = GP15, RD = GP20).
const mech_bus_t *hw_mech_bus(void);
```

Append to `firmware/src/hw.c`:
```c
#include "pico/stdlib.h"

#define ADDR_MASK ((1u << PIN_CA0) | (1u << PIN_CA1) | (1u << PIN_CA2) | (1u << PIN_SEL))

static void bus_set_addr(void *ctx, uint8_t a) {
    (void)ctx;
    uint32_t v = (((a >> 1) & 1u) << PIN_CA0) | (((a >> 2) & 1u) << PIN_CA1) |
                 (((a >> 3) & 1u) << PIN_CA2) | ((a & 1u) << PIN_SEL);
    gpio_put_masked(ADDR_MASK, v);
}
static void bus_set_lstrb(void *ctx, bool high) { (void)ctx; gpio_put(PIN_LSTRB, high); }
static bool bus_read_rd(void *ctx) { (void)ctx; return gpio_get(PIN_MECH_RD); }
static void bus_delay_us(void *ctx, uint32_t us) { (void)ctx; busy_wait_us_32(us); }

static const mech_bus_t gpio_bus = { bus_set_addr, bus_set_lstrb, bus_read_rd, bus_delay_us, NULL };

const mech_bus_t *hw_mech_bus(void) { return &gpio_bus; }
```

- [ ] **Step 6: Replace `firmware/src/main.c`**

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "hw.h"
#include "mech.h"
#include "console.h"

static mech_state_t ms;
static bool oe_on, enbl_on, motor_on;

static void read_line(char *buf, size_t len) {
    size_t n = 0;
    for (;;) {
        int ch = getchar_timeout_us(100000);
        if (ch == PICO_ERROR_TIMEOUT) continue;
        if (ch == '\r' || ch == '\n') {
            if (n == 0) continue;
            buf[n] = 0;
            return;
        }
        if (n + 1 < len) buf[n++] = (char)ch;
    }
}

static bool need_mech(void) {
    if (!oe_on) { puts("ERR oe off"); return false; }
    if (!enbl_on) { puts("ERR enbl off"); return false; }
    return true;
}

static void reply(mech_err_t e) {
    if (e == MECH_OK) puts("OK");
    else printf("ERR %s\n", mech_err_str(e));
}

static void print_status_all(const mech_bus_t *b) {
    uint16_t st = mech_status_all(b, &ms);
    printf("STATUS");
    for (int r = 0; r < 16; r++) printf(" %s=%d", mech_reg_names[r], (st >> r) & 1);
    printf("\n");
}

// /TACH: 60 pulses per revolution (Apple drive status codes), so over 0.5 s, rpm = edges * 2.
static void do_tach(const mech_bus_t *b) {
    b->set_addr(b->ctx, MREG_TACH);
    sleep_us(ms.settle_us);
    uint32_t edges = 0;
    bool last = hw_mech_rd();
    uint64_t end = time_us_64() + 500000;
    while (time_us_64() < end) {
        bool v = hw_mech_rd();
        if (v && !last) edges++;
        last = v;
    }
    printf("TACH edges=%lu rpm=%lu\n", (unsigned long)edges, (unsigned long)(edges * 2));
    puts("OK");
}

static void execute(const con_cmd_t *c) {
    const mech_bus_t *b = hw_mech_bus();
    switch (c->op) {
    case CON_NONE:
        return;
    case CON_HELP:
        puts(console_help());
        puts("OK");
        return;
    case CON_OE:
        if (!c->arg[0]) {
            hw_mech_enbl(false);
            enbl_on = motor_on = false;
            hw_led(false);
        }
        hw_mech_outputs_enable(c->arg[0]);
        oe_on = c->arg[0];
        puts("OK");
        return;
    case CON_ENBL:
        if (!oe_on) { puts("ERR oe off"); return; }
        hw_mech_enbl(c->arg[0]);
        enbl_on = c->arg[0];
        puts("OK");
        return;
    case CON_STATUS:
        if (!need_mech()) return;
        if (c->argc == 0) print_status_all(b);
        else printf("STATUS %s=%d\n", mech_reg_names[c->arg[0]],
                    mech_status(b, &ms, (mech_reg_t)c->arg[0]));
        puts("OK");
        return;
    case CON_CMD:
        if (!need_mech()) return;
        mech_command(b, (mech_cmd_t)c->arg[0]);
        ms.track_known = false;     // a raw command may have stepped
        puts("OK");
        return;
    case CON_MOTOR: {
        if (!need_mech()) return;
        mech_err_t e = mech_motor(b, &ms, c->arg[0]);
        motor_on = c->arg[0] && e == MECH_OK;
        hw_led(motor_on);
        reply(e);
        return;
    }
    case CON_RECAL:
        if (!need_mech()) return;
        reply(mech_recal(b, &ms));
        return;
    case CON_SEEK:
        if (!need_mech()) return;
        reply(mech_seek(b, &ms, (int)c->arg[0]));
        return;
    case CON_HEAD:
        if (!need_mech()) return;
        mech_select_head(b, (int)c->arg[0]);
        puts("OK");
        return;
    case CON_EJECT: {
        if (!need_mech()) return;
        mech_err_t e = mech_eject(b, &ms);
        if (e == MECH_OK) { motor_on = false; hw_led(false); }
        reply(e);
        return;
    }
    case CON_TACH:
        if (!need_mech()) return;
        do_tach(b);
        return;
    case CON_SETTLE:
        ms.settle_us = (uint32_t)c->arg[0];
        puts("OK");
        return;
    case CON_CAPTURE:
        puts("ERR capture not built yet");
        return;
    case CON_BAD:
    default:
        puts("ERR bad command (try help)");
        return;
    }
}

int main(void) {
    hw_init_safe();             // first: mechanism shifter Hi-Z, idle levels
    stdio_init_all();
    mech_state_init(&ms);
    char line[CON_LINE_MAX];
    for (;;) {
        read_line(line, sizeof line);
        con_cmd_t c = console_parse(line);
        execute(&c);
    }
}
```

Update `add_executable` in `firmware/CMakeLists.txt`:
```cmake
add_executable(a2m_bringup
    src/main.c src/hw.c src/crc32.c src/mech.c src/mech_addr.c src/console.c)
```

- [ ] **Step 7: Build and test**

Run: `cd firmware && make test && make`
Expected: all host suites `0 failed`, `check_wrtgate: ok`, firmware links (`build/a2m_bringup.uf2` updated).

- [ ] **Step 8: Commit**

```bash
git add firmware/
git commit -m "feat(fw): USB console with mechanism status, commands, seek, eject, tach"
```

---

### Task 8: Flux capture (PIO1 + DMA) and the capture command

**Files:**
- Create: `firmware/src/flux_rx.pio`, `firmware/src/flux.h`, `firmware/src/flux.c`
- Modify: `firmware/src/main.c` (capture case, init), `firmware/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 7 console (`CON_CAPTURE` with `arg[0]=track, arg[1]=side, arg[2]=count`), `mech_seek`, `mech_select_head`, `crc32_update`, `CON_CAPTURE_MAX`.
- Produces: `flux_init(void)`, `flux_capture(uint32_t count, uint32_t timeout_ms) -> uint32_t captured`, `flux_data(void) -> const uint16_t*`, `FLUX_TICK_NS = 40`, `FLUX_MAX = CON_CAPTURE_MAX`. Wire format consumed by Task 4 `proto.parse_capture`.

- [ ] **Step 1: Write the PIO program**

`firmware/src/flux_rx.pio`:
```
; Flux interval timer for the mechanism RD line (falling edge = flux transition).
; Counts down X in 2-cycle loops through the low and high phases; on each
; falling edge pushes ~X (elapsed counts). 3 cycles per edge are not counted
; (mov isr, push, mov x): the host adds 1.5 counts back (tools/a2m/flux.py).
; With clkdiv 3.0 at 150 MHz: 50 MHz PIO, 25 MHz count rate = 40 ns per count.
.program flux_rx
.wrap_target
    mov x, ~null
count_low:
    jmp pin, count_high
    jmp x-- count_low
count_high:
    jmp x-- check_high
check_high:
    jmp pin, count_high
    mov isr, ~x
    push noblock
.wrap

% c-sdk {
static inline void flux_rx_program_init(PIO pio, uint sm, uint offset, uint pin, float clkdiv) {
    pio_sm_config c = flux_rx_program_get_default_config(offset);
    sm_config_set_jmp_pin(&c, pin);
    sm_config_set_in_pins(&c, pin);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&c, clkdiv);
    pio_sm_init(pio, sm, offset, &c);
}
%}
```

- [ ] **Step 2: Write `flux`**

`firmware/src/flux.h`:
```c
#pragma once
#include <stdint.h>
#include "console.h"

#define FLUX_MAX      CON_CAPTURE_MAX
#define FLUX_TICK_NS  40

void flux_init(void);
// Captures up to `count` intervals from the selected head. Returns how many
// were captured before `timeout_ms` (0 = no transitions at all).
uint32_t flux_capture(uint32_t count, uint32_t timeout_ms);
const uint16_t *flux_data(void);
```

`firmware/src/flux.c`:
```c
#include "flux.h"
#include "pins.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "flux_rx.pio.h"

#define FLUX_PIO pio1
#define FLUX_SM  0

static uint16_t flux_buf[FLUX_MAX];
static uint offset;
static int dma_ch;

void flux_init(void) {
    offset = pio_add_program(FLUX_PIO, &flux_rx_program);
    pio_sm_claim(FLUX_PIO, FLUX_SM);
    flux_rx_program_init(FLUX_PIO, FLUX_SM, offset, PIN_MECH_RD, 3.0f);
    dma_ch = dma_claim_unused_channel(true);
}

uint32_t flux_capture(uint32_t count, uint32_t timeout_ms) {
    if (count > FLUX_MAX) count = FLUX_MAX;
    pio_sm_set_enabled(FLUX_PIO, FLUX_SM, false);
    pio_sm_clear_fifos(FLUX_PIO, FLUX_SM);
    pio_sm_restart(FLUX_PIO, FLUX_SM);
    pio_sm_exec(FLUX_PIO, FLUX_SM, pio_encode_jmp(offset));

    dma_channel_config cfg = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);   // low 16 bits of each count
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, pio_get_dreq(FLUX_PIO, FLUX_SM, false));
    dma_channel_configure(dma_ch, &cfg, flux_buf, &FLUX_PIO->rxf[FLUX_SM], count, true);

    pio_sm_set_enabled(FLUX_PIO, FLUX_SM, true);
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (dma_channel_is_busy(dma_ch) && !time_reached(deadline))
        tight_loop_contents();

    uint32_t remaining = dma_channel_hw_addr(dma_ch)->transfer_count & 0x0FFFFFFFu;
    if (dma_channel_is_busy(dma_ch)) dma_channel_abort(dma_ch);
    pio_sm_set_enabled(FLUX_PIO, FLUX_SM, false);
    return count - remaining;
}

const uint16_t *flux_data(void) { return flux_buf; }
```

- [ ] **Step 3: Wire the capture command into `main.c`**

Add includes at the top of `firmware/src/main.c`:
```c
#include "flux.h"
#include "crc32.h"
```

Replace the `CON_CAPTURE` case with:
```c
    case CON_CAPTURE: {
        if (!need_mech()) return;
        if (!motor_on) { puts("ERR motor off"); return; }
        mech_err_t e = mech_seek(b, &ms, (int)c->arg[0]);
        if (e != MECH_OK) { printf("ERR seek: %s\n", mech_err_str(e)); return; }
        mech_select_head(b, (int)c->arg[1]);
        sleep_us(100);
        uint32_t n = flux_capture((uint32_t)c->arg[2], 2000);
        printf("FLUX BEGIN %lu %u\n", (unsigned long)n, FLUX_TICK_NS);
        stdio_flush();
        const uint8_t *p = (const uint8_t *)flux_data();
        for (uint32_t i = 0; i < n * 2; i++) putchar_raw(p[i]);
        printf("\nFLUX END %08lx\n", (unsigned long)crc32_update(0, p, n * 2));
        puts("OK");
        return;
    }
```

In `main()`, after `mech_state_init(&ms);` add:
```c
    flux_init();
```

Update `firmware/CMakeLists.txt`:
```cmake
add_executable(a2m_bringup
    src/main.c src/hw.c src/crc32.c src/mech.c src/mech_addr.c src/console.c src/flux.c)
pico_generate_pio_header(a2m_bringup ${CMAKE_CURRENT_LIST_DIR}/src/flux_rx.pio)
target_include_directories(a2m_bringup PRIVATE src)
target_compile_options(a2m_bringup PRIVATE -Wall -Wextra)
target_link_libraries(a2m_bringup pico_stdlib hardware_pio hardware_dma)
```
(keep the existing `pico_enable_stdio_*` and `pico_add_extra_outputs` lines).

- [ ] **Step 4: Build and test**

Run: `cd firmware && make test && make`
Expected: host suites pass, `check_wrtgate: ok`, firmware links with no warnings from `src/`.

- [ ] **Step 5: Commit**

```bash
git add firmware/
git commit -m "feat(fw): PIO+DMA flux capture with CRC-checked USB dump"
```

---

### Task 9: [BENCH] M0: bench rig, power, boot safety

The agent prepares this checklist and records results. **The operator does every physical step and says go before any power or flash.**

**Files:**
- Create: `docs/build_log/m0.md`
- Modify: `docs/research_notes.md` (tag measurements `[measured]`)

- [ ] **Step 1: Wire U1 and U2 (only these two for M0-M3)**

| Board | DIR | /OE | VCCA | VCCB | A side (Pico) | B side (mechanism CN104 tap) |
|---|---|---|---|---|---|---|
| U1 | GND (B->A) | GND | Pico 3V3(OUT) | bench +5V | ch8 A -> GP20 | ch8 B <- CN104 pin 16 (RD), 10k to +5V. ch1-7 B: tie to GND (unused until M6) |
| U2 | 3V3 (A->B) | GP21, 10k to 3V3 | Pico 3V3(OUT) | bench +5V | GP12-19 | CN104: CA0 pin 2, CA1 pin 4, CA2 pin 6, LSTRB pin 8, SEL pin 12, /ENBL pin 14, /WRTGATE pin 10, WRTDATA pin 18 |

B-side pulls on U2's outputs: /ENBL 10k to +5V, /WRTGATE 10k to +5V, LSTRB 10k to GND.
Mechanism power on the CN104 tap: pin 11 = +5V, pins 13/15/17/19 = +12V, pins 1/3/5/7 = GND, pin 20 = +5V (/PWM tied high), pin 9 = N/C.
One common ground: Pico GND, both shifters, bench supply, mechanism.
Pin 1 of CN104 = red stripe on the ribbon. Confirm orientation against `docs/research_notes.md` sec. 7 before power.

- [ ] **Step 2: Power the mechanism with the Pico unplugged**

Pico **unplugged** (USB and probe). With VCCA unpowered the 8T245 outputs are Hi-Z, so the mechanism sees only the B-side pulls. Never power the mechanism while the Pico runs an unknown image: old firmware could drive GP21 low. Bench supply: +5 V limited to 1.0 A, +12 V limited to 1.5 A (starting values; raise only if spin-up trips the limit later). Measure and record: +5 V and +12 V idle current; voltage at CN104 pins 11 and 13. Power the mechanism down again.

- [ ] **Step 3: Flash the bring-up firmware**

Prerequisite: debugprobe firmware v2.x on the picoprobe (RP2350 support). OpenOCD: Raspberry Pi prebuilt at `~/Development/tooling/openocd-rpi/current/openocd` (the Makefile default; the oss-cad-suite `openocd` on PATH has no rp2350.cfg). Check: `~/Development/tooling/openocd-rpi/current/openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "init; exit"`.

Mechanism supply **off** while flashing.

Probe SWDIO/SWCLK/GND to the Pico 2 debug header. `cd firmware && make flash` (or `make openocd` in one terminal and `make gdb` in another, then `load` / `continue`). Open the console: `python3 -m serial.tools.miniterm /dev/tty.usbmodem* 115200` (or `screen`). The probe also enumerates as a tty, so pick the Pico's USB CDC port. Press Enter. Expected (Task 5 scaffold firmware): `OK a2m_bringup (scaffold)`. The full console arrives in M1 (Task 7).

- [ ] **Step 4: Boot-safety scope check (operator; agent may drive the oscope MCP if the operator connects probes and says go)**

CH1 on GP21 (U2 /OE), CH2 on CN104 pin 8 (LSTRB, B side), CH3 on CN104 pin 4 (CA1). Turn the mechanism supply on. Trigger on CH1, single shot, then reset the Pico: `make reset` (the Pico 2 has no reset button), or briefly ground the RUN pin, or replug USB. Expected: GP21 stays high through reset and boot; LSTRB stays low; no pulses on CA1. The mechanism doesn't move. Save the screenshot to `docs/captures/m0/`.

- [ ] **Step 5: Record**

Create `docs/build_log/m0.md` with: date, wiring table as built, supply settings, idle currents, voltages, scope result + screenshot path, anything that surprised. Then:
```bash
git add docs/build_log/m0.md docs/captures/m0 docs/research_notes.md
git commit -m "docs(m0): bench rig and boot-safety results"
```

---

### Task 10: [BENCH] M1: mechanism status reads

**Bench safety:** before any Pico USB unplug or replug, run `oe off` and switch the mechanism supply off (M0 build log Step 4b: the 8T245 drives LSTRB high on 3V3 power edges, and /ENBL can now be held low by firmware). SWD flash and `make reset` are fine.

Console: the firmware echoes typed characters and handles backspace, so `screen /dev/cu.usbmodem* 115200` works (as does miniterm).

**Files:**
- Create: `docs/build_log/m1.md`
- Modify: `docs/research_notes.md`

- [ ] **Step 1: Enable and read (operator types, agent interprets)**

Console: `oe on`, `enbl on`, `status`. Expected for an 800K drive (tashnotes drive identification): `DRVIN=0`, `SIDES=1`, `REVISED=1`, `SUPERDRIVE=0`, `MOTORON=1` (off).

- [ ] **Step 2: Disk in / out, write-protect tab**

`status` with no disk: `CSTIN=1`. Insert a disk: `CSTIN=0`. Tab set to protect: `WRPROT=0`; tab set to write-enable: `WRPROT=1`. Record all four.

- [ ] **Step 3: Settle time**

Scope CH1 on CN104 pin 2 (CA0), CH2 on CN104 pin 16 (RD). Alternate `status 12` (SIDES, reads 1 on an 800K drive) and `status 14` (DRVIN, reads 0). They differ only in CA0, so RD toggles with or without a disk. (Addresses 0 and 1 differ only in SEL on pin 12, and with a disk in both DIRTN and CSTIN read 0, so they would show nothing.) Measure CA0 edge -> RD stable. Record it. If it exceeds 5 us, run `settle <2x measured>` and note the value for M4's default.

- [ ] **Step 4: Record**

`docs/build_log/m1.md`: full `status` dumps (no disk / disk / protected), settle time, screenshot path. Update `research_notes.md` sec. 2.7 items to `[measured]`. Commit:
```bash
git add docs/build_log/m1.md docs/captures/m1 docs/research_notes.md
git commit -m "docs(m1): mechanism status bits and settle time"
```

---

### Task 11: [BENCH] M2: mechanism commands

**Files:**
- Create: `docs/build_log/m2.md`
- Modify: `docs/research_notes.md`

- [ ] **Step 1: Motor and tach**

Disk in. `motor on` -> expect `OK` within 2 s (READY went low). `status 4` -> `MOTORON=0`. `tach` at track 0 -> expect rpm near 394 (ERS zone 1, ±2.5 %). Record supply current with the motor running.

- [ ] **Step 2: Recal and seek through every zone**

`recal` -> `OK`. Then for each of `seek 0`, `seek 16`, `seek 32`, `seek 48`, `seek 64`, `seek 79`: run `tach`. Expected rpm: 394 / 429 / 472 / 525 / 590 / 590 (±2.5 %). Record each. `status 5` at track 0 -> `TK0=0`; at track 79 -> `TK0=1`.

- [ ] **Step 3: Step timing**

Time `seek 0` -> `seek 79` from the console (wall clock) and divide by 79 for the per-step time; compare to ERS 36 ms. Optionally scope CH1 on LSTRB (CN104 pin 8) during the seek to see the step rate directly.

- [ ] **Step 4: Eject**

`eject` -> expect the disk to come out and `OK`. If nothing happens: **stop**, record it, and ask the operator. (Alternative from tashnotes: hold LSTRB high ~0.5 s for eject. The ROM uses a short strobe repeated up to 3 times, which is what the firmware does now.)

- [ ] **Step 5: Record**

`docs/build_log/m2.md`: rpm per zone, motor current, step timing, eject result. Commit:
```bash
git add docs/build_log/m2.md docs/captures/m2 docs/research_notes.md
git commit -m "docs(m2): motor, seek, zone speeds and eject on the bench"
```

---

### Task 12: [BENCH] M3: raw flux capture and full-disk decode

**Files:**
- Create: `docs/build_log/m3.md`, `docs/captures/m3/` (captures)
- Modify: `tools/a2m/gcr.py` (only if probe says the byte orders differ)

- [ ] **Step 1: Reference disk (operator; normal use of the working UniDisk)**

Reference image: `/Applications/ADTPro-v.r.m/disks/REF800K.po` (built 2026-10-10 with AppleCommander, outside git). ProDOS volume /REF800K/, 1600 blocks: DATA0-DATA5 (BIN, 120K each, Python `random.Random(0xA22053 + i)`) and README (TXT). 1453 unique non-zero blocks, 147 zero (free). SHA-256 `bfed064fef52e30578723b3c9fcc6768ab52907f4bb1aa4670f70823657742ab`.

On the Apple IIe with the **working** UniDisk, use ADTPro to write REF800K.po to an 800K floppy. That floppy is the test disk; the .po is ground truth. Only the 512-byte data blocks are compared; tag bytes and the format byte come from the UniDisk's own formatting.

- [ ] **Step 2: First capture and histogram**

Insert the reference disk in the dead-board drive's mechanism. Console: `oe on`, `enbl on`, `motor on`, `recal`. Then from `tools/`:
```bash
.venv/bin/python a2mflux.py capture --port /dev/cu.usbmodem114401 --track 0 --side 0 --out ../docs/captures/m3/t00s0.flux
.venv/bin/python a2mflux.py hist ../docs/captures/m3/t00s0.flux
```
Expected: three peaks around 2.0, 4.0 and 6.1 us (1, 2 and 3 bit cells). Record the histogram.

Notes for this step:
- Time the first `capture` (wall clock) and record it. The host allows 10 s per capture, covering the seek, a 2 s capture window and the USB dump.
- Close the console terminal before running a2mflux.py. Two readers on one port split the bytes and every capture fails CRC. Pass the Pico's port explicitly (this bench: `/dev/cu.usbmodem114401`; the Debug Probe is the other usbmodem port), not a glob.
- The motor keeps spinning between manual steps. Send `motor off` when pausing to analyse (Steps 2 and 3).
- If the histogram peaks sit near 4, 6 and 8 us instead of 2, 4 and 6 us, RD is toggling per transition rather than pulsing (a research gap). Stop and record it; do not change the decoder.

- [ ] **Step 3: Decode one track, probe the byte order**

```bash
.venv/bin/python a2mflux.py decode ../docs/captures/m3/t00s0.flux --track 0 --side 0
.venv/bin/python a2mflux.py probe  ../docs/captures/m3/t00s0.flux --track 0 --side 0
```
Expected: decode shows headers found (bad headers low). Probe's top line shows `12 good`. The on-disk checksum order may well not be the default (0, 1, 2). Probe covers all 36 orders, so a gcr.py commit here is expected, not a failure. If the top line's orders are not `(0, 1, 2) (0, 1, 2)`, set `GROUP_ORDER` and `CHECKSUM_ORDER` in `tools/a2m/gcr.py` to the probe's values, rerun `cd tools && .venv/bin/pytest -q` (all pass: the tests use the constants consistently), and commit:
```bash
git add tools/a2m/gcr.py
git commit -m "fix(tools): GCR byte order confirmed on real media"
```
If probe shows 0 good for every order but headers decode: **stop** and record. Check block 2 contents by hand with the operator before changing the decoder.

- [ ] **Step 4: Full disk**

```bash
.venv/bin/python a2mflux.py disk --port /dev/cu.usbmodem114401 --out ../docs/private/ref/read_800k.po \
    --compare /Applications/ADTPro-v.r.m/disks/REF800K.po
```
Pass: `1600/1600 blocks good` and `0 blocks differ`; exit code 0.

- [ ] **Step 5: Record**

`docs/build_log/m3.md`: histogram, probe result, per-track retry counts from the `disk` output, final block count, compare result, total time. Commit:
```bash
git add docs/build_log/m3.md docs/captures/m3
git commit -m "docs(m3): full 800K disk read matches the reference image"
```
