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
