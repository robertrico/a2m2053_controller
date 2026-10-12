#include <string.h>
#include <stdlib.h>
#include "test.h"
#include "sp_wire.h"

// Bits of a byte string, MSB first, into a bit array. Returns the new bit count.
static size_t put_bytes(uint8_t *bits, size_t nb, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        for (int k = 7; k >= 0; k--) bits[nb++] = (b[i] >> k) & 1u;
    return nb;
}

// Feed a bit array to the receiver in 8-bit chunks (as fm_rx autopushes them), starting at
// an arbitrary bit offset so chunk and byte boundaries do not line up.
static sp_rx_status_t feed(sp_rx_t *rx, const uint8_t *bits, size_t nb) {
    sp_rx_status_t st = SP_RX_MORE;
    for (size_t i = 0; i < nb && st == SP_RX_MORE; i += 8) {
        uint8_t c = 0;
        for (size_t k = 0; k < 8; k++) c = (uint8_t)((c << 1) | (i + k < nb ? bits[i + k] : 0));
        st = sp_rx_chunk(rx, c);
    }
    return st;
}

// Card ROM send path (liron-if.asm smartport_bus_write_packet + Scaee), transcribed the way
// the 6502 does it: checksum = XOR of every header byte as sent (bit 7 set) and of the raw data.
static size_t card_packet(uint8_t *out, uint8_t dest, uint8_t type, const uint8_t *d, size_t len) {
    static const uint8_t sync[] = { 0x3F, 0xCF, 0xF3, 0xFC, 0xFF, 0xC3 };
    size_t p = 0;
    memcpy(out, sync, sizeof sync);
    p += sizeof sync;
    uint8_t odd = (uint8_t)(len % 7), grp = (uint8_t)(len / 7), ck = 0;
    for (size_t i = 0; i < len; i++) ck ^= d[i];
    uint8_t hdr[7] = { (uint8_t)(dest | 0x80), 0x80, (uint8_t)(type | 0x80), 0x80, 0x80,
                       (uint8_t)(odd | 0x80), (uint8_t)(grp | 0x80) };
    for (int i = 0; i < 7; i++) { out[p++] = hdr[i]; ck ^= hdr[i]; }
    if (odd) {
        // drive ROM Sec84 style: ror the MSBs in from the last odd byte down, then sec ror.
        uint8_t m = 0;
        for (int y = odd - 1; y >= 0; y--) m = (uint8_t)((m >> 1) | (d[y] & 0x80));
        m = (uint8_t)((m >> 1) | 0x80);
        out[p++] = m;
        for (int i = 0; i < odd; i++) out[p++] = d[i] | 0x80;
    }
    for (int g = 0; g < grp; g++) {
        const uint8_t *s = d + odd + g * 7;
        uint8_t m = 0;
        for (int y = 6; y >= 0; y--) m = (uint8_t)((m >> 1) | (s[y] & 0x80));   // Seca4
        m = (uint8_t)((m >> 1) | 0x80);
        out[p++] = m;
        for (int i = 0; i < 7; i++) out[p++] = s[i] | 0x80;
    }
    out[p++] = ck | 0xAA;
    out[p++] = (uint8_t)((ck >> 1) | 0xAA);
    out[p++] = 0xC8;
    return p;
}

// Drive ROM receive (liron.asm Lea96 loop): MSB byte asl, then per byte asl the MSB byte and
// clear bit 7 when the carry is clear. Returns decoded length.
static size_t rom_decode(const uint8_t *w, uint8_t *d) {
    uint8_t odd = w[5] & 0x7F, grp = w[6] & 0x7F;
    size_t p = 7, o = 0;
    int runs = grp + (odd ? 1 : 0);
    for (int r = 0; r < runs; r++) {
        int cnt = (r == 0 && odd) ? odd : 7;
        uint8_t z = (uint8_t)(w[p++] << 1);
        for (int i = 0; i < cnt; i++) {
            uint8_t a = w[p++];
            int carry = z >> 7;
            z = (uint8_t)(z << 1);
            if (!carry) a ^= 0x80;
            d[o++] = a;
        }
    }
    return o;
}

static uint8_t bits[64 * 1024];

static void roundtrip(size_t len, unsigned seed) {
    static uint8_t data[SP_DATA_MAX], tx[SP_TX_MAX], out[SP_DATA_MAX], rom[SP_DATA_MAX];
    srand(seed);
    for (size_t i = 0; i < len; i++) data[i] = (uint8_t)rand();
    size_t n = sp_encode(tx, 0x05, SP_TYPE_STATUS, 0x2B, data, len);
    CHECK(n == SP_PREAMBLE_LEN + 7 + (len % 7 ? 1 + len % 7 : 0) + (len / 7) * 8 + 3);
    CHECK(tx[0] == 0xFF && tx[11] == 0xC3 && tx[n - 1] == 0xC8);
    for (size_t i = SP_PREAMBLE_LEN; i < n; i++) CHECK(tx[i] & 0x80);
    // through the framer at every chunk phase
    for (int phase = 0; phase < 8; phase++) {
        size_t nb = 0;
        for (int k = 0; k < phase; k++) bits[nb++] = 0;
        nb = put_bytes(bits, nb, tx, n);
        for (int k = 0; k < 16; k++) bits[nb++] = 0;     // idle cells after C8
        sp_rx_t rx;
        sp_rx_init(&rx);
        CHECK(feed(&rx, bits, nb) == SP_RX_DONE);
        sp_hdr_t h;
        CHECK(sp_decode(rx.wire, rx.n, &h, out) == SP_OK);
        CHECK(h.dest == 0 && h.src == 5 && h.type == SP_TYPE_STATUS && h.stat == 0x2B && h.len == len);
        CHECK(memcmp(out, data, len) == 0);
    }
    // the drive ROM's receive decoding agrees
    CHECK(rom_decode(tx + SP_PREAMBLE_LEN, rom) == len);
    CHECK(memcmp(rom, data, len) == 0);
}

