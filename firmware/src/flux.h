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

#define FLUX_RING_LEN 8192   // 16 KB ring of uint16 counts, about 16 ms at the worst-case rate

// Continuous capture for the mechanism server (core 1). The head must already be selected.
void flux_stream_start(void);
void flux_stream_stop(void);
int flux_stream_read(uint16_t *buf, int max);   // copied, 0 if none, -1 if the ring overran
