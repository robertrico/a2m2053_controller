#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "pico/stdlib.h"
#include "hw.h"
#include "mech.h"
#include "console.h"
#include "flux.h"
#include "crc32.h"
#include "pico/multicore.h"
#include "ipc.h"
#include "mech_server.h"
#include "gcr.h"

static mech_state_t ms;
static bool oe_on, enbl_on, motor_on;

static ipc_t ipc;
static mech_server_t srv;
static atomic_bool srv_run, srv_running;

static void hw_src_start(void *c) { (void)c; flux_stream_start(); }
static void hw_src_stop(void *c)  { (void)c; flux_stream_stop(); }
static int  hw_src_read(void *c, uint16_t *b, int max) { (void)c; return flux_stream_read(b, max); }
static uint64_t hw_src_now(void *c) { (void)c; return time_us_64(); }
static const flux_src_t hw_src = { hw_src_start, hw_src_stop, hw_src_read, hw_src_now, NULL };

static void core1_main(void) {
    mech_server_init(&srv, hw_mech_bus(), &hw_src);
    for (;;) {
        if (!atomic_load(&srv_run)) {
            if (atomic_load(&srv_running)) { mech_server_stop(&srv); atomic_store(&srv_running, false); }
            sleep_ms(1);
            continue;
        }
        atomic_store(&srv_running, true);
        req_t rq;
        if (ipc_req_pop(&ipc, &rq)) {
            static reply_t rp;
            mech_server_read(&srv, rq.block, &rp);
            while (!ipc_reply_push(&ipc, &rp)) tight_loop_contents();
        } else {
            mech_server_idle(&srv);
        }
    }
}

static bool server_set(bool on) {
    if (on) {
        hw_mech_outputs_enable(true);
        hw_mech_enbl(true);
        oe_on = enbl_on = true;
        atomic_store(&srv_run, true);
        return true;
    }
    atomic_store(&srv_run, false);
    absolute_time_t until = make_timeout_time_ms(2000);
    while (atomic_load(&srv_running) && !time_reached(until)) sleep_ms(1);
    hw_mech_enbl(false);
    hw_mech_outputs_enable(false);
    oe_on = enbl_on = motor_on = false;
    hw_led(false);
    return !atomic_load(&srv_running);
}

static bool request(uint16_t block, reply_t *r) {
    req_t rq = { REQ_READ, block };
    if (!ipc_req_push(&ipc, &rq)) return false;
    absolute_time_t until = make_timeout_time_ms(SRV_REQ_TIMEOUT_MS + 3000);
    while (!ipc_reply_pop(&ipc, r)) {
        if (time_reached(until)) return false;
        tight_loop_contents();
    }
    return true;
}

static void do_readblk(uint16_t block) {
    static reply_t r;
    if (!request(block, &r)) { puts("ERR server timeout"); return; }
    printf("BLK %u %02x %02x\n", block, r.status, r.soft);
    for (int i = 0; i < GCR_SECTOR_LEN; i += 16) {
        int n = GCR_SECTOR_LEN - i < 16 ? GCR_SECTOR_LEN - i : 16;
        for (int k = 0; k < n; k++) printf("%02x", r.data[i + k]);
        printf("\n");
    }
    puts("OK");
}

