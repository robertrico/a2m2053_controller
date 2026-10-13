# M7: the floppy behind SmartPort (design)

Status: implemented and passed on the bench 2026-10-12 (`docs/build_log/m7.md`).
Written without Rico in the loop, built as if the
J2 mechanism breakout is wired (it lands 2026-10-13). **[choice]** marks
calls that are Rico's to overrule. His brief, as for M6: functionality,
timing, does it work; the bench is the test.

Parent spec: `2026-10-07-a2m2053-controller-design.md` (sec. 1, 2.6, 3, 4).
Builds on M5 (mechanism server, `docs/build_log/m5.md`) and M6 (SmartPort
engine, `2026-10-12-m6-smartport-ramdisk-design.md`).

## 1. Goal

The Liron card reads and writes a real 800K floppy through our board.

**Pass (parent spec M7):**
1. `PR#5` boots ProDOS from a real 800K floppy in drive A through the board.
2. A comparison program (sec. 8) gives the same answers on our board and on
   the working UniDisk: STATUS `$00`/`$03`/`$05`, blocks 0-2, an out-of-range
   block, a bad status code, no disk, write-protected.
3. Worst-case hold-off measured: the longest command (seek across the disk
   plus retries) and what the card did meanwhile.

Out of scope: daisy chain (M8), disk-swap reporting `$2E` (extended calls
only), downloaded code, Mac formats. Whole-disk copy speed (M9).

## 2. What changes

```
core 0                                             core 1 (M5 mechanism server)
  sp_bus (M6) -> sp_dev -> sp_store_t                 REQ_READ / REQ_WRITE / REQ_FORMAT_TS (M5)
                             |- ramdisk store (M6)    REQ_MEDIA  (new): disk in, write-protect
                             '- floppy store --ipc-->  REQ_EJECT  (new)
                                                      idle: motor/LED off after 1 s, eject button
```

- `sp_dev` stops owning a `ramdisk_t` and calls a `sp_store_t` (read, write,
  format, eject, media). The M6 RAM disk becomes one store; the floppy is the
  other. `sp disk ram|floppy` on the console picks one. **[choice]** Floppy
  at boot.
- The floppy store runs on core 0 and turns each call into an IPC request to
  core 1, then waits for the reply. ACK stays low the whole time (M6
  engine), which is how the card is held off (sec. 4).
- Core 1 runs the M5 server from boot (U2/U4 enabled, /ENBL low, motor off).
  M5 console mechanism commands still need `server off` first.

## 3. Behaviour (drive ROM as oracle)

| Call | Floppy store does | Status to the host |
|---|---|---|
| READBLOCK n | `REQ_READ` | `$00` with data; `$2D` n >= 1600; `$2F` no disk; `$27` read failed |
| WRITEBLOCK n | `REQ_WRITE` (12 tag bytes zero, as `cmd_write_block`) | `$00`; `$2B` write-protected; `$2D`; `$2F`; `$27` |
| FORMAT | 160 x `REQ_FORMAT_TS`, track 0 side 0 first | `$00`; `$2B`; `$2F`; `$27` on the first track side that fails |
| CONTROL `$04` eject | `REQ_EJECT` (motor off, eject, clears the track buffer) | `$00`; `$27` if the disk did not come out (ROM `Se51b`) |
| STATUS `$00`/`$03` | `REQ_MEDIA` | general byte `$F8`; `$E8` no disk; `$FC` write-protected (ROM `Leda3`). Status code stays `$00`. |
| STATUS `$05` | none | 8 bytes: byte 1 = soft flags of the last command, byte 2 = tries, rest 0 **[choice]** (ROM `Led96` fields `$56-$5D`) |

Soft errors: the ROM sends `$E7` (status `$67`, counted a success by the
card) when `Z57` is non-zero after a read: data errors (`$08`/`$10`), an
address bit slip (`$20`), a wrong cylinder (`$04`). We send `$67` when the M5
reply carries `SOFT_BADDATA`, `SOFT_BADADDR` or `SOFT_FOREIGN`. A recal alone
(`Le4f7` leaves `Z57`) and an overrun (ours) report `$00`. Writes never send
`$67`. The card treats `$40-$7F` as success (`liron-if.asm` Lcd9f).

