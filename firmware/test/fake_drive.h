#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "mech.h"
#include "mech_server.h"
#include "gcr.h"
#include "wtrack.h"

// A fake Sony mechanism: decodes command strobes, answers status reads, serves a
// GCR flux stream for the current track and selected head, and records writes.
// Bit model: a revolution is a bit string (1 = flux transition). A track side that
// was never written is generated each revolution (6-sync gaps, coprime stride, the
// fault knobs apply). A written track side is a stored image one revolution long;
// the fake TX splices the streamed bits into it (fault knobs no longer apply there).
#define FD_REV_BITS_MAX 80000

// One revolution in bit cells: 489600 bit/s x 60 s / rpm (394/429/472/525/590 by zone).
static int fd_rev_bits(int track) {
    static const int rpm[5] = { 394, 429, 472, 525, 590 };
    return 29376000 / rpm[track / 16];
}

typedef struct {
    // mechanism state
    int pos, dir;                 // physical track, +1 inward / -1 outward
    bool motor, disk_in, wprot;   // wprot: tab set, WRPROT reads low, nothing is recorded
    uint8_t addr;
    int side;                     // last head selected via RDDATA0/1
    int step_strobes, starts;     // counters for the tests
    uint32_t step_cost_us;        // extra time each STEP strobe takes (slow mechanism)
    bool ready_stuck;             // READY reads not-ready once a step happened since motor-on
    int steps_since_motor;
    uint64_t now_us;              // shared clock (bus delays + flux time)
    int addr_while_writing;       // CA/SEL changes while the gate was low (must stay 0)
    // flux stream
    bool streaming, no_flux;
    int skew;                     // headers report pos + skew (positioning fault)
    int foreign_always;           // >=0: every header claims this track
    int bad_sector, bad_revs;     // corrupt this sector's data for the first bad_revs revolutions
    int overrun_once;             // next read returns -1 once
    bool overrun_always;          // every read returns -1 (1 ms passes per call)
    int overrun_after, reads;     // >0: read number overrun_after returns -1 once
    uint8_t cur[FD_REV_BITS_MAX]; // this revolution, one byte per bit
    int ncur;
    uint16_t ivs[65536];
    int ivend[65536];             // bit index just past interval i's transition
    int nivs, rd, revs;
    // written track sides: packed bits, wbits = 0 if never written
    uint8_t wimg[GCR_TRACKS][GCR_SIDES][FD_REV_BITS_MAX / 8];
    int wbits[GCR_TRACKS][GCR_SIDES];
    // fake TX
    const uint8_t *tx_buf;
    size_t tx_len;
    int tx_loads, tx_fires, tx_aborts;
    bool gate_low;                // between fire and abort
    uint64_t tx_end_us;
    bool tx_hang;                 // busy never clears
    uint32_t fire_delay_us;       // the write lands this long after the fire call
    int corrupt_writes;           // the next N writes land with the nibble at tx_len/2 changed
    int idle_bit;                 // where a write starts when the stream is not running
} fake_drive_t;

static void fd_set_addr(void *c, uint8_t a) {
    fake_drive_t *d = c; d->addr = a;
    if (d->gate_low) d->addr_while_writing++;
    if (a == MREG_RDDATA0) d->side = 0;
    if (a == MREG_RDDATA1) d->side = 1;
}
static void fd_set_lstrb(void *c, bool high) {
    fake_drive_t *d = c;
    if (!high) return;
    switch (d->addr) {
    case MCMD_DIR_IN:    d->dir = +1; break;
    case MCMD_DIR_OUT:   d->dir = -1; break;
    case MCMD_STEP:
        d->step_strobes++;
        d->steps_since_motor++;
        d->now_us += d->step_cost_us;
        d->pos += d->dir;
        if (d->pos < 0) d->pos = 0;
        if (d->pos > 79) d->pos = 79;
        break;
    case MCMD_MOTOR_ON:  d->motor = true; d->steps_since_motor = 0; break;
    case MCMD_MOTOR_OFF: d->motor = false; break;
    default: break;
    }
}
static bool fd_read_rd(void *c) {
    fake_drive_t *d = c;
    switch (d->addr) {
    case MREG_CSTIN:   return !d->disk_in;
    case MREG_STEP:    return true;
    case MREG_WRPROT:  return !d->wprot;
    case MREG_MOTORON: return !d->motor;
    case MREG_TK0:     return d->pos != 0;
    case MREG_READY:   if (d->ready_stuck && d->steps_since_motor > 0) return true;
                       return !(d->motor && d->disk_in);
    case MREG_DRVIN:   return false;
    default:           return true;
    }
}
static void fd_delay(void *c, uint32_t us) { ((fake_drive_t *)c)->now_us += us; }

