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
#include "ramdisk.h"
#include "sp_dev.h"
#include "sp_bus.h"
#include "sp_hw.h"
#include "fstore.h"
#include "pins.h"
#include "tusb.h"

static mech_state_t ms;
static bool oe_on, enbl_on, motor_on;

static ipc_t ipc;
static mech_server_t srv;
static atomic_bool srv_run, srv_running;
static bool srv_wedged;

// M6 SmartPort RAM disk (spec sec. 6.2): 384 overlay slots = 192 KB.
#define RD_SLOTS 384
static uint8_t rd_slots[RD_SLOTS][RD_BLOCK_LEN];
static ramdisk_t rd;
static sp_store_t rd_store;
static sp_dev_t spdev;
static sp_bus_t spbus;
static bool splog = true;
static fstore_t fl;
static sp_store_t fl_store;
static bool disk_floppy;

static void hw_src_start(void *c) { (void)c; flux_stream_start(); }
static void hw_src_stop(void *c)  { (void)c; flux_stream_stop(); }
static int  hw_src_read(void *c, uint16_t *b, int max) { (void)c; return flux_stream_read(b, max); }
static uint64_t hw_src_now(void *c) { (void)c; return time_us_64(); }
static const flux_src_t hw_src = { hw_src_start, hw_src_stop, hw_src_read, hw_src_now, NULL };

// Stream backlog over the armed pass only (load to fire), for the BLK line: fire latency.
static volatile uint32_t tx_fire_backlog;
static void hw_tx_load(void *c, const uint8_t *b, size_t n) { (void)c; flux_stream_backlog_reset(); flux_tx_load(b, n); }
static void hw_tx_fire(void *c)  { (void)c; flux_tx_fire(); tx_fire_backlog = flux_stream_backlog_max(); }
static bool hw_tx_busy(void *c)  { (void)c; return flux_tx_busy(); }
static void hw_tx_abort(void *c) { (void)c; flux_tx_abort(); }
static const flux_tx_t hw_tx = { hw_tx_load, hw_tx_fire, hw_tx_busy, hw_tx_abort, NULL };

// Eject button (GP22): off until `ejbtn on`; how CN105-108 reaches GP22 is not settled
// (pin_budget.md: direct, or through U5), and a floating input must not eject disks.
static atomic_bool ejbtn;
static bool eject_button(void *ctx) { return atomic_load(&ejbtn) && hw_eject_pressed(ctx); }

static void core1_main(void) {
    mech_server_init(&srv, hw_mech_bus(), &hw_src, &hw_tx);
    mech_server_set_button(&srv, eject_button, NULL);
    bool led = false;
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
            if (rq.op == REQ_READ || rq.op == REQ_WRITE || rq.op == REQ_FORMAT_TS) {
                hw_led(true);                           // "in use" from the start, not after
                led = true;
            }
            switch (rq.op) {
            case REQ_READ:      mech_server_read(&srv, rq.block, &rp); break;
            case REQ_WRITE:     mech_server_write(&srv, rq.block, rq.data, &rp); break;
            case REQ_FORMAT_TS: mech_server_format_ts(&srv, rq.cyl, rq.side, &rp); break;
            case REQ_MEDIA:     mech_server_media(&srv, &rp); break;
            case REQ_EJECT:     mech_server_eject(&srv, &rp); break;
            }
            rp.block = rq.block;
            rp.seq = rq.seq;                            // core 0 matches replies by this
            srv.button_t = 0;                           // a press must span idle samples only
            while (!ipc_reply_push(&ipc, &rp)) tight_loop_contents();
        } else {
            mech_server_idle(&srv);
        }
        if (srv.motor_on != led) { led = srv.motor_on; hw_led(led); }   // LED = "in use", as the motor
    }
}

