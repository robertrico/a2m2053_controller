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

static uint8_t st_read(void *c, uint32_t block, uint8_t *out) {
    return ramdisk_read(c, block, out) == RD_OK ? SP_E_OK : SP_E_BADBLOCK;
}
static uint8_t st_write(void *c, uint32_t block, const uint8_t *in) {
    switch (ramdisk_write(c, block, in)) {
    case RD_OK: return SP_E_OK;
    case RD_BADBLOCK: return SP_E_BADBLOCK;
    default: return SP_E_IOERROR;                  // overlay full
    }
}
static uint8_t st_format(void *c) { ramdisk_format(c); return SP_E_OK; }
static uint8_t st_eject(void *c) { (void)c; return SP_E_OK; }
static sp_media_t st_media(void *c) { (void)c; return SP_MEDIA_OK; }
static void st_unistat(void *c, uint8_t out[8]) { (void)c; memset(out, 0, 8); out[7] = 0xFF; }   // as the UniDisk

sp_store_t ramdisk_store(ramdisk_t *rd) {
    sp_store_t s = { st_read, st_write, st_format, st_eject, st_media, st_unistat, rd };
    return s;
}
