#include "ramdisk.h"
#include <string.h>

void ramdisk_init(ramdisk_t *rd, const uint8_t *base, size_t base_len,
                  uint8_t (*slots)[RD_BLOCK_LEN], uint16_t nslots) {
    rd->base = base;
    rd->base_len = base_len > (size_t)RD_BLOCKS * RD_BLOCK_LEN ? (size_t)RD_BLOCKS * RD_BLOCK_LEN : base_len;
    rd->base_hidden = false;
    rd->slots = slots;
    rd->nslots = nslots;
    rd->used = 0;
    memset(rd->map, 0, sizeof rd->map);
}

rd_err_t ramdisk_read(const ramdisk_t *rd, uint32_t block, uint8_t *out) {
    if (block >= RD_BLOCKS) return RD_BADBLOCK;
    if (rd->map[block]) {
        memcpy(out, rd->slots[rd->map[block] - 1], RD_BLOCK_LEN);
        return RD_OK;
    }
    memset(out, 0, RD_BLOCK_LEN);
    size_t off = (size_t)block * RD_BLOCK_LEN;
    if (!rd->base_hidden && rd->base && off < rd->base_len) {
        size_t n = rd->base_len - off < RD_BLOCK_LEN ? rd->base_len - off : RD_BLOCK_LEN;
        memcpy(out, rd->base + off, n);
    }
    return RD_OK;
}

rd_err_t ramdisk_write(ramdisk_t *rd, uint32_t block, const uint8_t *in) {
    if (block >= RD_BLOCKS) return RD_BADBLOCK;
    if (!rd->map[block]) {
        if (rd->used >= rd->nslots) return RD_FULL;
        rd->map[block] = ++rd->used;
    }
    memcpy(rd->slots[rd->map[block] - 1], in, RD_BLOCK_LEN);
    return RD_OK;
}

void ramdisk_format(ramdisk_t *rd) {
    memset(rd->map, 0, sizeof rd->map);
    rd->used = 0;
    rd->base_hidden = true;
}
