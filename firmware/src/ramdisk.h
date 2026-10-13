#pragma once
// M6 RAM disk (spec sec. 6.2): a read-only base image (in flash, may be absent) under a RAM
// overlay of written blocks. Lost at power off.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sp_store.h"

#define RD_BLOCKS     1600
#define RD_BLOCK_LEN  512

typedef enum { RD_OK, RD_BADBLOCK, RD_FULL } rd_err_t;

typedef struct {
    const uint8_t *base;
    size_t base_len;
    bool base_hidden;                    // after FORMAT every unwritten block reads zero
    uint8_t (*slots)[RD_BLOCK_LEN];
    uint16_t nslots, used;
    uint16_t map[RD_BLOCKS];             // 0 = unwritten, else slot index + 1
} ramdisk_t;

void ramdisk_init(ramdisk_t *rd, const uint8_t *base, size_t base_len,
                  uint8_t (*slots)[RD_BLOCK_LEN], uint16_t nslots);
rd_err_t ramdisk_read(const ramdisk_t *rd, uint32_t block, uint8_t *out);
rd_err_t ramdisk_write(ramdisk_t *rd, uint32_t block, const uint8_t *in);
void ramdisk_format(ramdisk_t *rd);
// The RAM disk as a SmartPort store (always online, writable).
sp_store_t ramdisk_store(ramdisk_t *rd);
