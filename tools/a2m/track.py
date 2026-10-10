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
        if addr.sector >= gcr.sectors_per_track(track):
            res.bad_headers += 1
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
