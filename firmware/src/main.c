#include <stdio.h>
#include "pico/stdlib.h"
#include "hw.h"
#include "mech.h"
#include "console.h"
#include "flux.h"
#include "crc32.h"

static mech_state_t ms;
static bool oe_on, enbl_on, motor_on;

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
    const mech_bus_t *b = hw_mech_bus();
    switch (c->op) {
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
        for (uint32_t i = 0; i < n * 2; i++) putchar_raw(p[i]);
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