Check order matches `Le8b2`: no disk, then (writes) write-protect, then the
block range. A failed FORMAT returns `$27` but does not eject the disk (the
ROM ejects it, `Le30c`). **[choice]**

Eject button (GP22, switch to GND): **off until `ejbtn on`** **[choice]**.
How CN105-108 reaches GP22 is not settled (`pin_budget.md` has it direct in
one table and on U5 in another); through U5 with an open input it would
float and eject disks. When on, core 1 latches a press held 20 ms across idle
samples (a request in between restarts it) and ejects on release.

LED (GP26, through U4): on while the motor runs. **[choice]** The ROM drives
its LED from the gate array; ours follows the motor.

## 4. Hold-off and timing

The card's reply wait has no timeout (`smartport_bus_read_packet` Lc97d), so
any seek fits. The bounds that remain are ours:

| Wait | Bound | Source |
|---|---|---|
| One READ/WRITE request | 5 s cap + up to 2 s overshoot (M5 `SRV_REQ_TIMEOUT_MS`) | M5 |
| Core 0 waiting for a READ/WRITE reply | cap + 3 s, then `$27` | M5 console path |
| One FORMAT track side | `SRV_FORMAT_TS_WAIT_MS` (41 s) | M5 |
| Whole FORMAT | 160 track sides; about 1.5 min typical [estimate] | M5 bench: a track side formats + verifies in well under a second [guess] |

Expected normal times [estimate, to measure]: a READ on the buffered track
side is under 1 ms; a READ that moves the head is seek (up to 79 steps) +
zone settle + one revolution, about 200-400 ms; a WRITE adds the header wait,
the write and a verify revolution, about 0.5 s (M5 measured).

The bench measures the hold-off directly: CH2 ACK low time on the scope, and
`splog` prints the core-0 wait per command (new field `ms=`).

While core 0 waits on core 1 it watches the bus for a reset (PH0+PH2). On a
reset it stops waiting at once, releases the bus and clears the ID, as a real
drive does (its reset is hardware, RSTCAP). Without that, the card's single
INIT try after a Ctrl-Reset would meet our ACK still low and the board would
drop off the bus. Core 1 finishes what it was doing; its late reply is
dropped by the request's sequence number. Any wait that times out no longer
locks the floppy out either: replies are matched by sequence number, not by
block.

## 5. IPC additions

- `REQ_MEDIA`: reply `status` = `ST_OK`, `ST_NODISK` or `ST_WRPROT` from two
  status reads (CSTIN, WRPROT). Takes microseconds.
- `REQ_EJECT`: `mech_eject`, motor off, track buffer dropped. Reply
  `ST_OK` or `ST_IOERR` (the mechanism did not report the disk out).
- The core-0 request helper moves from `main.c` into the floppy store, with
  the stale-reply filter (block tag) M5 already has.

## 6. Units

| Unit | Files | Host-tested |
|---|---|---|
| store interface | `sp_store.h` | - |
| RAM disk store | `ramdisk.c` (adapter added) | yes (existing + store calls) |
| floppy store | `fstore.c/.h`: calls, status mapping, FORMAT loop, timeouts | yes, against the M5 server on the fake drive (synchronous transact) |
| sp_dev | store calls replace `ramdisk_t` | yes |
| mech server | `mech_server_media`, `mech_server_eject`, eject-button latch in idle | yes (fake drive) |
| end to end | `test_sp_floppy.c`: simulated Liron card -> sp_bus -> sp_dev -> fstore -> mech server -> fake drive | yes |
| main/core 1 | REQ_MEDIA, REQ_EJECT, LED, server from boot, `sp disk` | build |

## 7. Tests

