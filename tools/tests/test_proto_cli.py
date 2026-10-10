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
