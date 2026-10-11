#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "wtrack.h"
#include "flux_dec.h"

#define FIX "fixtures/"

static uint8_t *load(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("missing %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*len);
    if (fread(b, 1, (size_t)*len, f) != (size_t)*len) { printf("short read %s\n", path); exit(1); }
    fclose(f);
    return b;
}

static void test_data_field_layout(void) {
    static const uint8_t zero[GCR_SECTOR_LEN];
    static const uint8_t head[9] = { 0xFF, 0x3F, 0xCF, 0xF3, 0xFC, 0xFF, 0xD5, 0xAA, 0xAD };
    uint8_t f[WT_DATA_FIELD_LEN];
    CHECK(wtrack_data_field(f, 3, zero) == 716);
    CHECK(memcmp(f, head, 9) == 0);
    CHECK(f[9] == gcr_nibbles[3]);
    int all96 = 1;
    for (int i = 10; i < 713; i++) if (f[i] != 0x96) all96 = 0;
    CHECK(all96);                                   // ROM Se3b9: 703 x $96 for zero data
    CHECK(f[713] == 0xDE && f[714] == 0xAA && f[715] == 0xFF);
}

static void test_data_field_matches_python(void) {
    long pl, bl;
    uint8_t *pay = load(FIX "rom_dfield_s5.pay", &pl), *exp = load(FIX "rom_dfield_s5.bin", &bl);
    CHECK(pl == GCR_SECTOR_LEN && bl == WT_DATA_FIELD_LEN);
    uint8_t f[WT_DATA_FIELD_LEN];
    wtrack_data_field(f, 5, pay);
    CHECK(memcmp(f, exp, WT_DATA_FIELD_LEN) == 0);
    free(pay); free(exp);
}

static void track_matches(const char *path, int cyl, int side) {
    static uint8_t img[WT_TRACK_MAX];
    long el;
    uint8_t *exp = load(path, &el);
    size_t n = wtrack_track(img, sizeof img, cyl, side);
    CHECK((long)n == el && memcmp(img, exp, n) == 0);
    free(exp);
}

static void test_track_matches_python(void) {
    track_matches(FIX "rom_track_t17s1.bin", 17, 1);
    track_matches(FIX "rom_track_t70s0.bin", 70, 0);
}

static void test_track_lengths_and_limits(void) {
    static uint8_t img[WT_TRACK_MAX];
    static const int cyl[6] = { 0, 16, 32, 48, 64, 79 }, ns[6] = { 12, 11, 10, 9, 8, 8 };
    for (int i = 0; i < 6; i++) CHECK(wtrack_track(img, sizeof img, cyl[i], 1) == (size_t)(1000 + ns[i] * 763));
    CHECK(WT_TRACK_MAX == 1000 + 12 * 763);
    CHECK(wtrack_track(img, 100, 0, 0) == 0);
    CHECK(wtrack_track(img, sizeof img, 80, 0) == 0);
    CHECK(wtrack_track(img, sizeof img, -1, 0) == 0);
    CHECK(wtrack_track(img, sizeof img, 0, 2) == 0);
}

static void test_order(void) {
    static const int o12[12] = { 0, 3, 6, 9, 1, 4, 7, 10, 2, 5, 8, 11 };   // synth.physical_order(12, 4)
    static const int o8[8] = { 0, 2, 4, 6, 1, 3, 5, 7 };                   // synth.physical_order(8, 4)
    int o[12];
    wtrack_order(12, 4, o);
    for (int i = 0; i < 12; i++) CHECK(o[i] == o12[i]);
    wtrack_order(8, 4, o);
    for (int i = 0; i < 8; i++) CHECK(o[i] == o8[i]);
}

static int seen[12];
static void on_sec(void *ctx, int sec, const uint8_t *p) {
    (void)ctx;
    for (int i = 0; i < GCR_SECTOR_LEN; i++) if (p[i]) return;
    if (sec >= 0 && sec < 12) seen[sec]++;
}

static void test_track_decodes(void) {
    static uint8_t img[WT_TRACK_MAX];
    static uint16_t iv[WT_TRACK_MAX * 8];
    static flux_dec_t d;
    size_t len = wtrack_track(img, sizeof img, 70, 0);
    int n = 0, run = 0;
    for (size_t k = 0; k < len * 8; k++) {
        run++;
        if ((img[k >> 3] >> (7 - (k & 7))) & 1) {
            iv[n++] = (uint16_t)((float)run * FLUX_DEC_CELL_COUNTS - FLUX_DEC_OVERHEAD + 0.5f);
            run = 0;
        }
    }
    memset(seen, 0, sizeof seen);
    flux_dec_sink_t k = { .sector = on_sec };
    flux_dec_reset(&d, 70, 0, &k);
    flux_dec_push_many(&d, iv, n);
    for (int s = 0; s < 8; s++) CHECK(seen[s] == 1);
}

int main(void) {
    test_data_field_layout();
    test_data_field_matches_python();
    test_track_matches_python();
    test_track_lengths_and_limits();
    test_order();
    test_track_decodes();
    TEST_END();
}
