"""USB CDC console to the bring-up firmware. Every command ends in OK or ERR."""
import re
import time
import serial

from .proto import END_RE, disk_stream_complete, DiskError


class ConsoleError(Exception):
    pass


class Console:
    def __init__(self, port: str, baud: int = 115200):
        self.ser = serial.Serial(port, baud, timeout=0.2)

    def cmd(self, line: str, timeout: float = 5.0) -> list[str]:
        self.ser.reset_input_buffer()
        self.ser.write((line + "\n").encode())
        deadline = time.monotonic() + timeout
        lines, buf = [], b""
        while time.monotonic() < deadline:
            buf += self.ser.read(4096)
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                s = raw.decode(errors="replace").strip()
                if s == "OK":
                    return lines
                if s.startswith("ERR"):
                    raise ConsoleError(f"{line!r}: {s}")
                if s:
                    lines.append(s)
        raise ConsoleError(f"timeout on {line!r}")

    def capture(self, track: int, side: int, count: int, timeout: float = 10.0) -> bytes:
        self.ser.reset_input_buffer()
        self.ser.write(f"capture {track} {side} {count}\n".encode())
        deadline = time.monotonic() + timeout
        buf = b""
        while time.monotonic() < deadline:
            buf += self.ser.read(65536)
            if b"FLUX BEGIN" not in buf:
                for raw in buf.split(b"\n")[:-1]:
                    if raw.strip().startswith(b"ERR"):
                        raise ConsoleError(raw.decode(errors="replace").strip())
            else:
                # FLUX BEGIN found; check if we have the full counted payload
                m = re.search(rb"FLUX BEGIN (\d+) (\d+)\r?\n", buf)
                if m:
                    payload_count = int(m[1])
                    end = m.end() + 2 * payload_count
                    if len(buf) >= end and END_RE.match(buf, end):
                        tail_deadline = time.monotonic() + 1.0
                        while not re.search(rb"\nOK\r?\n", buf[-16:]) and time.monotonic() < tail_deadline:
                            buf += self.ser.read(64)
                        return buf
        raise ConsoleError(f"capture {track} {side} timed out")

    def readblk(self, block: int, timeout: float = 10.0) -> list[str]:
        return self.cmd(f"readblk {block}", timeout=timeout)

    def readdisk(self, timeout: float = 900.0) -> bytes:
        self.ser.reset_input_buffer()
        self.ser.write(b"readdisk\n")
        deadline = time.monotonic() + timeout
        buf = b""
        while time.monotonic() < deadline:
            buf += self.ser.read(65536)
            if b"DISK BEGIN" not in buf:
                for raw in buf.split(b"\n")[:-1]:
                    if raw.strip().startswith(b"ERR"):
                        raise ConsoleError(raw.decode(errors="replace").strip())
            else:
                # DISK BEGIN found; check if we have the complete framed stream
                try:
                    if disk_stream_complete(buf):
                        tail_deadline = time.monotonic() + 1.0
                        while not re.search(rb"\nOK\r?\n", buf[-16:]) and time.monotonic() < tail_deadline:
                            buf += self.ser.read(64)
                        return buf
                except DiskError as e:
                    raise ConsoleError(str(e))
        raise ConsoleError("readdisk timed out")
