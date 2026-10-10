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
