"""Parse the firmware's flux capture block (see firmware/src/main.c, capture)."""
import re
import zlib

BEGIN_RE = re.compile(rb"FLUX BEGIN (\d+) (\d+)\r?\n")
END_RE = re.compile(rb"\r?\nFLUX END ([0-9a-f]{8})\r?\n")


class CaptureError(Exception):
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