static void do_readdisk(void) {
    static uint8_t ts[12 * GCR_DATA_LEN];
    static reply_t r;
    uint64_t t0 = time_us_64();
    printf("DISK BEGIN %d\n", GCR_BLOCKS);
    for (int t = 0; t < GCR_TRACKS; t++) {
        for (int s = 0; s < GCR_SIDES; s++) {
            int nsect = gcr_sectors_per_track(t);
            uint8_t status = ST_OK, soft = 0;
            for (int sec = 0; sec < nsect; sec++) {
                uint16_t b = (uint16_t)gcr_tss_to_block(t, s, sec);
                if (!request(b, &r)) { memset(&r, 0, sizeof r); r.status = ST_IOERR; }
                memcpy(ts + sec * GCR_DATA_LEN, r.data + GCR_TAG_LEN, GCR_DATA_LEN);
                if (r.status == ST_IOERR || r.status == ST_NODISK) status = r.status;
                else if (r.status == ST_SOFT && status == ST_OK) status = ST_SOFT;
                soft |= r.soft;
            }
            printf("TS %d %d %d %02x %02x\n", t, s, nsect, status, soft);
            stdio_flush();
            stdio_put_string((const char *)ts, nsect * GCR_DATA_LEN, false, false);
            printf("\nTS END %08lx\n", (unsigned long)crc32_update(0, ts, (size_t)nsect * GCR_DATA_LEN));
        }
    }
    printf("DISK END %lu %lu\n", (unsigned long)mech_server_stats(&srv)->worst_ns_per_interval,
           (unsigned long)((time_us_64() - t0) / 1000u));
    puts("OK");
}

static bool read_line(char *buf, size_t len) {
    size_t n = 0;
    bool overflow = false;
    for (;;) {
        int ch = getchar_timeout_us(100000);
        if (ch == PICO_ERROR_TIMEOUT) continue;
        if (ch == '\r' || ch == '\n') {
            if (n == 0 && !overflow) continue;
            buf[n] = 0;
            fputs("\r\n", stdout);
            fflush(stdout);
            return !overflow;
        }
        if (ch == 0x08 || ch == 0x7F) {
            if (n > 0 && !overflow) {
                n--;
                fputs("\b \b", stdout);
                fflush(stdout);
            }
            continue;
        }
        if (ch < 0x20) continue;
        if (n + 1 < len) {
            buf[n++] = (char)ch;
            putchar(ch);
            fflush(stdout);
        } else overflow = true;
    }
}

static bool need_mech(void) {
    if (!oe_on) { puts("ERR oe off"); return false; }
    if (!enbl_on) { puts("ERR enbl off"); return false; }
    return true;
}

static void reply(mech_err_t e) {
    if (e == MECH_OK) puts("OK");
    else printf("ERR %s\n", mech_err_str(e));
}

static void print_status_all(const mech_bus_t *b) {
    uint16_t st = mech_status_all(b, &ms);
    printf("STATUS");
    for (int r = 0; r < 16; r++) printf(" %s=%d", mech_reg_names[r], (st >> r) & 1);
    printf("\n");
}

// /TACH: 60 pulses per revolution (Apple drive status codes), so over 0.5 s, rpm = edges * 2.
static void do_tach(const mech_bus_t *b) {
    b->set_addr(b->ctx, MREG_TACH);
    sleep_us(ms.settle_us);
    uint32_t edges = 0;
    bool last = hw_mech_rd();
    uint64_t end = time_us_64() + 500000;
    while (time_us_64() < end) {
        bool v = hw_mech_rd();
        if (v && !last) edges++;
        last = v;
    }
    printf("TACH edges=%lu rpm=%lu\n", (unsigned long)edges, (unsigned long)(edges * 2));
    puts("OK");
}

