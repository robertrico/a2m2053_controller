#include "loop.h"

#define LOOP_BITS 0x6C3A95E1u      // 32 steps, 16 ones, runs of 1 to 4

void loop_pattern(bool drv[LOOP_STEPS]) {
    for (int k = 0; k < LOOP_STEPS; k++) drv[k] = (LOOP_BITS >> k) & 1u;
}

loop_rel_t loop_relation(const bool drv[LOOP_STEPS], const uint32_t in[LOOP_STEPS], unsigned pin) {
    bool follow = true, invert = true, low = true, high = true;
    for (int k = 0; k < LOOP_STEPS; k++) {
        bool v = (in[k] >> pin) & 1u;
        follow &= v == drv[k];
        invert &= v != drv[k];
        low &= !v;
        high &= v;
    }
    return follow ? LOOP_FOLLOW : invert ? LOOP_INVERT : low ? LOOP_LOW : high ? LOOP_HIGH : LOOP_MIXED;
}

const char *loop_rel_name(loop_rel_t r) {
    static const char *const n[] = { "mixed", "low", "high", "follows", "inverts" };
    return (unsigned)r < sizeof n / sizeof n[0] ? n[r] : "?";
}
