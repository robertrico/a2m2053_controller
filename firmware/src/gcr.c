#include "gcr.h"
#include <string.h>

const uint8_t gcr_nibbles[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

static int8_t dec_table[256];
static bool dec_built;

int gcr_decode6(uint8_t nibble) {
    if (!dec_built) {
        memset(dec_table, -1, sizeof dec_table);
        for (int i = 0; i < 64; i++) dec_table[gcr_nibbles[i]] = (int8_t)i;
        dec_built = true;
    }
    return dec_table[nibble];
}

int gcr_sectors_per_track(int track) {
    if (track < 0 || track >= GCR_TRACKS) return -1;
    return 12 - track / 16;
}

bool gcr_block_to_tss(int block, int *track, int *side, int *sector) {
    if (block < 0 || block >= GCR_BLOCKS) return false;
    int base = 0;
    for (int t = 0; t < GCR_TRACKS; t++) {
        int n = gcr_sectors_per_track(t);
        if (block < base + 2 * n) {
            *track = t;
            *side = (block - base) / n;
            *sector = (block - base) % n;
            return true;
        }
        base += 2 * n;
    }
    return false;
}

int gcr_tss_to_block(int track, int side, int sector) {
    int n = gcr_sectors_per_track(track);
    if (n < 0 || side < 0 || side > 1 || sector < 0 || sector >= n) return -1;
    int base = 0;
    for (int t = 0; t < track; t++) base += 2 * gcr_sectors_per_track(t);
    return base + side * n + sector;
}

void gcr_mangle(const uint8_t in[GCR_SECTOR_LEN], uint8_t out[GCR_SECTOR_LEN], uint8_t csum[3]) {
    unsigned a = 0, b = 0, c = 0, carry = 0, t;
    int i = 0, n = GCR_SECTOR_LEN;
    while (i < n) {
        uint8_t byte = in[i];
        t = a + byte + carry; a = t & 0xFF; carry = t >> 8;
        out[i++] = byte ^ (uint8_t)c;
        if (i >= n) break;
        byte = in[i];
        t = b + byte + carry; b = t & 0xFF; carry = t >> 8;
        out[i++] = byte ^ (uint8_t)a;
        if (i >= n) break;
        byte = in[i];
        t = c + byte + carry; c = t & 0xFF; carry = t >> 8;
        out[i++] = byte ^ (uint8_t)b;
        carry = (c & 0x80) ? 1 : 0;
        c = ((c << 1) & 0xFF) | carry;
    }
    csum[0] = (uint8_t)a; csum[1] = (uint8_t)b; csum[2] = (uint8_t)c;
}

void gcr_unmangle(const uint8_t in[GCR_SECTOR_LEN], uint8_t out[GCR_SECTOR_LEN], uint8_t csum[3]) {
    unsigned a = 0, b = 0, c = 0, carry, t;
    int i = 0, n = GCR_SECTOR_LEN;
    for (;;) {
        carry = (c & 0x80) ? 1 : 0;
        c = ((c << 1) & 0xFF) | carry;
        if (i >= n) break;
        uint8_t byte = in[i] ^ (uint8_t)c;
        out[i++] = byte;
        t = a + byte + carry; a = t & 0xFF; carry = t >> 8;
        if (i >= n) break;
        byte = in[i] ^ (uint8_t)a;
        out[i++] = byte;
        t = b + byte + carry; b = t & 0xFF; carry = t >> 8;
        if (i >= n) break;
        byte = in[i] ^ (uint8_t)b;
        out[i++] = byte;
        t = c + byte + carry; c = t & 0xFF; carry = t >> 8;
    }
    csum[0] = (uint8_t)a; csum[1] = (uint8_t)b; csum[2] = (uint8_t)c;
}

gcr_addr_t gcr_decode_address(const uint8_t s[GCR_ADDR_SIXES]) {
    gcr_addr_t a;
    a.ok = (s[0] ^ s[1] ^ s[2] ^ s[3]) == s[4];
    a.track = s[0] | ((s[2] & 1) << 6);
    a.sector = s[1];
    a.side = (s[2] >> 5) & 1;
    a.fmt = s[3];
    return a;
}

// Groups of up to 3 bytes: one six holding the top 2 bits of each byte
// (first byte in bits 5-4), then the low 6 bits of each byte.
static void decode_groups(const uint8_t *sx, uint8_t *out, int nbytes) {
    int i = 0, o = 0;
    while (o < nbytes) {
        int k = nbytes - o < 3 ? nbytes - o : 3;
        uint8_t hi = sx[i];
        for (int m = 0; m < k; m++)
            out[o + m] = (uint8_t)((((hi >> (4 - 2 * m)) & 3) << 6) | sx[i + 1 + m]);
        i += 1 + k;
        o += k;
    }
}

static int encode_groups(const uint8_t *raw, int nbytes, uint8_t *sx) {
    int i = 0;
    for (int o = 0; o < nbytes; o += 3) {
        int k = nbytes - o < 3 ? nbytes - o : 3;
        uint8_t hi = 0;
        for (int m = 0; m < k; m++) hi |= (uint8_t)((raw[o + m] >> 6) << (4 - 2 * m));
        sx[i++] = hi;
        for (int m = 0; m < k; m++) sx[i++] = raw[o + m] & 0x3F;
    }
    return i;
}

bool gcr_decode_data(const uint8_t s[GCR_DATA_SIXES], int *sector, uint8_t payload[GCR_SECTOR_LEN]) {
    uint8_t mangled[GCR_SECTOR_LEN], disk[3], calc[3];
    *sector = s[0];
    decode_groups(s + 1, mangled, GCR_SECTOR_LEN);
    decode_groups(s + 700, disk, 3);
    gcr_unmangle(mangled, payload, calc);
    return memcmp(disk, calc, 3) == 0;
}

void gcr_encode_address(int track, int sector, int side, uint8_t out[GCR_ADDR_FIELD_LEN]) {
    uint8_t v[5];
    v[0] = (uint8_t)(track & 0x3F);
    v[1] = (uint8_t)(sector & 0x3F);
    v[2] = (uint8_t)((side << 5) | (track >> 6));
    v[3] = GCR_FORMAT_UNIDISK & 0x3F;
    v[4] = v[0] ^ v[1] ^ v[2] ^ v[3];
    out[0] = 0xD5; out[1] = 0xAA; out[2] = 0x96;
    for (int i = 0; i < 5; i++) out[3 + i] = gcr_nibbles[v[i]];
    out[8] = 0xDE; out[9] = 0xAA;
}

void gcr_encode_data(int sector, const uint8_t payload[GCR_SECTOR_LEN], uint8_t out[GCR_DATA_FIELD_LEN]) {
    uint8_t mangled[GCR_SECTOR_LEN], csum[3], sx[GCR_DATA_SIXES];
    gcr_mangle(payload, mangled, csum);
    sx[0] = (uint8_t)(sector & 0x3F);
    int n = 1 + encode_groups(mangled, GCR_SECTOR_LEN, sx + 1);
    encode_groups(csum, 3, sx + n);
    out[0] = 0xD5; out[1] = 0xAA; out[2] = 0xAD;
    for (int i = 0; i < GCR_DATA_SIXES; i++) out[3 + i] = gcr_nibbles[sx[i]];
    out[707] = 0xDE; out[708] = 0xAA;
}
