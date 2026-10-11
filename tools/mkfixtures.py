#!/usr/bin/env python3
"""Generate firmware/test/fixtures from the Python decoder's synthetic tracks and REF800K.

Usage (from tools/): .venv/bin/python mkfixtures.py /Applications/ADTPro-v.r.m/disks/REF800K.po
"""
import hashlib
import random
import struct
import sys
from pathlib import Path

from a2m import gcr, synth, flux

OUT = Path(__file__).resolve().parent.parent / "firmware" / "test" / "fixtures"
REF_SHA256 = "bfed064fef52e30578723b3c9fcc6768ab52907f4bb1aa4670f70823657742ab"
REAL_TS = [(0, 0), (0, 1), (5, 0), (5, 1), (20, 0), (20, 1), (40, 0), (40, 1), (60, 0), (60, 1), (79, 0), (79, 1)]


def payloads_for(trk, side, seed=0):
    rng = random.Random(seed * 1000 + trk * 2 + side)
    return {s: bytes(rng.randrange(256) for _ in range(gcr.SECTOR_LEN))
            for s in range(gcr.sectors_per_track(trk))}


def write_u16(name, values):
    (OUT / name).write_bytes(b"".join(struct.pack("<H", min(v, 65535)) for v in values))


def write_exp(name, payloads, skip=None):
    (OUT / name).write_bytes(b"".join(payloads[s] for s in sorted(payloads) if s != skip))


def corrupt_track_bits(trk, side, payloads):
    """synth.track_bits with one data nibble of the first physical sector changed."""
    order = synth.physical_order(gcr.sectors_per_track(trk), 4)
    bits = []
    for idx, s in enumerate(order):
        bits += synth.SYNC * 6 + synth.bytes_bits(gcr.encode_address(trk, s, side))
        enc = bytearray(gcr.encode_data(s, payloads[s]))
        if idx == 0:
            enc[50] = gcr.NIBBLES[(gcr.DECODE[enc[50]] + 1) % 64]
        bits += synth.SYNC * 6 + synth.bytes_bits(bytes(enc))
    bits += synth.SYNC * 6
    return bits, order[0]


def main():
    ref = Path(sys.argv[1]).read_bytes()
    if hashlib.sha256(ref).hexdigest() != REF_SHA256:
        sys.exit("REF800K hash mismatch")
    OUT.mkdir(parents=True, exist_ok=True)

    p = payloads_for(0, 0)
    write_u16("synth_clean_t00s0.u16", flux.encode_flux(synth.track_bits(0, 0, p)))
    write_exp("synth_clean_t00s0.exp", p)

    p = payloads_for(70, 1)
    write_u16("synth_drift_t70s1.u16",
              flux.encode_flux(synth.track_bits(70, 1, p), jitter=0.06, drift=0.12, seed=3))
    write_exp("synth_drift_t70s1.exp", p)

    p = payloads_for(3, 1)
    write_u16("synth_wrongside_t03s1.u16", flux.encode_flux(synth.track_bits(3, 1, p)))

    p = payloads_for(20, 0)
    bits, bad = corrupt_track_bits(20, 0, p)
    write_u16("synth_corrupt_t20s0.u16", flux.encode_flux(bits))
    write_exp("synth_corrupt_t20s0.exp", p, skip=bad)
    (OUT / "synth_corrupt_t20s0.bad").write_bytes(bytes([bad]))

    blocks = bytearray()
    for t, s in REAL_TS:
        for sec in range(gcr.sectors_per_track(t)):
            b = gcr.block_number(t, s, sec)
            blocks += ref[b * 512:(b + 1) * 512]
    (OUT / "real_expected.bin").write_bytes(bytes(blocks))
    print(f"fixtures -> {OUT}")


if __name__ == "__main__":
    main()
