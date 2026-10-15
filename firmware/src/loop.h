#pragma once
// Rear-port loopback test (M8): drive one output at a time through a fixed pattern and
// classify how each input pin moved. Pure logic; the GPIO side is in main.c (`loop`).
#include <stdbool.h>
#include <stdint.h>

#define LOOP_STEPS 32

typedef enum { LOOP_MIXED, LOOP_LOW, LOOP_HIGH, LOOP_FOLLOW, LOOP_INVERT } loop_rel_t;

// The drive pattern: both levels, irregular, so no stuck or periodic input can pass.
void loop_pattern(bool drv[LOOP_STEPS]);
// How input `pin` (bit in each word of `in`) moved against `drv` over LOOP_STEPS steps.
loop_rel_t loop_relation(const bool drv[LOOP_STEPS], const uint32_t in[LOOP_STEPS], unsigned pin);
const char *loop_rel_name(loop_rel_t r);
