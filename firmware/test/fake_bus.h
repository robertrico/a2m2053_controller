#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "mech.h"

typedef enum { EV_ADDR, EV_LSTRB, EV_READ, EV_DELAY } ev_type_t;
typedef struct { ev_type_t t; uint32_t v; } ev_t;

typedef struct fake {
    ev_t ev[64]; int n;                 // first 64 events only
    bool lstrb; int violations;         // set_addr while LSTRB high
    uint8_t addr;
    bool (*rd)(struct fake *f, uint8_t addr);
    int reads[16];
    int strobes[16];
    int seq[256]; int nseq;             // strobed addresses in order
    uint64_t now_us;
} fake_t;

static void f_push(fake_t *f, ev_type_t t, uint32_t v) {
    if (f->n < 64) { f->ev[f->n].t = t; f->ev[f->n].v = v; f->n++; }
}
static void f_set_addr(void *c, uint8_t a) {
    fake_t *f = c; if (f->lstrb) f->violations++; f->addr = a; f_push(f, EV_ADDR, a);
}
static void f_set_lstrb(void *c, bool h) {
    fake_t *f = c;
    if (h && !f->lstrb) { f->strobes[f->addr]++; if (f->nseq < 256) f->seq[f->nseq++] = f->addr; }
    f->lstrb = h; f_push(f, EV_LSTRB, h);
}
static bool f_read(void *c) {
    fake_t *f = c; f->reads[f->addr]++; f_push(f, EV_READ, f->addr); return f->rd(f, f->addr);
}
static void f_delay(void *c, uint32_t us) { fake_t *f = c; f->now_us += us; f_push(f, EV_DELAY, us); }

static mech_bus_t fake_bus(fake_t *f, bool (*rd)(fake_t *, uint8_t)) {
    memset(f, 0, sizeof *f);
    f->rd = rd;
    mech_bus_t b = { f_set_addr, f_set_lstrb, f_read, f_delay, f };
    return b;
}
