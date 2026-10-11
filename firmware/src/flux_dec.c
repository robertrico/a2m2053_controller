#include "flux_dec.h"
#include <string.h>

void flux_dec_reset(flux_dec_t *d, int track, int side, const flux_dec_sink_t *sink) {
    memset(d, 0, sizeof *d);
    d->sink = sink;
    d->track = track;
    d->side = side;
    d->cell = FLUX_DEC_CELL_COUNTS;
    d->st = FD_HUNT_ADDR;
}

static void emit(flux_dec_t *d, flux_ev_t ev, int t, int s, int sec) {
    if (d->sink && d->sink->event) d->sink->event(d->sink->ctx, ev, t, s, sec);
}

static void nibble(flux_dec_t *d, uint8_t nb) {
    d->last3 = ((d->last3 << 8) | nb) & 0xFFFFFFu;
    int v;
    switch (d->st) {
    case FD_HUNT_ADDR:
        if (d->last3 == 0xD5AA96u) { d->st = FD_ADDR; d->n = 0; }
        return;
    case FD_ADDR:
        v = gcr_decode6(nb);
        if (v < 0) { emit(d, FLUX_EV_BAD_ADDR, -1, -1, -1); d->st = FD_HUNT_ADDR; return; }
        d->six[d->n++] = (uint8_t)v;
        if (d->n == GCR_ADDR_SIXES) {
            d->addr = gcr_decode_address(d->six);
            if (!d->addr.ok) { emit(d, FLUX_EV_BAD_ADDR, -1, -1, -1); d->st = FD_HUNT_ADDR; return; }
            d->st = FD_ADDR_EPILOG;
            d->n = 0;
        }
        return;
    case FD_ADDR_EPILOG:
        if ((d->n == 0 && nb == 0xDE) || (d->n == 1 && nb == 0xAA)) {
            if (++d->n < 2) return;
            if (d->addr.track != d->track || d->addr.side != d->side) {
                emit(d, FLUX_EV_FOREIGN, d->addr.track, d->addr.side, d->addr.sector);
                d->st = FD_HUNT_ADDR;
            } else if (d->addr.sector >= gcr_sectors_per_track(d->track)) {
                emit(d, FLUX_EV_RANGE, d->addr.track, d->addr.side, d->addr.sector);
                d->st = FD_HUNT_ADDR;
            } else {
                d->st = FD_HUNT_DATA;
                d->window = FLUX_DEC_DATA_WINDOW;
            }
            return;
        }
        emit(d, FLUX_EV_BAD_ADDR, -1, -1, -1);
        d->st = FD_HUNT_ADDR;
        return;
    case FD_HUNT_DATA:
        if (d->last3 == 0xD5AAADu) { d->st = FD_DATA; d->n = 0; return; }
        if (--d->window <= 0) {
            emit(d, FLUX_EV_NO_DATA, d->track, d->side, d->addr.sector);
            d->st = FD_HUNT_ADDR;
        }
        return;
    case FD_DATA: {
        v = gcr_decode6(nb);
        if (v < 0) {
            emit(d, FLUX_EV_BAD_DATA, d->track, d->side, d->addr.sector);
            d->st = FD_HUNT_ADDR;
            return;
        }
        d->six[d->n++] = (uint8_t)v;
        if (d->n < GCR_DATA_SIXES) return;
        int sec;
        bool ok = gcr_decode_data(d->six, &sec, d->payload);
        if (ok && sec == d->addr.sector) {
            if (d->sink && d->sink->sector) d->sink->sector(d->sink->ctx, sec, d->payload);
        } else {
            emit(d, FLUX_EV_BAD_DATA, d->track, d->side, d->addr.sector);
        }
        d->st = FD_HUNT_ADDR;
        return;
    }
    }
}

static inline void bit(flux_dec_t *d, int b) {
    if (d->nbits == 0 && !b) return;          // IWM latch: wait for a leading 1
    d->reg = (uint8_t)((d->reg << 1) | b);
    if (++d->nbits == 8) {
        uint8_t nb = d->reg;
        d->reg = 0;
        d->nbits = 0;
        nibble(d, nb);
    }
}

void flux_dec_push(flux_dec_t *d, uint16_t count) {
    float t = (float)count + FLUX_DEC_OVERHEAD;
    int n = (int)(t / d->cell + 0.5f);
    if (n < 1) n = 1;
    if (n > FLUX_DEC_MAX_CELLS) n = FLUX_DEC_MAX_CELLS;
    for (int i = 1; i < n; i++) bit(d, 0);
    bit(d, 1);
    if (n <= 3) d->cell += FLUX_DEC_ALPHA * (t / (float)n - d->cell);
    if (d->cell < FLUX_DEC_CELL_MIN) d->cell = FLUX_DEC_CELL_MIN;
    if (d->cell > FLUX_DEC_CELL_MAX) d->cell = FLUX_DEC_CELL_MAX;
}

void flux_dec_push_many(flux_dec_t *d, const uint16_t *counts, int n) {
    for (int i = 0; i < n; i++) flux_dec_push(d, counts[i]);
}
