"""fm_rx / fm_tx (firmware/src/sp_fm.pio) run cycle by cycle in the PIO model (M6 spec sec. 7)."""
import bisect
import random
from pathlib import Path

import pytest

from a2m import pio_model

ROOT = Path(__file__).resolve().parents[2]
PIO = ROOT / "firmware/src/sp_fm.pio"
SYS_HZ = 150e6
CELL = 28 / 7.15909e6                 # Liron IWM slow cell, 3.911 us
RX_HZ = SYS_HZ / (SYS_HZ * CELL / 48)   # what sp_hw.c sets: 48 PIO cycles per cell
TX_HZ = SYS_HZ / (SYS_HZ * CELL / 32)
WR, WRREQ, RD = 8, 6, 10              # GPIO (firmware/src/pins.h)


@pytest.fixture(scope="module")
def progs():
    pioasm = pio_model.find_pioasm()
    if not pioasm:
        pytest.skip("pioasm not built (make -C firmware)")
    p = pio_model.assemble(PIO, pioasm)
    assert len(p["fm_rx"]["code"]) + len(p["fm_tx"]["code"]) <= 32
    return p


def iwm_frame(bits):
    """IWM byte framing: skip 0s until a 1, take 8 bits."""
    out, acc, n = [], 0, 0
    for b in bits:
        if n == 0 and not b:
            continue
        acc = (acc << 1) | b
        n += 1
        if n == 8:
            out.append(acc)
            acc, n = 0, 0
    return out


def card_stream(payload):
    """Bytes the card writes: FF, sync, C3, payload (all payload bytes have bit 7 set)."""
    return [0xFF, 0x3F, 0xCF, 0xF3, 0xFC, 0xFF, 0xC3] + payload + [0xC8]


def wr_edges(data, t0, cell, jitter, rng):
    edges, bit = [], 0
    for byte in data:
        for k in range(7, -1, -1):
            if (byte >> k) & 1:
                edges.append(t0 + bit * cell + rng.uniform(-jitter, jitter))
            bit += 1
    return edges, t0 + bit * cell


def run_rx(progs, data, cell, jitter, seed, wr_idle=1, t_wrreq=20e-6):
    rng = random.Random(seed)
    edges, t_end = wr_edges(data, t_wrreq + 4e-6, cell, jitter, rng)
    edges.sort()

    def gpio(pin, t):
        if pin == WRREQ:
            return 0 if t_wrreq <= t < t_end else 1
        if pin == WR:
            n = bisect.bisect_right(edges, t)
            return wr_idle ^ (n & 1)
        return 0

    rx = progs["fm_rx"]
    sm = pio_model.SM(rx["code"], RX_HZ, gpio_in=gpio, in_base=WR, jmp_pin=WR,
                      autopush=True, push_thresh=8, fifo_depth=8, pc=rx["labels"]["entry"])
    sm.y = 1
    chunks = []
    t_stop = t_end + 20 * cell
    while sm.t < t_stop:
        sm.step()
        if sm.rx:                         # DMA drains the FIFO
            chunks.extend(sm.rx)
            sm.rx.clear()
    bits = [(c >> k) & 1 for c in chunks for k in range(7, -1, -1)]
    return bits, chunks, sm


def payload(rng, n):
    return [0x80 | rng.randrange(128) for _ in range(n)]


def after_c3(frames):
    i = frames.index(0xC3)
    return frames[i + 1:]


def test_rx_window_edges(progs):
    """An edge-to-edge gap is read as n cells for (n - 0.5, n + 0.5) cells, n = 1..3."""
    for gap_cells, want in [(0.6, [1, 1]), (1.44, [1, 1]), (1.56, [1, 0, 1]), (2.44, [1, 0, 1]),
                            (2.56, [1, 0, 0, 1]), (3.44, [1, 0, 0, 1])]:
        edges = [24e-6, 24e-6 + gap_cells * CELL]

        def gpio(pin, t, edges=edges):
            if pin == WRREQ:
                return 0 if t >= 20e-6 else 1
            if pin == WR:
                return 1 ^ (bisect.bisect_right(edges, t) & 1)
            return 0

        rx = progs["fm_rx"]
        sm = pio_model.SM(rx["code"], RX_HZ, gpio_in=gpio, in_base=WR, jmp_pin=WR,
                          autopush=True, push_thresh=8, fifo_depth=8, pc=rx["labels"]["entry"])
        sm.y = 1
        sm.run_until(edges[1] + 8 * CELL)
        bits = [(c >> k) & 1 for c in sm.rx for k in range(7, -1, -1)]
        assert bits[:len(want)] == want, gap_cells


