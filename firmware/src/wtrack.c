#include "wtrack.h"
#include <string.h>

static const uint8_t SYNC[WT_SYNC_LEN] = { 0xFF, 0x3F, 0xCF, 0xF3, 0xFC };
static const uint8_t ZERO[GCR_SECTOR_LEN];

size_t wtrack_data_field(uint8_t out[WT_DATA_FIELD_LEN], int sector, const uint8_t payload[GCR_SECTOR_LEN]) {
    memcpy(out, SYNC, WT_SYNC_LEN);
    out[WT_SYNC_LEN] = 0xFF;
    gcr_encode_data(sector, payload, out + WT_SYNC_LEN + 1);
    out[WT_DATA_FIELD_LEN - 1] = 0xFF;
    return WT_DATA_FIELD_LEN;
}

// ROM Se3f0: walk the slots `interleave` apart, taking the next free slot on a collision.
void wtrack_order(int nsect, int interleave, int order[12]) {
    for (int i = 0; i < nsect; i++) order[i] = -1;
    int pos = 0;
    for (int sec = 0; sec < nsect; sec++) {
        while (order[pos] >= 0) pos = (pos + 1) % nsect;
        order[pos] = sec;
        pos = (pos + interleave) % nsect;
    }
}

size_t wtrack_track(uint8_t *out, size_t cap, int cyl, int side) {
    int nsect = gcr_sectors_per_track(cyl);
    if (nsect < 0 || side < 0 || side >= GCR_SIDES) return 0;
    size_t len = WT_PREAMBLE_LEN + (size_t)nsect * WT_SECTOR_LEN;
    if (cap < len) return 0;
    uint8_t *p = out;
    for (int i = 0; i < WT_PREAMBLE_LEN / WT_SYNC_LEN; i++) { memcpy(p, SYNC, WT_SYNC_LEN); p += WT_SYNC_LEN; }
    int order[12];
    wtrack_order(nsect, WT_INTERLEAVE, order);
    for (int k = 0; k < nsect; k++) {
        for (int i = 0; i < WT_GAP_SYNCS; i++) { memcpy(p, SYNC, WT_SYNC_LEN); p += WT_SYNC_LEN; }
        *p++ = 0xFF;
        gcr_encode_address(cyl, order[k], side, p);
        p += GCR_ADDR_FIELD_LEN;
        *p++ = 0xFF;
        p += wtrack_data_field(p, order[k], ZERO);
    }
    return (size_t)(p - out);
}
