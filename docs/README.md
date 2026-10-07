# docs

Reference material for the A2M2053 (UniDisk 3.5) replacement controller.

Files under `private/` are third-party and git-ignored (not in history).
Get them from the sources in `SOURCES.md`.

| File | What | Notes |
|---|---|---|
| `private/Apple_UniDisk_3.5_Owners_Manual.pdf` | Owner's manual, 030-1151-A (1985) | Scanned, no text layer. User-facing only: install, daisy chain, eject. No electrical content. |
| `private/Apple_UniDisk_Technical_Notes.pdf` | Apple II Tech Notes, UniDisk 3.5 #1-#5 (rev. Nov 1988) | Text layer OK. Summary in `research_notes.md`. |
| `private/photos/661-0307_*_ebay.webp` | Controller board, service exchange module P/N 661-0307 | eBay listing photos, not our unit. Top, bottom, CXD1013 close-up. |
| `private/photos/IF-83_802_top_unshielded.webp` | IF-83 board, shield removed | Shows 65C02, ROM, IWM (344-0041-B), SRAM. |
| `private/APPLE 2 IWM *.pdf`, `private/Apple 2 IWM Spec 1982.pdf`, `private/iwm_*.pdf`, `private/apple2_IWM_PATENT1988.pdf` | IWM specs, memos, Parker 1994 notes, patent | Scans. Indexed with page cites in `iwm_docs_index.md`. |
| `private/Apple II IWM Port Info.pdf` | Misc TN#6, DB-19 port pinouts | Text. Liron pinout copied into `research_notes.md`. |
| `iwm_docs_index.md` | Per-doc summary + mechanism-relevant facts + "not found" list | |
| `research_notes.md` | Parsed facts from all of the above + the drive firmware disassembly | Start here. |

No schematic for the drive controller board has ever been released. The Sony
CXD1013 gate array has no public datasheet (Sony confirmed they have nothing).

## private/bitsavers_sony/

Mirror of bitsavers `pdf/apple/disk/sony/` items relevant to us: 800K mech
ERS (Aug85, Sep86), MP-F51W schematics (.tif), Apple drive command/status
codes, IWM software control, Apple 3.5 Drive schematic, 400K ERS, Liron board
photos, tashnotes connector/GCR notes. Digest in `research_notes.md` sec. 7.
