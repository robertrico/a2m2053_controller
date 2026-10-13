#include "mech_server.h"
#include <string.h>

// 1.2 x one revolution per speed zone (394/429/472/525/590 rpm).
static const uint32_t rev_ms[5] = { 183, 168, 153, 138, 123 };

static uint64_t now_us(const mech_server_t *s) { return s->src->now_us(s->src->ctx); }

static void on_sector(void *ctx, int sector, const uint8_t *payload) {
    mech_server_t *s = ctx;
    trackbuf_put(&s->tb, sector, payload);
}

static void on_event(void *ctx, flux_ev_t ev, int track, int side, int sector) {
    mech_server_t *s = ctx;
    (void)side; (void)sector;
    switch (ev) {
    case FLUX_EV_BAD_ADDR: case FLUX_EV_RANGE: s->soft |= SOFT_BADADDR; break;
    case FLUX_EV_NO_DATA:  case FLUX_EV_BAD_DATA: s->soft |= SOFT_BADDATA; break;
    case FLUX_EV_FOREIGN:
        s->soft |= SOFT_FOREIGN;
        if (track >= GCR_TRACKS) { s->soft |= SOFT_BADADDR; break; }   // impossible track: never resync to it
        if (track != s->tb.track) { s->foreign_seen = true; s->foreign_track = track; }
        break;
    }
}

static void on_header(void *ctx, int track, int side, int sector) {
    mech_server_t *s = ctx;
    if (s->arm_sector < 0 || s->fired) return;
    if (track != s->tb.track || side != s->tb.side || sector != s->arm_sector) return;
    s->tx->fire(s->tx->ctx);                           // first: every us here moves the splice
    s->fired = true;
    uint64_t dt = now_us(s) - s->read_us;
    s->fire_us = dt > 0xFFFFu ? 0xFFFFu : (uint16_t)dt;
}

static uint16_t all_sectors(int track) { return (uint16_t)((1u << gcr_sectors_per_track(track)) - 1u); }

static bool motor_up(mech_server_t *s) {
    if (s->motor_on) return true;
    if (mech_motor(s->bus, &s->ms, true) != MECH_OK) return false;
    s->motor_on = true;
    return true;
}

void mech_server_init(mech_server_t *s, const mech_bus_t *bus, const flux_src_t *src, const flux_tx_t *tx) {
    memset(s, 0, sizeof *s);
    s->bus = bus;
    s->src = src;
    s->tx = tx;
    s->arm_sector = -1;
    mech_state_init(&s->ms);
    trackbuf_init(&s->tb);
    s->sink.sector = on_sector;
    s->sink.event = on_event;
    s->sink.header = on_header;
    s->sink.ctx = s;
}

static void motor_off(mech_server_t *s) {
    mech_motor(s->bus, &s->ms, false);
    s->motor_on = false;
    trackbuf_invalidate(&s->tb);
}

