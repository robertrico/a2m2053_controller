#pragma once
#include <stdbool.h>
#include <stdint.h>

// Apple 3.5" 800K GCR (port of tools/a2m/gcr.py). Byte order (0,1,2) for the
// data groups and the checksum, confirmed on real media in M3.
#define GCR_TAG_LEN         12
#define GCR_DATA_LEN        512
#define GCR_SECTOR_LEN      524
#define GCR_TRACKS          80
#define GCR_SIDES           2
#define GCR_BLOCKS          1600
#define GCR_ADDR_SIXES      5
#define GCR_DATA_SIXES      704     // 1 sector + 699 data + 4 checksum
#define GCR_ADDR_FIELD_LEN  10      // prolog 3 + 5 + epilog 2
#define GCR_DATA_FIELD_LEN  709     // prolog 3 + 704 + epilog 2
#define GCR_FORMAT_UNIDISK  0x24

extern const uint8_t gcr_nibbles[64];

int gcr_decode6(uint8_t nibble);
int gcr_sectors_per_track(int track);
bool gcr_block_to_tss(int block, int *track, int *side, int *sector);
int gcr_tss_to_block(int track, int side, int sector);

void gcr_mangle(const uint8_t in[GCR_SECTOR_LEN], uint8_t out[GCR_SECTOR_LEN], uint8_t csum[3]);
void gcr_unmangle(const uint8_t in[GCR_SECTOR_LEN], uint8_t out[GCR_SECTOR_LEN], uint8_t csum[3]);

typedef struct { int track, sector, side, fmt; bool ok; } gcr_addr_t;
gcr_addr_t gcr_decode_address(const uint8_t sixes[GCR_ADDR_SIXES]);
bool gcr_decode_data(const uint8_t sixes[GCR_DATA_SIXES], int *sector, uint8_t payload[GCR_SECTOR_LEN]);

void gcr_encode_address(int track, int sector, int side, uint8_t out[GCR_ADDR_FIELD_LEN]);
void gcr_encode_data(int sector, const uint8_t payload[GCR_SECTOR_LEN], uint8_t out[GCR_DATA_FIELD_LEN]);
