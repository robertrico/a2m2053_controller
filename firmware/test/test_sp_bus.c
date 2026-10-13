// sp_bus against a simulated Liron card (liron-if.asm), in simulated time.
//
// The card side follows the card ROM: ACK-high check (50 polls x 11 us) before a send, ACK-low
// window after the write underrun (9 polls x 12 us), read waits with no timeout, 5 read retries
// with REQ held 1 ms after a bad reply. The device's fm_rx is modelled from the WR edge list
// exactly as the PIO program decides (a 1 at an edge, a 0 at 1.5 cells and every cell after).
// Every HAL call costs simulated time, so the engine's loops advance the clock.
#include <math.h>
#include <string.h>
#include "test.h"
#include "sp_bus.h"
#include "ramdisk.h"

#define T_CELL   3.9111                 // 28 FCLK at 7.159 MHz
#define T_CALL   0.2                    // simulated cost of one HAL call, us

// ---------------------------------------------------------------- card packet bytes
static size_t card_packet(uint8_t *out, uint8_t dest, uint8_t type, const uint8_t *d, size_t len) {
    static const uint8_t sync[] = { 0xFF, 0x3F, 0xCF, 0xF3, 0xFC, 0xFF, 0xC3 };   // FF from `sta q7h`
    size_t p = 0;
    memcpy(out, sync, sizeof sync);
    p += sizeof sync;
    uint8_t odd = (uint8_t)(len % 7), grp = (uint8_t)(len / 7), ck = 0;
    for (size_t i = 0; i < len; i++) ck ^= d[i];
    uint8_t hdr[7] = { (uint8_t)(dest | 0x80), 0x80, (uint8_t)(type | 0x80), 0x80, 0x80,
                       (uint8_t)(odd | 0x80), (uint8_t)(grp | 0x80) };
    for (int i = 0; i < 7; i++) { out[p++] = hdr[i]; ck ^= hdr[i]; }
    if (odd) {
        uint8_t m = 0x80;
        for (int i = 0; i < odd; i++) m |= (uint8_t)((d[i] >> 7) << (6 - i));
        out[p++] = m;
        for (int i = 0; i < odd; i++) out[p++] = d[i] | 0x80;
    }
    for (int g = 0; g < grp; g++) {
        const uint8_t *s = d + odd + g * 7;
        uint8_t m = 0x80;
        for (int i = 0; i < 7; i++) m |= (uint8_t)((s[i] >> 7) << (6 - i));
        out[p++] = m;
        for (int i = 0; i < 7; i++) out[p++] = s[i] | 0x80;
    }
    out[p++] = ck | 0xAA;
    out[p++] = (uint8_t)((ck >> 1) | 0xAA);
    out[p++] = 0xC8;
    return p;
}

// ---------------------------------------------------------------- card script
typedef enum { OP_RESET, OP_SEND, OP_READ, OP_WAIT, OP_END } opk_t;

typedef struct {
    opk_t k;
    uint8_t pkt[1400];
    size_t n;
    int retries;
    int corrupt;                        // SEND: corrupt the first N attempts; READ: first N replies
    double us;
    bool ok;
    int attempts;
    sp_hdr_t h;
    uint8_t data[SP_DATA_MAX];
} op_t;

#define MAX_OPS 64
static op_t ops[MAX_OPS];
static int nops, cur;

// ---------------------------------------------------------------- simulator state
static double t;                        // us
static uint32_t phases;                 // SP_IN_PH* driven by the card
static bool wrreq_low;
static bool ack_pin = true, enbus;
static int proto_err;                   // device did something the card was not expecting
static double ack_fall_t = -1;          // last time the ACK line went low
static double max_ack_after_underrun;   // worst underrun -> ACK low seen by the card

// WR edges (absolute times), appended per packet
#define MAX_EDGES 200000
static double edges[MAX_EDGES];
static size_t nedges;

// device fm_rx model
static uint8_t rxbuf[4096];
static size_t rx_chunks;
static bool rx_started;
static double rx_arm_t, rx_le, rx_nz;
static size_t rx_ei;
static uint8_t rx_isr, rx_nbits;

