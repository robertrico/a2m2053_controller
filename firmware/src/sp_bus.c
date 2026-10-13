#include "sp_bus.h"
#include "sp_ram.h"
#include <string.h>

typedef enum { W_OK, W_TIMEOUT, W_RESET } wait_t;
typedef enum { RX_GOOD, RX_IGNORE, RX_ABORT, RX_RESET } rxres_t;

#define H        (b->hal)
#define NOW()    (H->now_us(H->ctx))
#define INPUTS() (H->inputs(H->ctx))

static bool SP_RAM(is_reset)(uint32_t in) { return (in & SP_IN_PHASES) == SP_PH_RESET; }
static bool SP_RAM(bus_enabled)(uint32_t in) { return (in & (SP_IN_PH1 | SP_IN_PH3)) == (SP_IN_PH1 | SP_IN_PH3); }

static void SP_RAM(log_push)(sp_bus_t *b, sp_log_kind_t kind, uint8_t stage, int32_t ack_us) {
    uint32_t wr = b->log_wr;
    if (wr - b->log_rd >= SP_LOG_LEN) return;                  // full: drop, the console is behind
    sp_log_t *e = &b->log[wr & (SP_LOG_LEN - 1)];
    e->kind = (uint8_t)kind;
    e->stage = stage;
    e->cmd = b->cmd_len ? b->cmd[0] : 0xFF;
    e->dest = b->hdr.dest;
    e->stat = b->reply.stat;
    e->block = b->cmd_len >= 7 ? (uint32_t)b->cmd[4] | ((uint32_t)b->cmd[5] << 8) | ((uint32_t)b->cmd[6] << 16) : 0;
    e->ack_us = ack_us;
    b->log_wr = wr + 1;
}

bool sp_bus_log_pop(sp_bus_t *b, sp_log_t *e) {
    if (b->log_rd == b->log_wr) return false;
    *e = b->log[b->log_rd & (SP_LOG_LEN - 1)];
    b->log_rd++;
    return true;
}

static void release(sp_bus_t *b) {
    H->set_enbus(H->ctx, false);
    H->set_ack(H->ctx, true);
}

// Called on seeing the reset state (PH0+PH2); sp_bus_poll ignores it until it ends.
static void bus_reset(sp_bus_t *b) {
    b->in_reset = true;
    release(b);
    sp_dev_bus_reset(b->dev);
    b->tx_valid = false;
    b->cmd_len = 0;
    b->st.resets++;
    log_push(b, SP_LOG_RESET, 0, INT32_MIN);
    H->rx_arm(H->ctx);
}

void sp_bus_init(sp_bus_t *b, const sp_hal_t *hal, sp_dev_t *dev) {
    memset(b, 0, sizeof *b);
    b->hal = hal;
    b->dev = dev;
    b->st.ack_us_last = b->st.ack_us_max = b->st.data_ack_us_max = INT32_MIN;
    release(b);
    hal->rx_arm(hal->ctx);
}

void sp_bus_enable(sp_bus_t *b, bool on) {
    b->enabled = on;
    release(b);
    H->rx_arm(H->ctx);
}

// Wait for REQ to reach `high`. Bus reset aborts.
static wait_t wait_req(sp_bus_t *b, bool high, uint32_t timeout_us) {
    uint64_t t0 = NOW();
    for (;;) {
        uint32_t in = INPUTS();
        if (is_reset(in)) return W_RESET;
        if (((in & SP_IN_PH0) != 0) == high) return W_OK;
        if (NOW() - t0 > timeout_us) return W_TIMEOUT;
    }
}

