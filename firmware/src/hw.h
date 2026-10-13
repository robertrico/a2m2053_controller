#pragma once
#include <stdbool.h>
#include "mech.h"

// Call first thing in main(): mechanism shifter disabled, every output at its idle level.
void hw_init_safe(void);
void hw_mech_outputs_enable(bool on);   // U2 + U4 /OE
void hw_mech_enbl(bool on);             // mechanism /ENBL
void hw_led(bool on);
bool hw_eject_pressed(void *ctx);       // GP22 low (switch to GND, internal pull-up)
bool hw_mech_rd(void);

// GPIO-backed mechanism bus (CA0-2 = GP12-14, SEL = GP16, LSTRB = GP15, RD = GP20).
const mech_bus_t *hw_mech_bus(void);
