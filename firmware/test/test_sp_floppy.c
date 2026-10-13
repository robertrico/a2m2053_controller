// End to end (M7 spec sec. 7): the simulated Liron card (test_sp_bus.c) against sp_bus ->
// sp_dev -> floppy store -> M5 mechanism server -> fake Sony drive. Simulated time runs on
// while "core 1" works, so the card sits in its ACK-high wait for the whole seek.
#define SIM_NO_MAIN
#include "test_sp_bus.c"
#include "sync_core1.h"

static fake_drive_t D;
static mech_bus_t MB;
static flux_src_t MS;
static flux_tx_t MT;
static mech_server_t SRV;
static fstore_t F;
static sp_store_t FS;
static double longest_wait_us;

static bool timed_transact(void *ctx, const req_t *rq, reply_t *rp, uint32_t to) {
    uint64_t t0 = D.now_us;
    bool ok = sync_transact(ctx, rq, rp, to);
    double dt = (double)(D.now_us - t0);
    if (dt > longest_wait_us) longest_wait_us = dt;
    advance(dt);
    return ok;
}

static void floppy_reset(void) {
    fake_drive_init(&D);
    MB = fake_drive_bus(&D);
    MS = fake_flux_src(&D);
    MT = fake_flux_tx(&D);
    mech_server_init(&SRV, &MB, &MS, &MT);
    sync_srv = &SRV;
    sync_off = false;
    fstore_init(&F, timed_transact, NULL);
    FS = fstore_store(&F);
    sim_store = &FS;
    longest_wait_us = 0;
    sim_reset();
}

static bool fake_block(uint32_t block, const uint8_t *d) {
    int tr, sd, sec;
    if (!gcr_block_to_tss((int)block, &tr, &sd, &sec)) return false;
    for (int i = 0; i < 512; i++) if (d[i] != fd_payload_byte(tr, sd, sec, GCR_TAG_LEN + i)) return false;
    return true;
}

int main(void) {
    // 1. Boot off the floppy: INIT, STATUS, READBLOCK 0, then a block at the far end
    floppy_reset();
    op(OP_RESET);
    op_t *ini = send_cmd(1, SP_INIT, 2, 0, 0, 1);
    op_t *ini_r = read_reply();
    send_cmd(1, SP_STATUS, 3, 0, 0, 3000);
    op_t *st_r = read_reply();
    op_t *r0c = send_cmd(1, SP_READBLOCK, 3, 0, 0, 3000);
    op_t *r0 = read_reply();
    op_t *r9c = send_cmd(1, SP_READBLOCK, 3, 0x3F, 0x06, 3000);   // block 1599: track 79
    op_t *r9 = read_reply();
    run_script(5e6);
    CHECK(ini->ok && ini_r->ok && ini_r->h.stat == SP_E_LAST);
    CHECK(st_r->ok && st_r->h.len == 4 && st_r->data[0] == 0xF8);
    CHECK(r0c->ok && r0c->attempts == 1 && r0->ok && r0->h.stat == 0 && r0->h.len == 512);
    CHECK(fake_block(0, r0->data));
    CHECK(r9c->ok && r9c->attempts == 1 && r9->ok && r9->h.len == 512 && fake_block(1599, r9->data));
    CHECK(D.pos == 79 && proto_err == 0 && bus.st.late == 0);
    CHECK(longest_wait_us > 100000);                 // a real seek happened while the card waited
    printf("floppy boot: longest core-1 wait %.0f ms, card attempts all 1, ACK max %d us\n",
           longest_wait_us / 1000, (int)bus.st.ack_us_max);

    // 2. WRITEBLOCK lands on the floppy and reads back
    floppy_reset();
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    uint8_t blk[512];
    for (int i = 0; i < 512; i++) blk[i] = (uint8_t)(i ^ 0xA5);
    send_cmd(1, SP_WRITEBLOCK, 3, 40, 0, 3000);
    op_t *wd = send_data(1, blk, 512);
    op_t *wr = read_reply();
    send_cmd(1, SP_READBLOCK, 3, 40, 0, 3000);
    op_t *rr = read_reply();
    run_script(5e6);
    CHECK(wd->ok && wr->ok && wr->h.stat == 0);
    CHECK(rr->ok && rr->h.len == 512 && memcmp(rr->data, blk, 512) == 0);
    CHECK(proto_err == 0 && bus.st.late == 0);

    // 3. No disk: STATUS offline ($E8, code $00), READ $2F
    floppy_reset();
    D.disk_in = false;
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    send_cmd(1, SP_STATUS, 3, 0, 0, 3000);
    op_t *nd_s = read_reply();
    send_cmd(1, SP_READBLOCK, 3, 2, 0, 3000);
    op_t *nd_r = read_reply();
    run_script(3e6);
    CHECK(nd_s->ok && nd_s->h.stat == 0 && nd_s->data[0] == 0xE8);
    CHECK(nd_r->ok && nd_r->h.stat == SP_E_OFFLINE && nd_r->h.len == 0);

    // 4. Write-protected: STATUS $FC, WRITEBLOCK $2B, READ still fine
    floppy_reset();
    D.wprot = true;
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    send_cmd(1, SP_STATUS, 3, 0, 0, 3000);
    op_t *wp_s = read_reply();
    send_cmd(1, SP_WRITEBLOCK, 3, 9, 0, 3000);
    send_data(1, blk, 512);
    op_t *wp_w = read_reply();
    send_cmd(1, SP_READBLOCK, 3, 9, 0, 3000);
    op_t *wp_r = read_reply();
    run_script(3e6);
    CHECK(wp_s->ok && wp_s->data[0] == 0xFC);
    CHECK(wp_w->ok && wp_w->h.stat == SP_E_NOWRITE);
    CHECK(wp_r->ok && wp_r->h.stat == 0 && fake_block(9, wp_r->data));

    // 5. CONTROL $04 ejects
    floppy_reset();
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    send_cmd(1, SP_CONTROL, 3, 0x04, 0, 3000);
    uint8_t ctl[2] = { 0, 0 };
    send_data(1, ctl, 2);
    op_t *ej = read_reply();
    run_script(5e6);
    CHECK(ej->ok && ej->h.stat == 0 && !D.disk_in && D.ejects == 1);
    TEST_END();
}
