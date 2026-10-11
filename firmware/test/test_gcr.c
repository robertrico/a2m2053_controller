#include <string.h>
#include "test.h"
#include "gcr.h"

static void test_nibble_table(void) {
    int seen[256] = {0};
    for (int i = 0; i < 64; i++) {
        CHECK(gcr_nibbles[i] >= 0x96);
        CHECK(seen[gcr_nibbles[i]]++ == 0);
        CHECK(gcr_decode6(gcr_nibbles[i]) == i);
    }
    CHECK(gcr_nibbles[0] == 0x96 && gcr_nibbles[63] == 0xFF);
    CHECK(gcr_decode6(0xD5) == -1 && gcr_decode6(0xAA) == -1 && gcr_decode6(0x00) == -1);
}

static void test_geometry(void) {
    CHECK(gcr_sectors_per_track(0) == 12 && gcr_sectors_per_track(15) == 12);
    CHECK(gcr_sectors_per_track(16) == 11 && gcr_sectors_per_track(47) == 10);
    CHECK(gcr_sectors_per_track(79) == 8 && gcr_sectors_per_track(80) == -1);
    int t, s, sec, total = 0;
    for (int tr = 0; tr < 80; tr++) total += 2 * gcr_sectors_per_track(tr);
    CHECK(total == GCR_BLOCKS);
    CHECK(gcr_block_to_tss(0, &t, &s, &sec) && t == 0 && s == 0 && sec == 0);
    CHECK(gcr_block_to_tss(12, &t, &s, &sec) && t == 0 && s == 1 && sec == 0);
    CHECK(gcr_block_to_tss(24, &t, &s, &sec) && t == 1 && s == 0 && sec == 0);
    CHECK(gcr_block_to_tss(1599, &t, &s, &sec) && t == 79 && s == 1 && sec == 7);
    CHECK(!gcr_block_to_tss(1600, &t, &s, &sec) && !gcr_block_to_tss(-1, &t, &s, &sec));
    for (int b = 0; b < GCR_BLOCKS; b++) {
        CHECK(gcr_block_to_tss(b, &t, &s, &sec));
        CHECK(gcr_tss_to_block(t, s, sec) == b);
    }
    CHECK(gcr_tss_to_block(79, 0, 8) == -1);
}

// tashnotes gcr_checksum.md: "TAGS" x3 + "DATA" x128.
static void test_mangle_vector(void) {
    uint8_t in[524], out[524], csum[3], back[524], csum2[3];
    for (int i = 0; i < 12; i++) in[i] = "TAGS"[i % 4];
    for (int i = 12; i < 524; i++) in[i] = "DATA"[(i - 12) % 4];
    gcr_mangle(in, out, csum);
    static const uint8_t head[16] = {0x54,0x15,0x06,0xDD,0xF3,0xD4,0xD8,0xBC,0xBC,0xA6,0x76,0x63,0x32,0x34,0x25,0xD4};
    static const uint8_t tail[12] = {0x0A,0x4B,0x9B,0xE3,0xD2,0x52,0xB0,0x94,0x11,0x69,0x7A,0xE8};
    CHECK(memcmp(out, head, 16) == 0);
    CHECK(memcmp(out + 0x200, tail, 12) == 0);
    CHECK(csum[0] == 0xA9 && csum[1] == 0x69 && csum[2] == 0x2E);
    gcr_unmangle(out, back, csum2);
    CHECK(memcmp(back, in, 524) == 0 && memcmp(csum, csum2, 3) == 0);
}

static void test_address_round_trip(void) {
    static const int cases[][3] = {{0,0,0},{17,10,1},{70,7,1},{79,7,0}};
    for (int k = 0; k < 4; k++) {
        uint8_t f[10], sx[5];
        gcr_encode_address(cases[k][0], cases[k][1], cases[k][2], f);
        CHECK(f[0] == 0xD5 && f[1] == 0xAA && f[2] == 0x96 && f[8] == 0xDE && f[9] == 0xAA);
        for (int i = 0; i < 5; i++) sx[i] = (uint8_t)gcr_decode6(f[3 + i]);
        gcr_addr_t a = gcr_decode_address(sx);
        CHECK(a.ok && a.track == cases[k][0] && a.sector == cases[k][1] && a.side == cases[k][2]);
        CHECK(a.fmt == GCR_FORMAT_UNIDISK);
        sx[4] ^= 1;
        CHECK(!gcr_decode_address(sx).ok);
    }
}

static void test_data_round_trip(void) {
    uint8_t p[524], f[709], sx[704], out[524];
    for (int i = 0; i < 524; i++) p[i] = (uint8_t)(i * 37 + 11);
    gcr_encode_data(9, p, f);
    CHECK(f[0] == 0xD5 && f[1] == 0xAA && f[2] == 0xAD && f[707] == 0xDE && f[708] == 0xAA);
    for (int i = 0; i < 704; i++) sx[i] = (uint8_t)gcr_decode6(f[3 + i]);
    int sec = -1;
    CHECK(gcr_decode_data(sx, &sec, out) && sec == 9 && memcmp(out, p, 524) == 0);
    sx[100] ^= 1;
    CHECK(!gcr_decode_data(sx, &sec, out));
}

int main(void) {
    test_nibble_table();
    test_geometry();
    test_mangle_vector();
    test_address_round_trip();
    test_data_round_trip();
    TEST_END();
}
