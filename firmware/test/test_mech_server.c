#include <string.h>
#include "test.h"
#include "fake_drive.h"

static fake_drive_t D;
static mech_bus_t B;
static flux_src_t S;
static flux_tx_t T;
static mech_server_t SRV;

static void setup(void) {
    fake_drive_init(&D);
    B = fake_drive_bus(&D);
    S = fake_flux_src(&D);
    T = fake_flux_tx(&D);
    mech_server_init(&SRV, &B, &S, &T);
}

static bool data_ok(const reply_t *r) {
    int t, s, sec;
    if (!gcr_block_to_tss(r->block, &t, &s, &sec)) return false;
    for (int i = 0; i < GCR_SECTOR_LEN; i++)
        if (r->data[i] != fd_payload_byte(t, s, sec, i)) return false;
    return true;
}

static void test_miss_then_hit(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    CHECK(r.status == ST_OK && r.soft == 0 && r.block == 0 && data_ok(&r));
    CHECK(D.pos == 0);
    CHECK(mech_server_stats(&SRV)->intervals > 0);
    int strobes = D.step_strobes;
    mech_server_read(&SRV, 5, &r);                   // same track side: served from the buffer
    CHECK(r.status == ST_OK && data_ok(&r));
    CHECK(D.step_strobes == strobes && mech_server_stats(&SRV)->fills == 1);
    mech_server_read(&SRV, 1599, &r);               // track 79 side 1
    CHECK(r.status == ST_OK && data_ok(&r) && D.pos == 79 && D.side == 1);
}

static void test_no_disk(void) {
    setup();
    D.disk_in = false;
    reply_t r;
    mech_server_read(&SRV, 100, &r);
    CHECK(r.status == ST_NODISK && D.step_strobes == 0 && !D.motor);
}

static void test_bad_block(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 1600, &r);
    CHECK(r.status == ST_BADBLK && D.step_strobes == 0 && !D.motor);
}

static void test_retry_then_ok(void) {
    setup();
    D.bad_sector = 3; D.bad_revs = 2;
    reply_t r;
    mech_server_read(&SRV, 3, &r);                   // track 0 side 0 sector 3
    CHECK(r.status == ST_SOFT && (r.soft & SOFT_BADDATA) && data_ok(&r));
}