// Collect one packet from the fm_rx buffer. t_up gets the time /WRREQ was seen going high
// (the host's write underrun), 0 if not seen; *wr_seen says /WRREQ was seen low at all (false:
// we started after the underrun and cannot tell how late we are).
static rxres_t SP_RAM(receive)(sp_bus_t *b, uint8_t want_type, uint64_t *t_up, bool *wr_seen) {
    sp_rx_init(&b->rx);
    size_t idx = 0;
    uint64_t t0 = NOW();
    bool wr_low = false;
    *t_up = 0;
    *wr_seen = false;
    sp_rx_status_t st = SP_RX_MORE;
    for (;;) {
        uint32_t in = INPUTS();
        if (is_reset(in)) return RX_RESET;
        if (!(in & SP_IN_WRREQ)) { wr_low = true; b->t_low_last = NOW(); }
        else if (wr_low && !*t_up) *t_up = NOW();
        size_t n = H->rx_count(H->ctx);
        while (idx < n && st == SP_RX_MORE) st = sp_rx_chunk(&b->rx, H->rx_buf[idx++]);
        if (st == SP_RX_DONE) { *wr_seen = wr_low; break; }
        if (st == SP_RX_BAD) { b->st.bad++; log_push(b, SP_LOG_BAD, 0, INT32_MIN); return RX_ABORT; }
        if (!(in & SP_IN_PH0)) return RX_ABORT;                // host dropped REQ: it gave up
        if (n >= H->rx_cap || NOW() - t0 > SP_T_RX_MAX) return RX_ABORT;
    }
    b->st.packets++;
    if (sp_decode(b->rx.wire, b->rx.n, &b->hdr, b->data) != SP_OK) {
        b->st.cksum++;
        log_push(b, SP_LOG_CKSUM, 0, INT32_MIN);
        return RX_IGNORE;
    }
    bool ours = want_type == SP_TYPE_CMD
              ? sp_dev_accepts(b->dev, &b->hdr, b->data[0]) && bus_enabled(INPUTS())
              : b->hdr.type == SP_TYPE_DATA && b->dev->id != 0 && b->hdr.dest == b->dev->id;
    if (!ours) {
        b->st.ignored++;
        if (want_type == SP_TYPE_CMD) {             // inside get_data b->cmd is the pending command
            b->cmd_len = b->hdr.len < SP_CMD_LEN ? b->hdr.len : SP_CMD_LEN;
            memcpy(b->cmd, b->data, b->cmd_len);
            b->reply.stat = 0;
        }
        log_push(b, SP_LOG_IGNORED, 0, INT32_MIN);
        return RX_IGNORE;
    }
    return RX_GOOD;
}

// ROM order: ACK low, then ENBUS on.
static void SP_RAM(ack_low)(sp_bus_t *b) {
    H->set_ack(H->ctx, false);
    H->set_enbus(H->ctx, true);
    b->st.acks++;
}

// ACK a good packet only while the card can still take it: REQ high and, when we saw the
// underrun, less than SP_T_ACK_LATE after it. A late ACK would land in the card's retry.
// Returns false (nothing driven) when late. *ack_us: /WRREQ rise to ACK, negative when ACK came
// first; INT32_MIN when we started after the underrun.
static bool SP_RAM(ack_if_in_time)(sp_bus_t *b, uint64_t t_up, bool wr_seen, int32_t *ack_us) {
    uint32_t in = INPUTS();
    // /WRREQ rose while we were busy with the last chunk: it rose after the last low sample.
    // Using that sample makes ack_us an upper bound (and the late check stricter).
    if (wr_seen && !t_up && (in & SP_IN_WRREQ)) t_up = b->t_low_last;
    if (!(in & SP_IN_PH0) || (t_up && NOW() - t_up > SP_T_ACK_LATE)) {
        b->st.late++;
        log_push(b, SP_LOG_LATE, 0, t_up ? (int32_t)(NOW() - t_up) : INT32_MIN);
        return false;
    }
    ack_low(b);
    uint64_t t_ack = NOW();
    // C8's last 0 is decided half a cell before the cell ends, so ACK can come just before the
    // host's underrun (/WRREQ rise): then measure the rise afterwards.
    while (wr_seen && !t_up && NOW() - t_ack < 1000u)
        if (INPUTS() & SP_IN_WRREQ) t_up = NOW();
    if (!wr_seen) b->st.late_entry++;
    *ack_us = wr_seen && t_up ? (int32_t)((int64_t)t_ack - (int64_t)t_up) : INT32_MIN;
    return true;
}

// A packet we do not answer: let the host finish and give up, then listen again.
static void skip_packet(sp_bus_t *b, rxres_t r) {
    if (r == RX_RESET) { bus_reset(b); return; }
    if (wait_req(b, false, SP_T_IGNORE) == W_RESET) { bus_reset(b); return; }
    H->rx_arm(H->ctx);
}

static void timeout(sp_bus_t *b, uint8_t stage) {
    release(b);
    b->st.timeouts++;
    log_push(b, SP_LOG_TIMEOUT, stage, INT32_MIN);
    H->rx_arm(H->ctx);
}

// Receive the data packet that follows WRITEBLOCK/CONTROL/WRITE. ENBUS off: ACK goes high
// through the pull-up, which tells the card to send.
static bool get_data(sp_bus_t *b, size_t *dlen) {
    release(b);
    uint64_t t0 = NOW();
    for (;;) {
        for (;;) {
            if (is_reset(INPUTS())) { bus_reset(b); return false; }
            if (H->rx_count(H->ctx) > 0) break;
            if (NOW() - t0 > SP_T_DATA_WAIT) { timeout(b, 2); return false; }
        }
        uint32_t irq = H->irq_off(H->ctx);
        uint64_t t_up;
        bool wr_seen;
        int32_t ack_us;
        rxres_t r = receive(b, SP_TYPE_DATA, &t_up, &wr_seen);
        if (r == RX_GOOD && !ack_if_in_time(b, t_up, wr_seen, &ack_us)) r = RX_IGNORE;
        if (r == RX_GOOD) {
            H->irq_restore(H->ctx, irq);
            if (ack_us != INT32_MIN && ack_us > b->st.data_ack_us_max) b->st.data_ack_us_max = ack_us;
            *dlen = b->hdr.len;
            wait_t w = wait_req(b, false, SP_T_REQ_FALL);
            if (w == W_RESET) { bus_reset(b); return false; }
            if (w == W_TIMEOUT) { timeout(b, 3); return false; }
            H->rx_arm(H->ctx);
            return true;
        }
        H->irq_restore(H->ctx, irq);
        skip_packet(b, r);                                      // the card sends it again
        if (r == RX_RESET) return false;
    }
}