// device TX as the card sees it
static uint8_t txbytes[SP_TX_MAX];
static size_t txlen;
static double tx_t0 = -1, tx_end = -1;

// card state
static int sub, k_poll;
static double ev, t_req, underrun_t;

static bool ack_line(void) { return enbus ? ack_pin : true; }

static void op_finish(bool ok) {
    ops[cur].ok = ok;
    cur++;
    sub = 0;
    k_poll = 0;
}

static void push_bit(int bit) {
    if (rx_chunks >= sizeof rxbuf) return;          // DMA done: SM stalls, bits lost
    rx_isr = (uint8_t)((rx_isr << 1) | bit);
    if (++rx_nbits == 8) { rxbuf[rx_chunks++] = rx_isr; rx_nbits = 0; rx_isr = 0; }
}

static void rx_run(double now) {
    if (!rx_started) {
        while (rx_ei < nedges && edges[rx_ei] < rx_arm_t) rx_ei++;
        if (rx_ei >= nedges || edges[rx_ei] > now) return;
        rx_started = true;
        rx_le = edges[rx_ei++];
        rx_nz = rx_le + 1.5 * T_CELL;
        push_bit(1);
    }
    for (;;) {
        double ne = rx_ei < nedges ? edges[rx_ei] : 1e300;
        if (ne <= rx_nz) {
            if (ne > now) return;
            push_bit(1);
            rx_le = ne;
            rx_ei++;
            rx_nz = rx_le + 1.5 * T_CELL;
        } else {
            if (rx_nz > now) return;
            push_bit(0);
            rx_nz += T_CELL;
        }
    }
}

// Card decodes our reply: drops bits before t_q6, needs C3 within 30 nibbles, checksum, C8.
static bool card_decode(op_t *o, double t_q6, bool corrupt) {
    static uint8_t bits[SP_TX_MAX * 8];
    size_t nb = 0;
    for (size_t i = 0; i < txlen; i++)
        for (int b = 7; b >= 0; b--) bits[nb++] = (txbytes[i] >> b) & 1u;
    size_t first = 0;
    while (first < nb && tx_t0 + (double)first * T_CELL < t_q6) first++;
    sp_framer_t fr = {0};
    int nib = 0;
    size_t i = first;
    for (; i < nb; i++) {
        int byte = sp_framer_bit(&fr, bits[i]);
        if (byte < 0) continue;
        if (byte == 0xC3) break;
        if (++nib > 30) return false;
    }
    if (i >= nb) return false;
    static uint8_t wire[SP_WIRE_MAX];
    size_t n = 0;
    for (i++; i < nb && n < sizeof wire; i++) {
        int byte = sp_framer_bit(&fr, bits[i]);
        if (byte >= 0) wire[n++] = (uint8_t)byte;
    }
    if (corrupt && n > 9) wire[8] ^= 0x01;
    return sp_decode(wire, n, &o->h, o->data) == SP_OK;
}

static void start_stream(op_t *o, double tf) {
    uint8_t pk[1400];
    memcpy(pk, o->pkt, o->n);
    if (o->attempts < o->corrupt) pk[o->n - 5] ^= 0x02;   // a data/checksum bit
    size_t bit = 0;
    for (size_t i = 0; i < o->n; i++)
        for (int b = 7; b >= 0; b--, bit++)
            if ((pk[i] >> b) & 1u) { if (nedges < MAX_EDGES) edges[nedges++] = tf + (double)bit * T_CELL; }
    underrun_t = tf + (double)(o->n * 8) * T_CELL;
}

