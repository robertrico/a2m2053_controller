#include "test.h"
#include "loop.h"

// Input words for a run: pin 2 follows the driver, pin 3 inverts it, pin 4 stays low,
// pin 5 stays high, pin 6 follows except at one step.
static void fill(const bool *drv, uint32_t *in) {
    for (int k = 0; k < LOOP_STEPS; k++) {
        uint32_t w = 1u << 5;
        if (drv[k]) w |= 1u << 2;
        else w |= 1u << 3;
        if (drv[k] != (k == 7)) w |= 1u << 6;
        in[k] = w;
    }
}

int main(void) {
    bool drv[LOOP_STEPS];
    loop_pattern(drv);
    int ones = 0;
    for (int k = 0; k < LOOP_STEPS; k++) ones += drv[k];
    CHECK(ones > 0 && ones < LOOP_STEPS);       // both levels, or a stuck input could pass
    bool toggles = false;
    for (int k = 1; k < LOOP_STEPS; k++) toggles |= drv[k] != drv[k - 1];
    CHECK(toggles);

    uint32_t in[LOOP_STEPS];
    fill(drv, in);
    CHECK(loop_relation(drv, in, 2) == LOOP_FOLLOW);
    CHECK(loop_relation(drv, in, 3) == LOOP_INVERT);
    CHECK(loop_relation(drv, in, 4) == LOOP_LOW);
    CHECK(loop_relation(drv, in, 5) == LOOP_HIGH);
    CHECK(loop_relation(drv, in, 6) == LOOP_MIXED);

    CHECK(loop_rel_name(LOOP_FOLLOW)[0] == 'f');
    CHECK(loop_rel_name((loop_rel_t)99)[0] == '?');
    TEST_END();
}
