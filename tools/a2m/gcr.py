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


from dataclasses import dataclass

ADDR_PROLOG = b"\xD5\xAA\x96"
DATA_PROLOG = b"\xD5\xAA\xAD"
EPILOG = b"\xDE\xAA"
DATA_NIBBLES = 699        # 174 groups of 4 + 1 group of 3
CHECKSUM_NIBBLES = 4

# Address-field format byte: 0x20 = double-sided, low bits = interleave.
# The decoder records it but never checks it. The UniDisk value is unverified.
FORMAT_UNIDISK = 0x24

# On-disk order of the 3 mangled bytes inside each 4-nibble group, and of the
# 3 checksum bytes. Confirmed on real media with `a2mflux.py probe`.
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
