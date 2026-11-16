"""Host test for tools/apple/hamrtest.s.

Assembles the program with ACME and runs it in py65 against a simulated
ProDOS MLI with four fake disks. Skipped when ACME or py65 is missing.

ACME: set ACME=/path/to/acme, or have `acme` on PATH.
py65: pip install py65.

A full run is about 40 million 6502 instructions, around a minute in py65.
The clean run and the fast refusals always run. The fault runs need
HAMRTEST_FULL=1.
"""
import os
import random
import shutil
import subprocess
from pathlib import Path

import pytest

py65 = pytest.importorskip("py65.devices.mpu6502")

SRC = Path(__file__).resolve().parents[1] / "apple" / "hamrtest.s"
ACME = os.environ.get("ACME") or shutil.which("acme")
FULL = os.environ.get("HAMRTEST_FULL") == "1"
full = pytest.mark.skipif(not FULL, reason="set HAMRTEST_FULL=1")

S5D1, S5D2, S2D1, S6D1 = 0x50, 0xD0, 0x20, 0x60
IDS = {S5D1: 0, S5D2: 1, S2D1: 2}
SEEDS = [0x3C, 0xA7, 0x59]
PATH = b"/HAMRTEST/SCRATCH"


@pytest.fixture(scope="session")
def binary(tmp_path_factory):
    if not ACME:
        pytest.skip("ACME not found")
    out = tmp_path_factory.mktemp("hamrtest") / "HAMRTEST.SYSTEM"
    subprocess.run([ACME, "-f", "plain", "-o", str(out), str(SRC)], check=True)
    return out.read_bytes()


def stamp(drive, blk):
    """Python model of the block stamp in hamrtest.s (gen)."""
    hi, lo = blk >> 8, blk & 0xFF
    x = (((hi << 4) | hi) ^ lo ^ SEEDS[drive]) & 0xFF
    out = bytearray()
    for page in range(2):
        for _ in range(256):
            x = (5 * x + 0x2B) & 0xFF
            out.append(x)
        x ^= 0x5A
    out[0:4] = bytes([lo, hi, drive, lo ^ 0xFF])
    return bytes(out)


def blank800k(name, rng):
    d = bytearray(1600 * 512)
    d[0:7 * 512] = rng.randbytes(7 * 512)
    hdr = 2 * 512
    d[hdr + 4] = 0xF0 | len(name)
    d[hdr + 5:hdr + 5 + len(name)] = name
    d[hdr + 0x29] = 0x40
    d[hdr + 0x2A] = 0x06
    return d


