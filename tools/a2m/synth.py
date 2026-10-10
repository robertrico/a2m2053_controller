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