- Floppy store: read hit/miss, no disk `$2F`, bad block `$2D`, write OK and
  write-protected `$2B`, FORMAT on the fake drive then read back zeros, eject,
  media states, soft error mapping (`$67` only for recal/foreign), core-1
  timeout -> `$27`.
- sp_dev: STATUS general byte follows media (`$F8`/`$E8`/`$FC`), `$05` bytes.
- End to end on the simulated card: boot sequence reads block 0 off the fake
  floppy; a WRITEBLOCK lands on the fake disk; a no-disk READ returns `$2F`;
  the card keeps waiting through a 400 ms seek (no retries, no `late`).

## 8. Comparison program (oracle)

`tools/apple/SPCOMPARE.bas`, Applesoft, tokenized onto the M6 RAM disk image
with AppleCommander (`--basic`). It POKEs a 10-byte caller at `$300`
(`JSR` to the slot 5 SmartPort entry, `$C500 + PEEK($C5FF) + 3`, inline
command and parameter list at `$310`, result in A to `$30F`) and prints, per
call, the error code and the reply bytes:

1. STATUS unit 1 code `$00` (4 bytes), `$03` (25 bytes), `$05` (8 bytes).
2. READBLOCK unit 1 blocks 0, 1, 2: first 8 bytes and an additive checksum.
3. READBLOCK block 1600 (expect `$2D`, or `$2F` with no disk), STATUS code
   `$01` (expect `$21`).

It has no WRITEBLOCK or CONTROL call: a write test on the oracle drive would
change the disk. `$2B` and eject are compared by hand (sec. 9).

Run it on our board and on the working UniDisk with the same disk, then with
no disk and with the tab set. The screens must match except where the spec
says otherwise (our STATUS `$05` content).

Getting it onto the IIe: boot the M6 RAM disk (`sp disk ram`, `PR#5`),
`LOAD SPCOMPARE`, then `sp disk floppy` from the console and `RUN`. For the
UniDisk: `SAVE` it to a scratch floppy first.

## 9. Bench plan (Rico)

Prerequisite: J2 wired and checked (HANDOFF step 2), read scan clean at 5.0 V
on drive A, REF800K write-protected.

1. Flash. Power order: Pico, board (+5 then +12), IIe. Console `sp`: `disk=floppy`,
   `server on`.
2. REF800K in drive A. `PR#5`: REF800K has no PRODOS file, so the card loads
   block 0 and stops with ProDOS's "UNABLE TO LOAD PRODOS". The `SPL` lines
   show READBLOCK 0 (and 1) served from the floppy. That proves the path.
3. A bootable 800K floppy (disk 2, rewritten with the M6 image by our board in
   RAM-to-floppy copy, or any ProDOS 800K disk): `PR#5` boots it.
4. Scope: CH2 ACK during a READ that moves the head; measure the low time.
5. Comparison program on both drives (sec. 8).
6. Eject from the button and from software (`CONTROL $04` via the comparison
   program or the Finder-style eject in a utility, if available).

Writing to REF800K is never part of the plan; it stays write-protected.

## 10. Risks

1. **No bootable 800K floppy on hand** [guess]: REF800K has no PRODOS. Step
   2 still proves reads; step 3 needs a ProDOS disk. Our board can write one:
   a console command copies the M6 image to the floppy (`imgcopy`, sec. 11).
2. **Core 1 shares flash with core 0**: the M5 server runs from flash. Fine for
   reads; if `ack_us` grows when the server is busy, check flash contention.
3. **Eject button wiring unknown** (CN105-108): the firmware reads GP22 only.

## 11. Console

- `sp` adds `disk=ram|floppy`, `server=on|off`, the last command's core-1 time.
- `sp disk ram|floppy`.
- `imgcopy`: writes the flash image (M6 `m6_boot800k.po`) to the floppy, block
  by block through `REQ_WRITE` (refuses when write-protected). About 13 min
  at M5 write speed [estimate]. This makes a bootable test floppy from the
  image we already have. **[choice]**
