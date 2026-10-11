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
