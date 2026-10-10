#pragma once
#include <stdint.h>
#include "console.h"

#define FLUX_MAX      CON_CAPTURE_MAX
#define FLUX_TICK_NS  40

void flux_init(void);
// Captures up to `count` intervals from the selected head. Returns how many
// were captured before `timeout_ms` (0 = no transitions at all).
uint32_t flux_capture(uint32_t count, uint32_t timeout_ms);
const uint16_t *flux_data(void);