static mech_bus_t fake_drive_bus(fake_drive_t *d) {
    mech_bus_t b = { fd_set_addr, fd_set_lstrb, fd_read_rd, fd_delay, d };
    return b;
}

static uint8_t fd_payload_byte(int t, int s, int sec, int i) { return (uint8_t)(t * 7 + s * 3 + sec + i); }

static void fd_put_bits(fake_drive_t *d, const uint8_t *bytes, int n) {
    for (int k = 0; k < n; k++)
        for (int b = 7; b >= 0; b--)
            if (d->ncur < FD_REV_BITS_MAX) d->cur[d->ncur++] = (uint8_t)((bytes[k] >> b) & 1);
}
static void fd_put_sync(fake_drive_t *d) {         // 10-bit self-sync byte
    static const uint8_t ff = 0xFF;
    fd_put_bits(d, &ff, 1);
    for (int i = 0; i < 2 && d->ncur < FD_REV_BITS_MAX; i++) d->cur[d->ncur++] = 0;
}

// The generated revolution for (pos, side); rev 0.. lets bad_revs corrupt early revolutions.
static void fd_generate(fake_drive_t *d) {
    d->ncur = 0;
    int t = d->pos, hdr_t = d->foreign_always >= 0 ? d->foreign_always : d->pos + d->skew;
    int spt = gcr_sectors_per_track(t);
    int stride = 3;                                // any stride coprime to spt gives a permutation
    while (spt % stride == 0) stride += 2;
    for (int k = 0; k < spt; k++) {
        int sec = (k * stride) % spt;              // fixed interleave; must visit every sector
        uint8_t a[GCR_ADDR_FIELD_LEN], p[GCR_SECTOR_LEN], f[GCR_DATA_FIELD_LEN];
        for (int i = 0; i < GCR_SECTOR_LEN; i++) p[i] = fd_payload_byte(hdr_t, d->side, sec, i);   // content belongs to the track the header claims
        gcr_encode_address(hdr_t < 0 ? 0 : (hdr_t > 127 ? 127 : hdr_t), sec, d->side, a);
        gcr_encode_data(sec, p, f);
        if (sec == d->bad_sector && d->revs < d->bad_revs) f[60] = gcr_nibbles[(gcr_decode6(f[60]) + 1) % 64];
        for (int i = 0; i < 6; i++) fd_put_sync(d);
        fd_put_bits(d, a, GCR_ADDR_FIELD_LEN);
        for (int i = 0; i < 6; i++) fd_put_sync(d);
        fd_put_bits(d, f, GCR_DATA_FIELD_LEN);
    }
    for (int i = 0; i < 6; i++) fd_put_sync(d);
}

static int fd_wget(const fake_drive_t *d, int t, int s, int i) { return (d->wimg[t][s][i >> 3] >> (7 - (i & 7))) & 1; }
static void fd_wset(fake_drive_t *d, int t, int s, int i, int v) {
    uint8_t m = (uint8_t)(0x80u >> (i & 7));
    if (v) d->wimg[t][s][i >> 3] |= m; else d->wimg[t][s][i >> 3] &= (uint8_t)~m;
}

static void fd_to_ivs(fake_drive_t *d) {
    float cell = 1e9f / 489600.0f / 40.0f;
    int run = 0;
    d->nivs = 0;
    for (int i = 0; i < d->ncur; i++) {
        run++;
        if (d->cur[i]) {
            float t = (float)run * cell - 2.0f;
            if (d->nivs < 65536) { d->ivs[d->nivs] = (uint16_t)(t + 0.5f); d->ivend[d->nivs] = i + 1; d->nivs++; }
            run = 0;
        }
    }
}

static void fd_build(fake_drive_t *d) {
    int t = d->pos, s = d->side;
    if (d->wbits[t][s]) {
        d->ncur = d->wbits[t][s];
        for (int i = 0; i < d->ncur; i++) d->cur[i] = (uint8_t)fd_wget(d, t, s, i);
    } else {
        fd_generate(d);
    }
    fd_to_ivs(d);
}