int main(void) {
    // framer: zeros before a byte are skipped, bytes are 8 bits from the first 1
    sp_framer_t f = {0};
    int got = -1;
    const uint8_t s[] = { 0, 0, 1, 1, 0, 0, 0, 0, 1, 1 };
    for (size_t i = 0; i < sizeof s; i++) { int b = sp_framer_bit(&f, s[i]); if (b >= 0) got = b; }
    CHECK(got == 0xC3);

    // measured PR#5 start (m6.md 2026-10-12): FF 3F CF F3 FC FF C3 81, after junk bits
    {
        uint8_t ini[9] = { 0x05, 0x02, 0x00, 0x00, 0x80, 0, 0, 0, 0 };
        uint8_t pk[64];
        size_t n = card_packet(pk, 0x01, SP_TYPE_CMD, ini, 9);
        size_t nb = 0;
        const uint8_t junk[] = { 1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1 };
        memcpy(bits, junk, sizeof junk);
        nb = sizeof junk;
        const uint8_t ff = 0xFF;
        nb = put_bytes(bits, nb, &ff, 1);
        nb = put_bytes(bits, nb, pk, n);
        CHECK(pk[6] == 0x81);                      // measured destination byte
        sp_rx_t rx;
        sp_rx_init(&rx);
        CHECK(feed(&rx, bits, nb) == SP_RX_DONE);
        CHECK(rx.n == 7 + 3 + 8 + 3);
        sp_hdr_t h;
        uint8_t out[SP_DATA_MAX];
        CHECK(sp_decode(rx.wire, rx.n, &h, out) == SP_OK);
        CHECK(h.dest == 1 && h.src == 0 && h.type == SP_TYPE_CMD && h.len == 9);
        CHECK(out[0] == 0x05 && out[1] == 0x02 && out[4] == 0x80);

        // corrupt one data bit: checksum error
        uint8_t w[SP_WIRE_MAX];
        memcpy(w, rx.wire, rx.n);
        w[9] ^= 0x01;
        CHECK(sp_decode(w, rx.n, &h, out) == SP_ERR_CKSUM);
        memcpy(w, rx.wire, rx.n);
        w[rx.n - 1] = 0xC9;
        CHECK(sp_decode(w, rx.n, &h, out) == SP_ERR_PEND);
        CHECK(sp_decode(rx.wire, rx.n - 1, &h, out) == SP_ERR_SHORT);
        CHECK(sp_decode(rx.wire, 5, &h, out) == SP_ERR_SHORT);
    }

    // absurd counts: rejected by the receiver as soon as the header is in
    {
        uint8_t pk[] = { 0xFF, 0xC3, 0x81, 0x80, 0x80, 0x80, 0x80, 0x87, 0x81 };   // ODDCNT 7
        size_t nb = put_bytes(bits, 0, pk, sizeof pk);
        sp_rx_t rx;
        sp_rx_init(&rx);
        CHECK(feed(&rx, bits, nb) == SP_RX_BAD);
        uint8_t pk2[] = { 0xFF, 0xC3, 0x81, 0x80, 0x80, 0x80, 0x80, 0x80, 0xFF };  // 127 groups
        nb = put_bytes(bits, 0, pk2, sizeof pk2);
        sp_rx_init(&rx);
        CHECK(feed(&rx, bits, nb) == SP_RX_MORE);   // 889 bytes: allowed, needs more bits
        uint8_t w[] = { 0x81, 0x80, 0x80, 0x80, 0x80, 0x87, 0x81, 0, 0, 0 };
        sp_hdr_t h;
        uint8_t out[SP_DATA_MAX];
        CHECK(sp_decode(w, sizeof w, &h, out) == SP_ERR_COUNT);
    }

    // round trips: no data, status 4, command 9, DIB 25, block 512, format maximum (895)
    roundtrip(0, 1);
    roundtrip(4, 2);
    roundtrip(8, 3);
    roundtrip(9, 4);
    roundtrip(25, 5);
    roundtrip(512, 6);
    roundtrip(SP_DATA_MAX, 7);

    // a 512-byte card data packet (WRITEBLOCK) decodes
    {
        uint8_t d[512], pk[700], out[SP_DATA_MAX];
        for (int i = 0; i < 512; i++) d[i] = (uint8_t)(i * 7 + 3);
        size_t n = card_packet(pk, 0x01, SP_TYPE_DATA, d, 512);
        size_t nb = put_bytes(bits, 0, (const uint8_t[]){0xFF, 0xFF}, 2);
        nb = put_bytes(bits, nb, pk, n);
        sp_rx_t rx;
        sp_rx_init(&rx);
        CHECK(feed(&rx, bits, nb) == SP_RX_DONE);
        sp_hdr_t h;
        CHECK(sp_decode(rx.wire, rx.n, &h, out) == SP_OK);
        CHECK(h.type == SP_TYPE_DATA && h.len == 512 && memcmp(out, d, 512) == 0);
    }

    // encode clamps an oversize length
    {
        static uint8_t big[SP_DATA_MAX + 10], tx[SP_TX_MAX];
        size_t n = sp_encode(tx, 1, 1, 0, big, sizeof big);
        CHECK(n <= SP_TX_MAX);
    }
    TEST_END();
}
