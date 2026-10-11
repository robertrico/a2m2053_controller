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
