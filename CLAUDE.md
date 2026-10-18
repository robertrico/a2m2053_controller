# A2M2053 controller (UniDisk 3.5 replacement board)

Replacement controller board for the Apple UniDisk 3.5 (A2M2053). The board
speaks SmartPort to an Apple IIe (Liron card) and drives the stock Sony 800K
mechanism directly.

Design spec: `docs/superpowers/specs/2026-10-07-a2m2053-controller-design.md`.
Research digest: `docs/research_notes.md`. Start there.

## Hard rules

- **The working UniDisk is hands-off.** Never open it, never probe it. Normal
  cable use only (Apple <-> drive, or daisy-chained behind our board). Its
  CXD1013 gate array has no datasheet and no spare.
- **Test beds:** two drives with dead boards and good mechanisms. Probe those
  freely.
- **Scope: Apple IIe + 800K double-sided ProDOS disks.** No Mac disks
  (524-byte blocks, Mac interleave) and no 400K single-sided disks. They show
  up in Apple docs and the ROM but were never used on the IIe.
- **Practical over period-correct.** Our own C/ARM on an RP2350. The original
  drive ROM (`liron.asm`) is a test **oracle** for protocol, status, errors
  and timing. Never copy its structure, and never emulate it in v1.
- **Proven sources:** `~/Development/project_byte_hamr` only (PicoPort
  SmartPort device, `iwm_asic.v`, `liron.asm`, `liron-if.asm`). Do **not** use
  `~/Development/ironsteel-fpga-disk-controller` for research (defunct IWM
  attempt). Only the 8T245 datasheet came from there.
- **Level shifters are SN74LVC8T245, 8 channels per board, one DIR and one
  /OE per board.** Every signal on a board shares direction and enable. Group
  signals by (direction, when-enabled), never by function. Plan:
  `docs/pin_budget.md`.
- **Cost matters.** The only spare original board has sat unsold on eBay at
  $150 for 7+ months. Every part choice keeps the drop-in board well under that.
- **No CXD1013 or CX23077A reverse engineering.** We talk to the mechanism
  through its 20-pin connector (ERS 699-0321) and to the host through the DB-19.

## Hardware

- MCU: RP2350A on a Pico 2 for the prototype. Move to an FPGA only if timing
  forces it.
- 5x 8T245 boards (U1-U5). ACK rides U3 with host RD (no 74LVC1G07). 25 of 26 GPIO used.
- Mechanism: Sony 800K, CX23077A (logic) + CX20185 (R/W amp). Self-regulating
  speed. CN104 pin 20 (/PWM) tied to +5V.
- Host: Liron DB-19 / CN102. ACK is on CN102 pin 20. The rear daisy-chain port
  needs PH3_OUT, ENBL_OUT and LASTONE/ (rear pin 16, 2k pull-up).

## Layout

| Path | What |
|---|---|
| `docs/research_notes.md` | Facts with sources: [doc] / [fw] / [photo] / [measured] / [guess] |
| `docs/level_shifting.md`, `docs/pin_budget.md` | Signal directions, shifter grouping, GPIO map |
| `docs/iwm_docs_index.md` | Index of the scanned IWM docs, with page cites |
| `docs/private/bitsavers_sony/` | Mirror of bitsavers Sony/Apple drive docs (800K ERS, schematics, command codes) |
| `docs/photos/` | Our own photos (public) |
| `docs/private/` | Third-party docs, datasheets and photos. **Git-ignored, never in history.** Where to get each one: `docs/SOURCES.md` |
| `docs/superpowers/specs/` | Design spec(s) |
| `docs/build_log/` | Per-milestone notes and measurements (create as we go) |
| `hardware/kicad/` | KiCad project for the PCB (outline only so far) |
| `hardware/bom.md`, `hardware/bom.csv` | Bill of materials with part numbers, vendors and cost |
| `FABRICATION.md` | PCB phase: hand-solder rule, 8T245 package, board requirements from the bench |

Tag every new fact in `research_notes.md` with its source. Mark guesses
`[guess]` until bench-confirmed, then change the tag to `[measured]`.

## Public repo hygiene

This repo will be public. Before any `git push` to a public remote, sort the
third-party material:

- Anything not ours (Apple/Sony docs, TI datasheets, eBay photos, the
  bitsavers mirror) goes in `docs/private/` (git-ignored) with a source entry
  in `docs/SOURCES.md`. Link to it, never commit it. Never `git add -f` there.
- Our own photos, notes, specs, code and measurements are fine to publish.
- Strip EXIF (GPS!) from every photo before committing it. Phone JPEGs carry the
  location. Bake the orientation into the pixels first, then drop APP1/APP13.
- Never commit Apple ROM binaries. `liron.asm` stays in project_byte_hamr. We
  reference it, we don't copy it.

## Conventions

- Caveman/terse mode is Rico's chat preference. Docs, commits and logs use
  plain technical English.
- Never flash, power or connect hardware without Rico saying so. Ask before
  any action on the bench.
- Commit messages: Conventional Commits.
