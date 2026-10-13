#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include "gcr.h"

#define ST_OK       0x00
#define ST_SOFT     0xE7   // ok after soft errors (ROM: $E7)
#define ST_IOERR    0xA7   // SmartPort $27 with the ROM's $80 bit
#define ST_NODISK   0xAF   // $2F
#define ST_BADBLK   0xAD   // $2D (also a bad cyl/side on REQ_FORMAT_TS)
#define ST_WRPROT   0xAB   // $2B

#define SOFT_RECAL    0x01
#define SOFT_FOREIGN  0x02
#define SOFT_BADDATA  0x04
#define SOFT_BADADDR  0x08
#define SOFT_OVERRUN  0x10

typedef enum { REQ_READ, REQ_WRITE, REQ_FORMAT_TS, REQ_MEDIA, REQ_EJECT } req_op_t;
typedef struct {
    uint32_t seq;                   // set by core 0; the reply echoes it (stale replies are dropped)
    req_op_t op;
    uint16_t block;                 // REQ_READ / REQ_WRITE; REQ_FORMAT_TS: a tag the reply echoes
    uint8_t cyl, side;              // REQ_FORMAT_TS
    uint8_t data[GCR_SECTOR_LEN];   // REQ_WRITE: 12 tag + 512 data
} req_t;
typedef struct {
    uint32_t seq;
    uint16_t block;
    uint8_t status;
    uint8_t soft;
    uint8_t tries;                  // REQ_WRITE / REQ_FORMAT_TS: writes used
    uint16_t fire_us;               // REQ_WRITE: header interval read -> TX fired, last write
    uint8_t data[GCR_SECTOR_LEN];   // REQ_READ: 12 tag + 512 data; zeros on error
} reply_t;

#define IPC_REQ_SLOTS   4
#define IPC_REPLY_SLOTS 2

// Two single-producer single-consumer rings: core 0 -> core 1 requests, core 1 -> core 0 replies.
typedef struct {
    req_t req[IPC_REQ_SLOTS];
    atomic_uint req_head, req_tail;
    reply_t rep[IPC_REPLY_SLOTS];
    atomic_uint rep_head, rep_tail;
} ipc_t;

void ipc_init(ipc_t *q);
bool ipc_req_push(ipc_t *q, const req_t *r);
bool ipc_req_pop(ipc_t *q, req_t *r);
bool ipc_reply_push(ipc_t *q, const reply_t *r);
bool ipc_reply_pop(ipc_t *q, reply_t *r);
