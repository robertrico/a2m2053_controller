# M7 implementation plan: the floppy behind SmartPort

Spec: `docs/superpowers/specs/2026-10-12-m7-floppy-behind-smartport-design.md`.
Branch `m7`. Executed inline; every task ends with host tests green and a
commit.

1. **Store interface.** `sp_store.h` (read, write, format, eject, media).
   `sp_dev` uses it; `ramdisk` gets an adapter. Existing sp_dev/sp_bus tests
   move to the adapter unchanged in behaviour; STATUS general byte from media.
2. **Mechanism server additions.** `REQ_MEDIA`, `REQ_EJECT`,
   `mech_server_media`, `mech_server_eject`, eject-button latch in
   `mech_server_idle` (button read through a callback). Fake-drive tests.
3. **Floppy store.** `fstore.c/.h` over a transact callback (core 0 -> core 1
   request with timeout). Status mapping (ST_* -> SmartPort codes, `$67` for
   recal/foreign), FORMAT loop, STATUS `$05` bytes. Tests with a synchronous
   transact into the M5 server on the fake drive.
4. **End to end.** `test_sp_floppy.c`: the M6 simulated Liron card against
   sp_bus -> sp_dev -> fstore -> mech server -> fake drive.
5. **Firmware integration.** Core 1 handles the new ops and the LED; server
   on at boot; `sp disk ram|floppy`; `imgcopy`; `ms=` in splog; `sp` fields.
6. **Comparison program.** `tools/apple/SPCOMPARE.bas`; add it to the M6 image
   with AppleCommander; document.
7. **Review and docs.** Code review pass (subagent), fixes, build log section,
   HANDOFF.