static void sim_update(double now) {
    while (cur < nops) {
        op_t *o = &ops[cur];
        if (ev > now) return;
        switch (o->k) {
        case OP_END: return;
        case OP_WAIT:
            if (sub == 0) { sub = 1; ev += o->us; break; }
            op_finish(true);
            break;
        case OP_RESET:                  // reset_smartport_bus: PH0+PH2 80 ms, all off 10 ms
            if (sub == 0) { phases = SP_IN_PH0 | SP_IN_PH2; sub = 1; ev += 80000; break; }
            if (sub == 1) { phases = 0; sub = 2; ev += 10000; break; }
            phases = SP_IN_PH1 | SP_IN_PH3;
            op_finish(true);
            break;
        case OP_SEND:
            switch (sub) {
            case 0:                     // Lc813: wait for ACK high, 50 polls
                phases = SP_IN_PH1 | SP_IN_PH3;
                if (ack_line()) { sub = 1; ev += 3; break; }
                if (++k_poll < 50) { ev += 11; break; }
                goto send_fail;
            case 1:                     // REQ high
                phases |= SP_IN_PH0;
                t_req = ev;
                sub = 2;
                ev += 7.7;
                break;
            case 2:                     // write mode: /WRREQ low, first edge 3.95 us later
                wrreq_low = true;
                start_stream(o, ev + 3.95);
                sub = 3;
                ev = underrun_t;
                break;
            case 3:                     // underrun
                wrreq_low = false;
                sub = 4;
                k_poll = 0;
                ev += 7;
                break;
            case 4:                     // Lc938: 9 polls for ACK low
                if (!ack_line()) {
                    double dt = ev - underrun_t;
                    if (dt > max_ack_after_underrun) max_ack_after_underrun = dt;
                    sub = 5;
                    ev += 3;
                    break;
                }
                if (++k_poll < 9) { ev += 12; break; }
                phases &= ~SP_IN_PH0;
                goto send_fail;
            case 5:
                phases &= ~SP_IN_PH0;
                o->attempts++;
                ev += 20;
                op_finish(true);
                break;
            }
            break;
        send_fail:
            o->attempts++;
            if (o->attempts < o->retries) { sub = 0; k_poll = 0; ev += 50; }
            else { ev += 20; op_finish(false); }
            break;
        case OP_READ:
            switch (sub) {
            case 0:                     // Lc97d: wait for ACK high, no timeout
                phases = SP_IN_PH1 | SP_IN_PH3;
                if (ack_line()) { sub = 1; ev += 3; break; }
                ev += 11;
                break;
            case 1:                     // REQ high; the card reads from Q6 off, 6 us later
                phases |= SP_IN_PH0;
                t_req = ev;
                tx_t0 = tx_end = -1;
                sub = 2;
                ev += 1;
                break;
            case 2:                     // nibble loop: no timeout; decode when the reply is out
                if (tx_end < 0 || now < tx_end) { ev = now + 1; return; }
                if (card_decode(o, t_req + 6, o->attempts < o->corrupt)) { sub = 3; ev = tx_end + 5; break; }
                o->attempts++;
                sub = 4;
                ev = tx_end + 1000;     // error return with REQ high, 1 ms delay, then Sc93d
                break;
            case 3:                     // Lcne8: wait for ACK low, no timeout
                if (!ack_line()) { phases &= ~SP_IN_PH0; o->attempts++; ev += 3; op_finish(true); break; }
                ev += 12;
                break;
            case 4:
                phases &= ~SP_IN_PH0;
                if (o->attempts < 5) { sub = 0; ev += 20; }
                else { ev += 20; op_finish(false); }
                break;
            }
            break;
        }
    }
}

static void advance(double dt) { t += dt; sim_update(t); }

// ---------------------------------------------------------------- HAL
static uint32_t h_inputs(void *c) { (void)c; advance(T_CALL); return phases | (wrreq_low ? 0 : SP_IN_WRREQ); }
static void h_set_ack(void *c, bool high) {
    (void)c; advance(T_CALL);
    bool was = ack_line();
    ack_pin = high;
    if (was && !ack_line()) ack_fall_t = t;
}
static void h_set_enbus(void *c, bool on) {
    (void)c; advance(T_CALL);
    bool was = ack_line();
    enbus = on;
    if (was && !ack_line()) ack_fall_t = t;
}
static void h_rx_arm(void *c) {
    (void)c; advance(T_CALL);
    rx_chunks = 0; rx_started = false; rx_arm_t = t; rx_isr = 0; rx_nbits = 0;
}
static double stall_us;                 // one main-loop stall when the first chunk shows up
static size_t h_rx_count(void *c) {
    (void)c; advance(T_CALL); rx_run(t);
    if (rx_chunks && stall_us > 0) { double d = stall_us; stall_us = 0; advance(d); rx_run(t); }
    return rx_chunks;
}
static void h_tx_start(void *c, const uint8_t *buf, size_t len) {
    (void)c; advance(T_CALL);
    if (cur >= nops || ops[cur].k != OP_READ || sub != 2 || !(phases & SP_IN_PH0) || !enbus) proto_err++;
    memcpy(txbytes, buf, len);
    txlen = len;
    tx_t0 = t;
    tx_end = t + (double)(len * 8) * T_CELL;
}
static bool h_tx_busy(void *c) { (void)c; advance(T_CALL); return t < tx_end; }
static uint64_t h_now(void *c) { (void)c; return (uint64_t)t; }
static void h_delay(void *c, uint32_t us) { (void)c; advance((double)us); }
static uint32_t h_irq_off(void *c) { (void)c; return 0; }
static void h_irq_restore(void *c, uint32_t s) { (void)c; (void)s; }