static void test_budget_exhausted(void) {
    setup();
    D.bad_sector = 3; D.bad_revs = 1000;
    reply_t r;
    uint64_t t0 = D.now_us;
    mech_server_read(&SRV, 3, &r);
    CHECK(r.status == ST_IOERR && (r.soft & SOFT_RECAL));
    for (int i = 0; i < GCR_SECTOR_LEN; i++) CHECK(r.data[i] == 0);
    CHECK(D.now_us - t0 <= (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u + 200000u);
    CHECK(D.now_us - t0 < 3000000u);                 // 12 revolutions x 183 ms + one recal
    mech_server_read(&SRV, 4, &r);                   // other sectors of that track were good
    CHECK(r.status == ST_OK && data_ok(&r));
}

static void test_wrong_track_reseeks(void) {
    setup();
    D.skew = 1;                                      // head lands one track off, once
    reply_t r;
    mech_server_read(&SRV, 24 * 2 * 10, &r);         // a block in track 10..
    CHECK(r.status == ST_SOFT && (r.soft & SOFT_FOREIGN) && data_ok(&r));
}

static void test_foreign_forever_fails(void) {
    setup();
    D.foreign_always = 50;
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    CHECK(r.status == ST_IOERR && (r.soft & SOFT_FOREIGN) && (r.soft & SOFT_RECAL));
    CHECK(D.starts == 4);
}

static void test_foreign_impossible_track(void) {
    setup();
    D.foreign_always = 100;
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    CHECK(r.status == ST_IOERR && r.status == 0xA7 && (r.soft & SOFT_BADADDR));
    CHECK(D.pos <= 79 && D.pos == 0);                // never resynced to or stepped toward track 100
}

static void test_start_turns_motor_off(void) {
    setup();
    D.motor = true;                                  // left on outside the server
    mech_server_start(&SRV);
    CHECK(!D.motor);
}

static void test_no_flux_times_out(void) {
    setup();
    D.no_flux = true;
    reply_t r;
    uint64_t t0 = D.now_us;
    mech_server_read(&SRV, 0, &r);
    CHECK(r.status == ST_IOERR);
    CHECK(D.now_us - t0 <= (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u + 2000000u);
}

static void test_overrun_recovers(void) {
    setup();
    D.overrun_once = 1;
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    CHECK(r.status == ST_SOFT && (r.soft & SOFT_OVERRUN) && data_ok(&r));
}

static void test_persistent_overrun_times_out(void) {
    setup();
    D.overrun_always = true;
    reply_t r;
    uint64_t t0 = D.now_us;
    mech_server_read(&SRV, 0, &r);
    CHECK(r.status == ST_IOERR);
    CHECK(D.now_us - t0 <= (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u + 2000000u);
}

static void test_ready_stuck_times_out(void) {
    setup();
    D.ready_stuck = true;
    reply_t r;
    uint64_t t0 = D.now_us;
    mech_server_read(&SRV, 24 * 2 * 10, &r);
    CHECK(r.status == ST_IOERR);
    CHECK(D.now_us - t0 <= (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u + 2000000u);
}

static void test_slow_seek_hits_cap(void) {
    setup();
    D.pos = 0;
    D.step_cost_us = 100000;                         // 79 steps = 7.9 s: the seek itself passes the cap
    reply_t r;
    mech_server_read(&SRV, 1599, &r);
    CHECK(r.status == ST_IOERR && D.starts == 0);    // no read may start once the cap has passed
}

static void test_overrun_mid_stream(void) {
    setup();
    D.overrun_after = 50;
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    CHECK(r.status == ST_SOFT && (r.soft & SOFT_OVERRUN) && data_ok(&r));
}

static void test_no_disk_beats_range(void) {
    setup();
    D.disk_in = false;
    reply_t r;
    mech_server_read(&SRV, 1600, &r);
    CHECK(r.status == ST_NODISK && D.step_strobes == 0 && !D.motor);
}

static void test_idle_off_invalidates(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    CHECK(D.motor);
    mech_server_idle(&SRV);
    CHECK(D.motor);                                  // not idle long enough
    D.now_us += (uint64_t)SRV_IDLE_OFF_MS * 1000u + 1000u;
    mech_server_idle(&SRV);
    CHECK(!D.motor);
    int strobes = D.step_strobes;
    mech_server_read(&SRV, 1, &r);                   // same track: buffer was dropped, so re-read
    CHECK(r.status == ST_OK && data_ok(&r) && D.motor);
    CHECK(D.step_strobes == strobes);                // already on the track: no steps, but a fresh fill
    CHECK(mech_server_stats(&SRV)->fills == 2);
}

static void test_no_disk_invalidates(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    D.disk_in = false;
    mech_server_read(&SRV, 1, &r);
    CHECK(r.status == ST_NODISK);
    D.disk_in = true;
    mech_server_read(&SRV, 1, &r);
    CHECK(r.status == ST_OK && mech_server_stats(&SRV)->fills == 2);
}

static void test_stop(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    mech_server_stop(&SRV);
    CHECK(!D.motor && !D.streaming);
    D.motor = true;                                  // stale motor_on: stop still sends MOTOR_OFF
    mech_server_stop(&SRV);
    CHECK(!D.motor);
}

static void test_reads_never_touch_tx(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 0, &r);
    mech_server_read(&SRV, 1599, &r);
    CHECK(r.status == ST_OK && D.tx_loads == 0 && D.tx_fires == 0 && !D.gate_low);
}

static void pattern(uint8_t *p, int seed) { for (int i = 0; i < GCR_SECTOR_LEN; i++) p[i] = (uint8_t)(i * 13 + seed); }

static void test_write_then_read_not_stale(void) {
    setup();
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN];
    pattern(p, 1);
    mech_server_read(&SRV, 30, &r);                  // t1 s0 sector 6: cached now
    CHECK(r.status == ST_OK && data_ok(&r));
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_OK && r.block == 30 && r.tries == 1 && r.soft == 0);
    CHECK(D.tx_fires == 1 && !D.gate_low && D.addr_while_writing == 0);
    CHECK(r.fire_us < 100);
    CHECK(D.start_us - D.abort_us >= SRV_ERASE_TAIL_US);   // erase tail before the verify read starts
    mech_server_read(&SRV, 30, &r);
    CHECK(r.status == ST_OK && memcmp(r.data, p, GCR_SECTOR_LEN) == 0);
    mech_server_read(&SRV, 31, &r);                  // neighbour untouched
    CHECK(r.status == ST_OK && data_ok(&r));
    mech_server_read(&SRV, 29, &r);
    CHECK(r.status == ST_OK && data_ok(&r));
}

static void test_write_protected(void) {
    setup();
    D.wprot = true;
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN] = {0};
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_WRPROT && D.tx_fires == 0 && D.step_strobes == 0 && !D.motor);
}