// First write to (pos, side): store what is there now, padded with sync to one revolution.
static void fd_materialize(fake_drive_t *d) {
    int t = d->pos, s = d->side;
    if (d->wbits[t][s]) return;
    if (!d->streaming) fd_generate(d);             // streaming: cur already holds this revolution
    int n = fd_rev_bits(t);
    for (int i = 0; i < n; i++) fd_wset(d, t, s, i, i < d->ncur ? d->cur[i] : ((i - d->ncur) % 10) < 8);
    d->wbits[t][s] = n;
}

static void fd_splice(fake_drive_t *d, int start, const uint8_t *buf, size_t len) {
    int t = d->pos, s = d->side;
    fd_materialize(d);
    int n = d->wbits[t][s];
    for (size_t k = 0; k < len * 8; k++)
        fd_wset(d, t, s, (int)(((size_t)start + k) % (size_t)n), (buf[k >> 3] >> (7 - (k & 7))) & 1);
}

static void fs_start(void *c) { fake_drive_t *d = c; d->streaming = true; d->starts++; d->reads = 0; d->rd = 0; d->revs = 0; fd_build(d); }
static void fs_stop(void *c)  { ((fake_drive_t *)c)->streaming = false; }
static int fs_read(void *c, uint16_t *buf, int max) {
    fake_drive_t *d = c;
    if (!d->streaming) return 0;
    if (d->overrun_always) { d->now_us += 1000; return -1; }
    d->reads++;
    if (d->overrun_after && d->reads == d->overrun_after) return -1;
    if (d->overrun_once) { d->overrun_once = 0; return -1; }
    if (d->no_flux || !d->disk_in || !d->motor) { d->now_us += 1000; return 0; }
    int n = 0;
    while (n < max) {
        if (d->rd >= d->nivs) { d->revs++; d->rd = 0; fd_build(d); }
        uint16_t v = d->ivs[d->rd++];
        buf[n++] = v;
        d->now_us += (uint64_t)v * 40u / 1000u + 1u;   // ~ interval length in us
    }
    return n;
}
static uint64_t fs_now_us(void *c) { return ((fake_drive_t *)c)->now_us; }

static flux_src_t fake_flux_src(fake_drive_t *d) {
    flux_src_t s = { fs_start, fs_stop, fs_read, fs_now_us, d };
    return s;
}

static void ft_load(void *c, const uint8_t *buf, size_t len) {
    fake_drive_t *d = c; d->tx_buf = buf; d->tx_len = len; d->tx_loads++;
}
static void ft_fire(void *c) {
    fake_drive_t *d = c;
    d->tx_fires++;
    d->gate_low = true;
    d->tx_end_us = d->now_us + d->fire_delay_us + (uint64_t)d->tx_len * 16340u / 1000u;
    if (d->wprot || !d->motor || !d->disk_in) return;          // the drive records only then
    static uint8_t w[WT_TRACK_MAX];
    memcpy(w, d->tx_buf, d->tx_len);
    if (d->corrupt_writes > 0) {
        d->corrupt_writes--;
        size_t m = d->tx_len / 2;
        int v = gcr_decode6(w[m]);
        if (v >= 0) w[m] = gcr_nibbles[(v + 1) % 64];
    }
    int delay_bits = (int)((double)d->fire_delay_us * 489600.0 / 1e6);
    int start = (d->streaming && d->rd > 0 ? d->ivend[d->rd - 1] : d->idle_bit) + delay_bits;
    fd_splice(d, start, w, d->tx_len);
}
static bool ft_busy(void *c) { fake_drive_t *d = c; return d->tx_hang || d->now_us < d->tx_end_us; }
static void ft_abort(void *c) { fake_drive_t *d = c; d->tx_aborts++; d->gate_low = false; }

static flux_tx_t fake_flux_tx(fake_drive_t *d) {
    flux_tx_t t = { ft_load, ft_fire, ft_busy, ft_abort, d };
    return t;
}

static void fake_drive_init(fake_drive_t *d) {
    memset(d, 0, sizeof *d);
    d->disk_in = true;
    d->pos = 37;                  // somewhere unknown
    d->bad_sector = -1;
    d->foreign_always = -1;
    d->idle_bit = 1234;
}
