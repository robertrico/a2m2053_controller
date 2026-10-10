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


def test_drift_tracking_is_needed():
    p = payloads_for(70, 1)
    ivs = flux.encode_flux(synth.track_bits(70, 1, p), jitter=0.06, drift=0.12, seed=3)
    tracked = track.decode_track(flux.bits_to_nibbles(flux.intervals_to_bits(ivs)), 70, 1)
    assert sorted(tracked.sectors) == list(range(8))
    assert all(tracked.sectors[s].payload == p[s] for s in p)
    fixed = track.decode_track(flux.bits_to_nibbles(flux.intervals_to_bits(ivs, alpha=0.0)), 70, 1)
    assert len(fixed.sectors) < 8


def test_out_of_range_sector_is_bad_header():
    p = payloads_for(70, 0)
    bits = (synth.SYNC * 6 + synth.bytes_bits(gcr.encode_address(70, 15, 0))
            + synth.SYNC * 6 + synth.bytes_bits(gcr.encode_data(15, bytes(gcr.SECTOR_LEN))))
    res = track.decode_track(_decode(bits), 70, 0)
    assert 15 not in res.sectors
    assert res.bad_headers >= 1
