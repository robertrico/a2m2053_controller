#pragma once
// SmartPort wire format (M6 spec sec. 3.4): IWM byte framing, packet decode and encode.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SP_DATA_MAX   (127 * 7 + 6)                     // format limit: GRP7CNT and ODDCNT are 7-bit
#define SP_GRP_MAX    (SP_DATA_MAX / 7 + 1)
#define SP_HDR_LEN    7                                 // DEST SRC TYPE AUX STAT ODDCNT GRP7CNT
#define SP_WIRE_MAX   (SP_HDR_LEN + 7 + SP_GRP_MAX * 8 + 3)   // bytes after C3, up to and with C8
#define SP_PREAMBLE_LEN 12                              // FF + 2 x (3F CF F3 FC FF) + C3
#define SP_TX_MAX     (SP_PREAMBLE_LEN + SP_WIRE_MAX)

#define SP_PBEGIN     0xC3
#define SP_PEND       0xC8
#define SP_TYPE_CMD   0x00                              // packet types with bit 7 stripped
#define SP_TYPE_STATUS 0x01
#define SP_TYPE_DATA  0x02

// IWM read framing: skip 0 bits until a 1, then take 8 bits. Every byte it returns has bit 7 set.
typedef struct { uint8_t acc, n; } sp_framer_t;

static inline int sp_framer_bit(sp_framer_t *f, unsigned bit) {
    if (f->n == 0 && !bit) return -1;
    f->acc = (uint8_t)((f->acc << 1) | (bit & 1u));
    if (++f->n < 8) return -1;
    f->n = 0;
    return f->acc;
}

typedef enum { SP_RX_MORE, SP_RX_DONE, SP_RX_BAD } sp_rx_status_t;

// Packet receiver over a bit stream: hunts C3, then collects wire bytes until the length the
// header announces (through C8). wire[] holds the bytes after C3.
typedef struct {
    sp_framer_t fr;
    bool framed;
    size_t n, expect;               // expect = 0 until ODDCNT and GRP7CNT are in
    sp_rx_status_t st;
    uint8_t wire[SP_WIRE_MAX];
} sp_rx_t;

void sp_rx_init(sp_rx_t *rx);
// Feed 8 cell bits, earliest bit in bit 7 (one fm_rx autopush). Returns the receiver status;
// bits after DONE or BAD are ignored.
sp_rx_status_t sp_rx_chunk(sp_rx_t *rx, uint8_t chunk);

typedef struct {
    uint8_t dest, src, type, aux, stat;   // bit 7 stripped
    size_t len;                           // decoded data bytes
} sp_hdr_t;

typedef enum {
    SP_OK = 0, SP_ERR_SHORT = -1, SP_ERR_COUNT = -2, SP_ERR_CKSUM = -3, SP_ERR_PEND = -4,
} sp_err_t;

// Decode wire bytes (after C3, through C8). data must hold SP_DATA_MAX bytes.
sp_err_t sp_decode(const uint8_t *wire, size_t n, sp_hdr_t *h, uint8_t *data);

// Encode a packet to the host (DEST $80): preamble, header, data, checksum, C8.
// out must hold SP_TX_MAX bytes. Returns the length.
size_t sp_encode(uint8_t *out, uint8_t src, uint8_t type, uint8_t stat,
                 const uint8_t *data, size_t len);
