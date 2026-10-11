#include <string.h>
#include "test.h"
#include "trackbuf.h"
#include "ipc.h"

static void test_trackbuf(void) {
    static trackbuf_t b;
    uint8_t p[524], q[524];
    memset(p, 0x11, sizeof p); memset(q, 0x22, sizeof q);
    trackbuf_init(&b);
    CHECK(!trackbuf_matches(&b, 0, 0));
    trackbuf_start(&b, 70, 1);
    CHECK(b.nsect == 8 && trackbuf_matches(&b, 70, 1) && !trackbuf_matches(&b, 70, 0));
    CHECK(!trackbuf_complete(&b) && !trackbuf_has(&b, 3));
    CHECK(trackbuf_put(&b, 3, p));
    CHECK(!trackbuf_put(&b, 3, q));                 // first good copy wins
    CHECK(memcmp(trackbuf_get(&b, 3), p, 524) == 0);
    CHECK(!trackbuf_put(&b, 8, p) && !trackbuf_put(&b, -1, p));
    for (int s = 0; s < 8; s++) trackbuf_put(&b, s, p);
    CHECK(trackbuf_complete(&b));
    trackbuf_invalidate(&b);
    CHECK(!trackbuf_matches(&b, 70, 1) && !trackbuf_has(&b, 3));
}

static void test_ipc(void) {
    static ipc_t q;
    ipc_init(&q);
    req_t r = { .op = REQ_READ, .block = 0 };
    for (int i = 0; i < IPC_REQ_SLOTS; i++) { r.block = (uint16_t)i; CHECK(ipc_req_push(&q, &r)); }
    CHECK(!ipc_req_push(&q, &r));                    // full
    for (int i = 0; i < IPC_REQ_SLOTS; i++) { CHECK(ipc_req_pop(&q, &r)); CHECK(r.block == i); }
    CHECK(!ipc_req_pop(&q, &r));                     // empty
    static reply_t a, b;
    memset(&a, 0, sizeof a); a.block = 7; a.status = ST_SOFT; a.soft = SOFT_RECAL; a.data[523] = 0x5A;
    CHECK(ipc_reply_push(&q, &a));
    CHECK(ipc_reply_pop(&q, &b));
    CHECK(b.block == 7 && b.status == 0xE7 && b.soft == 0x01 && b.data[523] == 0x5A);
    CHECK(!ipc_reply_pop(&q, &b));
    memset(&a, 0, sizeof a); a.block = 0x8007; a.tries = 3; a.fire_us = 1234;
    CHECK(ipc_reply_push(&q, &a) && ipc_reply_pop(&q, &b));
    CHECK(b.block == 0x8007 && b.tries == 3 && b.fire_us == 1234);
    req_t w = { .op = REQ_WRITE, .block = 9 };
    w.data[0] = 0x11; w.data[523] = 0x22;
    CHECK(ipc_req_push(&q, &w) && ipc_req_pop(&q, &r));
    CHECK(r.op == REQ_WRITE && r.block == 9 && r.data[0] == 0x11 && r.data[523] == 0x22);
}

int main(void) {
    test_trackbuf();
    test_ipc();
    TEST_END();
}
