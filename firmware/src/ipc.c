#include "ipc.h"
#include <string.h>

void ipc_init(ipc_t *q) {
    memset(q, 0, sizeof *q);
    atomic_init(&q->req_head, 0); atomic_init(&q->req_tail, 0);
    atomic_init(&q->rep_head, 0); atomic_init(&q->rep_tail, 0);
}

// head = next write (producer), tail = next read (consumer); counts run freely.
#define PUSH(arr, slots, head, tail, item)                                         \
    do {                                                                           \
        unsigned h = atomic_load_explicit(&q->head, memory_order_relaxed);         \
        unsigned t = atomic_load_explicit(&q->tail, memory_order_acquire);         \
        if (h - t >= (slots)) return false;                                        \
        q->arr[h % (slots)] = *(item);                                             \
        atomic_store_explicit(&q->head, h + 1, memory_order_release);              \
        return true;                                                               \
    } while (0)

#define POP(arr, slots, head, tail, item)                                          \
    do {                                                                           \
        unsigned t = atomic_load_explicit(&q->tail, memory_order_relaxed);         \
        unsigned h = atomic_load_explicit(&q->head, memory_order_acquire);         \
        if (h == t) return false;                                                  \
        *(item) = q->arr[t % (slots)];                                             \
        atomic_store_explicit(&q->tail, t + 1, memory_order_release);              \
        return true;                                                               \
    } while (0)

bool ipc_req_push(ipc_t *q, const req_t *r)     { PUSH(req, IPC_REQ_SLOTS, req_head, req_tail, r); }
bool ipc_req_pop(ipc_t *q, req_t *r)            { POP(req, IPC_REQ_SLOTS, req_head, req_tail, r); }
bool ipc_reply_push(ipc_t *q, const reply_t *r) { PUSH(rep, IPC_REPLY_SLOTS, rep_head, rep_tail, r); }
bool ipc_reply_pop(ipc_t *q, reply_t *r)        { POP(rep, IPC_REPLY_SLOTS, rep_head, rep_tail, r); }
