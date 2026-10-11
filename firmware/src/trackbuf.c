#include "trackbuf.h"
#include <string.h>

void trackbuf_init(trackbuf_t *b) { memset(b, 0, sizeof *b); }

void trackbuf_start(trackbuf_t *b, int track, int side) {
    b->track = track;
    b->side = side;
    b->nsect = gcr_sectors_per_track(track);
    b->present = 0;
    b->valid = b->nsect > 0;
}

bool trackbuf_put(trackbuf_t *b, int sector, const uint8_t *payload) {
    if (!b->valid || sector < 0 || sector >= b->nsect || (b->present & (1u << sector))) return false;
    memcpy(b->data[sector], payload, GCR_SECTOR_LEN);
    b->present |= (uint16_t)(1u << sector);
    return true;
}

bool trackbuf_has(const trackbuf_t *b, int sector) {
    return b->valid && sector >= 0 && sector < b->nsect && (b->present & (1u << sector));
}

bool trackbuf_complete(const trackbuf_t *b) {
    return b->valid && b->present == (uint16_t)((1u << b->nsect) - 1);
}

bool trackbuf_matches(const trackbuf_t *b, int track, int side) {
    return b->valid && b->track == track && b->side == side;
}

void trackbuf_invalidate(trackbuf_t *b) { b->valid = false; b->present = 0; }

const uint8_t *trackbuf_get(const trackbuf_t *b, int sector) {
    return trackbuf_has(b, sector) ? b->data[sector] : NULL;
}
