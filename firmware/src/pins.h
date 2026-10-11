#pragma once
// GPIO map: docs/pin_budget.md. Shifters: U1 (B->A, always on),
// U2 + U4 (A->B, /OE = PIN_MECH_OE), U3 (A->B, /OE = PIN_HOST_RD_OE), U5 (B->A).
#define PIN_PH3_OUT       0
#define PIN_LASTONE       1
#define PIN_PH0           2
#define PIN_PH1           3
#define PIN_PH2           4
#define PIN_PH3           5
#define PIN_HOST_WRREQ    6
#define PIN_HOST_HSTEN    7
#define PIN_HOST_WR       8
#define PIN_ACK           9   // via 74LVC1G07: high = released
#define PIN_HOST_RD      10
#define PIN_HOST_RD_OE   11   // U3 /OE, active low
#define PIN_CA0          12
#define PIN_CA1          13
#define PIN_CA2          14
#define PIN_LSTRB        15
#define PIN_SEL          16
#define PIN_MECH_ENBL    17   // /ENBL, active low
#define PIN_MECH_WRTGATE 18   // /WRTGATE, active low. Driven only by flux_tx.c (M5).
#define PIN_MECH_WRTDATA 19
#define PIN_MECH_RD      20   // read data + status (CA0-2/SEL select)
#define PIN_MECH_OE      21   // U2 + U4 /OE, active low, 10k pull-up to 3V3
#define PIN_EJECT_BTN    22
#define PIN_LED          26
#define PIN_ENBL_OUT     27
