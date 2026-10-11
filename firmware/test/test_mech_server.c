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

int main(void) {
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
    TEST_END();
}
