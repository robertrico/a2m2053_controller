#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "mech.h"
#include "flux_dec.h"
#include "trackbuf.h"
#include "ipc.h"

// Retry policy: drive ROM (liron.asm cmd_read_block, retry loop lines 1297-1317)
// translated from single-sector attempts to whole-track revolutions (M4 spec sec. 6).
#define SRV_REVS_MAX          12
#define SRV_RECAL_AFTER_REVS  6
#define SRV_SEEK_TRIES_MAX    4
// Cap: no new mechanism operation or read starts after this many ms from the request; an
// operation already running may finish (worst overshoot is its own timeout, 2 s).
#define SRV_REQ_TIMEOUT_MS    5000
#define SRV_IDLE_OFF_MS       1000
#define SRV_BATCH             256

typedef struct {
    void (*start)(void *ctx);                       // stream RD (head already selected)
    void (*stop)(void *ctx);
    int (*read)(void *ctx, uint16_t *buf, int max); // >=0 copied, -1 ring overrun
    uint64_t (*now_us)(void *ctx);
    void *ctx;
} flux_src_t;

typedef struct {
    uint64_t decode_us;     // time spent in flux_dec_push_many, all batches
    uint32_t intervals;     // intervals decoded
    uint32_t fills;
} srv_stats_t;

typedef struct {
    const mech_bus_t *bus;
    const flux_src_t *src;
    mech_state_t ms;
    bool motor_on;
    uint64_t last_us;
    trackbuf_t tb;
    flux_dec_t dec;
    flux_dec_sink_t sink;
    uint8_t soft;
    bool foreign_seen;
    int foreign_track;
    srv_stats_t stats;
    uint16_t batch[SRV_BATCH];
} mech_server_t;

void mech_server_init(mech_server_t *s, const mech_bus_t *bus, const flux_src_t *src);
void mech_server_read(mech_server_t *s, uint16_t block, reply_t *r);
void mech_server_idle(mech_server_t *s);
void mech_server_start(mech_server_t *s);   // stopped -> running: motor off, buffer dropped
void mech_server_stop(mech_server_t *s);
const srv_stats_t *mech_server_stats(const mech_server_t *s);
