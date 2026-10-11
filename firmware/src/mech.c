#include "mech.h"

void mech_state_init(mech_state_t *s) {
    s->track = 0;
    s->track_known = false;
    s->settle_us = MECH_SETTLE_US_DEFAULT;
}

const char *mech_err_str(mech_err_t e) {
    switch (e) {
    case MECH_OK: return "ok";
    case MECH_ERR_STEP_TIMEOUT: return "step timeout";
    case MECH_ERR_TRACK0: return "track 0 not found";
    case MECH_ERR_NOT_READY: return "not ready";
    case MECH_ERR_EJECT: return "eject failed";
    case MECH_ERR_RANGE: return "track out of range";
    }
    return "unknown";
}

bool mech_status(const mech_bus_t *b, const mech_state_t *s, mech_reg_t reg) {
    b->set_addr(b->ctx, (uint8_t)reg);
    b->delay_us(b->ctx, s->settle_us);
    return b->read_rd(b->ctx);
}

uint16_t mech_status_all(const mech_bus_t *b, const mech_state_t *s) {
    uint16_t v = 0;
    for (int r = 0; r < 16; r++)
        if (mech_status(b, s, (mech_reg_t)r)) v |= (uint16_t)(1u << r);
    return v;
}

void mech_command(const mech_bus_t *b, mech_cmd_t cmd) {
    b->set_addr(b->ctx, (uint8_t)cmd);
    b->delay_us(b->ctx, MECH_SETUP_US);
    b->set_lstrb(b->ctx, true);
    b->delay_us(b->ctx, MECH_STROBE_US);
    b->set_lstrb(b->ctx, false);
    b->delay_us(b->ctx, MECH_HOLD_US);
}

static bool wait_level(const mech_bus_t *b, const mech_state_t *s, mech_reg_t reg,
                       bool level, uint32_t poll_us, uint32_t timeout_us) {
    uint32_t waited = 0;
    for (;;) {
        if (mech_status(b, s, reg) == level) return true;
        if (waited >= timeout_us) return false;
        b->delay_us(b->ctx, poll_us);
        waited += poll_us + s->settle_us;
    }
}

mech_err_t mech_step(const mech_bus_t *b, const mech_state_t *s, bool dir_in, int count) {
    mech_command(b, dir_in ? MCMD_DIR_IN : MCMD_DIR_OUT);
    for (int i = 0; i < count; i++) {
        mech_command(b, MCMD_STEP);
        if (!wait_level(b, s, MREG_STEP, true, MECH_STEP_POLL_US, MECH_STEP_TIMEOUT_US))
            return MECH_ERR_STEP_TIMEOUT;
        b->delay_us(b->ctx, MECH_STEP_SETTLE_US);
    }
    return MECH_OK;
}

mech_err_t mech_recal(const mech_bus_t *b, mech_state_t *s) {
    s->track_known = false;
    mech_err_t e = mech_step(b, s, false, MECH_TRACKS);   // ROM Le4f7: 80 steps outward
    if (e != MECH_OK) return e;
    for (int i = 0; i < MECH_RECAL_POLLS; i++) {
        b->delay_us(b->ctx, MECH_RECAL_POLL_US);
        if (!mech_status(b, s, MREG_TK0)) {
            s->track = 0;
            s->track_known = true;
            return MECH_OK;
        }
    }
    return MECH_ERR_TRACK0;
}

mech_err_t mech_seek(const mech_bus_t *b, mech_state_t *s, int track) {
    if (track < 0 || track >= MECH_TRACKS) return MECH_ERR_RANGE;
    bool moved = false;
    if (!s->track_known) {
        mech_err_t e = mech_recal(b, s);
        if (e != MECH_OK) return e;
        moved = true;                   // a recal moves the head too: wait READY below
    }
    int delta = track - s->track;
    if (delta != 0) {
        mech_err_t e = mech_step(b, s, delta > 0, delta > 0 ? delta : -delta);
        if (e != MECH_OK) {
            s->track_known = false;
            return e;
        }
        s->track = track;
        moved = true;
    }
    if (!moved) return MECH_OK;
    if (!wait_level(b, s, MREG_READY, false, MECH_READY_POLL_US, MECH_READY_TIMEOUT_US))
        return MECH_ERR_NOT_READY;
    return MECH_OK;
}

mech_err_t mech_motor(const mech_bus_t *b, const mech_state_t *s, bool on) {
    if (!on) {
        mech_command(b, MCMD_MOTOR_OFF);
        return MECH_OK;
    }
    mech_command(b, MCMD_MOTOR_ON);
    if (!wait_level(b, s, MREG_READY, false, MECH_READY_POLL_US, MECH_SPINUP_TIMEOUT_US)) {
        mech_command(b, MCMD_MOTOR_OFF);
        return MECH_ERR_NOT_READY;
    }
    return MECH_OK;
}

mech_err_t mech_eject(const mech_bus_t *b, mech_state_t *s) {
    if (mech_status(b, s, MREG_CSTIN)) return MECH_OK;       // no disk: nothing to do
    mech_command(b, MCMD_MOTOR_OFF);
    b->delay_us(b->ctx, MECH_EJECT_MOTOR_OFF_US);
    for (int t = 0; t < MECH_EJECT_TRIES; t++) {
        mech_command(b, MCMD_EJECT);
        for (int p = 0; p < MECH_EJECT_POLLS; p++) {
            b->delay_us(b->ctx, MECH_EJECT_POLL_US);
            if (mech_status(b, s, MREG_CSTIN)) {
                s->track_known = false;
                return MECH_OK;
            }
        }
    }
    return MECH_ERR_EJECT;
}

void mech_select_head(const mech_bus_t *b, int side) {
    b->set_addr(b->ctx, side ? MREG_RDDATA1 : MREG_RDDATA0);
}