class Rig:
    def __init__(self, binary, keys="Y"):
        rng = random.Random(2053)
        self.disks = {
            S5D1: blank800k(b"STOCK", rng),
            S5D2: blank800k(b"BLANK1", rng),
            S2D1: blank800k(b"BLANK2", rng),
            S6D1: bytearray(rng.randbytes(280 * 512)),
        }
        self.orig = {u: bytes(d) for u, d in self.disks.items()}
        self.keys = list(keys)
        self.screen = []
        self.log = []           # (op, unit, blk)
        self.fault = None       # fault(op, unit, blk, data) -> None | int | (blk, data)
        self.file = None
        self.mark = 0
        self.file_error = None  # MLI error code for CREATE
        self.cpu = py65.MPU()
        self.cpu.memory[0x2000:0x2000 + len(binary)] = list(binary)
        self.cpu.pc = 0x2000

    # --- 6502 plumbing

    def pull_word(self):
        c = self.cpu
        c.sp = (c.sp + 1) & 0xFF
        lo = c.memory[0x100 + c.sp]
        c.sp = (c.sp + 1) & 0xFF
        return lo | (c.memory[0x100 + c.sp] << 8)

    def word(self, a):
        return self.cpu.memory[a] | (self.cpu.memory[a + 1] << 8)

    def rts(self):
        self.cpu.pc = (self.pull_word() + 1) & 0xFFFF

    def out(self, ch):
        ch &= 0x7F
        self.screen.append("\n" if ch == 13 else chr(ch))

    def run(self, limit=400_000_000):
        c = self.cpu
        rom = {0xFE84, 0xFB2F, 0xFE93, 0xFE89, 0xFC58}
        for _ in range(limit):
            pc = c.pc
            if pc < 0xBF00:
                c.step()
            elif pc == 0xBF00:
                self.mli()
            elif pc == 0xFDED:
                self.out(c.a)
                self.rts()
            elif pc == 0xFD8E:
                self.out(13)
                self.rts()
            elif pc == 0xFDDA:
                for ch in "%02X" % c.a:
                    self.out(ord(ch))
                self.rts()
            elif pc == 0xFD0C:
                if not self.keys:
                    return "".join(self.screen)
                c.a = ord(self.keys.pop(0)) | 0x80
                self.rts()
            elif pc in rom:
                self.rts()
            else:
                raise AssertionError("jumped to $%04X" % pc)
        raise AssertionError("did not finish")

    # --- simulated ProDOS

    def mli(self):
        c = self.cpu
        ret = self.pull_word()
        cmd = c.memory[ret + 1]
        p = self.word(ret + 2)
        err = self.call(cmd, p)
        c.a = err
        c.p = (c.p | 1) if err else (c.p & ~1)
        c.pc = ret + 4

    def call(self, cmd, p):
        m = self.cpu.memory
        if cmd in (0x80, 0x81):
            assert m[p] == 3
            unit, buf, blk = m[p + 1], self.word(p + 2), self.word(p + 4)
            op = "r" if cmd == 0x80 else "w"
            self.log.append((op, unit, blk))
            disk = self.disks.get(unit)
            if disk is None:
                return 0x28
            if blk * 512 >= len(disk):
                return 0x27
            data = bytes(m[buf:buf + 512])
            if self.fault:
                f = self.fault(op, unit, blk, data)
                if isinstance(f, int):
                    return f
                if f:
                    blk, data = f
            if op == "r":
                m[buf:buf + 512] = list(disk[blk * 512:blk * 512 + 512])
            else:
                disk[blk * 512:blk * 512 + 512] = data
            return 0
        path = lambda a: bytes(m[a + 1:a + 1 + m[a]])
        if cmd == 0xC1:
            assert path(self.word(p + 1)) == PATH
            if self.file is None:
                return 0x46
            self.file = None
            return 0
        if cmd == 0xC0:
            assert m[p] == 7 and path(self.word(p + 1)) == PATH
            if self.file_error:
                return self.file_error
            assert self.file is None
            self.file = bytearray()
            return 0
        if cmd == 0xC8:
            assert path(self.word(p + 1)) == PATH and self.file is not None
            m[p + 5] = 1
            self.mark = 0
            return 0
        if cmd in (0xCA, 0xCB):
            assert m[p + 1] == 1
            buf, n = self.word(p + 2), self.word(p + 4)
            if cmd == 0xCB:
                self.file[self.mark:self.mark + n] = bytes(m[buf:buf + n])
            else:
                got = self.file[self.mark:self.mark + n]
                if not got:
                    return 0x4C
                m[buf:buf + len(got)] = list(got)
            self.mark += n
            return 0
        if cmd == 0xCE:
            self.mark = m[p + 2] | (m[p + 3] << 8) | (m[p + 4] << 16)
            return 0
        if cmd == 0xCC:
            return 0
        raise AssertionError("MLI call $%02X" % cmd)

    # --- helpers

    def block(self, unit, blk):
        return bytes(self.disks[unit][blk * 512:blk * 512 + 512])

    def writes(self, unit=None):
        return [e for e in self.log if e[0] == "w" and unit in (None, e[1])]


def failed(screen):
    """Step lines that reported an error."""
    lines = screen.split("\n")
    return [lines[i - 1] for i, l in enumerate(lines) if l.startswith("  ERR")]


def test_stamp_model_marks_block_and_drive():
    assert stamp(1, 0x0309)[:4] == bytes([0x09, 0x03, 1, 0xF6])
    assert stamp(1, 700) != stamp(2, 700)
    assert len({stamp(1, n) for n in range(1600)}) == 1600


def test_clean_run_passes_and_leaves_blank_disks(binary):
    rig = Rig(binary)
    screen = rig.run()
    print(screen)
    print("cycles:", rig.cpu.processorCycles)
    assert "ALL PASS" in screen and "ERR" not in screen
    assert screen.count(" PASS\n") == 26     # 25 steps and ALL PASS
    assert all(len(l) < 40 for l in screen.split("\n"))
    assert "/BLANK1" in screen and "/STOCK" in screen

    for unit in (S5D2, S2D1):
        me, other = IDS[unit], 3 - IDS[unit]
        for n in range(1600):
            if n < 7:
                want = rig.orig[unit][n * 512:n * 512 + 512]
            elif 20 <= n < 300:
                want = rig.orig[S6D1][(n - 20) * 512:(n - 19) * 512]
            elif (unit == S5D2 and 1000 <= n < 1200) or (unit == S2D1 and 400 <= n < 600):
                want = stamp(other, n)
            elif 1300 <= n < 1316:
                want = stamp(0, 8 + 100 * (n - 1300))
            else:
                want = stamp(me, n)
            assert rig.block(unit, n) == want, (hex(unit), n)

    stock = {8 + 100 * i: stamp(0, 8 + 100 * i) for i in range(16)}
    stock.update({340 + i: stamp(1, 300 + i) for i in range(16)})
    stock.update({360 + i: stamp(2, 320 + i) for i in range(16)})
    for n in range(1600):
        want = stock.get(n, rig.orig[S5D1][n * 512:n * 512 + 512])
        assert rig.block(S5D1, n) == want, n
    assert len(rig.writes(S5D1)) == 48

    assert rig.disks[S6D1] == rig.orig[S6D1] and not rig.writes(S6D1)
    assert rig.file is None


