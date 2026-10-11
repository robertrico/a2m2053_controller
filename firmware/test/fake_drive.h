#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "mech.h"
#include "mech_server.h"
#include "gcr.h"

// A fake Sony mechanism: decodes command strobes, answers status reads, and
// serves a GCR flux stream for the current track and selected head.
typedef struct {
    // mechanism state
    int pos, dir;                 // physical track, +1 inward / -1 outward
    bool motor, disk_in;
    uint8_t addr;
    int side;                     // last head selected via RDDATA0/1
    int step_strobes, starts;     // counters for the tests
    uint32_t step_cost_us;        // extra time each STEP strobe takes (slow mechanism)
    bool ready_stuck;             // READY reads not-ready once a step happened since motor-on
    int steps_since_motor;
    uint64_t now_us;              // shared clock (bus delays + flux time)
    // flux stream
    bool streaming, no_flux;
    int skew;                     // headers report pos + skew (positioning fault)
    int foreign_always;           // >=0: every header claims this track
    int bad_sector, bad_revs;     // corrupt this sector's data for the first bad_revs revolutions
    int overrun_once;             // next read returns -1 once
    bool overrun_always;          // every read returns -1 (1 ms passes per call)
    int overrun_after, reads;     // >0: read number overrun_after returns -1 once
    uint16_t ivs[65536];
    int nivs, rd, revs;
} fake_drive_t;

static void fd_set_addr(void *c, uint8_t a) {
    fake_drive_t *d = c; d->addr = a;
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

static void fd_bits(fake_drive_t *d, const uint8_t *bytes, int n, int *run, float cell) {
    for (int k = 0; k < n; k++)
        for (int b = 7; b >= 0; b--) {
            (*run)++;
            if ((bytes[k] >> b) & 1) {
                float t = (float)*run * cell - 2.0f;
                if (d->nivs < 65536) d->ivs[d->nivs++] = (uint16_t)(t + 0.5f);
                *run = 0;
            }
        }
}
static void fd_sync(fake_drive_t *d, int *run, float cell) {
    static const uint8_t ff = 0xFF;
    fd_bits(d, &ff, 1, run, cell);
    *run += 2;                                     // 10-bit self-sync byte
}

// Build one revolution for (pos, side); rev 0.. lets bad_revs corrupt early revolutions.
static void fd_build(fake_drive_t *d) {
    d->nivs = 0;
    int t = d->pos, hdr_t = d->foreign_always >= 0 ? d->foreign_always : d->pos + d->skew;
    int spt = gcr_sectors_per_track(t);
    float cell = 1e9f / 489600.0f / 40.0f;
    int run = 0;
    int stride = 3;                                // any stride coprime to spt gives a permutation
    while (spt % stride == 0) stride += 2;
    for (int k = 0; k < spt; k++) {
        int sec = (k * stride) % spt;              // fixed interleave; must visit every sector
        uint8_t a[GCR_ADDR_FIELD_LEN], p[GCR_SECTOR_LEN], f[GCR_DATA_FIELD_LEN];
        for (int i = 0; i < GCR_SECTOR_LEN; i++) p[i] = fd_payload_byte(hdr_t, d->side, sec, i);   // content belongs to the track the header claims
        gcr_encode_address(hdr_t < 0 ? 0 : (hdr_t > 127 ? 127 : hdr_t), sec, d->side, a);
        gcr_encode_data(sec, p, f);
        if (sec == d->bad_sector && d->revs < d->bad_revs) f[60] = gcr_nibbles[(gcr_decode6(f[60]) + 1) % 64];
        for (int i = 0; i < 6; i++) fd_sync(d, &run, cell);
        fd_bits(d, a, GCR_ADDR_FIELD_LEN, &run, cell);
        for (int i = 0; i < 6; i++) fd_sync(d, &run, cell);
        fd_bits(d, f, GCR_DATA_FIELD_LEN, &run, cell);
    }
    for (int i = 0; i < 6; i++) fd_sync(d, &run, cell);
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

static void fake_drive_init(fake_drive_t *d) {
    memset(d, 0, sizeof *d);
    d->disk_in = true;
    d->pos = 37;                  // somewhere unknown
    d->bad_sector = -1;
    d->foreign_always = -1;
}
