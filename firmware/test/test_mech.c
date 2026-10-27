#include "test.h"
#include "fake_bus.h"
#include "mech.h"
#include "mech_addr.h"

// --- read-data behaviours -------------------------------------------------
static bool rd_all_high(fake_t *f, uint8_t a) { (void)f; (void)a; return true; }
static bool rd_drvin_low(fake_t *f, uint8_t a) { (void)f; return a != MREG_DRVIN; }
static bool rd_step_stuck(fake_t *f, uint8_t a) { (void)f; return a != MREG_STEP; }
static bool rd_tk0_on_third(fake_t *f, uint8_t a) {
    if (a == MREG_TK0) return f->reads[MREG_TK0] < 3;
    return a != MREG_READY;   // READY low, everything else high
}
static bool rd_tk0_never(fake_t *f, uint8_t a) { (void)f; return a != MREG_READY; }
static bool rd_ready_low(fake_t *f, uint8_t a) {
    if (a == MREG_TK0) return false;
    (void)f; return a != MREG_READY;
}
static bool rd_eject_ok(fake_t *f, uint8_t a) {
    if (a == MREG_CSTIN) return f->strobes[MCMD_EJECT] > 0 && f->reads[MREG_CSTIN] >= 5;
    return true;
}
static bool rd_eject_stuck(fake_t *f, uint8_t a) { (void)f; return a != MREG_CSTIN; }

static void test_rom_mapping(void) {
    // liron.asm call sites: Se640/Se64a argument -> our (CA2 CA1 CA0 SEL) address
    CHECK(mech_addr_from_rom(0x02) == MREG_CSTIN);       // Se51b disk-in check
    CHECK(mech_addr_from_rom(0x0a) == MREG_TK0);         // Le4f7 recal poll
    CHECK(mech_addr_from_rom(0x0b) == MREG_READY);       // Se56a
    CHECK(mech_addr_from_rom(0x04) == MCMD_STEP);        // Se4cf
    CHECK(mech_addr_from_rom(0x01) == MCMD_DIR_OUT);     // Le4f7, Le4b3
    CHECK(mech_addr_from_rom(0x00) == MCMD_DIR_IN);      // Le4b3
    CHECK(mech_addr_from_rom(0x09) == MCMD_MOTOR_OFF);   // Se51b
    CHECK(mech_addr_from_rom(0x0d) == MCMD_EJECT);       // Le534
}

static void test_command_strobe_sequence(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_all_high);
    mech_command(&b, MCMD_MOTOR_ON);
    CHECK(f.n == 6);
    CHECK(f.ev[0].t == EV_ADDR && f.ev[0].v == MCMD_MOTOR_ON);
    CHECK(f.ev[1].t == EV_DELAY && f.ev[1].v >= 1);
    CHECK(f.ev[2].t == EV_LSTRB && f.ev[2].v == 1);
    CHECK(f.ev[3].t == EV_DELAY && f.ev[3].v >= 2);
    CHECK(f.ev[4].t == EV_LSTRB && f.ev[4].v == 0);
    CHECK(f.strobes[MCMD_MOTOR_ON] == 1);
}

static void test_status_all(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_drvin_low);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_status_all(&b, &s) == (uint16_t)~(1u << MREG_DRVIN));
    for (int r = 0; r < 16; r++) CHECK(f.reads[r] == 1);
}

static void test_step_timeout(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_step_stuck);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_step(&b, &s, true, 1) == MECH_ERR_STEP_TIMEOUT);
    CHECK(f.now_us <= MECH_STEP_TIMEOUT_US + 2 * (MECH_STEP_POLL_US + s.settle_us) + 100);
}

static void test_recal_success(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_tk0_on_third);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_recal(&b, &s) == MECH_OK);
    CHECK(s.track == 0 && s.track_known);
    CHECK(f.strobes[MCMD_DIR_OUT] == 1);
    CHECK(f.strobes[MCMD_STEP] == MECH_TRACKS);
}

static void test_recal_fail(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_tk0_never);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_recal(&b, &s) == MECH_ERR_TRACK0);
    CHECK(!s.track_known);
    CHECK(f.reads[MREG_TK0] == MECH_RECAL_POLLS);
}

static void test_seek_range(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_ready_low);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_seek(&b, &s, -1) == MECH_ERR_RANGE);
    CHECK(mech_seek(&b, &s, MECH_TRACKS) == MECH_ERR_RANGE);
    CHECK(f.n == 0);
}

