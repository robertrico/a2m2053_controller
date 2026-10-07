#pragma once
#include <stdbool.h>

// Call first thing in main(): mechanism shifter disabled, every output at its idle level.
void hw_init_safe(void);
void hw_mech_outputs_enable(bool on);   // U2 + U4 /OE
void hw_mech_enbl(bool on);             // mechanism /ENBL
void hw_led(bool on);
bool hw_mech_rd(void);
