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
#include "flux_tx.h"
#include "wtrack.h"

static mech_state_t ms;
static bool oe_on, enbl_on, motor_on;

static ipc_t ipc;
static mech_server_t srv;
static atomic_bool srv_run, srv_running;
static bool srv_wedged;

static void hw_src_start(void *c) { (void)c; flux_stream_start(); }
static void hw_src_stop(void *c)  { (void)c; flux_stream_stop(); }
static int  hw_src_read(void *c, uint16_t *b, int max) { (void)c; return flux_stream_read(b, max); }
static uint64_t hw_src_now(void *c) { (void)c; return time_us_64(); }
static const flux_src_t hw_src = { hw_src_start, hw_src_stop, hw_src_read, hw_src_now, NULL };

static void hw_tx_load(void *c, const uint8_t *b, size_t n) { (void)c; flux_tx_load(b, n); }
static void hw_tx_fire(void *c)  { (void)c; flux_tx_fire(); }
static bool hw_tx_busy(void *c)  { (void)c; return flux_tx_busy(); }
static void hw_tx_abort(void *c) { (void)c; flux_tx_abort(); }
static const flux_tx_t hw_tx = { hw_tx_load, hw_tx_fire, hw_tx_busy, hw_tx_abort, NULL };

static void core1_main(void) {
    mech_server_init(&srv, hw_mech_bus(), &hw_src, &hw_tx);
    for (;;) {
        if (!atomic_load(&srv_run)) {
            if (atomic_load(&srv_running)) { mech_server_stop(&srv); atomic_store(&srv_running, false); }
            sleep_ms(1);
            continue;
        }
        if (!atomic_load(&srv_running)) { mech_server_start(&srv); atomic_store(&srv_running, true); }
        static req_t rq;
        if (ipc_req_pop(&ipc, &rq)) {
            static reply_t rp;
            switch (rq.op) {
            case REQ_READ:      mech_server_read(&srv, rq.block, &rp); break;
            case REQ_WRITE:     mech_server_write(&srv, rq.block, rq.data, &rp); break;
            case REQ_FORMAT_TS: mech_server_format_ts(&srv, rq.cyl, rq.side, &rp); rp.block = rq.block; break;
            }
            while (!ipc_reply_push(&ipc, &rp)) tight_loop_contents();
        } else {
            mech_server_idle(&srv);
        }
    }
}

static bool server_set(bool on) {
    if (on) {
        if (atomic_load(&srv_run)) return true;         // already on: nothing reset
        if (atomic_load(&srv_running)) { puts("ERR server still stopping"); return false; }
        ipc_init(&ipc);                 // core 1 is stopped (srv_running false)
        srv.ms.track_known = false;     // core 1 re-homes; core 0's record is stale
        hw_mech_outputs_enable(true);
        hw_mech_enbl(true);
        oe_on = enbl_on = true;
        atomic_store(&srv_run, true);
        absolute_time_t up = make_timeout_time_ms(100);
        while (!atomic_load(&srv_running) && !time_reached(up)) sleep_ms(1);
        if (!atomic_load(&srv_running)) {
            atomic_store(&srv_run, false);
            hw_mech_enbl(false);
            hw_mech_outputs_enable(false);
            oe_on = enbl_on = false;
            puts("ERR server did not start");
            return false;
        }
        return true;
    }
    atomic_store(&srv_run, false);
    absolute_time_t until = make_timeout_time_ms(SRV_REQ_TIMEOUT_MS + 3000);
    while (atomic_load(&srv_running) && !time_reached(until)) sleep_ms(1);
    hw_mech_enbl(false);
    hw_mech_outputs_enable(false);
    oe_on = enbl_on = motor_on = false;
    hw_led(false);
    if (atomic_load(&srv_running)) return false;
    ms.track_known = false;             // core 1 may have stepped
    srv_wedged = false;
    return true;
}

static bool request(const req_t *rq, reply_t *r, uint32_t timeout_ms) {
    if (!ipc_req_push(&ipc, rq)) return false;
    absolute_time_t until = make_timeout_time_ms(timeout_ms);
    for (;;) {
        if (ipc_reply_pop(&ipc, r)) {
            if (r->block == rq->block) return true;
            continue;                   // stale reply from an earlier timed-out request
        }
        if (time_reached(until)) { srv_wedged = true; return false; }
        tight_loop_contents();
    }
}

static bool read_request(uint16_t block, reply_t *r) {
    static req_t rq;
    memset(&rq, 0, sizeof rq);
    rq.op = REQ_READ;
    rq.block = block;
    return request(&rq, r, SRV_REQ_TIMEOUT_MS + 3000);
}

static void do_readblk(uint16_t block) {
    static reply_t r;
    if (srv_wedged) { puts("ERR server wedged (server off, then on)"); return; }
    if (!read_request(block, &r)) { puts("ERR server timeout"); return; }
    printf("BLK %u %02x %02x\n", block, r.status, r.soft);
    for (int i = 0; i < GCR_SECTOR_LEN; i += 16) {
        int n = GCR_SECTOR_LEN - i < 16 ? GCR_SECTOR_LEN - i : 16;
        for (int k = 0; k < n; k++) printf("%02x", r.data[i + k]);
        printf("\n");
    }
    puts("OK");
}

