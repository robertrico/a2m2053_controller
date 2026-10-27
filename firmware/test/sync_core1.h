#pragma once
// Core 1 for host tests: a transact that runs the request on the mechanism server at once
// (no queue), the way core1_main dispatches it.
#include "fake_drive.h"
#include "fstore.h"

static mech_server_t *sync_srv;
static bool sync_off;                    // true: the server is off, every transact fails
static int sync_calls;

static bool sync_transact(void *ctx, req_t *rq, reply_t *rp, uint32_t timeout_ms) {
    (void)ctx; (void)timeout_ms;
    sync_calls++;
    if (sync_off) return false;
    switch (rq->op) {
    case REQ_READ:      mech_server_read(sync_srv, rq->block, rp); break;
    case REQ_WRITE:     mech_server_write(sync_srv, rq->block, rq->data, rp); break;
    case REQ_FORMAT_TS: mech_server_format_ts(sync_srv, rq->cyl, rq->side, rp); break;
    case REQ_MEDIA:     mech_server_media(sync_srv, rp); break;
    case REQ_EJECT:     mech_server_eject(sync_srv, rp); break;
    }
    rp->block = rq->block;
    rp->seq = rq->seq;
    return true;
}
