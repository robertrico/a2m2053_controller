#pragma once
// The floppy as a SmartPort store (M7 spec sec. 3): each call becomes one request to the
// mechanism server on core 1 and waits for its reply. ACK stays low meanwhile (sp_bus).
#include <stdbool.h>
#include <stdint.h>
#include "ipc.h"
#include "sp_store.h"

// Send one request to core 1 and wait up to timeout_ms for its reply. False = no reply
// (server off or wedged).
typedef bool (*fs_transact_t)(void *ctx, const req_t *rq, reply_t *rp, uint32_t timeout_ms);

typedef struct {
    fs_transact_t transact;
    void *ctx;
    uint8_t last_status, last_soft, last_tries;   // last mechanism reply (STATUS $05, console)
    req_t rq;
    reply_t rp;
} fstore_t;

void fstore_init(fstore_t *f, fs_transact_t transact, void *ctx);
sp_store_t fstore_store(fstore_t *f);
// ST_* reply status -> SmartPort code. Reads report $67 for a recal or wrong-cylinder seek.
uint8_t fstore_code(uint8_t st, uint8_t soft, bool is_read);
