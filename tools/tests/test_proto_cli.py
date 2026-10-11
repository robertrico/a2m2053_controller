import struct
import zlib
import pytest
from unittest.mock import patch
from a2m import gcr, synth, flux, proto, cli, serialio
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


class FakeSerial:
    """Mock serial port that yields data in chunks."""
    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.chunk_idx = 0

    def read(self, n):
        if self.chunk_idx >= len(self.chunks):
            return b""
        chunk = self.chunks[self.chunk_idx]
        self.chunk_idx += 1
        return chunk

    def write(self, data):
        pass

    def reset_input_buffer(self):
        pass


def test_capture_handles_chunked_data():
    """Verify capture() waits for counted payload, not regex match inside binary.

    Embeds both a fake FLUX END marker and a fake OK inside the payload.
    Old code would: match the fake marker, see the fake OK, and return early
    with a truncated stream. New code uses the FLUX BEGIN count to read exactly
    the right number of bytes, then matches the real marker at the correct position.
    """
    # Create payload containing fake FLUX END marker and fake OK
    raw = b"\nFLUX END 00000000\n" + b"\nOK\r\n" + b"\x01\x02\x03\x04"  # 28 bytes, even
    values = list(struct.unpack(f"<{len(raw)//2}H", raw))
    stream = make_stream(values)

    # Feed the stream in 7-byte chunks, forcing chunk boundaries inside the fake marker.
    # Old code (END_RE.search) would:
    #   1. Match the embedded fake FLUX END marker
    #   2. See the fake OK in buf[-16:], satisfying its tail condition
    #   3. Return prematurely, missing bytes after the fake OK
    # New code (payload_count-based) reads exactly len(values)*2 bytes, then checks
    # for the real FLUX END marker at the correct position.
    chunks = [stream[i:i+7] for i in range(0, len(stream), 7)]

    con = serialio.Console.__new__(serialio.Console)
    con.ser = FakeSerial(chunks)

    # Verify capture returns the complete stream (would fail with old code)
    result = con.capture(0, 0, len(values), timeout=5.0)
    assert result == stream
    # Verify we can parse it back to the original values
    parsed_vals, _ = proto.parse_capture(result)
    assert parsed_vals == values


def test_disk_cleanup_on_error(tmp_path):
    """Verify _disk() always runs cleanup commands even on error."""
    # Track which commands are issued
    commands = []

    class FakeConsole:
        def __init__(self, port):
            self.port = port

        def cmd(self, line, timeout=5.0):
            commands.append(line)
            if line == "recal":
                raise serialio.ConsoleError("recal failed")
            return []

        def capture(self, track, side, count):
            # Never reached due to recal failure
            pass

    # Patch Console in the cli module
    with patch("a2m.serialio.Console", FakeConsole):
        with pytest.raises(serialio.ConsoleError):
            cli.main(["disk", "--port", "fake", "--out", str(tmp_path / "o.po")])

    # Verify setup commands were issued before recal
    recal_idx = commands.index("recal")
    assert commands[:recal_idx] == ["oe on", "enbl on", "motor on"]

    # Verify cleanup commands were issued in finally
    assert "motor off" in commands
    assert "enbl off" in commands
    assert "oe off" in commands
    # Verify they are the last three commands (in order)
    assert commands[-3:] == ["motor off", "enbl off", "oe off"]


from a2m import proto as _proto


def make_disk(image: bytes, bad_crc_at=None, cut=0, fail=()):
    out = b"junk\r\nDISK BEGIN 1600\r\n"
    for t in range(80):
        for s in range(2):
            n = gcr.sectors_per_track(t)
            b0 = gcr.block_number(t, s, 0)
            payload = image[b0 * 512:(b0 + n) * 512]
            status = 0xA7 if (t, s) in fail else 0x00
            crc = zlib.crc32(payload)
            if bad_crc_at == (t, s):
                crc ^= 1
            out += b"TS %d %d %d %02x %02x\r\n" % (t, s, n, status, 0) + payload + b"\r\nTS END %08x\r\n" % crc
    out += b"DISK END 812 37 131000\r\nOK\r\n"
    return out[:len(out) - cut] if cut else out


def test_parse_disk_ok():
    img = bytes((i * 7) & 0xFF for i in range(1600 * 512))
    out, tracks, mean_ns, backlog, ms = _proto.parse_disk(make_disk(img))
    assert out == img and len(tracks) == 160 and mean_ns == 812 and backlog == 37 and ms == 131000
    assert all(st == 0 for (_, _, st, _) in tracks)