static bool server_set(bool on) {
    if (on) {
        if (atomic_load(&srv_run)) return true;         // already on: nothing reset
        if (atomic_load(&srv_running)) { puts("ERR server still stopping"); return false; }
        ipc_init(&ipc);                 // core 1 is stopped (srv_running false)
        srv_wedged = false;             // fresh queues: nothing stale left
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

// One request to core 1. The reply must carry this request's sequence number: a reply that
// arrives after its request timed out (or was abandoned) is dropped here. A SmartPort bus reset
// seen while waiting is latched for sp_bus (the card's reset is 80 ms; a seek or a FORMAT is
// longer). With abort_on_reset (SmartPort calls) the wait ends at once, so the bus is released
// in time for the card's single INIT try, as a real drive does (its reset is hardware).
static uint32_t req_seq;
static bool request_ex(req_t *rq, reply_t *r, uint32_t timeout_ms, bool abort_on_reset) {
    rq->seq = ++req_seq;
    if (!ipc_req_push(&ipc, rq)) return false;
    absolute_time_t until = make_timeout_time_ms(timeout_ms);
    for (;;) {
        if (ipc_reply_pop(&ipc, r)) {
            if (r->seq == rq->seq) return true;
            continue;                   // late reply to an earlier timed-out request
        }
        if (((sp_hw_pins() >> PIN_PH0) & 0xFu) == SP_PH_RESET) {
            sp_bus_note_reset(&spbus);
            if (abort_on_reset) return false;
        }
        if (time_reached(until)) { srv_wedged = true; return false; }
        tight_loop_contents();
    }
}

static bool request(req_t *rq, reply_t *r, uint32_t timeout_ms) { return request_ex(rq, r, timeout_ms, false); }

// Floppy store -> core 1. No server: no reply, the store reports offline / $27. A timeout
// does not lock the store out (sequence numbers drop the late reply); the console M5 paths
// still refuse while srv_wedged.
static bool fs_transact(void *ctx, req_t *rq, reply_t *rp, uint32_t timeout_ms) {
    (void)ctx;
    if (!atomic_load(&srv_run) || !atomic_load(&srv_running)) return false;
    return request_ex(rq, rp, timeout_ms, true);
}

static void sp_set_disk(bool floppy) {
    disk_floppy = floppy;
    spdev.store = floppy ? &fl_store : &rd_store;
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
        if (ch == PICO_ERROR_TIMEOUT) {
            // Late payload bytes must not run as console commands: drain until 100 ms of silence.
            absolute_time_t cap = make_timeout_time_ms(2000);
            while (!time_reached(cap) && getchar_timeout_us(100000) != PICO_ERROR_TIMEOUT) {}
            puts("ERR timeout");
            return;
        }
        rq.data[GCR_TAG_LEN + i] = (uint8_t)ch;
    }
    uint32_t crc = crc32_update(0, rq.data + GCR_TAG_LEN, GCR_DATA_LEN);
    if (!request(&rq, &r, SRV_REQ_TIMEOUT_MS + 3000)) { puts("ERR server timeout"); return; }
    printf("BLK %u %02x %02x %08lx %u %u %lu\n", block, r.status, r.soft, (unsigned long)crc,
           r.tries, r.fire_us, (unsigned long)tx_fire_backlog);
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
            if (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) {   // any byte stops a running format
                puts("FMT STOPPED");
                printf("FORMAT END %02x %lu\n", ST_IOERR, (unsigned long)((time_us_64() - t0) / 1000u));
                puts("OK");
                return;
            }
            if (!request(&rq, &r, SRV_FORMAT_TS_WAIT_MS)) { puts("ERR server timeout"); return; }
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

// Console line editor, polled from the main loop so the SmartPort bus keeps being served.
static char line[CON_LINE_MAX];
static size_t line_n;
static bool line_over;

// One character at most per call. True when a whole line is in `line` (or it overflowed).
static bool line_poll(void) {
    int ch = getchar_timeout_us(0);
    if (ch == PICO_ERROR_TIMEOUT) return false;
    if (ch == '\r' || ch == '\n') {
        if (line_n == 0 && !line_over) return false;
        line[line_n] = 0;
        fputs("\r\n", stdout);
        fflush(stdout);
        return true;
    }
    if (ch == 0x08 || ch == 0x7F) {
        if (line_n > 0 && !line_over) {
            line_n--;
            fputs("\b \b", stdout);
            fflush(stdout);
        }
        return false;
    }
    if (ch < 0x20) return false;
    if (line_n + 1 < sizeof line) {
        line[line_n++] = (char)ch;
        putchar(ch);
        fflush(stdout);
    } else line_over = true;
    return false;
}

static void print_us(const char *name, int32_t v) {
    if (v == INT32_MIN) printf(" %s=-", name);
    else printf(" %s=%ld", name, (long)v);
}

static void print_sp(void) {
    const sp_stats_t *s = &spbus.st;
    printf("SP %s id=%u last=%d pass=%d img=%lu ovl=%u/%u\n", spbus.enabled ? "on" : "off",
           spdev.id, spdev.last, spdev.passthrough, (unsigned long)(sp_image_end - sp_image),
           rd.used, rd.nslots);
    printf("SP disk=%s server=%s wedged=%d exec_ms last=%lu max=%lu mech st=%02x soft=%02x tries=%u\n",
           disk_floppy ? "floppy" : "ram", atomic_load(&srv_run) ? "on" : "off", srv_wedged,
           (unsigned long)s->exec_ms_last, (unsigned long)s->exec_ms_max, fl.last_status, fl.last_soft,
           fl.last_tries);
    printf("SP pkts=%lu acks=%lu cksum=%lu bad=%lu ign=%lu tmo=%lu resets=%lu rereads=%lu replies=%lu\n",
           (unsigned long)s->packets, (unsigned long)s->acks, (unsigned long)s->cksum, (unsigned long)s->bad,
           (unsigned long)s->ignored, (unsigned long)s->timeouts, (unsigned long)s->resets,
           (unsigned long)s->rereads, (unsigned long)s->replies);
    printf("SP ack_us");
    print_us("last", s->ack_us_last);
    print_us("max", s->ack_us_max);
    print_us("data_max", s->data_ack_us_max);
    printf(" late=%lu late_entry=%lu\n", (unsigned long)s->late, (unsigned long)s->late_entry);
    uint32_t other = 0;
    for (int i = SP_INIT + 1; i <= SP_NCMDS; i++) other += spdev.cmds[i];
    printf("SP cmds status=%lu readblk=%lu writeblk=%lu format=%lu control=%lu init=%lu other=%lu\n",
           (unsigned long)spdev.cmds[SP_STATUS], (unsigned long)spdev.cmds[SP_READBLOCK],
           (unsigned long)spdev.cmds[SP_WRITEBLOCK], (unsigned long)spdev.cmds[SP_FORMAT],
           (unsigned long)spdev.cmds[SP_CONTROL], (unsigned long)spdev.cmds[SP_INIT], (unsigned long)other);
    uint32_t g = sp_hw_pins();
    printf("SP pins PH0=%lu PH1=%lu PH2=%lu PH3=%lu WRREQ=%lu HSTEN=%lu WR=%lu LASTONE=%lu\n",
           (unsigned long)((g >> PIN_PH0) & 1), (unsigned long)((g >> PIN_PH1) & 1),
           (unsigned long)((g >> PIN_PH2) & 1), (unsigned long)((g >> PIN_PH3) & 1),
           (unsigned long)((g >> PIN_HOST_WRREQ) & 1), (unsigned long)((g >> PIN_HOST_HSTEN) & 1),
           (unsigned long)((g >> PIN_HOST_WR) & 1), (unsigned long)((g >> PIN_LASTONE) & 1));
    puts("OK");
}

// One transaction log line, only when the USB buffer has room for all of it (the CDC TX buffer
// is 64 bytes): never blocks the bus loop. A line that does not fit waits for the next call.
static void splog_poll(void) {
    static const char *const kind[] = { "CMD", "IGN", "CKSUM", "BAD", "TIMEOUT", "RESET", "REREAD", "LATE" };
    static char pend[64];
    static int pend_len;
    if (!splog || !tud_cdc_connected()) return;
    if (!pend_len) {
        sp_log_t e;
        if (!sp_bus_log_pop(&spbus, &e)) return;
        int n = snprintf(pend, sizeof pend, "SPL %s", e.kind < sizeof kind / sizeof kind[0] ? kind[e.kind] : "?");
        if (e.kind == SP_LOG_CMD || e.kind == SP_LOG_IGNORED || e.kind == SP_LOG_TIMEOUT)
            n += snprintf(pend + n, sizeof pend - n, " d=%u c=%02x b=%lu s=%02x", e.dest, e.cmd,
                          (unsigned long)e.block, e.stat);
        if (e.kind == SP_LOG_TIMEOUT && n < (int)sizeof pend)
            n += snprintf(pend + n, sizeof pend - n, " stage=%u", e.stage);
        if ((e.kind == SP_LOG_CMD || e.kind == SP_LOG_LATE) && n < (int)sizeof pend) {
            if (e.ack_us == INT32_MIN) n += snprintf(pend + n, sizeof pend - n, " ack=-");
            else n += snprintf(pend + n, sizeof pend - n, " ack=%ld", (long)e.ack_us);
        }
        if (e.kind == SP_LOG_CMD && e.ms && n < (int)sizeof pend)
            n += snprintf(pend + n, sizeof pend - n, " ms=%u", e.ms);
        if (n > (int)sizeof pend - 2) n = (int)sizeof pend - 2;
        pend[n++] = '\n';
        pend[n] = 0;
        pend_len = n;
    }
    if ((int)tud_cdc_write_available() < pend_len) return;
    fputs(pend, stdout);
    fflush(stdout);
    pend_len = 0;
}

// imgcopy: the flash ProDOS image onto the floppy, block by block (REQ_WRITE, verified).
// Makes a bootable test floppy (M7 spec sec. 11). The bus is not served meanwhile.
static void do_imgcopy(void) {
    static req_t rq;
    static reply_t r;
    size_t len = (size_t)(sp_image_end - sp_image);
    if (len < (size_t)GCR_BLOCKS * GCR_DATA_LEN) { puts("ERR no 800K image in flash"); return; }
    if (srv_wedged) { puts("ERR server wedged (server off, then on)"); return; }
    uint64_t t0 = time_us_64();
    uint8_t status = ST_OK;
    int done = 0;
    for (int b = 0; b < GCR_BLOCKS; b++) {
        if (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) { puts("IMGCOPY STOPPED"); status = ST_IOERR; break; }
        memset(&rq, 0, sizeof rq);
        rq.op = REQ_WRITE;
        rq.block = (uint16_t)b;
        memcpy(rq.data + GCR_TAG_LEN, sp_image + (size_t)b * GCR_DATA_LEN, GCR_DATA_LEN);
        if (!request(&rq, &r, SRV_REQ_TIMEOUT_MS + 3000)) { puts("ERR server timeout"); return; }
        if (r.status != ST_OK && r.status != ST_SOFT) {
            printf("IMG %d %02x\n", b, r.status);
            status = r.status;
            break;
        }
        done = b + 1;
        if (done % 100 == 0) { printf("IMG %d\n", done); stdio_flush(); }
    }
    printf("IMGCOPY END %02x %d %lu\n", status, done, (unsigned long)((time_us_64() - t0) / 1000u));
    puts("OK");
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
    case CON_SP:
        if (c->argc) sp_bus_enable(&spbus, c->arg[0]);
        print_sp();
        return;
    case CON_SPDISK:
        sp_set_disk(c->arg[0]);
        if (c->arg[0] && !atomic_load(&srv_run) && !server_set(true)) { puts("ERR server did not start"); return; }
        print_sp();
        return;
    case CON_IMGCOPY:
        if (!atomic_load(&srv_run)) { puts("ERR server off"); return; }
        do_imgcopy();
        return;
    case CON_EJBTN:
        atomic_store(&ejbtn, c->arg[0] != 0);
        puts("OK");
        return;
    case CON_SPLOG:
        splog = c->arg[0];
        puts("OK");
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
    sp_hw_init();
    ramdisk_init(&rd, sp_image, (size_t)(sp_image_end - sp_image), rd_slots, RD_SLOTS);
    rd_store = ramdisk_store(&rd);
    fstore_init(&fl, fs_transact, NULL);
    fl_store = fstore_store(&fl);
    sp_dev_init(&spdev, &rd_store, sp_hw_lastone, NULL);
    server_set(true);                   // M7: the mechanism server runs from boot
    sp_set_disk(true);                  // the floppy is the disk (sp disk ram for the M6 RAM disk)
    sp_bus_init(&spbus, sp_hw_hal(), &spdev);
    sp_bus_enable(&spbus, true);        // serve the Liron card from power-on
    for (;;) {
        sp_bus_poll(&spbus);
        if (line_poll()) {
            if (line_over) puts("ERR line too long");
            else {
                con_cmd_t c = console_parse(line);
                execute(&c);
            }
            line_n = 0;
            line_over = false;
        } else {
            splog_poll();
        }
    }
}
