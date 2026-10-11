#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "gcr.h"

// Streaming 800K GCR decoder (C port of tools/a2m/flux.py + track.py).
// Feed raw PIO interval counts (40 ns units) one at a time or in batches.
#define FLUX_DEC_OVERHEAD      2.0f                          // counts lost per edge in the PIO loop
#define FLUX_DEC_CELL_COUNTS   (1e9f / 489600.0f / 40.0f)    // 51.0621 counts per bit cell
#define FLUX_DEC_ALPHA         0.05f
// 1-3 cell intervals classify correctly only for c in (0.857C, 1.2C); inside that the loop always
// pulls back toward nominal. Mechanism speed is within +-2.5% (ERS).
#define FLUX_DEC_CELL_MIN      (FLUX_DEC_CELL_COUNTS * 0.87f)
#define FLUX_DEC_CELL_MAX      (FLUX_DEC_CELL_COUNTS * 1.19f)
#define FLUX_DEC_MAX_CELLS     8
#define FLUX_DEC_DATA_WINDOW   25                            // nibbles after the address epilog (ROM)

typedef enum { FLUX_EV_BAD_ADDR, FLUX_EV_FOREIGN, FLUX_EV_NO_DATA, FLUX_EV_BAD_DATA, FLUX_EV_RANGE } flux_ev_t;

typedef struct {
    void (*sector)(void *ctx, int sector, const uint8_t payload[GCR_SECTOR_LEN]);
    void (*event)(void *ctx, flux_ev_t ev, int track, int side, int sector);
    void *ctx;
    // At a good address field's DE AA (M5 writes start here), before the foreign/range checks.
    void (*header)(void *ctx, int track, int side, int sector);
} flux_dec_sink_t;

typedef enum { FD_HUNT_ADDR, FD_ADDR, FD_ADDR_EPILOG, FD_HUNT_DATA, FD_DATA } flux_dec_state_t;

typedef struct {
    const flux_dec_sink_t *sink;
    int track, side;
    float cell;                  // current cell estimate in counts
    uint8_t reg;                 // nibble latch
    int nbits;
    uint32_t last3;              // last three nibbles
    flux_dec_state_t st;
    int n;                       // sixes collected / epilog bytes seen
    int window;                  // nibbles left to find the data prolog
    gcr_addr_t addr;
    uint8_t six[GCR_DATA_SIXES];
    uint8_t payload[GCR_SECTOR_LEN];
} flux_dec_t;

void flux_dec_reset(flux_dec_t *d, int track, int side, const flux_dec_sink_t *sink);
void flux_dec_push(flux_dec_t *d, uint16_t count);
void flux_dec_push_many(flux_dec_t *d, const uint16_t *counts, int n);
