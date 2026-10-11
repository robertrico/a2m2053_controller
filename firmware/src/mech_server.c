#include "mech_server.h"
#include <string.h>

// 1.2 x one revolution per speed zone (394/429/472/525/590 rpm).
static const uint32_t rev_ms[5] = { 183, 168, 153, 138, 123 };

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

void mech_server_init(mech_server_t *s, const mech_bus_t *bus, const flux_src_t *src) {
    memset(s, 0, sizeof *s);
    s->bus = bus;
    s->src = src;
    mech_state_init(&s->ms);
    trackbuf_init(&s->tb);
    s->sink.sector = on_sector;
    s->sink.event = on_event;
    s->sink.ctx = s;
}

static uint64_t now_us(const mech_server_t *s) { return s->src->now_us(s->src->ctx); }

static void motor_off(mech_server_t *s) {
    mech_motor(s->bus, &s->ms, false);
    s->motor_on = false;
    trackbuf_invalidate(&s->tb);
}

// Fill the track buffer for (track, side). Returns true if every sector is present.
static bool fill(mech_server_t *s, int track, int side, uint64_t t_req) {
    s->stats.fills++;
    trackbuf_start(&s->tb, track, side);
    if (!s->motor_on) {
        if (mech_motor(s->bus, &s->ms, true) != MECH_OK) return false;
        s->motor_on = true;
    }
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
        mech_select_head(s->bus, side);
        s->src->start(s->src->ctx);
        flux_dec_reset(&s->dec, track, side, &s->sink);
        s->foreign_seen = false;
        uint64_t rev_t = now_us(s);
        uint32_t rev_len = rev_ms[track / 16] * 1000u;
        bool reseek = false;
        while (!reseek) {
            int n = s->src->read(s->src->ctx, s->batch, SRV_BATCH);
            if (n < 0) { s->soft |= SOFT_OVERRUN; flux_dec_reset(&s->dec, track, side, &s->sink); }
            if (n > 0) {
                uint64_t d0 = now_us(s);
                flux_dec_push_many(&s->dec, s->batch, n);
                uint64_t d1 = now_us(s);
                s->stats.decode_us += d1 - d0;
                s->stats.intervals += (uint32_t)n;
            }
            if (trackbuf_complete(&s->tb)) { s->src->stop(s->src->ctx); return true; }
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
                if (revs >= SRV_REVS_MAX) { s->src->stop(s->src->ctx); return false; }
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
    fill(s, track, side, t_req);
    s->last_us = now_us(s);
    r->soft = s->soft;
    if (trackbuf_has(&s->tb, sector)) {
        memcpy(r->data, trackbuf_get(&s->tb, sector), GCR_SECTOR_LEN);
        r->status = s->soft ? ST_SOFT : ST_OK;
    } else {
        r->status = ST_IOERR;
    }
}

void mech_server_idle(mech_server_t *s) {
    if (s->motor_on && now_us(s) - s->last_us > (uint64_t)SRV_IDLE_OFF_MS * 1000u) motor_off(s);
}

void mech_server_start(mech_server_t *s) {
    motor_off(s);                                      // motor may have been left on outside the server
}

void mech_server_stop(mech_server_t *s) {
    s->src->stop(s->src->ctx);
    motor_off(s);                                      // unconditional: motor_on may be stale
}

const srv_stats_t *mech_server_stats(const mech_server_t *s) { return &s->stats; }