static void test_write_no_disk_and_range(void) {
    setup();
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN] = {0};
    mech_server_write(&SRV, 1600, p, &r);
    CHECK(r.status == ST_BADBLK && D.tx_fires == 0);
    D.disk_in = false;
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_NODISK && D.tx_fires == 0 && D.step_strobes == 0);
}

static void test_write_retry_after_bad_verify(void) {
    setup();
    D.corrupt_writes = 1;
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN];
    pattern(p, 2);
    mech_server_write(&SRV, 100, p, &r);
    CHECK(r.status == ST_SOFT && (r.soft & SOFT_BADDATA) && r.tries == 2 && D.tx_fires == 2);
    mech_server_read(&SRV, 100, &r);
    CHECK(r.status == ST_OK && memcmp(r.data, p, GCR_SECTOR_LEN) == 0);
}

static void test_write_gives_up(void) {
    setup();
    D.corrupt_writes = 100;
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN];
    pattern(p, 3);
    mech_server_write(&SRV, 100, p, &r);
    CHECK(r.status == ST_IOERR && r.tries == SRV_WRITE_TRIES && D.tx_fires == SRV_WRITE_TRIES);
    CHECK(!SRV.tb.valid && !D.gate_low);
}

static void test_write_header_missing(void) {
    setup();
    D.no_flux = true;
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN] = {0};
    uint64_t t0 = D.now_us;
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_IOERR && D.tx_fires == 0 && D.tx_aborts >= 1 && !D.gate_low);
    CHECK(D.now_us - t0 <= (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u + 200000u);
}

static void test_write_tx_hang(void) {
    setup();
    D.tx_hang = true;
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN] = {0};
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_IOERR && D.tx_fires == 1 && !D.gate_low && r.tries == 1);
    CHECK(D.now_us - D.abort_us >= SRV_ERASE_TAIL_US);   // tail after the cap abort too
}

static void test_write_late_fire(void) {
    setup();
    D.fire_delay_us = 40;                            // well inside the gap: fine
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN];
    pattern(p, 4);
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_OK);
    setup();
    D.fire_delay_us = 300;                           // past the old data mark: never verifies
    mech_server_write(&SRV, 30, p, &r);
    CHECK(r.status == ST_IOERR && r.tries == SRV_WRITE_TRIES);
}

static void test_write_ignores_bad_neighbour(void) {
    setup();
    D.bad_sector = 7; D.bad_revs = 1000;             // t1 s0 sector 7 never reads
    reply_t r;
    uint8_t p[GCR_SECTOR_LEN];
    pattern(p, 5);
    uint64_t t0 = D.now_us;
    mech_server_write(&SRV, 30, p, &r);              // sector 6 of the same track side
    CHECK(r.status == ST_OK && D.now_us - t0 < 1500000u);
}

