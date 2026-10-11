#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "gcr.h"

// One track side of decoded sectors (12 max). First good copy of a sector wins.
typedef struct {
    int track, side, nsect;
    bool valid;
    uint16_t present;            // bit per sector
    uint8_t data[12][GCR_SECTOR_LEN];
} trackbuf_t;

void trackbuf_init(trackbuf_t *b);
void trackbuf_start(trackbuf_t *b, int track, int side);
bool trackbuf_put(trackbuf_t *b, int sector, const uint8_t *payload);
bool trackbuf_has(const trackbuf_t *b, int sector);
bool trackbuf_complete(const trackbuf_t *b);
bool trackbuf_matches(const trackbuf_t *b, int track, int side);
void trackbuf_invalidate(trackbuf_t *b);
const uint8_t *trackbuf_get(const trackbuf_t *b, int sector);
