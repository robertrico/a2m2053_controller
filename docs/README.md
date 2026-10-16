# docs

Notes, measurements and design material for the A2M2053 (UniDisk 3.5)
replacement controller.

| File | What |
|---|---|
| `research_notes.md` | Facts with sources ([doc], [fw], [photo], [measured], [guess]). Start here. |
| `level_shifting.md` | Signal directions and 8T245 grouping |
| `pin_budget.md` | GPIO map, rear port, front panel |
| `iwm_docs_index.md` | Index of the IWM documents with page cites |
| `superpowers/specs/` | Design spec and milestone plan |
| `build_log/` | Per-milestone notes and measurements (M0-M9) |
| `captures/` | Scope shots, Apple screens and console logs, by milestone |
| `photos/` | Our own bench photos (EXIF stripped) |
| `SOURCES.md` | Where the third-party documents came from |

Apple and Sony documents, datasheets and photos of other people's boards were
used during the work but aren't ours to host. They live in `private/`, which
git ignores. `SOURCES.md` says where to get each one.

No schematic for the UniDisk 3.5 controller board has ever been released, and
the Sony CXD1013 gate array has no public datasheet.