def test_parse_disk_bad_crc():
    img = bytes(1600 * 512)
    with pytest.raises(_proto.CaptureError):
        _proto.parse_disk(make_disk(img, bad_crc_at=(33, 1)))


def test_parse_disk_truncated():
    img = bytes(1600 * 512)
    with pytest.raises(_proto.CaptureError):
        _proto.parse_disk(make_disk(img, cut=5000))


def test_parse_disk_reports_failed_track():
    img = bytes(1600 * 512)
    _, tracks, _, _, _ = _proto.parse_disk(make_disk(img, fail={(12, 0)}))
    assert [(t, s) for (t, s, st, _) in tracks if st == 0xA7] == [(12, 0)]


def test_parse_blk():
    data = bytes(range(256)) * 2 + bytes(12)
    lines = ["readblk 7", "BLK 7 e7 04"] + [data[i:i + 16].hex() for i in range(0, 524, 16)]
    block, status, soft, out = _proto.parse_blk(lines)
    assert (block, status, soft, out) == (7, 0xE7, 0x04, data)


def test_stream_complete_progression():
    img = bytes((i * 7) & 0xFF for i in range(1600 * 512))
    full_stream = make_disk(img)
    # Feed growing prefixes; all should return False until the full stream, which returns True
    for i in range(0, len(full_stream), 10000):
        prefix = full_stream[:i]
        if i < len(full_stream):
            assert _proto.disk_stream_complete(prefix) == False
    # Full stream should return True
    assert _proto.disk_stream_complete(full_stream) == True


def test_stream_err_mid_disk():
    img = bytes(1600 * 512)
    # Truncate right after 3rd TS END, then append an ERR
    stream = make_disk(img)
    # Count TS END occurrences to find the 3rd one
    pos = 0
    for i in range(3):
        pos = stream.find(b"TS END", pos) + 1
    # pos is now after the 3rd TS END; find the end of the line
    pos = stream.find(b"\r\n", pos) + 2
    # exactly what the firmware sends: puts("\nERR server timeout") -> "\r\nERR server timeout\r\n"
    prefix = stream[:pos]
    err = b"\r\nERR server timeout\r\n"
    with pytest.raises(_proto.DiskError) as exc:
        _proto.disk_stream_complete(prefix + err)
    assert str(exc.value) == "ERR server timeout"
    # partial line: keep waiting, no raise
    assert _proto.disk_stream_complete(prefix + b"\r\nERR ser") is False
    for cut in (b"\r\n", b"\r\nE", b"\r\nER", b"\r\nERR"):
        assert _proto.disk_stream_complete(prefix + cut) is False
    with pytest.raises(_proto.DiskError) as exc2:
        _proto.disk_stream_complete(prefix + b"\r\nERR ser" + b"ver timeout\r\n")
    assert str(exc2.value) == "ERR server timeout"


def test_stream_disk_end_in_payload():
    # Create an image where block 0 starts with "DISK END" text
    img = bytearray(1600 * 512)
    img[0:len(b"\r\nDISK END 1 2\r\nOK\r\n")] = b"\r\nDISK END 1 2\r\nOK\r\n"
    stream = make_disk(bytes(img))
    # A prefix ending right after that payload text should return False (not True)
    # Find the position: DISK BEGIN + 1st TS header + payload with the fake DISK END text
    fake_disk_end_text = b"\r\nDISK END 1 2\r\nOK\r\n"
    pos = stream.find(fake_disk_end_text)
    assert pos > 0
    prefix = stream[:pos + len(fake_disk_end_text)]
    # Should return False because we're still in the middle of the disk
    assert _proto.disk_stream_complete(prefix) == False
    # Full stream should return True and parse_disk should return the image with that text in block 0
    assert _proto.disk_stream_complete(stream) == True
    out, _, _, _, _ = _proto.parse_disk(stream)
    assert out[0:len(fake_disk_end_text)] == fake_disk_end_text


def test_parse_disk_wrong_count():
    img = bytes(1600 * 512)
    stream = make_disk(img)
    # Replace "DISK BEGIN 1600" with "DISK BEGIN 1599"
    stream_bad = stream.replace(b"DISK BEGIN 1600", b"DISK BEGIN 1599")
    with pytest.raises(_proto.DiskError) as exc:
        _proto.parse_disk(stream_bad)
    assert "1599" in str(exc.value)