// writeblk: 512 raw bytes follow the SEND line (no echo); tags are zeroed as the ROM's WriteBlock.
static void do_writeblk(uint16_t block) {
    static req_t rq;
    static reply_t r;
    if (srv_wedged) { puts("ERR server wedged (server off, then on)"); return; }
    memset(&rq, 0, sizeof rq);
    rq.op = REQ_WRITE;
    rq.block = block;
    puts("SEND 512");
    stdio_flush();
    absolute_time_t until = make_timeout_time_ms(2000);
    for (int i = 0; i < GCR_DATA_LEN; i++) {
        int ch;
        do ch = getchar_timeout_us(1000); while (ch == PICO_ERROR_TIMEOUT && !time_reached(until));
        if (ch == PICO_ERROR_TIMEOUT) { puts("ERR timeout"); return; }
        rq.data[GCR_TAG_LEN + i] = (uint8_t)ch;
    }
    uint32_t crc = crc32_update(0, rq.data + GCR_TAG_LEN, GCR_DATA_LEN);
    flux_stream_backlog_reset();
    if (!request(&rq, &r, SRV_REQ_TIMEOUT_MS + 3000)) { puts("ERR server timeout"); return; }
    printf("BLK %u %02x %02x %08lx %u %u %lu\n", block, r.status, r.soft, (unsigned long)crc,
           r.tries, r.fire_us, (unsigned long)flux_stream_backlog_max());
    puts("OK");
}

static void do_format(const con_cmd_t *c) {
    static req_t rq;
    static reply_t r;
    if (srv_wedged) { puts("ERR server wedged (server off, then on)"); return; }
    uint64_t t0 = time_us_64();
    int t_lo = c->argc ? (int)c->arg[0] : 0, t_hi = c->argc ? (int)c->arg[0] : GCR_TRACKS - 1;
    int s_lo = c->argc ? (int)c->arg[1] : 0, s_hi = c->argc ? (int)c->arg[1] : GCR_SIDES - 1;
    uint8_t status = ST_OK;
    for (int t = t_lo; t <= t_hi && (status == ST_OK || status == ST_SOFT); t++) {
        for (int s = s_lo; s <= s_hi && (status == ST_OK || status == ST_SOFT); s++) {
            memset(&rq, 0, sizeof rq);
            rq.op = REQ_FORMAT_TS;
            rq.block = (uint16_t)(0x8000u | (unsigned)(t << 1) | (unsigned)s);
            rq.cyl = (uint8_t)t;
            rq.side = (uint8_t)s;
            if (!request(&rq, &r, SRV_FORMAT_TS_TIMEOUT_MS + 3000)) { puts("ERR server timeout"); return; }
            printf("FMT %d %d %u %02x\n", t, s, r.tries, r.status);
            stdio_flush();
            if (r.status == ST_SOFT && status == ST_OK) status = ST_SOFT;
            else if (r.status != ST_OK && r.status != ST_SOFT) status = r.status;
        }
    }
    printf("FORMAT END %02x %lu\n", status, (unsigned long)((time_us_64() - t0) / 1000u));
    puts("OK");
}

static void do_readdisk(void) {
    static uint8_t ts[12 * GCR_DATA_LEN];
    static reply_t r;
    uint64_t t0 = time_us_64();
    if (srv_wedged) { puts("ERR server wedged (server off, then on)"); return; }
    uint64_t dec0 = mech_server_stats(&srv)->decode_us;
    uint32_t iv0 = mech_server_stats(&srv)->intervals;
    flux_stream_backlog_reset();
    printf("DISK BEGIN %d\n", GCR_BLOCKS);
    for (int t = 0; t < GCR_TRACKS; t++) {
        for (int s = 0; s < GCR_SIDES; s++) {
            int nsect = gcr_sectors_per_track(t);
            uint8_t status = ST_OK, soft = 0;
            for (int sec = 0; sec < nsect; sec++) {
                uint16_t b = (uint16_t)gcr_tss_to_block(t, s, sec);
                if (!read_request(b, &r)) { puts("\nERR server timeout"); return; }
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
    // Core 1 stats are read only after the last reply was popped (queue acquire orders them).
    uint64_t dec_us = mech_server_stats(&srv)->decode_us - dec0;
    uint32_t ivs = mech_server_stats(&srv)->intervals - iv0;
    printf("DISK END %lu %lu %lu\n", (unsigned long)(ivs ? dec_us * 1000u / ivs : 0),
           (unsigned long)flux_stream_backlog_max(), (unsigned long)((time_us_64() - t0) / 1000u));
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
                    c->op == CON_EJECT || c->op == CON_TACH || c->op == CON_CAPTURE || c->op == CON_SETTLE;
    if (mech_cmd && (atomic_load(&srv_run) || atomic_load(&srv_running))) {
        if (c->op == CON_OE && !c->arg[0]) {            // kill switch: stop the server, then the usual oe off
            if (!server_set(false)) { puts("ERR server did not stop"); return; }
        } else {
            puts("ERR server on");
            return;
        }
    }
    const mech_bus_t *b = hw_mech_bus();
    switch (c->op) {
    case CON_SERVER:
        if (server_set(c->arg[0])) puts("OK");
        else if (!c->arg[0]) puts("ERR server did not stop");
        return;
    case CON_READBLK:
        if (!atomic_load(&srv_run)) { puts("ERR server off"); return; }
        do_readblk((uint16_t)c->arg[0]);
        return;
    case CON_READDISK:
        if (!atomic_load(&srv_run)) { puts("ERR server off"); return; }
        do_readdisk();
        return;
    case CON_WRITEBLK:
        if (!atomic_load(&srv_run)) { puts("ERR server off"); return; }
        do_writeblk((uint16_t)c->arg[0]);
        return;
    case CON_FORMAT:
        if (!atomic_load(&srv_run)) { puts("ERR server off"); return; }
        do_format(c);
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
    flux_tx_init();
    ipc_init(&ipc);
    atomic_init(&srv_run, false);
    atomic_init(&srv_running, false);
    multicore_reset_core1();            // after an SWD flash/reset core 1 may not be in its boot-ROM wait
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
