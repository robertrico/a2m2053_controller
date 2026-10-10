"""a2mflux: capture and decode UniDisk 800K flux from the bring-up firmware."""
import argparse
import itertools
import sys
from pathlib import Path

from . import gcr
from .flux import intervals_to_bits, bits_to_nibbles, histogram_lines
from .proto import parse_capture, CaptureError
from .track import decode_track, TrackResult


def decode_capture(stream: bytes, track: int, side: int, group_order=gcr.GROUP_ORDER,
                   checksum_order=gcr.CHECKSUM_ORDER):
    ivs, tick = parse_capture(stream)
    nibs = bits_to_nibbles(intervals_to_bits(ivs[1:], tick_ns=tick))  # first interval is partial
    return decode_track(nibs, track, side, group_order, checksum_order), nibs


def probe(nibbles: bytes, track: int, side: int):
    out = []
    for go in itertools.permutations((0, 1, 2)):
        for co in itertools.permutations((0, 1, 2)):
            out.append((len(decode_track(nibbles, track, side, go, co).sectors), go, co))
    out.sort(key=lambda x: -x[0])
    return out


def assemble_image(results):
    img = bytearray(gcr.TOTAL_BLOCKS * gcr.DATA_LEN)
    good, missing = 0, []
    for t in range(gcr.TRACKS):
        for s in range(gcr.SIDES):
            r = results.get((t, s))
            for sec in range(gcr.sectors_per_track(t)):
                if r is not None and sec in r.sectors:
                    blk = gcr.block_number(t, s, sec)
                    img[blk * 512:(blk + 1) * 512] = r.sectors[sec].payload[gcr.TAG_LEN:]
                    good += 1
                else:
                    missing.append((t, s, sec))
    return bytes(img), good, missing


def _print_track(res: TrackResult, track: int, side: int):
    want = gcr.sectors_per_track(track)
    print(f"track {track} side {side}: {len(res.sectors)}/{want} sectors good, "
          f"bad headers {res.bad_headers}, bad data {res.bad_data}, misplaced {len(res.misplaced)}")
    print("  good:", sorted(res.sectors))
    if res.misplaced:
        print("  misplaced (track, side, sector):", sorted(set(res.misplaced))[:12])


def _disk(args) -> int:
    from .serialio import Console, ConsoleError
    con = Console(args.port)
    results = {}
    try:
        for line in ("oe on", "enbl on", "motor on", "recal"):
            con.cmd(line, timeout=10.0)
        for t in range(gcr.TRACKS):
            for s in range(gcr.SIDES):
                merged = TrackResult()
                for _ in range(args.retries):
                    try:
                        res, _ = decode_capture(con.capture(t, s, args.count), t, s)
                    except (CaptureError, ConsoleError) as e:
                        print(f"  T{t:02d} S{s} retry: {e}")
                        continue
                    for k, v in res.sectors.items():
                        merged.sectors.setdefault(k, v)
                    if len(merged.sectors) == gcr.sectors_per_track(t):
                        break
                results[(t, s)] = merged
                print(f"T{t:02d} S{s} {len(merged.sectors)}/{gcr.sectors_per_track(t)}", flush=True)
    finally:
        for line in ("motor off", "enbl off", "oe off"):
            try:
                con.cmd(line, timeout=5.0)
            except ConsoleError:
                pass
    img, good, missing = assemble_image(results)
    Path(args.out).write_bytes(img)
    print(f"{good}/{gcr.TOTAL_BLOCKS} blocks good -> {args.out}")
    if missing:
        print("missing (track, side, sector):", missing[:40])
    ok = good == gcr.TOTAL_BLOCKS
    if args.compare:
        ref = Path(args.compare).read_bytes()
        diff = [b for b in range(gcr.TOTAL_BLOCKS) if img[b * 512:(b + 1) * 512] != ref[b * 512:(b + 1) * 512]]
        print(f"compare vs {args.compare}: {len(diff)} blocks differ", diff[:40])
        ok = ok and not diff
    return 0 if ok else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="a2mflux")
    sub = ap.add_subparsers(dest="cmd", required=True)
    h = sub.add_parser("hist", help="interval histogram of a saved capture")
    h.add_argument("file")
    d = sub.add_parser("decode", help="decode a saved capture")
    d.add_argument("file"); d.add_argument("--track", type=int, required=True); d.add_argument("--side", type=int, required=True)
    p = sub.add_parser("probe", help="try all group/checksum byte orders on a saved capture")
    p.add_argument("file"); p.add_argument("--track", type=int, required=True); p.add_argument("--side", type=int, required=True)
    c = sub.add_parser("capture", help="capture one track side to a file")
    c.add_argument("--port", required=True); c.add_argument("--track", type=int, required=True)
    c.add_argument("--side", type=int, required=True); c.add_argument("--count", type=int, default=100000)
    c.add_argument("--out", required=True)
    k = sub.add_parser("disk", help="read the whole disk into a .po image")
    k.add_argument("--port", required=True); k.add_argument("--out", required=True)
    k.add_argument("--compare"); k.add_argument("--count", type=int, default=100000)
    k.add_argument("--retries", type=int, default=3)
    args = ap.parse_args(argv)

    if args.cmd == "hist":
        ivs, tick = parse_capture(Path(args.file).read_bytes())
        print("\n".join(histogram_lines(ivs[1:], tick_ns=tick)))
        return 0
    if args.cmd == "decode":
        res, _ = decode_capture(Path(args.file).read_bytes(), args.track, args.side)
        _print_track(res, args.track, args.side)
        return 0 if len(res.sectors) == gcr.sectors_per_track(args.track) else 1
    if args.cmd == "probe":
        _, nibs = decode_capture(Path(args.file).read_bytes(), args.track, args.side)
        for good, go, co in probe(nibs, args.track, args.side)[:6]:
            print(f"{good:2d} good  GROUP_ORDER={go}  CHECKSUM_ORDER={co}")
        return 0
    if args.cmd == "capture":
        from .serialio import Console
        con = Console(args.port)
        raw = con.capture(args.track, args.side, args.count)
        Path(args.out).write_bytes(raw)
        ivs, _ = parse_capture(raw)
        print(f"{len(ivs)} intervals -> {args.out}")
        return 0
    return _disk(args)


if __name__ == "__main__":
    sys.exit(main())
