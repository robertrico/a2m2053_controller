#pragma once
// SmartPort bus handshake engine (M6 spec sec. 3.3, 4): receive, ACK, data packet, reply.
// Hardware goes through sp_hal_t so the whole sequence runs against a simulated Liron card
// on the host (firmware/test/test_sp_bus.c).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sp_dev.h"
#include "sp_wire.h"

// sp_hal_t.inputs() bits. WRREQ is the pin level: 1 = /WRREQ high (host not writing).
#define SP_IN_PH0    0x01u          // REQ
#define SP_IN_PH1    0x02u
#define SP_IN_PH2    0x04u
#define SP_IN_PH3    0x08u
#define SP_IN_WRREQ  0x10u
#define SP_IN_PHASES 0x0Fu
#define SP_PH_RESET  (SP_IN_PH0 | SP_IN_PH2)    // PH3..PH0 = 0101

typedef struct {
    uint32_t (*inputs)(void *ctx);
    void (*set_ack)(void *ctx, bool high);              // ACK pin level (on the bus only with ENBUS)
    void (*set_enbus)(void *ctx, bool on);              // U3 /OE low: drive RD and ACK
    void (*rx_arm)(void *ctx);                          // fm_rx back to its /WRREQ wait, buffer empty
    size_t (*rx_count)(void *ctx);                      // 8-cell chunks in rx_buf since rx_arm
    const volatile uint8_t *rx_buf;
    size_t rx_cap;
    void (*tx_start)(void *ctx, const uint8_t *buf, size_t len);
    bool (*tx_busy)(void *ctx);
    uint64_t (*now_us)(void *ctx);
    void (*delay_us)(void *ctx, uint32_t us);
    uint32_t (*irq_off)(void *ctx);
    void (*irq_restore)(void *ctx, uint32_t saved);
    void *ctx;
} sp_hal_t;

// Timeouts in us. Host limits they answer to: M6 spec sec. 3 and 4.
#define SP_T_RX_MAX        60000u   // one packet, sync to C8 (max packet is about 38 ms)
#define SP_T_REQ_FALL      10000u   // host drops REQ after our ACK (card: at once)
#define SP_T_IGNORE        5000u    // host gives up on a packet we did not ACK (card: 105 us)
#define SP_T_DATA_WAIT     4000000u // data packet after WRITEBLOCK/CONTROL (card: 3000 tries, about 3 s)
#define SP_T_REPLY_REQ     1000000u // host raises REQ after our ACK high (ROM: about 0.5 s)
#define SP_T_TX_PAUSE      20u      // REQ high to first reply edge (card flips Q6 about 6 us after REQ)
#define SP_T_REPLY_DONE    50000u   // host drops REQ after the reply
#define SP_T_REREAD        200u     // REQ high, no write, this long: host re-reads our reply

typedef enum {
    SP_LOG_CMD, SP_LOG_IGNORED, SP_LOG_CKSUM, SP_LOG_BAD, SP_LOG_TIMEOUT, SP_LOG_RESET, SP_LOG_REREAD,
} sp_log_kind_t;

typedef struct {
    uint8_t kind, cmd, dest, stat, stage;   // stage: which wait timed out (SP_LOG_TIMEOUT)
    uint32_t block;
    int32_t ack_us;                          // /WRREQ rise to ACK low (negative: ACK first);
                                             // INT32_MIN = not measured
} sp_log_t;

#define SP_LOG_LEN 32                        // power of two

typedef struct {
    uint32_t packets, acks, cksum, bad, ignored, timeouts, resets, rereads, replies;
    int32_t ack_us_max, ack_us_last;         // INT32_MIN = none yet
} sp_stats_t;

typedef struct {
    const sp_hal_t *hal;
    sp_dev_t *dev;
    bool enabled, in_reset;
    uint64_t reread_t;
    bool tx_valid;
    size_t tx_len;
    sp_hdr_t hdr;
    sp_stats_t st;
    sp_rx_t rx;
    sp_reply_t reply;
    uint8_t cmd[SP_DATA_MAX];
    size_t cmd_len;
    uint8_t data[SP_DATA_MAX];
    uint8_t tx[SP_TX_MAX];
    sp_log_t log[SP_LOG_LEN];
    volatile uint32_t log_wr, log_rd;
} sp_bus_t;

void sp_bus_init(sp_bus_t *b, const sp_hal_t *hal, sp_dev_t *dev);
void sp_bus_enable(sp_bus_t *b, bool on);
// Call as often as possible. Returns at once when the bus is idle; otherwise runs one whole
// transaction (bounded by the timeouts above).
void sp_bus_poll(sp_bus_t *b);
bool sp_bus_log_pop(sp_bus_t *b, sp_log_t *e);