def test_rx_waits_for_wrreq(progs):
    rx = progs["fm_rx"]
    sm = pio_model.SM(rx["code"], RX_HZ, gpio_in=lambda p, t: 1 if p == WRREQ else (int(t * 1e6) & 1),
                      in_base=WR, jmp_pin=WR, autopush=True, push_thresh=8, pc=rx["labels"]["entry"])
    sm.y = 1
    sm.run_until(200e-6)                  # WR toggling, /WRREQ high: nothing
    assert sm.rx == [] and sm.isr_cnt == 0


@pytest.mark.parametrize("seed", range(6))
@pytest.mark.parametrize("clock_err", [-0.01, 0.0, 0.01])
def test_rx_decodes_packets(progs, seed, clock_err):
    rng = random.Random(seed)
    pl = payload(rng, 7 + 3 + 8 + 2)                       # a command packet's worth
    data = card_stream(pl)
    bits, chunks, _ = run_rx(progs, data, CELL * (1 + clock_err), 0.8e-6, seed)
    assert after_c3(iwm_frame(bits))[:len(pl) + 1] == pl + [0xC8]
    # the first bit is the first edge (FF's MSB), cells after the packet are 0s
    assert bits[:8] == [1] * 8


@pytest.mark.parametrize("wr_idle", [0, 1])
def test_rx_either_idle_level(progs, wr_idle):
    rng = random.Random(9)
    pl = payload(rng, 40)
    bits, _, _ = run_rx(progs, card_stream(pl), CELL, 0.5e-6, 9, wr_idle=wr_idle)
    assert after_c3(iwm_frame(bits))[:len(pl)] == pl


def test_rx_slow_rising_edges(progs):
    """WR crosses the 3.5 V threshold late on rising edges (about 1.9k against the input
    capacitance, idle 3.96 V): every rising edge 0.8 us late still decodes."""
    rng = random.Random(11)
    pl = payload(rng, 60)
    data = card_stream(pl)
    edges, t_end = wr_edges(data, 24e-6, CELL, 0.0, rng)
    # edge k leaves WR at level 1 ^ ((k + 1) & 1) with idle high: odd-numbered edges rise
    edges = [e + (0.8e-6 if k % 2 == 1 else 0.0) for k, e in enumerate(edges)]
    bits = rx_from_edges(progs, edges, t_end)
    assert after_c3(iwm_frame(bits))[:len(pl)] == pl


def test_rx_ringing_is_blanked(progs):
    """A 0.2 us bounce right after an edge (two extra level changes) is not a 1."""
    rng = random.Random(12)
    pl = payload(rng, 60)
    data = card_stream(pl)
    edges, t_end = wr_edges(data, 24e-6, CELL, 0.0, rng)
    bounced = []
    for e in edges:
        bounced += [e, e + 0.15e-6, e + 0.3e-6]
    bits = rx_from_edges(progs, bounced, t_end)
    assert after_c3(iwm_frame(bits))[:len(pl)] == pl


def rx_from_edges(progs, edges, t_end, t_wrreq=20e-6):
    def gpio(pin, t):
        if pin == WRREQ:
            return 0 if t_wrreq <= t < t_end else 1
        if pin == WR:
            return 1 ^ (bisect.bisect_right(edges, t) & 1)
        return 0

    rx = progs["fm_rx"]
    sm = pio_model.SM(rx["code"], RX_HZ, gpio_in=gpio, in_base=WR, jmp_pin=WR,
                      autopush=True, push_thresh=8, fifo_depth=8, pc=rx["labels"]["entry"])
    sm.y = 1
    chunks = []
    while sm.t < t_end + 20 * CELL:
        sm.step()
        if sm.rx:
            chunks.extend(sm.rx)
            sm.rx.clear()
    return [(c >> k) & 1 for c in chunks for k in range(7, -1, -1)]


def test_rx_long_zero_runs(progs):
    # $80 bytes: 7 zero cells in a row between edges
    pl = [0x80] * 30 + [0x81, 0xC0, 0x80]
    bits, _, _ = run_rx(progs, card_stream(pl), CELL * 1.01, 0.8e-6, 3)
    assert after_c3(iwm_frame(bits))[:len(pl)] == pl


def test_rx_data_packet_512(progs):
    rng = random.Random(5)
    pl = payload(rng, 600)
    bits, _, _ = run_rx(progs, card_stream(pl), CELL * 0.995, 0.8e-6, 5)
    assert after_c3(iwm_frame(bits))[:len(pl)] == pl


