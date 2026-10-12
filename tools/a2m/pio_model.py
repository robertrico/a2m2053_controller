"""Cycle model of one RP2040/RP2350 PIO state machine, for host tests of our PIO programs.

Runs the instruction words pioasm assembled (not the source), one PIO clock per step:
JMP, WAIT (gpio/pin), IN, OUT, PUSH, PULL, MOV, SET, with delay, autopush/autopull, stalls.
No side-set, IRQ or EXEC (our programs do not use them). Inputs are functions of time, so a
test can drive pins from an edge list. Pin outputs are logged as (time, pin, level).
"""
import re
import subprocess
from pathlib import Path


def assemble(pio_file, pioasm):
    """Assemble with pioasm; return {program: {"code": [...], "labels": {...}}}."""
    out = subprocess.run([pioasm, "-o", "c-sdk", str(pio_file)],
                         check=True, capture_output=True, text=True).stdout
    progs = {}
    for m in re.finditer(r"static const uint16_t (\w+)_program_instructions\[\] = \{(.*?)\};", out, re.S):
        words = [int(w, 16) for w in re.findall(r"0x([0-9a-fA-F]{4})", m.group(2))]
        progs[m.group(1)] = {"code": words, "labels": {}}
    for m in re.finditer(r"#define (\w+?)_offset_(\w+) (\d+)u", out):
        if m.group(1) in progs:
            progs[m.group(1)]["labels"][m.group(2)] = int(m.group(3))
    return progs


def find_pioasm():
    root = Path(__file__).resolve().parents[2]
    for p in (root / "firmware/build/pioasm-install/pioasm/pioasm", root / "firmware/build/pioasm/pioasm"):
        if p.exists():
            return str(p)
    return None


