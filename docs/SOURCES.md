# Sources for third-party material

These files aren't ours, so they live in `docs/private/`, which git ignores and
which never enters history. To rebuild `docs/private/` on a fresh clone, fetch
each file from the source below.

## Asimov (ftp.apple.asimov.net)

Apple II documentation archive: https://www.apple.asimov.net/ (also
ftp://ftp.apple.asimov.net/). Rico found these there. Exact paths still to
be filled in.

| File (in `docs/private/`) | What |
|---|---|
| `Apple_UniDisk_3.5_Owners_Manual.pdf` | UniDisk 3.5 Owner's Manual, 030-1151-A |
| `Apple_UniDisk_Technical_Notes.pdf` | Apple II Tech Notes, UniDisk 3.5 #1-#5 |
| `Apple II IWM Port Info.pdf` | Apple II Misc Tech Note #6, IWM port pinouts |
| `APPLE 2 IWM INFO 198205.pdf` ... `APPLE 2 IWM INFO APPLE 2C.pdf` (8 files) | IWM memos and info, 1982-1984 |
| `APPLE 2 IWM PARKER 1994-02.pdf` | Parker IWM notes, 1994 |
| `Apple 2 IWM Spec 1982.pdf`, `APPLE 2 IWM SPEC 19820924.pdf` | IWM specification |
| `apple2_IWM_PATENT1988.pdf` | IWM patent |
| `iwm_19820924.pdf`, `iwm_19840426.pdf`, `iwm_discussion_19820531.pdf` | Same material, other scans (via project_byte_hamr) |

## bitsavers (mirror used: mirrors.apple2.org.za)

Base: https://mirrors.apple2.org.za/www.bitsavers.org/pdf/apple/disk/sony/
(origin: http://www.bitsavers.org/pdf/apple/disk/sony/)

| File (in `docs/private/bitsavers_sony/`) | URL suffix |
|---|---|
| `MP-F51_specs/699-0321_800K_Double-Sided_ERS_Aug85.pdf` | `MP-F51_specs/699-0321_800K_Double-Sided_ERS_Aug85.pdf` |
| `MP-F51_specs/669-0452-A_800K_Double-Sided_ERS_Sep86.pdf` | `MP-F51_specs/669-0452-A_800K_Double-Sided_ERS_Sep86.pdf` |
| `MP-F51_specs/MP-F51W_Int_800K_Schematic.tif` | `MP-F51_specs/MP-F51W_Int_800K_Schematic.tif` |
| `MP-F51_specs/MP-F51W-01_Ext_800K_Schematic.tif` | `MP-F51_specs/MP-F51W-01_Ext_800K_Schematic.tif` |
| `MP-F51_specs/MP-F51W-03_800K_Unified_Schematic.tif` | `MP-F51_specs/MP-F51W-03_800K_Unified_Schematic.tif` |
| `Apple_drive_command_and_status_codes.pdf` | same name |
| `Apple_3.5_Drive_Schematic.pdf` | same name |
| `Software_control_of_IWM.pdf` | same name |
| `IWM_undoco_features.pdf` | same name |
| `Mac_v_IIgs_sectors.pdf` | same name |
| `Hand_notes_on_floppy_stuff.pdf` | same name |
| `699-0285-A_400K_Disk_Dec84.pdf` | same name |
| `Liron/case_t.jpg`, `Liron/liron_t.jpg`, `Liron/liron_b.jpg` | `Liron/` |
| `MP-F51W-23/b.jpg`, `s.jpg`, `t.jpg` | `MP-F51W-23/` |
| `tashnotes_floppy/*.md` | `tashnotes_floppy/` (origin: https://github.com/lampmerchant/tashnotes) |

## Other

| File | Source |
|---|---|
| `datasheets/sn74lvc8t245.pdf` | Texas Instruments SN74LVC8T245 datasheet (SCES584B): https://www.ti.com/product/SN74LVC8T245 |
| `datasheets/TE_AMP_CT_292161_header_drawing.pdf` | TE Connectivity customer drawing C-292161 (AMP CT 2 mm post header, vertical), from the product page: https://www.te.com/en/product-292161-3.html (Documents, "Customer Drawing") |
| `photos/661-0307_*_ebay.webp` (3) | eBay listing for a spare 661-0307 board (not our unit) |
| `photos/IF-83_802_top_unshielded.webp` | IF-83 board with the shield removed. Found online, not Rico's. |
| `../hardware/kicad/3d/292161-2.step` | TE 292161-2 STEP model, SnapEDA/SnapMagic export (license next to it). Not in git. |
| `../hardware/kicad/3d/292161-3.step` | TE 292161-3 STEP model from https://www.te.com/en/product-292161-3.html (Documents, 3D model). Not in git. |

Our own photos (e.g. `docs/photos/mechanism_board_cx23077a_cx20185.jpeg`)
stay public in `docs/photos/`.