// Reply: ACK high, wait REQ, pause, FM out, ACK low, wait REQ low, release.
static void send_reply(sp_bus_t *b, bool encode) {
    if (encode) {
        b->tx_len = sp_encode(b->tx, b->dev->id, SP_TYPE_STATUS, b->reply.stat, b->reply.data, b->reply.len);
        b->tx_valid = true;
    }
    H->set_ack(H->ctx, true);
    H->set_enbus(H->ctx, true);
    wait_t w = wait_req(b, true, SP_T_REPLY_REQ);
    if (w == W_RESET) { bus_reset(b); return; }
    if (w == W_TIMEOUT) { timeout(b, 4); return; }
    H->delay_us(H->ctx, SP_T_TX_PAUSE);
    H->tx_start(H->ctx, b->tx, b->tx_len);
    uint64_t t0 = NOW(), limit = (uint64_t)b->tx_len * 8u * 4u + 2000u;
    while (H->tx_busy(H->ctx)) {
        if (NOW() - t0 > limit) { timeout(b, 5); return; }
    }
    H->set_ack(H->ctx, false);
    b->st.replies++;
    w = wait_req(b, false, SP_T_REPLY_DONE);
    if (w == W_RESET) { bus_reset(b); return; }
    release(b);
    H->rx_arm(H->ctx);
    if (w == W_TIMEOUT) { b->st.timeouts++; log_push(b, SP_LOG_TIMEOUT, 6, INT32_MIN); }
}

static void transaction(sp_bus_t *b) {
    uint32_t irq = H->irq_off(H->ctx);
    uint64_t t_up;
    bool wr_seen;
    int32_t ack_us = INT32_MIN;
    rxres_t r = receive(b, SP_TYPE_CMD, &t_up, &wr_seen);
    if (r == RX_GOOD && !ack_if_in_time(b, t_up, wr_seen, &ack_us)) r = RX_IGNORE;
    if (r != RX_GOOD) {
        H->irq_restore(H->ctx, irq);
        skip_packet(b, r);
        return;
    }
    H->irq_restore(H->ctx, irq);
    b->tx_valid = false;                        // a new command: the old reply is never re-sent
    b->st.ack_us_last = ack_us;
    if (ack_us != INT32_MIN && ack_us > b->st.ack_us_max) b->st.ack_us_max = ack_us;

    b->cmd_len = b->hdr.len < SP_CMD_LEN ? b->hdr.len : SP_CMD_LEN;
    memcpy(b->cmd, b->data, b->cmd_len);
    uint8_t dest = b->hdr.dest;
    wait_t w = wait_req(b, false, SP_T_REQ_FALL);
    if (w == W_RESET) { bus_reset(b); return; }
    if (w == W_TIMEOUT) { timeout(b, 1); return; }
    H->rx_arm(H->ctx);

    size_t dlen = 0;
    if (b->cmd_len && sp_dev_needs_data(b->cmd[0])) {
        if (!get_data(b, &dlen)) return;
    }
    sp_dev_execute(b->dev, dest, b->cmd, b->cmd_len, b->data, dlen, &b->reply);
    b->hdr.dest = dest;
    send_reply(b, true);
    log_push(b, SP_LOG_CMD, 0, ack_us);
}

void sp_bus_poll(sp_bus_t *b) {
    uint32_t in = INPUTS();
    if (is_reset(in)) {
        if (!b->in_reset) bus_reset(b);
        return;
    }
    b->in_reset = false;
    if (!b->enabled) return;
    if (H->rx_count(H->ctx) > 0) {
        b->reread_t = 0;
        transaction(b);
        return;
    }
    // Card re-reading a reply it could not decode: REQ high with the bus enabled, no write.
    if (b->tx_valid && (in & SP_IN_PH0) && bus_enabled(in) && (in & SP_IN_WRREQ)) {
        uint64_t now = NOW();
        if (!b->reread_t) b->reread_t = now ? now : 1;
        else if (now - b->reread_t > SP_T_REREAD) {
            b->reread_t = 0;
            b->st.rereads++;
            log_push(b, SP_LOG_REREAD, 0, INT32_MIN);
            send_reply(b, false);
        }
    } else {
        b->reread_t = 0;
    }
}
