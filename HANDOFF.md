# Handoff

Last updated: 2026-10-10, end of session.

## Where things stand

- M0 to M4 done. `main` at `5b00a58`, no remote. Branch `m4-board-read` merged
  (fast-forward) and deleted.
- M4 retested with Drive A mounted in a bracket: whole disk byte-identical to
  REF800K in 26.1 s, decode 1179 ns/interval, backlog max 36 of 8192
  (`docs/build_log/m4.md`, "Retest with the mechanism in a bracket").
- bytehamr.com: M4 log published ("M4: the board reads the disk itself"),
  Current state and Open problems updated. The bracket retest is not in the
  public log yet (optional short append).
- Untracked `blinky/` is not part of this work; leave it alone.

## Bench state at shutdown

- Pico 2 flashed with `main` (M4 firmware), verified.
- Drive A in the bracket, REF800K inserted, tab protected.
- Head recalibrated to track 0; motor off, `enbl off`, `oe off`, server off.
- Pico console is `/dev/cu.usbmodem114401`; the Debug Probe is `114202`.
- Power-down order still applies: mechanism supply off before any Pico USB
  unplug.

## Bench notes from this session

- First `make flash` after the bracket mount failed before erasing anything:
  `failed to exit flash XIP mode`, QSPI flash ID read back garbled
  (`0x0316ef`, `0x16ef00`) at 5 MHz and 1 MHz SWD. A Pico power cycle fixed
  it. If it repeats, power cycle first, then look at SWD/3V3 wiring.
- Host tools run as `cd tools && PYTHONPATH=. .venv/bin/python -m a2m.cli ...`.
  `readblk`/`readdisk` need `server on` first; send `server off` after.

## Next: M5, write and format

Brainstorming started (architectural path: questions, design, spec, plan).
No questions asked yet. Context already gathered:

- Exit criteria (design spec sec. 4.2): sector write + verify on a scratch
  disk; a disk formatted by our board reads correctly in the working UniDisk
  (normal cable use only, hands-off rule).
- Design spec sec. 2.4: decode live until the target header passes, assert
  /WRTGATE after `DE AA` and the gap, PIO streams the encoded data field from a
  DMA buffer to WRTDATA, deassert, verify on the next revolution. Per-sector
  writes only (no index pulse).
- Sec. 2.5: format writes every header + data of each track, zones
  12/11/10/9/8, 4:1 interleave, double-sided. Write-protect returns `$2B`.
- Pins: /WRTGATE GP18, WRTDATA GP19, both on U2 (A->B), 10k pull-ups on the
  B side. `hw.c` has a static assert that the mech bus never drives /WRTGATE;
  M5 lifts that deliberately.
- Open questions for the brainstorm: write precompensation (no doc found),
  WRTDATA polarity/encoding (transition per 1 bit?), gap lengths and sync
  bytes for format, header-to-data gap timing, core 1 headroom with encode,
  core 1 stack size (deferred from M4 reviews), scratch disk choice (never
  write REF800K).
- Sources to check: `docs/private/bitsavers_sony/` (800K ERS 699-0321 write
  timing), `IWM_undoco_features.pdf` and the IWM spec (write windows, see
  `docs/iwm_docs_index.md`), tashnotes, `liron.asm` write/format paths as the
  oracle.
- Per CLAUDE.local.md: at M5 start, give the shot list (write + verify
  console; a board-formatted disk catalogued in the working UniDisk).
