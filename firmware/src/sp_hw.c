#include "sp_hw.h"
#include "sp_ram.h"
#include "pins.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/structs/sio.h"
#include "pico/time.h"
#include "sp_fm.pio.h"

#define SP_PIO       pio0
#define RX_SM        0
#define TX_SM        1
#define CELL_US      (28.0 / 7.15909)   // Liron IWM slow mode: 28 FCLK at 7.159 MHz
#define RX_CYCLES    48
#define TX_CYCLES    32
#define RX_BUF_LEN   4096               // 8-cell chunks: 128 ms, more than the largest packet
#define DMA_COUNT(ch) (dma_channel_hw_addr(ch)->transfer_count & 0x0FFFFFFFu)

_Static_assert(PIN_HOST_WRREQ == 6, "fm_rx waits on `gpio 6`");
_Static_assert(PIN_PH0 == 2 && PIN_PH1 == 3 && PIN_PH2 == 4 && PIN_PH3 == 5,
               "inputs(): PH0-3 and /WRREQ are GP2-6");

static uint8_t rx_buf[RX_BUF_LEN];
static uint rx_off, tx_off;
static int rx_dma, tx_dma;
static pio_sm_config rx_cfg, tx_cfg;

static uint32_t SP_RAM(h_inputs)(void *c) { (void)c; return (sio_hw->gpio_in >> PIN_PH0) & 0x1Fu; }
static void SP_RAM(h_set_ack)(void *c, bool high) { (void)c; gpio_put(PIN_ACK, high); }
static void SP_RAM(h_set_enbus)(void *c, bool on) { (void)c; gpio_put(PIN_HOST_RD_OE, !on); }

static void h_rx_arm(void *c) {
    (void)c;
    pio_sm_set_enabled(SP_PIO, RX_SM, false);
    if (dma_channel_is_busy(rx_dma)) dma_channel_abort(rx_dma);
    pio_sm_init(SP_PIO, RX_SM, rx_off + fm_rx_offset_entry, &rx_cfg);   // FIFOs and ISR cleared
    pio_sm_exec(SP_PIO, RX_SM, pio_encode_set(pio_y, 1));
    dma_channel_config d = dma_channel_get_default_config(rx_dma);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_8);              // the 8 bits sit in the low byte
    channel_config_set_read_increment(&d, false);
    channel_config_set_write_increment(&d, true);
    channel_config_set_dreq(&d, pio_get_dreq(SP_PIO, RX_SM, false));
    dma_channel_configure(rx_dma, &d, rx_buf, &SP_PIO->rxf[RX_SM], RX_BUF_LEN, true);
    pio_sm_set_enabled(SP_PIO, RX_SM, true);
}

static size_t SP_RAM(h_rx_count)(void *c) { (void)c; return RX_BUF_LEN - DMA_COUNT(rx_dma); }

static void h_tx_start(void *c, const uint8_t *buf, size_t len) {
    (void)c;
    pio_sm_set_enabled(SP_PIO, TX_SM, false);
    if (dma_channel_is_busy(tx_dma)) dma_channel_abort(tx_dma);
    if (len == 0) return;
    pio_sm_init(SP_PIO, TX_SM, tx_off, &tx_cfg);
    pio_sm_set_pins_with_mask(SP_PIO, TX_SM, 1u << PIN_HOST_RD, 1u << PIN_HOST_RD);   // RD high
    pio_sm_put(SP_PIO, TX_SM, (uint32_t)(len * 8u - 1u));
    dma_channel_config d = dma_channel_get_default_config(tx_dma);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_8);              // byte in every lane; MSB-first takes it
    channel_config_set_read_increment(&d, true);
    channel_config_set_write_increment(&d, false);
    channel_config_set_dreq(&d, pio_get_dreq(SP_PIO, TX_SM, true));
    dma_channel_configure(tx_dma, &d, &SP_PIO->txf[TX_SM], buf, len, true);
    pio_sm_set_enabled(SP_PIO, TX_SM, true);
}

static bool h_tx_busy(void *c) {
    (void)c;
    bool on = (SP_PIO->ctrl & (1u << (PIO_CTRL_SM_ENABLE_LSB + TX_SM))) != 0;
    return on && pio_sm_get_pc(SP_PIO, TX_SM) != tx_off + fm_tx_offset_park;
}

static uint64_t SP_RAM(h_now)(void *c) { (void)c; return time_us_64(); }
static void h_delay(void *c, uint32_t us) { (void)c; busy_wait_us_32(us); }
static uint32_t SP_RAM(h_irq_off)(void *c) { (void)c; return save_and_disable_interrupts(); }
static void SP_RAM(h_irq_restore)(void *c, uint32_t s) { (void)c; restore_interrupts(s); }

static const sp_hal_t hal = {
    h_inputs, h_set_ack, h_set_enbus, h_rx_arm, h_rx_count, rx_buf, RX_BUF_LEN,
    h_tx_start, h_tx_busy, h_now, h_delay, h_irq_off, h_irq_restore, NULL,
};

const sp_hal_t *sp_hw_hal(void) { return &hal; }
bool sp_hw_lastone(void *ctx) { (void)ctx; return gpio_get(PIN_LASTONE); }
uint32_t sp_hw_pins(void) { return sio_hw->gpio_in; }

void sp_hw_rd_gpio(bool gpio) {
    gpio_put(PIN_HOST_RD, 1);           // latch high before the pad changes hands: no glitch
    if (gpio) {
        gpio_set_dir(PIN_HOST_RD, GPIO_OUT);
        gpio_set_function(PIN_HOST_RD, GPIO_FUNC_SIO);
    } else {
        pio_gpio_init(SP_PIO, PIN_HOST_RD);   // fm_tx parks with RD high
    }
}

void sp_hw_init(void) {
    float sys = (float)clock_get_hz(clk_sys);
    rx_off = pio_add_program(SP_PIO, &fm_rx_program);
    tx_off = pio_add_program(SP_PIO, &fm_tx_program);
    pio_sm_claim(SP_PIO, RX_SM);
    pio_sm_claim(SP_PIO, TX_SM);
    rx_cfg = fm_rx_config(rx_off, PIN_HOST_WR, sys * (float)CELL_US / 1e6f / RX_CYCLES);
    tx_cfg = fm_tx_config(tx_off, PIN_HOST_RD, sys * (float)CELL_US / 1e6f / TX_CYCLES);
    rx_dma = dma_claim_unused_channel(true);
    tx_dma = dma_claim_unused_channel(true);
    gpio_pull_up(PIN_LASTONE);                                          // U5 not fitted in M6: read high
    // RD: hand the pad to the PIO at its idle level (high). U3 /OE is still high, so nothing
    // reaches the host until the engine turns ENBUS on.
    gpio_put(PIN_HOST_RD, 1);
    pio_sm_init(SP_PIO, TX_SM, tx_off, &tx_cfg);
    pio_sm_set_pins_with_mask(SP_PIO, TX_SM, 1u << PIN_HOST_RD, 1u << PIN_HOST_RD);
    pio_sm_set_pindirs_with_mask(SP_PIO, TX_SM, 1u << PIN_HOST_RD, 1u << PIN_HOST_RD);
    pio_gpio_init(SP_PIO, PIN_HOST_RD);
    gpio_put(PIN_ACK, 1);
    gpio_put(PIN_HOST_RD_OE, 1);
}