static void test_seek_counts_steps(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_ready_low);
    mech_state_t s; mech_state_init(&s);
    s.track = 10; s.track_known = true;
    CHECK(mech_seek(&b, &s, 13) == MECH_OK);
    CHECK(s.track == 13);
    CHECK(f.strobes[MCMD_DIR_IN] == 1 && f.strobes[MCMD_STEP] == 3);
    CHECK(mech_seek(&b, &s, 11) == MECH_OK);
    CHECK(s.track == 11);
    CHECK(f.strobes[MCMD_DIR_OUT] == 1 && f.strobes[MCMD_STEP] == 5);
    CHECK(mech_seek(&b, &s, 11) == MECH_OK);
    CHECK(f.strobes[MCMD_STEP] == 5);
}

static void test_seek_unknown_recals_first(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_ready_low);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_seek(&b, &s, 5) == MECH_OK);
    CHECK(f.strobes[MCMD_STEP] == MECH_TRACKS + 5);
    CHECK(s.track == 5 && s.track_known);
}

// READY goes low only on the third read: the head is still settling after the recal.
static bool rd_ready_late(fake_t *f, uint8_t a) {
    if (a == MREG_TK0) return false;
    if (a == MREG_READY) return f->reads[MREG_READY] < 3;
    return true;
}

// Regression: a seek that only needs the recal (target track 0) must still wait for READY;
// without it a format loop sees READY high on every try (bench 2026-10-11).
static void test_seek_after_recal_waits_ready(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_ready_late);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_seek(&b, &s, 0) == MECH_OK);
    CHECK(f.reads[MREG_READY] >= 3);
    CHECK(s.track == 0 && s.track_known);
}

static void test_motor_on_not_ready(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_all_high);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_motor(&b, &s, true) == MECH_ERR_NOT_READY);
    CHECK(f.strobes[MCMD_MOTOR_ON] == 1);
    CHECK(f.strobes[MCMD_MOTOR_OFF] == 1);
    CHECK(f.now_us <= MECH_SPINUP_TIMEOUT_US + 2 * (MECH_READY_POLL_US + s.settle_us) + 200);
}

static void test_eject_no_disk(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_all_high);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_eject(&b, &s) == MECH_OK);
    CHECK(f.nseq == 0);
}

static void test_eject_success(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_eject_ok);
    mech_state_t s; mech_state_init(&s);
    s.track = 40; s.track_known = true;
    CHECK(mech_eject(&b, &s) == MECH_OK);
    CHECK(f.nseq == 2 && f.seq[0] == MCMD_MOTOR_OFF && f.seq[1] == MCMD_EJECT);
    CHECK(!s.track_known);
    // ERS 699-0452 3.4.7: eject LSTRB high 1 us min, 300 ms max.
    CHECK(f.hi_us[MCMD_EJECT] >= 1 && f.hi_us[MCMD_EJECT] <= 300000);
    CHECK(f.violations == 0 && !f.lstrb);
}

static void test_eject_fail(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_eject_stuck);
    mech_state_t s; mech_state_init(&s);
    CHECK(mech_eject(&b, &s) == MECH_ERR_EJECT);
    CHECK(f.strobes[MCMD_EJECT] == MECH_EJECT_TRIES);
}

static void test_no_addr_change_while_strobe_high(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_ready_low);
    mech_state_t s; mech_state_init(&s);
    mech_seek(&b, &s, 30);
    mech_motor(&b, &s, true);
    mech_motor(&b, &s, false);
    mech_select_head(&b, 1);
    CHECK(f.violations == 0);
    fake_t g; mech_bus_t b2 = fake_bus(&g, rd_eject_ok);
    mech_eject(&b2, &s);
    CHECK(g.violations == 0);
}

static void test_lstrb_low_after_every_op(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_ready_low);
    mech_state_t s; mech_state_init(&s);
    mech_command(&b, MCMD_STEP);        CHECK(!f.lstrb);
    mech_recal(&b, &s);                 CHECK(!f.lstrb);
    mech_seek(&b, &s, 12);              CHECK(!f.lstrb);
    mech_motor(&b, &s, true);           CHECK(!f.lstrb);
    mech_select_head(&b, 0);            CHECK(!f.lstrb);
}

static void test_select_head(void) {
    fake_t f; mech_bus_t b = fake_bus(&f, rd_all_high);
    mech_select_head(&b, 1);
    CHECK(f.addr == MREG_RDDATA1 && f.nseq == 0);
    mech_select_head(&b, 0);
    CHECK(f.addr == MREG_RDDATA0);
}

int main(void) {
    test_rom_mapping();
    test_command_strobe_sequence();
    test_status_all();
    test_step_timeout();
    test_recal_success();
    test_recal_fail();
    test_seek_range();
    test_seek_counts_steps();
    test_seek_unknown_recals_first();
    test_seek_after_recal_waits_ready();
    test_motor_on_not_ready();
    test_eject_no_disk();
    test_eject_success();
    test_eject_fail();
    test_no_addr_change_while_strobe_high();
    test_lstrb_low_after_every_op();
    test_select_head();
    TEST_END();
}