// Read loop on (track, side). Ends true once every sector in `want` is in the track buffer, or,
// with arm >= 0, once the TX has fired at sector `arm`'s header (one interval per read while armed).
// Gives up after revs_max revolutions (reads and header search: SRV_REVS_MAX).
static bool fill(mech_server_t *s, int track, int side, uint64_t t_req, uint16_t want, int arm, int revs_max) {
    s->stats.fills++;
    trackbuf_start(&s->tb, track, side);
    s->arm_sector = arm;
    s->fired = false;
    if (!motor_up(s)) return false;
    int batch = arm >= 0 ? 1 : SRV_BATCH;
    int seeks = 0, revs = 0;
    bool recal_done = false;
    for (;;) {                                         // one pass per (re)seek
        if (now_us(s) - t_req > (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u) return false;
        if (seeks >= SRV_SEEK_TRIES_MAX) return false;
        if (seeks == 2) {
            mech_recal(s->bus, &s->ms); s->soft |= SOFT_RECAL;
            if (now_us(s) - t_req > (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u) return false;
        }
        if (mech_seek(s->bus, &s->ms, track) != MECH_OK) { seeks++; s->ms.track_known = false; continue; }
        if (now_us(s) - t_req > (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u) return false;
        if (arm >= 0 && mech_status(s->bus, &s->ms, MREG_READY)) { seeks++; continue; }   // ERS86 T1
        mech_select_head(s->bus, side);
        if (arm >= 0) s->bus->delay_us(s->bus->ctx, SRV_SEL_SETTLE_US);                  // ERS86 T6
        s->src->start(s->src->ctx);
        flux_dec_reset(&s->dec, track, side, &s->sink);
        s->foreign_seen = false;
        uint64_t rev_t = now_us(s);
        uint32_t rev_len = rev_ms[track / 16] * 1000u;
        bool reseek = false;
        while (!reseek) {
            int n = s->src->read(s->src->ctx, s->batch, batch);
            if (n < 0) { s->soft |= SOFT_OVERRUN; flux_dec_reset(&s->dec, track, side, &s->sink); }
            if (n > 0) {
                uint64_t d0 = now_us(s);
                s->read_us = d0;
                flux_dec_push_many(&s->dec, s->batch, n);
                uint64_t d1 = now_us(s);
                s->stats.decode_us += d1 - d0;
                s->stats.intervals += (uint32_t)n;
            }
            if (s->fired) { s->src->stop(s->src->ctx); return true; }
            if (want && (s->tb.present & want) == want) { s->src->stop(s->src->ctx); return true; }
            uint64_t now = now_us(s);
            if (now - t_req > (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u) { s->src->stop(s->src->ctx); return false; }
            if (s->foreign_seen) {                     // ROM: resync position to the header, re-seek
                s->src->stop(s->src->ctx);
                s->ms.track = s->foreign_track;
                s->ms.track_known = true;
                seeks++;
                reseek = true;
                continue;
            }
            if (now - rev_t >= rev_len) {
                revs++;
                rev_t = now;
                if (revs >= revs_max) { s->src->stop(s->src->ctx); return false; }
                if (revs == SRV_RECAL_AFTER_REVS && !recal_done) {
                    recal_done = true;
                    s->src->stop(s->src->ctx);
                    mech_recal(s->bus, &s->ms);
                    s->soft |= SOFT_RECAL;
                    if (now_us(s) - t_req > (uint64_t)SRV_REQ_TIMEOUT_MS * 1000u) return false;
                    reseek = true;
                }
            }
        }
    }
}

// After fire: wait for the PIO to raise the gate (or abort at the cap), then the erase tail.
static bool tx_finish(mech_server_t *s, size_t len) {
    uint64_t t0 = now_us(s);
    bool done = true;
    while (s->tx->busy(s->tx->ctx)) {
        if (now_us(s) - t0 > SRV_TX_CAP_US(len)) { done = false; break; }
        s->bus->delay_us(s->bus->ctx, SRV_TX_POLL_US);
    }
    s->tx->abort(s->tx->ctx);                          // pins back to SIO, gate high
    s->bus->delay_us(s->bus->ctx, SRV_ERASE_TAIL_US);  // ERS86 T7 erase tail; RD valid after (also after a cap abort)
    return done;
}

void mech_server_write(mech_server_t *s, uint16_t block, const uint8_t data[GCR_SECTOR_LEN], reply_t *r) {
    memset(r, 0, sizeof *r);
    r->block = block;
    int track, side, sector;
    if (mech_status(s->bus, &s->ms, MREG_CSTIN)) {     // CSTIN high = no disk
        trackbuf_invalidate(&s->tb);
        r->status = ST_NODISK;
        return;
    }
    if (!mech_status(s->bus, &s->ms, MREG_WRPROT)) { r->status = ST_WRPROT; return; }   // WRPROT low = protected
    if (!gcr_block_to_tss(block, &track, &side, &sector)) { r->status = ST_BADBLK; return; }   // ROM: after Se5f0
    wtrack_data_field(s->wbuf, sector, data);          // encode before the search, as the ROM
    uint64_t t_req = now_us(s);
    s->soft = 0;
    bool ok = false;
    for (int w = 0; w < SRV_WRITE_TRIES && !ok; w++) {
        r->tries = (uint8_t)(w + 1);
        s->tx->load(s->tx->ctx, s->wbuf, WT_DATA_FIELD_LEN);
        if (!fill(s, track, side, t_req, 0, sector, SRV_REVS_MAX)) { s->tx->abort(s->tx->ctx); break; }   // header never seen
        r->fire_us = s->fire_us;
        if (!tx_finish(s, WT_DATA_FIELD_LEN)) break;   // TX stuck: aborted, gate high
        uint16_t want = (uint16_t)(1u << sector);
        if (fill(s, track, side, t_req, want, -1, SRV_VERIFY_REVS) && memcmp(trackbuf_get(&s->tb, sector), data, GCR_SECTOR_LEN) == 0)
            ok = true;
        else
            s->soft |= SOFT_BADDATA;
    }
    s->arm_sector = -1;
    s->last_us = now_us(s);
    r->soft = s->soft;
    if (ok) {
        r->status = s->soft ? ST_SOFT : ST_OK;
    } else {
        trackbuf_invalidate(&s->tb);
        r->status = ST_IOERR;
    }
}

static bool track_all_zero(const mech_server_t *s) {
    for (int sec = 0; sec < s->tb.nsect; sec++) {
        const uint8_t *p = trackbuf_get(&s->tb, sec);
        if (!p) return false;
        for (int i = 0; i < GCR_SECTOR_LEN; i++) if (p[i]) return false;
    }
    return true;
}

// ROM cmd_format_actual for one track side: one write of the whole image (longer than a
// revolution; the tail lands in the preamble), then a verify read. No index: starts anywhere.
void mech_server_format_ts(mech_server_t *s, int cyl, int side, reply_t *r) {
    memset(r, 0, sizeof *r);
    if (cyl < 0 || cyl >= GCR_TRACKS || side < 0 || side >= GCR_SIDES) { r->status = ST_BADBLK; return; }
    if (mech_status(s->bus, &s->ms, MREG_CSTIN)) {
        trackbuf_invalidate(&s->tb);
        r->status = ST_NODISK;
        return;
    }
    if (!mech_status(s->bus, &s->ms, MREG_WRPROT)) { r->status = ST_WRPROT; return; }
    trackbuf_invalidate(&s->tb);
    size_t len = wtrack_track(s->timg, sizeof s->timg, cyl, side);
    if (len == 0) { r->status = ST_BADBLK; return; }   // never arm an empty write
    uint64_t t0 = now_us(s);
    s->soft = 0;
    bool ok = false;
    for (int k = 0; k < SRV_FORMAT_TRIES && !ok; k++) {
        if (now_us(s) - t0 > (uint64_t)SRV_FORMAT_TS_TIMEOUT_MS * 1000u) break;
        r->tries = (uint8_t)(k + 1);
        if (!motor_up(s)) break;
        if (mech_seek(s->bus, &s->ms, cyl) != MECH_OK) { s->ms.track_known = false; s->soft |= SOFT_RECAL; continue; }
        if (mech_status(s->bus, &s->ms, MREG_READY)) continue;          // ERS86 T1: READY low first
        mech_select_head(s->bus, side);
        s->bus->delay_us(s->bus->ctx, SRV_SEL_SETTLE_US);             // ERS86 T6
        s->tx->load(s->tx->ctx, s->timg, len);
        s->tx->fire(s->tx->ctx);
        if (!tx_finish(s, len)) break;
        if (fill(s, cyl, side, now_us(s), all_sectors(cyl), -1, SRV_VERIFY_REVS) && track_all_zero(s)) ok = true;
        else s->soft |= SOFT_BADDATA;
    }
    if (!ok) trackbuf_invalidate(&s->tb);
    s->last_us = now_us(s);
    r->soft = s->soft;
    r->status = ok ? (s->soft ? ST_SOFT : ST_OK) : ST_IOERR;
}

void mech_server_read(mech_server_t *s, uint16_t block, reply_t *r) {
    memset(r, 0, sizeof *r);
    r->block = block;
    int track, side, sector;
    if (mech_status(s->bus, &s->ms, MREG_CSTIN)) {     // CSTIN high = no disk
        trackbuf_invalidate(&s->tb);
        r->status = ST_NODISK;
        return;
    }
    if (!gcr_block_to_tss(block, &track, &side, &sector)) { r->status = ST_BADBLK; return; }
    uint64_t t_req = now_us(s);
    s->last_us = t_req;
    if (trackbuf_matches(&s->tb, track, side) && trackbuf_has(&s->tb, sector)) {
        memcpy(r->data, trackbuf_get(&s->tb, sector), GCR_SECTOR_LEN);
        r->status = ST_OK;
        return;
    }
    s->soft = 0;
    fill(s, track, side, t_req, all_sectors(track), -1, SRV_REVS_MAX);
    s->last_us = now_us(s);
    r->soft = s->soft;
    if (trackbuf_has(&s->tb, sector)) {
        memcpy(r->data, trackbuf_get(&s->tb, sector), GCR_SECTOR_LEN);
        r->status = s->soft ? ST_SOFT : ST_OK;
    } else {
        r->status = ST_IOERR;
    }
}

void mech_server_media(mech_server_t *s, reply_t *r) {
    memset(r, 0, sizeof *r);
    if (mech_status(s->bus, &s->ms, MREG_CSTIN)) {     // CSTIN high = no disk
        trackbuf_invalidate(&s->tb);
        r->status = ST_NODISK;
    } else if (!mech_status(s->bus, &s->ms, MREG_WRPROT)) {
        r->status = ST_WRPROT;                         // WRPROT low = protected
    } else {
        r->status = ST_OK;
    }
}

void mech_server_eject(mech_server_t *s, reply_t *r) {
    memset(r, 0, sizeof *r);
    motor_off(s);                                      // mech_eject also waits for the spin-down
    r->status = mech_eject(s->bus, &s->ms) == MECH_OK ? ST_OK : ST_IOERR;
    trackbuf_invalidate(&s->tb);
    s->ejects++;
    s->last_us = now_us(s);
}

void mech_server_set_button(mech_server_t *s, bool (*pressed)(void *ctx), void *ctx) {
    s->button = pressed;
    s->button_ctx = ctx;
}

void mech_server_idle(mech_server_t *s) {
    uint64_t now = now_us(s);
    if (s->button) {
        if (!s->button(s->button_ctx)) s->button_t = 0;
        else if (!s->button_t) s->button_t = now ? now : 1;
        else if (now - s->button_t >= (uint64_t)SRV_BUTTON_DEBOUNCE_MS * 1000u) s->eject_latched = true;
    }
    if (s->eject_latched && (!s->button || !s->button(s->button_ctx))) {   // eject on release
        s->eject_latched = false;
        s->button_t = 0;
        reply_t r;
        mech_server_eject(s, &r);
        return;
    }
    if (s->motor_on && now - s->last_us > (uint64_t)SRV_IDLE_OFF_MS * 1000u) motor_off(s);
    else if (!s->motor_on && now - s->motor_off_us > (uint64_t)SRV_MOTOR_OFF_REPEAT_MS * 1000u) {
        mech_command(s->bus, MCMD_MOTOR_OFF);
        s->motor_off_us = now;
    }
}

void mech_server_start(mech_server_t *s) {
    s->tx->abort(s->tx->ctx);
    motor_off(s);                                      // motor may have been left on outside the server
}

void mech_server_stop(mech_server_t *s) {
    s->tx->abort(s->tx->ctx);
    s->src->stop(s->src->ctx);
    motor_off(s);                                      // unconditional: motor_on may be stale
}

const srv_stats_t *mech_server_stats(const mech_server_t *s) { return &s->stats; }
