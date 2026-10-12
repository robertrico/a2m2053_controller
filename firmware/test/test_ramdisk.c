#include <string.h>
#include "test.h"
#include "ramdisk.h"

static uint8_t base[3 * RD_BLOCK_LEN + 100];      // 3.2 blocks: block 3 is partly base, then zeros
static uint8_t slots[4][RD_BLOCK_LEN];
static ramdisk_t rd;

static bool all(const uint8_t *b, uint8_t v, size_t n) {
    for (size_t i = 0; i < n; i++) if (b[i] != v) return false;
    return true;
}

int main(void) {
    for (size_t i = 0; i < sizeof base; i++) base[i] = (uint8_t)(i / RD_BLOCK_LEN + 1);
    uint8_t b[RD_BLOCK_LEN], w[RD_BLOCK_LEN];

    ramdisk_init(&rd, base, sizeof base, slots, 4);
    CHECK(ramdisk_read(&rd, 0, b) == RD_OK && all(b, 1, RD_BLOCK_LEN));
    CHECK(ramdisk_read(&rd, 2, b) == RD_OK && all(b, 3, RD_BLOCK_LEN));
    CHECK(ramdisk_read(&rd, 3, b) == RD_OK && all(b, 4, 100) && all(b + 100, 0, RD_BLOCK_LEN - 100));
    CHECK(ramdisk_read(&rd, 1599, b) == RD_OK && all(b, 0, RD_BLOCK_LEN));
    CHECK(ramdisk_read(&rd, 1600, b) == RD_BADBLOCK);

    memset(w, 0xA5, sizeof w);
    CHECK(ramdisk_write(&rd, 1, w) == RD_OK && rd.used == 1);
    CHECK(ramdisk_read(&rd, 1, b) == RD_OK && all(b, 0xA5, RD_BLOCK_LEN));
    CHECK(ramdisk_read(&rd, 0, b) == RD_OK && all(b, 1, RD_BLOCK_LEN));    // neighbours untouched
    memset(w, 0x5A, sizeof w);
    CHECK(ramdisk_write(&rd, 1, w) == RD_OK && rd.used == 1);              // rewrite reuses the slot
    CHECK(ramdisk_read(&rd, 1, b) == RD_OK && all(b, 0x5A, RD_BLOCK_LEN));
    CHECK(ramdisk_write(&rd, 1600, w) == RD_BADBLOCK);

    CHECK(ramdisk_write(&rd, 10, w) == RD_OK);
    CHECK(ramdisk_write(&rd, 11, w) == RD_OK);
    CHECK(ramdisk_write(&rd, 1599, w) == RD_OK && rd.used == 4);
    CHECK(ramdisk_write(&rd, 12, w) == RD_FULL);
    CHECK(ramdisk_write(&rd, 10, w) == RD_OK);                             // existing block still writable
    CHECK(ramdisk_read(&rd, 12, b) == RD_OK && all(b, 0, RD_BLOCK_LEN));

    ramdisk_format(&rd);
    CHECK(rd.used == 0);
    CHECK(ramdisk_read(&rd, 0, b) == RD_OK && all(b, 0, RD_BLOCK_LEN));    // base hidden
    CHECK(ramdisk_read(&rd, 1, b) == RD_OK && all(b, 0, RD_BLOCK_LEN));    // overlay gone
    CHECK(ramdisk_write(&rd, 2, w) == RD_OK && rd.used == 1);
    CHECK(ramdisk_read(&rd, 2, b) == RD_OK && all(b, 0x5A, RD_BLOCK_LEN));

    // no base image at all
    ramdisk_init(&rd, NULL, 0, slots, 4);
    CHECK(ramdisk_read(&rd, 0, b) == RD_OK && all(b, 0, RD_BLOCK_LEN));
    // oversize base is cut at 1600 blocks
    static uint8_t huge[(RD_BLOCKS + 1) * RD_BLOCK_LEN];
    ramdisk_init(&rd, huge, sizeof huge, slots, 4);
    CHECK(rd.base_len == (size_t)RD_BLOCKS * RD_BLOCK_LEN);
    TEST_END();
}
