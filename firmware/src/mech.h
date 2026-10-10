#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "mech_addr.h"

// Timing. "ROM" = liron.asm drive firmware; delay(A) there is ~A ms at ~2 MHz.
#define MECH_TRACKS              80
#define MECH_SETTLE_US_DEFAULT   10        // ROM Se640 samples ~20 us after select
#define MECH_SETUP_US            1
#define MECH_STROBE_US           2         // ROM Se64d: ph3 on, ph3 off
#define MECH_HOLD_US             1
#define MECH_STEP_POLL_US        100
#define MECH_STEP_TIMEOUT_US     50000     // ROM has none. /STEP goes high once the step is counted (ERS 3.2.4.2, T2 72 us max),
                                           // not when the head arrives; 50 ms is a generous bound
#define MECH_STEP_SETTLE_US      150       // ROM Se4cf tail loop
#define MECH_RECAL_POLLS         80        // ROM Le4f7: $50 polls
#define MECH_RECAL_POLL_US       7000      // ROM: delay(7)
#define MECH_READY_POLL_US       1000
#define MECH_READY_TIMEOUT_US    1000000   // ERS 3.4.3.3 T2 is 500 ms max with steps at the maximum rate; 2x margin
#define MECH_SPINUP_TIMEOUT_US   2000000
#define MECH_EJECT_MOTOR_OFF_US  200000    // ROM Se51b: delay($C8)
#define MECH_EJECT_STROBE_US     500000    // LSTRB held high for eject: tashnotes mci.md "~0.5 s". The ROM's short
                                           // strobe only works through the CXD1013; bench 2026-10-10: 2 us does nothing.
#define MECH_EJECT_TRIES         3         // ROM Z39
#define MECH_EJECT_POLLS         150       // ROM Z3B = $96
#define MECH_EJECT_POLL_US       10000     // ROM: delay($0A)

typedef struct {
    void (*set_addr)(void *ctx, uint8_t addr);   // CA2..CA0 + SEL only
    void (*set_lstrb)(void *ctx, bool high);
    bool (*read_rd)(void *ctx);
    void (*delay_us)(void *ctx, uint32_t us);
    void *ctx;
} mech_bus_t;

typedef struct {
    int track;
    bool track_known;
    uint32_t settle_us;
} mech_state_t;

typedef enum {
    MECH_OK = 0,
    MECH_ERR_STEP_TIMEOUT,
    MECH_ERR_TRACK0,
    MECH_ERR_NOT_READY,
    MECH_ERR_EJECT,
    MECH_ERR_RANGE,
} mech_err_t;

void mech_state_init(mech_state_t *s);
const char *mech_err_str(mech_err_t e);
bool mech_status(const mech_bus_t *b, const mech_state_t *s, mech_reg_t reg);
uint16_t mech_status_all(const mech_bus_t *b, const mech_state_t *s);
void mech_command(const mech_bus_t *b, mech_cmd_t cmd);
mech_err_t mech_step(const mech_bus_t *b, const mech_state_t *s, bool dir_in, int count);
mech_err_t mech_recal(const mech_bus_t *b, mech_state_t *s);
mech_err_t mech_seek(const mech_bus_t *b, mech_state_t *s, int track);
mech_err_t mech_motor(const mech_bus_t *b, const mech_state_t *s, bool on);
mech_err_t mech_eject(const mech_bus_t *b, mech_state_t *s);
void mech_select_head(const mech_bus_t *b, int side);