static const sp_hal_t hal = {
    h_inputs, h_set_ack, h_set_enbus, h_rx_arm, h_rx_count, rxbuf, sizeof rxbuf,
    h_tx_start, h_tx_busy, h_now, h_delay, h_irq_off, h_irq_restore, NULL,
};

// ---------------------------------------------------------------- scripts
static uint8_t base[4 * RD_BLOCK_LEN];
static uint8_t slots[8][RD_BLOCK_LEN];
static ramdisk_t rd;
static sp_store_t store;
static sp_dev_t dev;
static sp_bus_t bus;
static bool lastone_high = true;
static const sp_store_t *sim_store;     // NULL: the RAM disk (test_sp_floppy.c sets the floppy)
static bool lastone_cb(void *c) { (void)c; return lastone_high; }

static void sim_reset(void) {
    memset(ops, 0, sizeof ops);
    nops = cur = 0;
    t = 0;
    ev = 0;
    sub = k_poll = 0;
    phases = SP_IN_PH1 | SP_IN_PH3;
    wrreq_low = false;
    ack_pin = true;
    enbus = false;
    proto_err = 0;
    nedges = 0;
    rx_chunks = 0; rx_started = false; rx_arm_t = 0; rx_ei = 0;
    tx_t0 = tx_end = -1;
    max_ack_after_underrun = 0;
    ramdisk_init(&rd, base, sizeof base, slots, 8);
    store = ramdisk_store(&rd);
    sp_dev_init(&dev, sim_store ? sim_store : &store, lastone_cb, NULL);
    sp_bus_init(&bus, &hal, &dev);
    sp_bus_enable(&bus, true);
}

static op_t *op(opk_t k) { op_t *o = &ops[nops++]; memset(o, 0, sizeof *o); o->k = k; return o; }

static op_t *send_cmd(uint8_t dest, uint8_t cmd, uint8_t pc, uint8_t p2, uint8_t p3, int retries) {
    uint8_t c[9] = { cmd, pc, 0x00, 0x20, p2, p3, 0, 0, 0 };
    op_t *o = op(OP_SEND);
    o->n = card_packet(o->pkt, dest, SP_TYPE_CMD, c, 9);
    o->retries = retries;
    return o;
}

static op_t *send_data(uint8_t dest, const uint8_t *d, size_t len) {
    op_t *o = op(OP_SEND);
    o->n = card_packet(o->pkt, dest, SP_TYPE_DATA, d, len);
    o->retries = 3000;
    return o;
}

static op_t *read_reply(void) { return op(OP_READ); }

static void run_script(double limit_us) {
    op(OP_END);
    while (cur < nops - 1 && t < limit_us) sp_bus_poll(&bus);
    // let the engine settle (release the bus) after the last op
    double stop = t + 500;
    while (t < stop) sp_bus_poll(&bus);
}