class SM:
    def __init__(self, code, clk_hz, gpio_in=None, in_base=0, jmp_pin=0, set_base=0, set_count=1,
                 out_base=0, in_shift_right=False, autopush=False, push_thresh=32,
                 out_shift_right=False, autopull=False, pull_thresh=32, fifo_depth=4, pc=0):
        self.code = code
        self.t_cycle = 1.0 / clk_hz
        self.gpio_in = gpio_in or (lambda pin, t: 0)
        self.in_base, self.jmp_pin = in_base, jmp_pin
        self.set_base, self.set_count, self.out_base = set_base, set_count, out_base
        self.in_right, self.autopush, self.push_thresh = in_shift_right, autopush, push_thresh
        self.out_right, self.autopull, self.pull_thresh = out_shift_right, autopull, pull_thresh
        self.depth = fifo_depth
        self.pc = pc
        self.x = self.y = 0
        self.isr = self.isr_cnt = 0
        self.osr, self.osr_cnt = 0, 32           # OSR starts empty
        self.rx, self.tx = [], []
        self.delay = 0
        self.cycle = 0
        self.pins = {}                            # pin -> level (outputs we drive)
        self.log = []                             # (time, pin, level)

    @property
    def t(self):
        return self.cycle * self.t_cycle

    def _pin(self, pin):
        return self.gpio_in(pin % 32, self.t) & 1

    def _set_pins(self, base, count, value):
        for i in range(count):
            p = (base + i) % 32
            v = (value >> i) & 1
            if self.pins.get(p) != v:
                self.pins[p] = v
                self.log.append((self.t, p, v))

    def _in(self, val, n):
        n = n or 32
        mask = (1 << n) - 1 if n < 32 else 0xFFFFFFFF
        if self.in_right:
            self.isr = ((self.isr >> n) | ((val & mask) << (32 - n))) & 0xFFFFFFFF if n < 32 else val & mask
        else:
            self.isr = ((self.isr << n) | (val & mask)) & 0xFFFFFFFF if n < 32 else val & mask
        self.isr_cnt = min(32, self.isr_cnt + n)

    def _out(self, n):
        n = n or 32
        if self.out_right:
            v = self.osr & ((1 << n) - 1 if n < 32 else 0xFFFFFFFF)
            self.osr = (self.osr >> n) if n < 32 else 0
        else:
            v = (self.osr >> (32 - n)) if n < 32 else self.osr
            self.osr = (self.osr << n) & 0xFFFFFFFF if n < 32 else 0
        self.osr_cnt = min(32, self.osr_cnt + n)
        return v

    def step(self):
        """One PIO clock."""
        self.cycle += 1
        if self.delay:
            self.delay -= 1
            return
        ins = self.code[self.pc]
        op, dly, arg = ins >> 13, (ins >> 8) & 0x1F, ins & 0xFF
        nxt = (self.pc + 1) % len(self.code)
        stall = False
        if op == 0:                               # JMP
            cond, addr = arg >> 5, arg & 0x1F
            take = {0: True, 1: self.x == 0, 2: self.x != 0, 3: self.y == 0, 4: self.y != 0,
                    5: self.x != self.y, 6: self._pin(self.jmp_pin) == 1, 7: self.osr_cnt < self.pull_thresh}[cond]
            if cond == 2:
                self.x = (self.x - 1) & 0xFFFFFFFF
            if cond == 4:
                self.y = (self.y - 1) & 0xFFFFFFFF
            if take:
                nxt = addr
        elif op == 1:                             # WAIT
            pol, src, idx = arg >> 7, (arg >> 5) & 3, arg & 0x1F
            if src == 0:
                level = self._pin(idx)
            elif src == 1:
                level = self._pin(self.in_base + idx)
            else:
                raise NotImplementedError("wait irq/jmppin")
            stall = level != pol
        elif op == 2:                             # IN
            src, n = arg >> 5, arg & 0x1F
            if self.autopush and self.isr_cnt >= self.push_thresh and len(self.rx) >= self.depth:
                stall = True
            else:
                val = {0: sum(self._pin(self.in_base + i) << i for i in range(32)), 1: self.x, 2: self.y,
                       3: 0, 6: self.isr, 7: self.osr}[src]
                self._in(val, n)
                if self.autopush and self.isr_cnt >= self.push_thresh:
                    if len(self.rx) < self.depth:
                        self.rx.append(self.isr)
                        self.isr = self.isr_cnt = 0
                    # else: next IN stalls (handled above)
        elif op == 3:                             # OUT
            dst, n = arg >> 5, arg & 0x1F
            if self.autopull and self.osr_cnt >= self.pull_thresh:
                if not self.tx:
                    stall = True
                else:
                    self.osr, self.osr_cnt = self.tx.pop(0), 0
            if not stall:
                v = self._out(n)
                if dst == 0:
                    self._set_pins(self.out_base, n or 32, v)
                elif dst == 1:
                    self.x = v
                elif dst == 2:
                    self.y = v
                elif dst == 3:
                    pass
                elif dst == 5:
                    nxt = v & 0x1F
                else:
                    raise NotImplementedError("out dest %d" % dst)
        elif op == 4:                             # PUSH / PULL
            is_pull, if_x, block = arg >> 7, (arg >> 6) & 1, (arg >> 5) & 1
            if not is_pull:
                if if_x and self.isr_cnt < self.push_thresh:
                    pass
                elif len(self.rx) >= self.depth:
                    stall = bool(block)
                else:
                    self.rx.append(self.isr)
                    self.isr = self.isr_cnt = 0
            else:
                if if_x and self.osr_cnt < self.pull_thresh:
                    pass
                elif not self.tx:
                    if block:
                        stall = True
                    else:
                        self.osr, self.osr_cnt = self.x, 0
                else:
                    self.osr, self.osr_cnt = self.tx.pop(0), 0
        elif op == 5:                             # MOV
            dst, mop, src = arg >> 5, (arg >> 3) & 3, arg & 7
            val = {0: sum(self._pin(self.in_base + i) << i for i in range(32)), 1: self.x, 2: self.y, 3: 0,
                   6: self.isr, 7: self.osr}[src]
            if mop == 1:
                val = ~val & 0xFFFFFFFF
            elif mop == 2:
                val = int(f"{val:032b}"[::-1], 2)
            if dst == 1:
                self.x = val
            elif dst == 2:
                self.y = val
            elif dst == 5:
                nxt = val & 0x1F
            elif dst == 6:
                self.isr, self.isr_cnt = val, 0
            elif dst == 7:
                self.osr, self.osr_cnt = val, 0
            elif dst == 0:
                self._set_pins(self.out_base, 32, val)
            else:
                raise NotImplementedError("mov dest %d" % dst)
        elif op == 7:                             # SET
            dst, data = arg >> 5, arg & 0x1F
            if dst == 0:
                self._set_pins(self.set_base, self.set_count, data)
            elif dst == 1:
                self.x = data
            elif dst == 2:
                self.y = data
            else:
                raise NotImplementedError("set dest %d" % dst)
        else:
            raise NotImplementedError("op %d" % op)
        if not stall:
            self.pc = nxt
            self.delay = dly

    def run_until(self, t_end):
        while self.t < t_end:
            self.step()