def test_missing_drive_stops_before_any_write(binary):
    rig = Rig(binary, keys="")
    del rig.disks[S2D1]
    screen = rig.run()
    assert "ROLL CALL FAILED. NOTHING WRITTEN." in screen
    assert "CODE=28" in screen and "ALL PASS" not in screen
    assert not rig.writes() and rig.file is None


def test_wrong_size_disk_stops_before_any_write(binary):
    rig = Rig(binary, keys="")
    rig.disks[S5D2][2 * 512 + 0x29] = 0x18      # 280 blocks
    rig.disks[S5D2][2 * 512 + 0x2A] = 0x01
    screen = rig.run()
    assert failed(screen) == ["ROLL CALL S5D2 "]
    assert "NOTHING WRITTEN" in screen and not rig.writes()


def test_any_key_but_y_stops_before_any_write(binary):
    rig = Rig(binary, keys="N")
    screen = rig.run()
    assert "STOPPED. NOTHING WRITTEN." in screen and not rig.writes()


@full
def test_bit_flip_on_write_is_caught_with_block_and_offset(binary):
    rig = Rig(binary)
    hit = []

    def fault(op, unit, blk, data):
        if op == "w" and unit == S5D2 and blk == 777 and not hit:
            hit.append(1)
            bad = bytearray(data)
            bad[0x1F3] ^= 0x04
            return blk, bytes(bad)

    rig.fault = fault
    screen = rig.run()
    assert "FULL READ  S5D2 ........\n  ERR BLK=0309 OFFS=01F3 N=01" in screen
    assert "FULL READ  S2D1 ........ PASS" in screen
    assert "STEPS FAILED" in screen


@full
def test_write_to_the_wrong_block_is_caught(binary):
    rig = Rig(binary)
    hit = []

    def fault(op, unit, blk, data):
        if op == "w" and unit == S2D1 and blk == 900 and not hit:
            hit.append(1)
            return 100, data

    rig.fault = fault
    screen = rig.run()
    assert "FULL WRITE S2D1 ........ PASS" in screen
    bad = failed(screen)
    assert bad[0].startswith("FULL READ  S2D1")
    assert "ERR BLK=0064 OFFS=0000 N=02" in screen      # 100 foreign, 900 stale


@full
def test_write_to_the_wrong_drive_is_caught(binary):
    rig = Rig(binary)

    # Serve S5D2's block 725 when the chain test asks S2D1 for it.
    def call(cmd, p, real=rig.call):
        m = rig.cpu.memory
        if cmd == 0x80 and m[p + 1] == S2D1 and rig.word(p + 4) == 725 \
                and rig.screen[-17:] == list("CHAIN SWITCH ALL "):
            buf = rig.word(p + 2)
            m[buf:buf + 512] = list(rig.block(S5D2, 725))
            return 0
        return real(cmd, p)

    rig.call = call
    screen = rig.run()
    assert failed(screen) == ["CHAIN SWITCH ALL "]
    assert "ERR BLK=02D5 OFFS=0002 N=01" in screen


@full
def test_read_error_reports_code_and_other_steps_carry_on(binary):
    rig = Rig(binary)
    rig.fault = lambda op, unit, blk, data: 0x27 if (op, unit, blk) == ("r", S5D2, 1599) else None
    screen = rig.run()
    bad = failed(screen)
    assert bad[0].startswith("FULL READ  S5D2") and bad[1].startswith("SEEK S5D2")
    assert len(bad) == 2 and "\n2 STEPS FAILED" in screen
    assert "ERR BLK=063F CODE=27 N=01" in screen


@full
def test_dead_drive_gives_up_after_eight_errors_per_step(binary):
    rig = Rig(binary)
    armed = []

    def fault(op, unit, blk, data):
        if unit == S2D1 and armed:
            return 0x27

    rig.fault = fault
    real_out = rig.out

    def out(ch):
        real_out(ch)
        if "".join(rig.screen[-2:]) == "Y\n":
            armed.append(1)

    rig.out = out
    screen = rig.run()
    assert "FULL WRITE S2D1 \n  ERR BLK=0000 CODE=27 N=08" in screen
    assert "FULL WRITE S5D2 ........ PASS" in screen
    assert len([e for e in rig.log if e[1] == S2D1]) < 400
    assert "STEPS FAILED" in screen


@full
def test_write_protected_boot_floppy_fails_only_the_file_steps(binary):
    rig = Rig(binary)
    rig.file_error = 0x2B
    screen = rig.run()
    assert failed(screen) == ["TO 5.25 FILE S5D2 ", "TO 5.25 FILE S2D1 "]
    assert "ERR BLK=0000 CODE=2B N=01" in screen
    assert "\n2 STEPS FAILED" in screen


def test_rerun_after_a_clean_run_passes_again(binary):
    if not FULL:
        pytest.skip("set HAMRTEST_FULL=1")
    rig = Rig(binary, keys="YxY")
    screen = rig.run()
    assert screen.count("ALL PASS") == 2 and "ERR" not in screen
