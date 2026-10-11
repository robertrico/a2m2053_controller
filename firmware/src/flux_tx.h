#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// TX state machine on PIO1 SM1 (M5). The PIO owns GP18/GP19 only from flux_tx_load()
// to flux_tx_abort(); otherwise they are SIO outputs at the hw.c idle levels (gate high).
void flux_tx_init(void);
void flux_tx_load(const uint8_t *buf, size_t len);   // armed, SM stopped, gate high
void flux_tx_fire(void);                              // one register write: SM enable
bool flux_tx_busy(void);                              // false between load and fire; false once the program has raised the gate
void flux_tx_abort(void);                             // SM off, DMA off, pins back to SIO, gate high