static bool ts_zero(int t, int s) {
    reply_t r;
    for (int sec = 0; sec < gcr_sectors_per_track(t); sec++) {
        mech_server_read(&SRV, (uint16_t)gcr_tss_to_block(t, s, sec), &r);
        if (r.status != ST_OK) return false;
        for (int i = 0; i < GCR_SECTOR_LEN; i++) if (r.data[i]) return false;
    }
    return true;
}

static void test_format_ts_ok(void) {
    setup();
    reply_t r;
    mech_server_format_ts(&SRV, 5, 1, &r);
    CHECK(r.status == ST_OK && r.tries == 1 && D.tx_fires == 1 && !D.gate_low && D.addr_while_writing == 0);
    CHECK(D.wbits[5][1] == fd_rev_bits(5));
    CHECK(ts_zero(5, 1));
    mech_server_format_ts(&SRV, 79, 0, &r);           // zone 4: shortest revolution, still wraps
    CHECK(r.status == ST_OK && ts_zero(79, 0));
}

static void test_format_starts_anywhere(void) {
    setup();
    D.idle_bit = 50000;                              // no index: the write wraps over its own preamble
    reply_t r;
    mech_server_format_ts(&SRV, 3, 0, &r);
    CHECK(r.status == ST_OK && ts_zero(3, 0));
}

static void test_format_then_read_not_stale(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 24, &r);                  // t1 s0 cached
    CHECK(r.status == ST_OK && data_ok(&r));
    mech_server_format_ts(&SRV, 1, 0, &r);
    CHECK(r.status == ST_OK && ts_zero(1, 0));
}

static void test_format_protected_and_args(void) {
    setup();
    reply_t r;
    mech_server_format_ts(&SRV, 80, 0, &r);
    CHECK(r.status == ST_BADBLK);
    mech_server_format_ts(&SRV, 0, 2, &r);
    CHECK(r.status == ST_BADBLK);
    D.wprot = true;
    mech_server_format_ts(&SRV, 0, 0, &r);
    CHECK(r.status == ST_WRPROT && D.tx_fires == 0 && D.step_strobes == 0);
    D.wprot = false; D.disk_in = false;
    mech_server_format_ts(&SRV, 0, 0, &r);
    CHECK(r.status == ST_NODISK && D.tx_fires == 0);
}

static void test_format_gives_up_after_10(void) {
    setup();
    D.corrupt_writes = 1000;                         // the middle of every image is a data nibble
    reply_t r;
    mech_server_format_ts(&SRV, 20, 1, &r);
    CHECK(r.status == ST_IOERR && r.tries == SRV_FORMAT_TRIES && D.tx_fires == SRV_FORMAT_TRIES);
    CHECK(!SRV.tb.valid && !D.gate_low);
}

static void test_format_retry_then_ok(void) {
    setup();
    D.corrupt_writes = 2;
    reply_t r;
    mech_server_format_ts(&SRV, 40, 0, &r);
    CHECK(r.status == ST_SOFT && r.tries == 3 && (r.soft & SOFT_BADDATA) && ts_zero(40, 0));
}

static void test_format_tx_hang(void) {
    setup();
    D.tx_hang = true;
    reply_t r;
    mech_server_format_ts(&SRV, 0, 0, &r);
    CHECK(r.status == ST_IOERR && D.tx_fires == 1 && !D.gate_low);
}

