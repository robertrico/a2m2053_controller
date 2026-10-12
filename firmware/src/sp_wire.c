#include "sp_wire.h"
#include <string.h>

// Drive ROM reply preamble (liron.asm spb_send_packet): FF written first, then spb_sync_pattern.
static const uint8_t preamble[SP_PREAMBLE_LEN] = {
    0xFF, 0x3F, 0xCF, 0xF3, 0xFC, 0xFF, 0x3F, 0xCF, 0xF3, 0xFC, 0xFF, SP_PBEGIN,
};

static size_t expected_len(uint8_t odd, uint8_t grp) {
    return SP_HDR_LEN + (odd ? 1u + odd : 0u) + (size_t)grp * 8u + 3u;
}

static bool counts_ok(uint8_t odd, uint8_t grp) {
    return odd <= 6 && (size_t)odd + (size_t)grp * 7u <= SP_DATA_MAX;
}

void sp_rx_init(sp_rx_t *rx) {
    memset(&rx->fr, 0, sizeof rx->fr);
    rx->framed = false;
    rx->n = rx->expect = 0;
    rx->st = SP_RX_MORE;
}

static void rx_byte(sp_rx_t *rx, uint8_t b) {
    if (!rx->framed) {
        if (b == SP_PBEGIN) rx->framed = true;
        return;
    }
    rx->wire[rx->n++] = b;
    if (rx->n == SP_HDR_LEN) {
        uint8_t odd = rx->wire[5] & 0x7F, grp = rx->wire[6] & 0x7F;
        if (!counts_ok(odd, grp)) { rx->st = SP_RX_BAD; return; }
        rx->expect = expected_len(odd, grp);
    }
    if (rx->expect && rx->n == rx->expect) rx->st = SP_RX_DONE;
}

sp_rx_status_t sp_rx_chunk(sp_rx_t *rx, uint8_t chunk) {
    for (int i = 7; i >= 0 && rx->st == SP_RX_MORE; i--) {
        int b = sp_framer_bit(&rx->fr, (chunk >> i) & 1u);
        if (b >= 0) rx_byte(rx, (uint8_t)b);
    }
    return rx->st;
}

sp_err_t sp_decode(const uint8_t *wire, size_t n, sp_hdr_t *h, uint8_t *data) {
    if (n < SP_HDR_LEN + 3) return SP_ERR_SHORT;
    uint8_t odd = wire[5] & 0x7F, grp = wire[6] & 0x7F;
    if (!counts_ok(odd, grp)) return SP_ERR_COUNT;
    if (n < expected_len(odd, grp)) return SP_ERR_SHORT;
    h->dest = wire[0] & 0x7F;
    h->src  = wire[1] & 0x7F;
    h->type = wire[2] & 0x7F;
    h->aux  = wire[3] & 0x7F;
    h->stat = wire[4] & 0x7F;
    h->len  = (size_t)odd + (size_t)grp * 7u;
    uint8_t ck = 0;
    for (int i = 0; i < SP_HDR_LEN; i++) ck ^= (uint8_t)(wire[i] | 0x80);
    size_t p = SP_HDR_LEN, o = 0;
    if (odd) {
        uint8_t msb = wire[p++];
        for (unsigned i = 0; i < odd; i++, o++)
            data[o] = (uint8_t)((wire[p++] & 0x7F) | (((msb >> (6 - i)) & 1u) << 7));
    }
    for (unsigned g = 0; g < grp; g++) {
        uint8_t msb = wire[p++];
        for (unsigned i = 0; i < 7; i++, o++)
            data[o] = (uint8_t)((wire[p++] & 0x7F) | (((msb >> (6 - i)) & 1u) << 7));
    }
    for (size_t i = 0; i < o; i++) ck ^= data[i];
    uint8_t ck1 = wire[p++], ck2 = wire[p++];
    if (((uint8_t)((ck2 << 1) | 1u) & ck1) != ck) return SP_ERR_CKSUM;
    if (wire[p] != SP_PEND) return SP_ERR_PEND;
    return SP_OK;
}

size_t sp_encode(uint8_t *out, uint8_t src, uint8_t type, uint8_t stat,
                 const uint8_t *data, size_t len) {
    if (len > SP_DATA_MAX) len = SP_DATA_MAX;
    uint8_t odd = (uint8_t)(len % 7), grp = (uint8_t)(len / 7);
    memcpy(out, preamble, sizeof preamble);
    size_t p = sizeof preamble;
    const uint8_t hdr[SP_HDR_LEN] = {
        0x80, (uint8_t)(src | 0x80), (uint8_t)(type | 0x80), 0x80,
        (uint8_t)(stat | 0x80), (uint8_t)(odd | 0x80), (uint8_t)(grp | 0x80),
    };
    uint8_t ck = 0;
    for (int i = 0; i < SP_HDR_LEN; i++) { out[p++] = hdr[i]; ck ^= hdr[i]; }
    for (size_t i = 0; i < len; i++) ck ^= data[i];
    if (odd) {
        uint8_t msb = 0x80;
        for (unsigned i = 0; i < odd; i++) msb |= (uint8_t)((data[i] >> 7) << (6 - i));
        out[p++] = msb;
        for (unsigned i = 0; i < odd; i++) out[p++] = data[i] | 0x80;
    }
    const uint8_t *g = data + odd;
    for (unsigned k = 0; k < grp; k++, g += 7) {
        uint8_t msb = 0x80;
        for (unsigned i = 0; i < 7; i++) msb |= (uint8_t)((g[i] >> 7) << (6 - i));
        out[p++] = msb;
        for (unsigned i = 0; i < 7; i++) out[p++] = g[i] | 0x80;
    }
    out[p++] = ck | 0xAA;
    out[p++] = (uint8_t)((ck >> 1) | 0xAA);
    out[p++] = SP_PEND;
    return p;
}
