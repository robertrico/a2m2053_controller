"""a2mflux: capture and decode UniDisk 800K flux from the bring-up firmware."""
import argparse
import itertools
import sys
import time
import zlib
from pathlib import Path

from . import gcr
from .flux import intervals_to_bits, bits_to_nibbles, histogram_lines
from .proto import parse_capture, CaptureError, parse_disk, parse_blk, parse_wblk, parse_format_end
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
            except Exception:
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
    r = sub.add_parser("readdisk", help="read the whole disk on the board (M4)")
    r.add_argument("--port", required=True); r.add_argument("--out", required=True)
    r.add_argument("--compare")
    b = sub.add_parser("readblk", help="read one block on the board (M4)")
    b.add_argument("--port", required=True); b.add_argument("--block", type=int, required=True)
    b.add_argument("--compare")
    w = sub.add_parser("writeblk", help="write one block on the board (M5)")
    w.add_argument("--port", required=True); w.add_argument("--block", type=int, required=True)
    g = w.add_mutually_exclusive_group(required=True)
    g.add_argument("--data", help="file with exactly 512 bytes")
    g.add_argument("--pattern", type=lambda x: int(x, 0), help="fill byte, e.g. 0xA5")
    wd = sub.add_parser("writedisk", help="write a whole .po image block by block (M5)")
    wd.add_argument("--port", required=True); wd.add_argument("--image", required=True)
    f = sub.add_parser("format", help="low-level format on the board (M5)")
    f.add_argument("--port", required=True)
    f.add_argument("--cyl", type=int); f.add_argument("--side", type=int)
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
    if args.cmd == "readdisk":
        from .serialio import Console, ConsoleError
        try:
            con = Console(args.port)
            img, tracks, mean_ns, backlog, ms = parse_disk(con.readdisk())
            Path(args.out).write_bytes(img)
            bad = [(t, s, st) for (t, s, st, _) in tracks if st not in (0x00, 0xE7)]
            soft = [(t, s) for (t, s, st, _) in tracks if st == 0xE7]
            print(f"readdisk: {ms} ms, decode {mean_ns} ns/interval (mean), "
                  f"ring backlog max {backlog} of 8192, "
                  f"{len(bad)} failed track sides, {len(soft)} with soft errors -> {args.out}")
            if bad:
                print("failed:", bad[:20])
            ok = not bad
            if args.compare:
                ref = Path(args.compare).read_bytes()
                diff = [k for k in range(gcr.TOTAL_BLOCKS) if img[k * 512:(k + 1) * 512] != ref[k * 512:(k + 1) * 512]]
                print(f"compare vs {args.compare}: {len(diff)} blocks differ", diff[:40])
                ok = ok and not diff
            return 0 if ok else 1
        except (CaptureError, ConsoleError) as e:
            print(f"readdisk failed: {e}")
            return 2
    if args.cmd == "readblk":
        from .serialio import Console, ConsoleError
        try:
            con = Console(args.port)
            block, status, soft, data = parse_blk(con.readblk(args.block))
            if block != args.block:
                print(f"readblk: got block {block}, expected {args.block}")
                return 2
            if status not in (0x00, 0xE7):
                print(f"readblk: block {block} has error status {status:02x}")
                return 1
            print(f"block {block}: status {status:02x} soft {soft:02x}")
            if args.compare:
                ref = Path(args.compare).read_bytes()
                same = data[gcr.TAG_LEN:] == ref[block * 512:(block + 1) * 512]
                print("matches reference" if same else "DIFFERS from reference")
                return 0 if same else 1
            return 0
        except (CaptureError, ConsoleError) as e:
            print(f"readblk failed: {e}")
            return 2
    if args.cmd == "writeblk":
        from .serialio import Console, ConsoleError
        data = Path(args.data).read_bytes() if args.data else bytes([args.pattern & 0xFF]) * 512
        if len(data) != 512:
            print("writeblk: data must be exactly 512 bytes")
            return 2
        try:
            r = parse_wblk(Console(args.port).writeblk(args.block, data))
        except (CaptureError, ConsoleError) as e:
            print(f"writeblk failed: {e}")
            return 2
        if r.block != args.block or r.crc != zlib.crc32(data):
            print("writeblk: transfer check failed (block or CRC)")
            return 2
        print(f"block {r.block}: status {r.status:02x} soft {r.soft:02x} tries {r.tries} "
              f"fire {r.fire_us} us backlog {r.backlog}")
        return 0 if r.status in (0x00, 0xE7) else 1
    if args.cmd == "writedisk":
        from .serialio import Console, ConsoleError
        img = Path(args.image).read_bytes()
        if len(img) != gcr.TOTAL_BLOCKS * 512:
            print(f"writedisk: {args.image} is {len(img)} bytes, expected {gcr.TOTAL_BLOCKS * 512}")
            return 2
        con = Console(args.port)
        t0, soft, fire_max = time.monotonic(), 0, 0
        try:
            for t in range(gcr.TRACKS):
                for s in range(gcr.SIDES):
                    for sec in range(gcr.sectors_per_track(t)):
                        b = gcr.block_number(t, s, sec)
                        data = img[b * 512:(b + 1) * 512]
                        r = parse_wblk(con.writeblk(b, data))
                        if r.block != b or r.crc != zlib.crc32(data):
                            print(f"writedisk: block {b}: transfer check failed")
                            return 2
                        if r.status not in (0x00, 0xE7):
                            print(f"writedisk: block {b} (t{t} s{s} sec{sec}): status {r.status:02x} after {r.tries} writes")
                            return 1
                        soft += r.status == 0xE7
                        fire_max = max(fire_max, r.fire_us)
                    print(f"TS {t} {s} ok")
        except (CaptureError, ConsoleError) as e:
            print(f"writedisk failed: {e}")
            return 2
        print(f"writedisk: {gcr.TOTAL_BLOCKS} blocks in {time.monotonic() - t0:.1f} s, "
              f"{soft} with soft errors, fire max {fire_max} us")
        return 0
    if args.cmd == "format":
        from .serialio import Console, ConsoleError
        if (args.cyl is None) != (args.side is None):
            print("format: give both --cyl and --side, or neither")
            return 2
        line = "format" if args.cyl is None else f"format {args.cyl} {args.side}"
        try:
            lines = Console(args.port).cmd(line, timeout=900.0,
                                           on_line=lambda s: print(s) if s.startswith("FMT") else None)
        except ConsoleError as e:
            print(f"format failed: {e}")
            return 2
        end = next((e for e in map(parse_format_end, lines) if e), None)
        if end is None:
            print("format: no FORMAT END line")
            return 2
        print(f"format: status {end[0]:02x}, {end[1]} ms")
        return 0 if end[0] in (0x00, 0xE7) else 1
    return _disk(args)


if __name__ == "__main__":
    sys.exit(main())