static void test_write_on_board_formatted_track(void) {
    setup();
    reply_t r;
    mech_server_format_ts(&SRV, 1, 0, &r);
    CHECK(r.status == ST_OK);
    uint8_t p[GCR_SECTOR_LEN];
    pattern(p, 9);
    mech_server_write(&SRV, 30, p, &r);              // ROM gaps: DE AA FF | FF 3F CF F3 FC FF D5 AA AD
    CHECK(r.status == ST_OK && r.tries == 1);
    mech_server_read(&SRV, 30, &r);
    CHECK(r.status == ST_OK && memcmp(r.data, p, GCR_SECTOR_LEN) == 0);
    mech_server_read(&SRV, 31, &r);
    CHECK(r.status == ST_OK);
    for (int i = 0; i < GCR_SECTOR_LEN; i++) CHECK(r.data[i] == 0);
}

static void test_media(void) {
    setup();
    reply_t r;
    mech_server_media(&SRV, &r);
    CHECK(r.status == ST_OK);
    D.wprot = true;
    mech_server_media(&SRV, &r);
    CHECK(r.status == ST_WRPROT);
    D.disk_in = false;                               // no disk wins over the tab
    mech_server_media(&SRV, &r);
    CHECK(r.status == ST_NODISK && D.step_strobes == 0 && !D.motor);
}

static void test_eject(void) {
    setup();
    reply_t r;
    mech_server_read(&SRV, 30, &r);                  // motor on, buffer filled
    CHECK(r.status == ST_OK && D.motor);
    mech_server_eject(&SRV, &r);
    CHECK(r.status == ST_OK && !D.disk_in && !D.motor && D.ejects == 1);
    mech_server_read(&SRV, 30, &r);                  // buffer dropped: no stale data
    CHECK(r.status == ST_NODISK);
    mech_server_eject(&SRV, &r);                     // nothing in: still OK, no eject strobe
    CHECK(r.status == ST_OK && D.ejects == 1);
}

static bool btn;
static bool btn_cb(void *c) { (void)c; return btn; }

static void test_eject_button(void) {
    setup();
    mech_server_set_button(&SRV, btn_cb, NULL);
    btn = true;                                      // a 5 ms bounce: ignored
    mech_server_idle(&SRV);
    D.now_us += 5000;
    mech_server_idle(&SRV);
    btn = false;
    mech_server_idle(&SRV);
    CHECK(D.disk_in && D.ejects == 0);
    btn = true;                                      // held 30 ms, then released: eject
    mech_server_idle(&SRV);
    D.now_us += 30000;
    mech_server_idle(&SRV);
    CHECK(D.disk_in);                                // not while held
    btn = false;
    mech_server_idle(&SRV);
    CHECK(!D.disk_in && D.ejects == 1);
    mech_server_idle(&SRV);
    CHECK(D.ejects == 1);                            // latch cleared
}

int main(void) {
    test_media();
    test_eject();
    test_eject_button();
    test_miss_then_hit();
    test_no_disk();
    test_bad_block();
    test_retry_then_ok();
    test_budget_exhausted();
    test_wrong_track_reseeks();
    test_foreign_forever_fails();
    test_foreign_impossible_track();
    test_start_turns_motor_off();
    test_no_flux_times_out();
    test_overrun_recovers();
    test_persistent_overrun_times_out();
    test_ready_stuck_times_out();
    test_slow_seek_hits_cap();
    test_overrun_mid_stream();
    test_no_disk_beats_range();
    test_idle_off_invalidates();
    test_no_disk_invalidates();
    test_stop();
    test_reads_never_touch_tx();
    test_write_then_read_not_stale();
    test_write_protected();
    test_write_no_disk_and_range();
    test_write_retry_after_bad_verify();
    test_write_gives_up();
    test_write_header_missing();
    test_write_tx_hang();
    test_write_late_fire();
    test_write_ignores_bad_neighbour();
    test_format_ts_ok();
    test_format_starts_anywhere();
    test_format_then_read_not_stale();
    test_format_protected_and_args();
    test_format_gives_up_after_10();
    test_format_retry_then_ok();
    test_format_tx_hang();
    test_write_on_board_formatted_track();
    TEST_END();
}
