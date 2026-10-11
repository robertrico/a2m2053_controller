"""Parse the firmware's flux capture block (see firmware/src/main.c, capture)."""
import re
import zlib

from . import gcr as _gcr

BEGIN_RE = re.compile(rb"FLUX BEGIN (\d+) (\d+)\r?\n")
END_RE = re.compile(rb"\r?\nFLUX END ([0-9a-f]{8})\r?\n")

DISK_BEGIN_RE = re.compile(rb"DISK BEGIN (\d+)\r?\n")
TS_RE = re.compile(rb"TS (\d+) (\d+) (\d+) ([0-9a-f]{2}) ([0-9a-f]{2})\r?\n")
TS_END_RE = re.compile(rb"\r?\nTS END ([0-9a-f]{8})\r?\n")
DISK_END_RE = re.compile(rb"DISK END (\d+) (\d+)\r?\n")
BLK_RE = re.compile(r"BLK (\d+) ([0-9a-f]{2}) ([0-9a-f]{2})")


class CaptureError(Exception):
    pass


class DiskError(CaptureError):
    pass


def parse_capture(stream: bytes) -> tuple[list[int], int]:
    m = BEGIN_RE.search(stream)
    if not m:
        raise CaptureError("no FLUX BEGIN in stream")
    count, tick_ns = int(m[1]), int(m[2])
    start = m.end()
    end = start + 2 * count
    if len(stream) < end:
        raise CaptureError(f"truncated: want {2 * count} bytes, have {len(stream) - start}")
    payload = stream[start:end]
    e = END_RE.match(stream, end)
    if not e:
        raise CaptureError("no FLUX END right after payload")
    if int(e[1], 16) != zlib.crc32(payload):
        raise CaptureError("crc mismatch")
    return [int.from_bytes(payload[k:k + 2], "little") for k in range(0, len(payload), 2)], tick_ns


def parse_disk(stream: bytes):
    m = DISK_BEGIN_RE.search(stream)
    if not m:
        raise DiskError("no DISK BEGIN in stream")
    count = int(m[1])
    if count != 1600:
        raise DiskError(f"DISK BEGIN count {count}, expected 1600")
    pos = m.end()
    image = bytearray(_gcr.TOTAL_BLOCKS * 512)
    tracks = []
    for t in range(_gcr.TRACKS):
        for s in range(_gcr.SIDES):
            h = TS_RE.match(stream, pos)
            if not h or (int(h[1]), int(h[2])) != (t, s):
                raise DiskError(f"expected TS {t} {s} at byte {pos}")
            n = int(h[3])
            if n != _gcr.sectors_per_track(t):
                raise DiskError(f"TS {t} {s}: {n} sectors")
            start = h.end()
            end = start + n * 512
            if len(stream) < end:
                raise DiskError(f"truncated in TS {t} {s}")
            payload = stream[start:end]
            e = TS_END_RE.match(stream, end)
            if not e:
                raise DiskError(f"no TS END after TS {t} {s}")
            if int(e[1], 16) != zlib.crc32(payload):
                raise DiskError(f"crc mismatch in TS {t} {s}")
            b0 = _gcr.block_number(t, s, 0)
            image[b0 * 512:(b0 + n) * 512] = payload
            tracks.append((t, s, int(h[4], 16), int(h[5], 16)))
            pos = e.end()
    d = DISK_END_RE.match(stream, pos)
    if not d:
        raise DiskError("no DISK END")
    return bytes(image), tracks, int(d[1]), int(d[2])


def parse_blk(lines):
    for i, line in enumerate(lines):
        m = BLK_RE.fullmatch(line.strip())
        if m:
            data = bytes.fromhex("".join(l.strip() for l in lines[i + 1:]))
            if len(data) != _gcr.SECTOR_LEN:
                raise CaptureError(f"BLK data is {len(data)} bytes")
            return int(m[1]), int(m[2], 16), int(m[3], 16), data
    raise CaptureError("no BLK line")


def disk_stream_complete(buf: bytes) -> bool:
    """True once the whole readdisk stream is in buf. Raises DiskError if the
    firmware printed an ERR line where a TS header or DISK END was expected."""
    m = DISK_BEGIN_RE.search(buf)
    if not m:
        return False
    pos = m.end()
    for t in range(_gcr.TRACKS):
        for s in range(_gcr.SIDES):
            # Check for ERR before TS header
            if buf[pos:].startswith(b"ERR") or buf[pos:].startswith(b"\r\nERR") or buf[pos:].startswith(b"\nERR"):
                err_line = buf[pos:].split(b"\n")[0].decode(errors="replace").strip()
                raise DiskError(err_line)
            # Match TS header
            h = TS_RE.match(buf, pos)
            if not h:
                if len(buf) - pos < 64:  # header may be incomplete
                    return False
                raise DiskError(f"bad TS header at {pos}")
            n = int(h[3])
            end = h.end() + n * 512
            if len(buf) < end:
                return False
            # Match TS END
            e = TS_END_RE.match(buf, end)
            if not e:
                if len(buf) - end < 32:
                    return False
                raise DiskError(f"no TS END after TS {t} {s}")
            pos = e.end()
    # Check for ERR before DISK END
    if buf[pos:].startswith(b"ERR") or buf[pos:].startswith(b"\r\nERR") or buf[pos:].startswith(b"\nERR"):
        err_line = buf[pos:].split(b"\n")[0].decode(errors="replace").strip()
        raise DiskError(err_line)
    # Match DISK END
    d = DISK_END_RE.match(buf, pos)
    if d:
        return True
    if len(buf) - pos < 64:
        return False
    raise DiskError("no DISK END")
