#include "fstore.h"
#include <string.h>
#include "mech_server.h"

// A request can queue behind a button eject (up to 4.7 s: 0.2 s + 3 x 150 x 10 ms, mech.h).
#define FS_EJECT_MAX_MS  5000
#define FS_RW_WAIT_MS    (SRV_REQ_TIMEOUT_MS + 3000 + FS_EJECT_MAX_MS)
#define FS_FAST_WAIT_MS  (2000 + FS_EJECT_MAX_MS)    // media query itself: microseconds
#define FS_EJECT_WAIT_MS (2 * FS_EJECT_MAX_MS)

uint8_t fstore_code(uint8_t st, uint8_t soft, bool is_read) {
    switch (st) {
    case ST_OK:     return SP_E_OK;
    case ST_SOFT:   return is_read && (soft & (SOFT_FOREIGN | SOFT_BADDATA | SOFT_BADADDR)) ? SP_E_SOFT_IO : SP_E_OK;
    case ST_NODISK: return SP_E_OFFLINE;
    case ST_BADBLK: return SP_E_BADBLOCK;
    case ST_WRPROT: return SP_E_NOWRITE;
    default:        return SP_E_IOERROR;
    }
}

static bool ask(fstore_t *f, uint32_t timeout_ms) {
    if (!f->transact(f->ctx, &f->rq, &f->rp, timeout_ms)) {
        f->last_status = ST_IOERR;
        f->last_soft = f->last_tries = 0;
        return false;
    }
    f->last_status = f->rp.status;
    f->last_soft = f->rp.soft;
    f->last_tries = f->rp.tries;
    return true;
}

static void req(fstore_t *f, req_op_t op, uint16_t block) {
    memset(&f->rq, 0, sizeof f->rq);
    f->rq.op = op;
    f->rq.block = block;
}

static uint8_t fs_read(void *c, uint32_t block, uint8_t *out) {
    fstore_t *f = c;
    req(f, REQ_READ, (uint16_t)block);
    if (!ask(f, FS_RW_WAIT_MS)) return SP_E_IOERROR;
    uint8_t code = fstore_code(f->rp.status, f->rp.soft, true);
    if (code == SP_E_OK || code == SP_E_SOFT_IO) memcpy(out, f->rp.data + GCR_TAG_LEN, GCR_DATA_LEN);
    return code;
}

static uint8_t fs_write(void *c, uint32_t block, const uint8_t *in) {
    fstore_t *f = c;
    req(f, REQ_WRITE, (uint16_t)block);                    // 12 tag bytes stay zero (ROM WriteBlock)
    memcpy(f->rq.data + GCR_TAG_LEN, in, GCR_DATA_LEN);
    if (!ask(f, FS_RW_WAIT_MS)) return SP_E_IOERROR;
    return fstore_code(f->rp.status, f->rp.soft, false);
}

// Every track side, track 0 side 0 first; the first failure ends it (ROM cmd_format_actual).
static uint8_t fs_format(void *c) {
    fstore_t *f = c;
    for (int t = 0; t < GCR_TRACKS; t++)
        for (int s = 0; s < GCR_SIDES; s++) {
            req(f, REQ_FORMAT_TS, (uint16_t)(0x8000u | (unsigned)(t << 1) | (unsigned)s));
            f->rq.cyl = (uint8_t)t;
            f->rq.side = (uint8_t)s;
            if (!ask(f, SRV_FORMAT_TS_WAIT_MS)) return SP_E_IOERROR;
            uint8_t code = fstore_code(f->rp.status, f->rp.soft, false);
            if (code != SP_E_OK) return code;
        }
    return SP_E_OK;
}

static uint8_t fs_eject(void *c) {
    fstore_t *f = c;
    req(f, REQ_EJECT, 0xFFFE);
    if (!ask(f, FS_EJECT_WAIT_MS)) return SP_E_IOERROR;
    return fstore_code(f->rp.status, 0, false);
}

static sp_media_t fs_media(void *c) {
    fstore_t *f = c;
    req(f, REQ_MEDIA, 0xFFFF);
    if (!ask(f, FS_FAST_WAIT_MS)) return SP_MEDIA_NODISK;  // no server: offline
    return f->rp.status == ST_NODISK ? SP_MEDIA_NODISK
         : f->rp.status == ST_WRPROT ? SP_MEDIA_WRPROT : SP_MEDIA_OK;
}

// STATUS $05 (ROM Led96, $56-$5D). The working UniDisk returns 00 x 7 then $FF
// (M7 bench, SPCOMPARE): byte 7 ($5D) is $FF. Bytes 1-2 carry our soft flags and tries
// of the last command; they are 0 on a clean read, as on the UniDisk.
static void fs_unistat(void *c, uint8_t out[8]) {
    fstore_t *f = c;
    memset(out, 0, 8);
    out[1] = f->last_soft;
    out[2] = f->last_tries;
    out[7] = 0xFF;
}

void fstore_init(fstore_t *f, fs_transact_t transact, void *ctx) {
    memset(f, 0, sizeof *f);
    f->transact = transact;
    f->ctx = ctx;
}

sp_store_t fstore_store(fstore_t *f) {
    sp_store_t s = { fs_read, fs_write, fs_format, fs_eject, fs_media, fs_unistat, f };
    return s;
}
