#!/usr/bin/env python3
"""M7 sentinel test disk (docs/build_log/m7.md): writes STARTUP.bas and the data files into
an output directory. mktest.sh puts them on an 800K ProDOS image.

STARTUP prints a sentinel line, BLOADs DATA1-4 (16 KB each, known pseudo-random content) at
$4000 and checks a 16-bit byte sum with a 6502 routine at $300. A FILLER file between DATA3
and DATA4 pushes DATA4 onto track 72 (zone 4), so the boot reads far seeks: DATA1-3 sit on tracks 2-6."""
import sys
from pathlib import Path

N_FILES, FILE_LEN = 4, 16384
FILLER_BLOCKS = 1300

# 16-bit sum of 64 pages from $4000 into $06/$07 (zero page free under Applesoft).
SUM_ROUTINE = [
    0xA9, 0x00, 0x85, 0x06, 0x85, 0x07,        # $300 LDA #0, STA $06, STA $07
    0xA9, 0x00, 0x85, 0x08, 0xA9, 0x40, 0x85, 0x09,   # $306 ($08) = $4000
    0xA2, 0x40, 0xA0, 0x00,                    # $30E LDX #64 pages, LDY #0
    0xB1, 0x08, 0x18, 0x65, 0x06, 0x85, 0x06,  # $312 loop: LDA ($08),Y; CLC; ADC $06; STA $06
    0x90, 0x02, 0xE6, 0x07,                    # $319 BCC +2; INC $07
    0xC8, 0xD0, 0xF2,                          # $31D INY; BNE loop
    0xE6, 0x09, 0xCA, 0xD0, 0xED,              # $320 INC $09; DEX; BNE loop
    0x60,                                      # $325 RTS
]


def data(n):
    x = 0x1234 + 977 * n
    out = bytearray()
    for _ in range(FILE_LEN):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out.append((x >> 16) & 0xFF)
    return bytes(out)


def model_sum(b):
    return sum(b) & 0xFFFF


def main(outdir):
    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    sums = []
    for n in range(1, N_FILES + 1):
        d = data(n)
        (out / f"DATA{n}").write_bytes(d)
        sums.append(model_sum(d))
    (out / "FILLER").write_bytes(bytes((i * 7) & 0xFF for i in range(FILLER_BLOCKS * 512)))
    code = ",".join(str(b) for b in SUM_ROUTINE)
    bas = f"""10 HOME : PRINT "M7 SENTINEL 2026-10-13 /M7TEST": PRINT
20 FOR I = 0 TO {len(SUM_ROUTINE) - 1}: READ X: POKE 768 + I,X: NEXT
30 DATA {code}
40 D$ = CHR$ (4):F = 0
50 FOR N = 1 TO {N_FILES}: READ E
60 PRINT D$;"BLOAD DATA";N;",A16384"
70 CALL 768:S = PEEK (6) + 256 * PEEK (7)
80 IF S = E THEN PRINT "FILE ";N;" OK   SUM ";S
90 IF S < > E THEN PRINT "FILE ";N;" MISMATCH ";S;" WANT ";E:F = F + 1
100 NEXT
110 PRINT : IF F = 0 THEN PRINT "ALL {N_FILES} FILES OK"
120 IF F > 0 THEN PRINT F;" FILES BAD"
130 END
200 DATA {",".join(str(s) for s in sums)}
"""
    (out / "STARTUP.bas").write_text(bas)
    print("sums", sums)


if __name__ == "__main__":
    main(sys.argv[1])
