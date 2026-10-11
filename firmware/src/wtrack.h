#pragma once
#include <stddef.h>
#include <stdint.h>
#include "gcr.h"

// Byte images the TX state machine streams, in the drive ROM's layout (liron.asm
// write_trk_actual, Se3b9, write_data_actual). Bits go out MSB first; the packed
// sync FF 3F CF F3 FC lays down four 10-bit self-sync groups.
#define WT_SYNC_LEN        5
#define WT_PREAMBLE_LEN    1000     // 200 sync blocks at the start of a format write
#define WT_GAP_SYNCS       7        // 35 bytes before each header
#define WT_DATA_FIELD_LEN  716      // FF 3F CF F3 FC FF + data field (709) + FF
#define WT_SECTOR_LEN      763      // gap 35 + FF + address 10 + FF + data-field image 716
#define WT_INTERLEAVE      4        // ROM zero-page $62 on the UniDisk
#define WT_TRACK_MAX       (WT_PREAMBLE_LEN + 12 * WT_SECTOR_LEN)

size_t wtrack_data_field(uint8_t out[WT_DATA_FIELD_LEN], int sector, const uint8_t payload[GCR_SECTOR_LEN]);
size_t wtrack_track(uint8_t *out, size_t cap, int cyl, int side);
void wtrack_order(int nsect, int interleave, int order[12]);