#ifndef SIM_NO_MAIN
int main(void) {
    for (size_t i = 0; i < sizeof base; i++) base[i] = (uint8_t)(i * 13 + (i >> 9));

    // 1. Boot: bus reset, INIT (single try, as Scded), STATUS, READBLOCK 0
    sim_reset();
    op(OP_RESET);
    op_t *init = send_cmd(1, SP_INIT, 2, 0, 0, 1);
    op_t *init_r = read_reply();
    op_t *st = send_cmd(1, SP_STATUS, 3, 0, 0, 3000);
    op_t *st_r = read_reply();
    op_t *rb = send_cmd(1, SP_READBLOCK, 3, 0, 0, 3000);
    op_t *rb_r = read_reply();
    op_t *rb3 = send_cmd(1, SP_READBLOCK, 3, 3, 0, 3000);
    op_t *rb3_r = read_reply();
    run_script(2e6);
    CHECK(init->ok && init->attempts == 1);
    CHECK(init_r->ok && init_r->h.stat == SP_E_LAST && init_r->h.src == 1 && init_r->h.dest == 0);
    CHECK(dev.id == 1);
    CHECK(st->ok && st->attempts == 1 && st_r->ok && st_r->h.len == 4);
    CHECK(st_r->data[0] == 0xF8 && st_r->data[1] == 0x40 && st_r->data[2] == 0x06);
    CHECK(rb->ok && rb_r->ok && rb_r->h.stat == 0 && rb_r->h.len == 512);
    CHECK(memcmp(rb_r->data, base, 512) == 0);
    CHECK(rb3->ok && rb3_r->ok && memcmp(rb3_r->data, base + 3 * 512, 512) == 0);
    CHECK(proto_err == 0);
    CHECK(!enbus);                                   // bus released when idle
    CHECK(bus.st.resets == 1 && bus.st.acks == 4 && bus.st.replies == 4);
    CHECK(max_ack_after_underrun > 0 && max_ack_after_underrun <= 7 + 12 * 4);
    CHECK(bus.st.ack_us_max != INT32_MIN && bus.st.ack_us_max <= 40);
    printf("boot: ACK after underrun (card view) max %.1f us, engine ack_us max %d us, sim t %.0f us\n",
           max_ack_after_underrun, (int)bus.st.ack_us_max, t);
    sp_log_t e;
    int ncmd = 0;
    while (sp_bus_log_pop(&bus, &e)) if (e.kind == SP_LOG_CMD) ncmd++;
    CHECK(ncmd == 4);

    // 2. WRITEBLOCK with its data packet, then read it back
    sim_reset();
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    uint8_t blk[512];
    for (int i = 0; i < 512; i++) blk[i] = (uint8_t)(255 - i);
    op_t *wb = send_cmd(1, SP_WRITEBLOCK, 3, 2, 0, 3000);
    op_t *wd = send_data(1, blk, 512);
    op_t *wb_r = read_reply();
    send_cmd(1, SP_READBLOCK, 3, 2, 0, 3000);
    op_t *rr = read_reply();
    run_script(2e6);
    CHECK(wb->ok && wd->ok && wd->attempts == 1);
    CHECK(wb_r->ok && wb_r->h.stat == 0 && wb_r->h.len == 0);
    CHECK(rr->ok && memcmp(rr->data, blk, 512) == 0);
    CHECK(rd.used == 1 && proto_err == 0);
    CHECK(bus.st.ack_us_max <= 40);

    // 3. Corrupted command packet: no ACK, the card retries, second try works
    sim_reset();
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    op_t *bad = send_cmd(1, SP_STATUS, 3, 0, 0, 3000);
    bad->corrupt = 1;
    op_t *bad_r = read_reply();
    run_script(2e6);
    CHECK(bad->ok && bad->attempts == 2);
    CHECK(bad_r->ok && bad_r->h.len == 4);
    CHECK(bus.st.cksum == 1 && proto_err == 0);

    // 4. Packet for another ID: ignored, no ACK; the next one for us works
    sim_reset();
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    op_t *other = send_cmd(2, SP_STATUS, 3, 0, 0, 3);
    op_t *mine = send_cmd(1, SP_STATUS, 3, 3, 0, 3000);
    op_t *mine_r = read_reply();
    run_script(2e6);
    CHECK(!other->ok && other->attempts == 3);
    CHECK(mine->ok && mine_r->ok && mine_r->h.len == 25);
    CHECK(bus.st.ignored == 3 && proto_err == 0);

    // 5. No ID yet: a READBLOCK is ignored (no ACK)
    sim_reset();
    op(OP_RESET);
    op_t *early = send_cmd(1, SP_READBLOCK, 3, 0, 0, 2);
    run_script(1e6);
    CHECK(!early->ok && early->attempts == 2 && dev.id == 0 && bus.st.ignored == 2);

    // 6. Bus reset in the middle: card resets instead of reading the reply; then INIT again
    sim_reset();
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    op_t *cut = send_cmd(1, SP_READBLOCK, 3, 0, 0, 3000);
    op(OP_RESET);
    op_t *init2 = send_cmd(2, SP_INIT, 2, 0, 0, 1);
    op_t *init2_r = read_reply();
    run_script(3e6);
    CHECK(cut->ok);
    CHECK(init2->ok && init2_r->ok && dev.id == 2 && init2_r->h.src == 2);
    CHECK(bus.st.resets == 2 && !enbus);

    // 7. Card cannot decode our reply once: REQ held 1 ms, then re-read; we send it again
    sim_reset();
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    send_cmd(1, SP_READBLOCK, 3, 1, 0, 3000);
    op_t *rr2 = read_reply();
    rr2->corrupt = 1;
    run_script(2e6);
    CHECK(rr2->ok && rr2->attempts == 2 && memcmp(rr2->data, base + 512, 512) == 0);
    CHECK(bus.st.rereads == 1 && proto_err == 0);

    // 8. WRITEBLOCK data packet corrupted once: no ACK, card sends it again
    sim_reset();
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    send_cmd(1, SP_WRITEBLOCK, 3, 7, 0, 3000);
    op_t *wd2 = send_data(1, blk, 512);
    wd2->corrupt = 1;
    op_t *wr2 = read_reply();
    run_script(2e6);
    CHECK(wd2->ok && wd2->attempts == 2 && wr2->ok && wr2->h.stat == 0);
    CHECK(rd.map[7] != 0 && proto_err == 0);

    // 10. Main loop stalls 1.1 ms into a command packet: the card has given up (REQ low) by the
    // time we look. No ACK, nothing answered; the card's retry is served.
    sim_reset();
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    op_t *lt = send_cmd(1, SP_STATUS, 3, 0, 0, 3000);
    op_t *lt_r = read_reply();
    // arm the stall once INIT is done (ops 0-2: reset, INIT, its reply)
    while (cur < 3 && t < 1e6) sp_bus_poll(&bus);
    stall_us = 1100;
    run_script(2e6);
    CHECK(lt->ok && lt->attempts == 2 && lt_r->ok && lt_r->h.len == 4);
    CHECK(bus.st.acks == 2 && bus.st.replies == 2 && proto_err == 0);
    CHECK(bus.st.late == 0 || bus.st.late == 1);    // seen late, or skipped on REQ low

    // 11. Stall that ends 30 us after the underrun, REQ still high: ACK, counted as late_entry
    sim_reset();
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    op_t *le = send_cmd(1, SP_STATUS, 3, 0, 0, 3000);
    op_t *le_r = read_reply();
    while (cur < 3 && t < 1e6) sp_bus_poll(&bus);
    stall_us = 875;                             // 28-byte packet: 876 us; first chunk at +31 us
    run_script(2e6);
    CHECK(le->ok && le->attempts == 1 && le_r->ok);
    CHECK(bus.st.late_entry == 1 && bus.st.late == 0 && proto_err == 0);

    // 12. Command retried while we wait for WRITEBLOCK data must not replace the command
    sim_reset();
    op(OP_RESET);
    send_cmd(1, SP_INIT, 2, 0, 0, 1);
    read_reply();
    send_cmd(1, SP_WRITEBLOCK, 3, 6, 0, 3000);
    send_cmd(1, SP_READBLOCK, 3, 0, 0, 1);      // stray command where data is expected
    send_data(1, blk, 512);
    op_t *w12 = read_reply();
    run_script(2e6);
    CHECK(w12->ok && w12->h.stat == 0 && rd.map[6] != 0 && dev.cmds[SP_READBLOCK] == 0);

    // 9. sp off: nothing is ACKed, the bus stays released
    sim_reset();
    sp_bus_enable(&bus, false);
    op(OP_RESET);
    op_t *off = send_cmd(1, SP_INIT, 2, 0, 0, 1);
    run_script(1e6);
    CHECK(!off->ok && !enbus && dev.id == 0);
    TEST_END();
}
#endif
