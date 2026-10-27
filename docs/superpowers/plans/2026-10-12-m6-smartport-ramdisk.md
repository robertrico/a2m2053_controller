# M6 implementation plan: SmartPort RAM disk

Spec: `docs/superpowers/specs/2026-10-12-m6-smartport-ramdisk-design.md`.
Branch: `m6`. Executed inline (owner away; bench last). Every task ends with
`make -C firmware test` green and a commit. Host tests use the existing
`firmware/test/test.h` harness; PIO tests use pytest in `tools/`.

## Task 1: sp_wire (framing, decode, encode)

Files: `firmware/src/sp_wire.h`, `sp_wire.c`, `firmware/test/test_sp_wire.c`,
Makefile entry.

- `sp_framer_t`: IWM rule (skip 0s until a 1, take 8). `sp_framer_bit()`
  returns a byte or -1. `sp_rx_t` wraps it: hunt `C3`, collect wire bytes,
  know the expected length once ODDCNT/GRP7CNT are in, report
  `SP_RX_MORE / SP_RX_DONE / SP_RX_BAD`. Feeds from 8-bit chunks
  (MSB = earliest bit).
- `sp_decode()`: wire bytes after `C3` -> header fields + data bytes,
  checksum verified, `C8` checked.
- `sp_encode()`: header + data -> wire bytes with the drive ROM preamble
  (`FF 3F CF F3 FC FF 3F CF F3 FC FF C3`), checksum, `C8`.
- Tests: PR#5 measured prefix with junk bits in front; round trips 0, 4, 9,
  25, 512 bytes; a packet built by a C transcription of the card ROM's send
  path decodes with the right checksum; bad checksum, bad `C8`, truncated,
  absurd counts rejected; bit-level stream from `sp_encode` through the
  framer reproduces the packet.

## Task 2: ramdisk

Files: `ramdisk.h/.c`, `test_ramdisk.c`.

- `ramdisk_init(rd, base, base_len, slots, nslots)`; `read`, `write`,
  `format`, `used`. Block map `uint16_t map[1600]` (0 = unwritten).
- Tests: base read, zero past short base, write then read, rewrite reuses
  the slot, full -> error, format hides base and frees slots, bad block.

## Task 3: sp_dev

Files: `sp_dev.h/.c`, `test_sp_dev.c`.

- `sp_dev_t` holds ID, last flag, pass-through flag, counters, a backend
  (`ramdisk`), LASTONE/ reader callback.
- `sp_dev_accepts(dev, dest, type)`: ID filter (spec 6.1).
- `sp_dev_needs_data(cmd)`: `$02`, `$04`, `$09`.
- `sp_dev_execute(dev, cmd9, data, data_len, reply)`: fills reply status +
  data per spec sec. 6.
- `sp_dev_bus_reset(dev)`.
- Tests: every table row, param counts, INIT both LASTONE/ levels, filter.

## Task 4: sp_bus engine + simulated card

Files: `sp_bus.h/.c`, `test/sim_liron.h` (simulated card), `test_sp_bus.c`.

- HAL (`sp_hal_t`): `phases`, `wrreq`, `set_ack`, `set_enbus`, `rx_arm`,
  `rx_count` + buffer pointer, `tx_start`, `tx_busy`, `now_us`, `delay_us`,
  `irq_save/restore`, `lastone`.
- `sp_bus_poll()` runs one transaction when the RX buffer has bits or REQ is
  high with /WRREQ high (re-read). Sequences spec 3.3. Every wait has a
  timeout and checks for bus reset. Records ACK latency (`/WRREQ` rise to
  ACK low) per transaction for `splog`.
- Simulated card: state machine stepped by simulated time from every HAL
  call; sends packets as chunked FM bits at 3.911 us/cell, enforces 540 us
  and 105 us windows, reads our TX buffer back through `sp_wire`, mirrors
  card retries. Scenarios per spec sec. 7.

## Task 5: PIO programs and the cycle model

Files: `firmware/src/sp_fm.pio`, `tools/pio_model.py`,
`tools/tests/test_fm_pio.py`.

- `fm_rx` and `fm_tx` per spec 5.1/5.2 (pio_version 0, both on PIO0, under
  32 instructions together).
- `pio_model.py`: decodes PIO instruction words (JMP, WAIT, IN, OUT, PUSH,
  PULL, MOV, SET; delay; side-set off) and steps one SM with GPIO inputs as a
  function of time; autopush/autopull; FIFO lists.
- Test reads the words from the pioasm-generated header
  (`firmware/build/sp_fm.pio.h`; built by `make -C firmware`), runs RX
  against edge trains (random packets via a Python FM encoder, +-1.5 us
  jitter, +-1 % clock), checks bits; runs TX and checks edge times.

## Task 6: sp_hw (RP2350 HAL) + image + hw_init_safe

Files: `sp_hw.c/.h`, `sp_image.S`, `CMakeLists.txt`, `hw.c`.

- PIO0 SM0 `fm_rx` + DMA (8-bit reads of RXF0 into a 4 KB buffer, count
  from the channel's transfer count), SM1 `fm_tx` + DMA.
- ACK GP9 SIO, ENBUS GP11 SIO (ACK/RD set before /OE low).
- `hw.c`: RD idle high.
- `sp_image.S`: `.incbin` when `A2M_HAVE_IMAGE`, else empty symbol with
  length 0. CMake option `A2M_DISK_IMAGE`, message when missing.

## Task 7: main loop, console

Files: `main.c`, `console.c/.h`, `test_console.c`.

- Non-blocking line input; loop = `sp_bus_poll()` + one console char +
  one log line (only if USB has room).
- `sp`, `sp on|off`, `splog on|off` (spec sec. 8).
- Boot: SmartPort on.

## Task 8: image, build, review, docs

- Build `docs/private/ref/m6_boot800k.po` (AppleCommander, `-f
  ProDOS_2_4_1.po`), confirm it lists PRODOS + BASIC.SYSTEM.
- Full build with the image; check size, RAM use, PIO instruction count.
- Code review pass (subagent) on the M6 diff; fix findings.
- Docs: `docs/build_log/m6.md` "Firmware ready" section with the bench
  steps; HANDOFF.md resume order; research_notes host protocol facts tagged
  `[fw]`.
