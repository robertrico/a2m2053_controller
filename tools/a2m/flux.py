"""Flux intervals <-> bit cells <-> GCR nibbles.

Firmware PIO counts in 40 ns ticks (Task 8: 150 MHz / clkdiv 3 / 2 cycles
per count). Each interval loses 3 PIO cycles (1.5 ticks) at the edge, so we
add them back. 800K data rate is 489.6 kbit/s (ERS 699-0321): 2.0425 us cells.
"""
import random

TICK_NS = 40.0
PIO_EDGE_OVERHEAD_TICKS = 1.5
NOMINAL_CELL_NS = 1e9 / 489_600


def encode_flux(bits, cell_ticks: float = NOMINAL_CELL_NS / TICK_NS, jitter: float = 0.0,
                drift: float = 0.0, seed: int = 0) -> list[int]:
    """Bit cells -> raw PIO counts between transitions ('1' = transition)."""
    rng = random.Random(seed)
    out = []
    run = 0
    total = max(1, len(bits))
    for i, b in enumerate(bits):
        run += 1
        if b:
            scale = 1.0 + drift * (i / total)
            t = run * cell_ticks * scale * (1.0 + rng.uniform(-jitter, jitter))
            out.append(max(1, int(round(t - PIO_EDGE_OVERHEAD_TICKS))))
            run = 0
    return out


def intervals_to_bits(intervals, tick_ns: float = TICK_NS, cell_ns: float = NOMINAL_CELL_NS,
                      alpha: float = 0.05) -> list[int]:
    """Raw PIO counts -> bit cells, with a cell estimate that follows speed drift."""
    cell = cell_ns / tick_ns
    bits = []
    for raw in intervals:
        t = raw + PIO_EDGE_OVERHEAD_TICKS
        n = max(1, min(int(t / cell + 0.5), 8))
        bits.extend([0] * (n - 1))
        bits.append(1)
        if n <= 3:
            cell += alpha * (t / n - cell)
    return bits


def bits_to_nibbles(bits) -> bytes:
    """IWM-style latch: skip zeros until a 1, then take 8 bits as one byte."""
    out = bytearray()
    reg = 0
    count = 0
    for b in bits:
        if count == 0 and b == 0:
            continue
        reg = ((reg << 1) | b) & 0xFF
        count += 1
        if count == 8:
            out.append(reg)
            reg = 0
            count = 0
    return bytes(out)


def histogram_lines(intervals, tick_ns: float = TICK_NS, bin_ns: int = 100) -> list[str]:
    counts: dict[int, int] = {}
    for v in intervals:
        k = int((v + PIO_EDGE_OVERHEAD_TICKS) * tick_ns // bin_ns)
        counts[k] = counts.get(k, 0) + 1
    if not counts:
        return ["(no intervals)"]
    peak = max(counts.values())
    lines = []
    for k in sorted(counts):
        if counts[k] * 200 < peak:
            continue
        bar = "#" * max(1, counts[k] * 60 // peak)
        lines.append(f"{k * bin_ns / 1000:5.1f} us {counts[k]:7d} {bar}")
    return lines