def test_rx_c8_latency(progs):
    """The SM starts on the first edge (FF's MSB) and bytes are 8 cells, so C8 always closes a
    chunk: its last 0 is pushed half a cell before the host's last cell ends."""
    pl = [0x81, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80]
    worst = -1.0
    for extra in range(8):                                 # every chunk phase
        data = card_stream(pl + [0x80] * extra)
        rng = random.Random(1)
        edges, t_end = wr_edges(data, 24e-6, CELL, 0.0, rng)

        def gpio(pin, t, edges=edges, t_end=t_end):
            if pin == WRREQ:
                return 0 if 20e-6 <= t < t_end else 1
            if pin == WR:
                return 1 ^ (bisect.bisect_right(edges, t) & 1)
            return 0

        rx = progs["fm_rx"]
        sm = pio_model.SM(rx["code"], RX_HZ, gpio_in=gpio, in_base=WR, jmp_pin=WR,
                          autopush=True, push_thresh=8, fifo_depth=8, pc=rx["labels"]["entry"])
        sm.y = 1
        nbits_needed = len(data) * 8
        got = 0
        while got < nbits_needed:
            sm.step()
            if sm.rx:
                got += 8 * len(sm.rx)
                sm.rx.clear()
        worst = max(worst, sm.t - t_end)
    assert -0.6 * CELL <= worst <= -0.4 * CELL
    print(f"C8 complete in a chunk {worst * 1e6:.2f} us after the host's last cell (negative: before)")


def tx_run(progs, data):
    tx = progs["fm_tx"]
    sm = pio_model.SM(tx["code"], TX_HZ, set_base=RD, autopull=True, pull_thresh=8, fifo_depth=8)
    sm.pins[RD] = 1                                         # sp_hw sets RD high before enabling
    sm.tx.append(len(data) * 8 - 1)
    feed = [b << 24 for b in data]                          # DMA 8-bit write: byte in every lane
    park = tx["labels"]["park"]
    t_limit = (len(data) * 8 + 4) * CELL
    while sm.t < t_limit:
        if feed and len(sm.tx) < 8:
            sm.tx.append(feed.pop(0))
        sm.step()
        if sm.pc == park and not feed and not sm.tx:
            break
    return sm


def test_tx_cells_and_pulses(progs):
    rng = random.Random(2)
    data = [0xFF, 0x3F, 0xCF, 0xF3, 0xFC, 0xFF, 0xC3] + payload(rng, 50) + [0xC8]
    sm = tx_run(progs, data)
    falls = [t for t, p, v in sm.log if p == RD and v == 0]
    rises = [t for t, p, v in sm.log if p == RD and v == 1]
    ones = sum(bin(b).count("1") for b in data)
    assert len(falls) == ones and len(rises) == ones
    widths = [r - f for f, r in zip(falls, rises)]
    assert all(0.95e-6 < w < 1.0e-6 for w in widths)       # 8 cycles = 0.98 us
    # falling edges sit on whole cells from the first one
    t0 = falls[0]
    bits = [0] * (len(data) * 8)
    for f in falls:
        n = (f - t0) / CELL
        assert abs(n - round(n)) < 0.01
        bits[round(n)] = 1
    first_one = next(i for i in range(len(data) * 8) if (data[i // 8] >> (7 - i % 8)) & 1)
    sent = [(data[i // 8] >> (7 - i % 8)) & 1 for i in range(len(data) * 8)]
    assert bits[:len(sent) - first_one] == sent[first_one:]
    assert sm.pins[RD] == 1                                 # parks high
    assert sm.pc == progs["fm_tx"]["labels"]["park"]


def test_tx_through_iwm_framing(progs):
    """What the host IWM frames from our falling edges is the packet."""
    data = [0xFF, 0x3F, 0xCF, 0xF3, 0xFC, 0xFF, 0x3F, 0xCF, 0xF3, 0xFC, 0xFF, 0xC3, 0x80, 0x81, 0x81, 0x80,
            0x80, 0x84, 0x80, 0xF0, 0xF8, 0xC0, 0x86, 0x80, 0xAA, 0xAB, 0xC8]
    sm = tx_run(progs, data)
    falls = [t for t, p, v in sm.log if p == RD and v == 0]
    t0 = falls[0]
    n = round((falls[-1] - t0) / CELL) + 1
    bits = [0] * (n + 8)
    for f in falls:
        bits[round((f - t0) / CELL)] = 1
    frames = iwm_frame(bits)
    assert after_c3(frames)[:15] == data[12:]
