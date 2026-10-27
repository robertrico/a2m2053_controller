# A2M2053 controller (UniDisk 3.5 replacement board)

Replacement controller board for the Apple UniDisk 3.5 (A2M2053). The board
speaks SmartPort to an Apple IIe (Liron card) and drives the stock Sony 800K
mechanism directly. The board is called Disk Hamr.

Design spec: `docs/superpowers/specs/2026-10-07-a2m2053-controller-design.md`.
Research digest: `docs/research_notes.md`. Build and flash: `README.md`,
"Getting started". Start there.

## Hard rules

- **Never probe a working UniDisk.** Its CXD1013 gate array has no datasheet
  and no spare, and one wrong probe kills it. Normal cable use only (Apple
  <-> drive, or daisy-chained with this board). Do all bench work on a drive
  whose controller board is already dead; the mechanism is what matters.
- **Scope: Apple IIe + 800K double-sided ProDOS disks.** No Mac disks
  (524-byte blocks, Mac interleave) and no 400K single-sided disks. They show
  up in Apple docs and the ROM but were never used on the IIe.
- **Practical over period-correct.** Our own C/ARM on an RP2350. The original
  drive ROM is a test **oracle** for protocol, status, errors and timing.
  Never copy its structure, and never emulate it.
- **Reference sources.** The SmartPort packet layout follows PicoPort in the
  Byte Hamr repo (https://github.com/robertrico/project_byte_hamr,
  `software/SMARTPORT/`). The drive ROM and Liron card ROM disassemblies are
  Eric Smith's a2-liron (https://github.com/brouhaha/a2-liron). Drive status
  bits, the 20-pin connector and the GCR checksum come from tashnotes
  (https://github.com/lampmerchant/tashnotes). Apple and Sony drive docs are
  listed in `docs/SOURCES.md`.
- **Level shifters are SN74LVC8T245, 8 channels per chip, one DIR and one
  /OE per chip.** Every signal on a chip shares direction and enable. Group
  signals by (direction, when-enabled), never by function. Plan:
  `docs/pin_budget.md`.
- **Cost matters.** The only spare original board has sat unsold on eBay at
  $150 for months. Every part choice keeps the drop-in board well under that.
- **No CXD1013 or CX23077A reverse engineering.** We talk to the mechanism
  through its 20-pin connector (ERS 699-0321) and to the host through the DB-19.
- **Bench safety.** Never flash, power or connect hardware without the person
  at the bench saying so. Ask before any action that touches a drive, the
  Apple or the Liron card.

## Hardware

- MCU: RP2350A on a Pico 2, soldered flat by its castellations on the PCB.
- 5x SN74LVC8T245 (U1-U5). ACK rides U3 with host RD (no 74LVC1G07). 25 of
  26 GPIO used.
- Mechanism: Sony 800K, CX23077A (logic) + CX20185 (R/W amp). Self-regulating
  speed. CN104 pin 20 (/PWM) tied to +5V.
- Host: Liron DB-19 / CN102. ACK is on CN102 pin 20. The rear daisy-chain port
  needs PH3_OUT, ENBL_OUT and LASTONE/ (rear pin 16, 2k pull-up).

## Layout

| Path | What |
|---|---|
| `README.md` | Overview, specs, build and flash, console |
| `docs/research_notes.md` | Facts with sources: [doc] / [fw] / [photo] / [measured] / [guess] |
| `docs/level_shifting.md`, `docs/pin_budget.md` | Signal directions, shifter grouping, GPIO map |
| `docs/iwm_docs_index.md` | Index of the scanned IWM docs, with page cites |
| `docs/SOURCES.md` | Where every third-party document came from, and how to rebuild `docs/private/` |
| `docs/private/` | Third-party docs, datasheets and photos. **Git-ignored, never in history.** |
| `docs/photos/`, `docs/captures/` | Our own photos, scope captures and console logs (public) |
| `docs/superpowers/specs/`, `docs/superpowers/plans/` | Design specs and the milestone plans they were built from |
| `docs/build_log/` | Per-milestone notes and measurements, M0 to M9 |
| `docs/manual/` | Owner's manual for the finished board |
| `hardware/kicad/` | KiCad project for Disk Hamr rev 1.0 |
| `hardware/bom.md`, `hardware/bom.csv` | Bill of materials with part numbers, vendors and cost |
| `FABRICATION.md` | PCB design rules: hand-solder rule, packages, board requirements from the bench |
| `PCB_BRINGUP.md` | Bring-up plan for an assembled board, four gates, with the prototype-to-PCB channel map |
| `firmware/`, `tools/` | Pico firmware with host tests; Python bench tools |

Tag every new fact in `research_notes.md` with its source. Mark guesses
`[guess]` until bench-confirmed, then change the tag to `[measured]`.

## Public repo hygiene

This repo is public. Before any push:

- Anything not ours (Apple/Sony docs, TI datasheets, eBay photos, the
  bitsavers mirror, vendor 3D models) goes in `docs/private/` or
  `hardware/kicad/3d/` (both git-ignored) with a source entry in
  `docs/SOURCES.md`. Link to it, never commit it. Never `git add -f` there.
- Our own photos, notes, specs, code and measurements are fine to publish.
- Strip EXIF (GPS!) from every photo before committing it. Phone JPEGs carry the
  location. Bake the orientation into the pixels first, then drop APP1/APP13.
- Never commit Apple ROM binaries or the ROM disassemblies. Reference a2-liron,
  don't copy it.

## Conventions

- Docs, commits and logs use plain technical English.
- Commit messages: Conventional Commits.
- `CLAUDE.local.md` (git-ignored) holds the owner's personal workflow notes
  and bench state. If it is missing you are not on the owner's machine; the
  rules above still apply.