static void execute(const con_cmd_t *c) {
    bool mech_cmd = c->op == CON_OE || c->op == CON_ENBL || c->op == CON_STATUS || c->op == CON_CMD ||
                    c->op == CON_MOTOR || c->op == CON_RECAL || c->op == CON_SEEK || c->op == CON_HEAD ||
                    c->op == CON_EJECT || c->op == CON_TACH || c->op == CON_CAPTURE;
    if (mech_cmd && atomic_load(&srv_run)) { puts("ERR server on"); return; }
    const mech_bus_t *b = hw_mech_bus();
    switch (c->op) {
    case CON_SERVER:
        if (server_set(c->arg[0])) puts("OK"); else puts("ERR server did not stop");
        return;
    case CON_READBLK:
        if (!atomic_load(&srv_run)) { puts("ERR server off"); return; }
        do_readblk((uint16_t)c->arg[0]);
        return;
    case CON_READDISK:
        if (!atomic_load(&srv_run)) { puts("ERR server off"); return; }
        do_readdisk();
        return;
    case CON_NONE:
        return;
    case CON_HELP:
        puts(console_help());
        puts("OK");
        return;
    case CON_OE:
        if (!c->arg[0]) {
            hw_mech_enbl(false);
            enbl_on = motor_on = false;
            hw_led(false);
        }
        hw_mech_outputs_enable(c->arg[0]);
        oe_on = c->arg[0];
        puts("OK");
        return;
    case CON_ENBL:
        if (!oe_on) { puts("ERR oe off"); return; }
        hw_mech_enbl(c->arg[0]);
        enbl_on = c->arg[0];
        puts("OK");
        return;
    case CON_STATUS:
        if (!need_mech()) return;
        if (c->argc == 0) print_status_all(b);
        else printf("STATUS %s=%d\n", mech_reg_names[c->arg[0]],
                    mech_status(b, &ms, (mech_reg_t)c->arg[0]));
        puts("OK");
        return;
    case CON_CMD:
        if (!need_mech()) return;
        mech_command(b, (mech_cmd_t)c->arg[0]);
        ms.track_known = false;     // a raw command may have stepped
        puts("OK");
        return;
    case CON_MOTOR: {
        if (!need_mech()) return;
        mech_err_t e = mech_motor(b, &ms, c->arg[0]);
        motor_on = c->arg[0] && e == MECH_OK;
        hw_led(motor_on);
        reply(e);
        return;
    }
    case CON_RECAL:
        if (!need_mech()) return;
        reply(mech_recal(b, &ms));
        return;
    case CON_SEEK:
        if (!need_mech()) return;
        reply(mech_seek(b, &ms, (int)c->arg[0]));
        return;
    case CON_HEAD:
        if (!need_mech()) return;
        mech_select_head(b, (int)c->arg[0]);
        puts("OK");
        return;
    case CON_EJECT: {
        if (!need_mech()) return;
        mech_err_t e = mech_eject(b, &ms);
        if (e == MECH_OK) { motor_on = false; hw_led(false); }
        reply(e);
        return;
    }
    case CON_TACH:
        if (!need_mech()) return;
        do_tach(b);
        return;
    case CON_SETTLE:
        ms.settle_us = (uint32_t)c->arg[0];
        puts("OK");
        return;
    case CON_CAPTURE: {
        if (!need_mech()) return;
        if (!motor_on) { puts("ERR motor off"); return; }
        mech_err_t e = mech_seek(b, &ms, (int)c->arg[0]);
        if (e != MECH_OK) { printf("ERR seek: %s\n", mech_err_str(e)); return; }
        mech_select_head(b, (int)c->arg[1]);
        sleep_us(100);
        uint32_t n = flux_capture((uint32_t)c->arg[2], 2000);
        printf("FLUX BEGIN %lu %u\n", (unsigned long)n, FLUX_TICK_NS);
        stdio_flush();
        const uint8_t *p = (const uint8_t *)flux_data();
        stdio_put_string((const char *)p, (int)(n * 2), false, false);
        printf("\nFLUX END %08lx\n", (unsigned long)crc32_update(0, p, n * 2));
        puts("OK");
        return;
    }
    case CON_BAD:
    default:
        puts("ERR bad command (try help)");
        return;
    }
}

int main(void) {
    hw_init_safe();             // first: mechanism shifter Hi-Z, idle levels
    stdio_init_all();
    mech_state_init(&ms);
    flux_init();
    ipc_init(&ipc);
    atomic_init(&srv_run, false);
    atomic_init(&srv_running, false);
    multicore_launch_core1(core1_main);
    char line[CON_LINE_MAX];
    for (;;) {
        if (!read_line(line, sizeof line)) {
            puts("ERR line too long");
            continue;
        }
        con_cmd_t c = console_parse(line);
        execute(&c);
    }
}
