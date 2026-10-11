#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mech.h"
#include "flux_dec.h"
#include "trackbuf.h"
#include "ipc.h"
#include "wtrack.h"

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
#define SRV_SEL_SETTLE_US     100     // ERS86 p24 T6: SEL settled before /WRTGATE low
#define SRV_ERASE_TAIL_US     620     // ERS86 p24/p27: after /WRTGATE high, RD valid and /STEP allowed
#define SRV_TX_POLL_US        100
#define SRV_WRITE_TRIES       3       // block write: writes per request, each verified
#define SRV_FORMAT_TRIES          10      // ROM cmd_format_actual: spb_packet_type = $0A
#define SRV_FORMAT_TS_TIMEOUT_MS  30000   // one track side, all tries
// Core 0 must outlast core 1: the cap is checked only at the start of a try, so one try can run
// past it by about SRV_REQ_TIMEOUT_MS (seek, tx cap, verify fill) plus slack.
#define SRV_FORMAT_TS_WAIT_MS     (SRV_FORMAT_TS_TIMEOUT_MS + SRV_REQ_TIMEOUT_MS + 6000)
#define SRV_VERIFY_REVS       2       // verify reads after a write or format (ROM verify: one pass)
// Cap on one TX stream: nominal (16.34 us per byte) + 10 % + 1 ms.
#define SRV_TX_CAP_US(len)    ((uint64_t)(len) * 16340u / 1000u * 11u / 10u + 1000u)

typedef struct {
    void (*start)(void *ctx);                       // stream RD (head already selected)
    void (*stop)(void *ctx);
    int (*read)(void *ctx, uint16_t *buf, int max); // >=0 copied, -1 ring overrun
    uint64_t (*now_us)(void *ctx);
    void *ctx;
} flux_src_t;

// The write side. The PIO program raises /WRTGATE itself at the end of the stream;
// abort() stops it and returns the pins to SIO with the gate high (also called after
// every normal finish).
typedef struct {
    void (*load)(void *ctx, const uint8_t *buf, size_t len);   // armed, not running; gate high
    void (*fire)(void *ctx);                                   // start: gate low, stream
    bool (*busy)(void *ctx);                                   // false once the gate is back high
    void (*abort)(void *ctx);
    void *ctx;
} flux_tx_t;

typedef struct {
    uint64_t decode_us;     // time spent in flux_dec_push_many, all batches
    uint32_t intervals;     // intervals decoded
    uint32_t fills;
} srv_stats_t;

typedef struct {
    const mech_bus_t *bus;
    const flux_src_t *src;
    const flux_tx_t *tx;
    mech_state_t ms;
    bool motor_on;
    uint64_t last_us;
    trackbuf_t tb;
    flux_dec_t dec;
    flux_dec_sink_t sink;
    uint8_t soft;
    bool foreign_seen;
    int foreign_track;
    int arm_sector;         // >= 0: fire the TX at this sector's header (fill)
    bool fired;
    uint64_t read_us;       // when the interval being decoded was read
    uint16_t fire_us;
    uint8_t wbuf[WT_DATA_FIELD_LEN];
    uint8_t timg[WT_TRACK_MAX];
    srv_stats_t stats;
    uint16_t batch[SRV_BATCH];
} mech_server_t;

void mech_server_init(mech_server_t *s, const mech_bus_t *bus, const flux_src_t *src, const flux_tx_t *tx);
void mech_server_read(mech_server_t *s, uint16_t block, reply_t *r);
void mech_server_write(mech_server_t *s, uint16_t block, const uint8_t data[GCR_SECTOR_LEN], reply_t *r);
void mech_server_format_ts(mech_server_t *s, int cyl, int side, reply_t *r);
void mech_server_idle(mech_server_t *s);
void mech_server_start(mech_server_t *s);   // stopped -> running: motor off, buffer dropped
void mech_server_stop(mech_server_t *s);
const srv_stats_t *mech_server_stats(const mech_server_t *s);
