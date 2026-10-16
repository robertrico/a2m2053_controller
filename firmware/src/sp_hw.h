#pragma once
// RP2350 side of the SmartPort bus (M6 spec sec. 5): PIO0 fm_rx/fm_tx with DMA, ACK, U3 /OE.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sp_bus.h"

void sp_hw_init(void);                  // after hw_init_safe(); U3 stays off
const sp_hal_t *sp_hw_hal(void);
bool sp_hw_lastone(void *ctx);          // LASTONE/ level (GP1, via U5); internal pull-up as a fallback
uint32_t sp_hw_pins(void);              // raw GPIO in, for the console
// Host RD pad: true = plain GPIO (high) for the loopback test, false = back to fm_tx.
void sp_hw_rd_gpio(bool gpio);
// Rear port: true = PIO2 copies host PH3 -> PH3_OUT and /ENBL -> ENBL_OUT; false = idle (low, high).
void sp_hw_chain(bool on);

// The ProDOS image linked into flash (sp_image.S); length 0 when built without one.
extern const uint8_t sp_image[], sp_image_end[];
