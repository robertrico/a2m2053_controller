#include "flux_tx.h"
#include "pins.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "flux_tx.pio.h"

// The only file allowed to touch /WRTGATE (firmware/test/check_wrtgate.sh).
#define TX_PIO              pio1
#define TX_SM               1
#define TX_CELL_US          2.04248366f     // 489.6 kbit/s (ERS86 2.3.2)
#define TX_CYCLES_PER_CELL  32
#define TX_PINS             ((1u << PIN_MECH_WRTGATE) | (1u << PIN_MECH_WRTDATA))

static uint offset;
static int dma_ch;
static pio_sm_config cfg;

void flux_tx_init(void) {
    offset = pio_add_program(TX_PIO, &flux_tx_program);
    pio_sm_claim(TX_PIO, TX_SM);
    cfg = flux_tx_program_get_default_config(offset);
    sm_config_set_out_pins(&cfg, PIN_MECH_WRTDATA, 1);
    sm_config_set_in_pins(&cfg, PIN_MECH_WRTDATA);              // mov pins, ~pins reads it back
    sm_config_set_set_pins(&cfg, PIN_MECH_WRTGATE, 1);
    sm_config_set_out_shift(&cfg, false, true, 8);              // MSB first, autopull per byte
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&cfg, (float)clock_get_hz(clk_sys) * TX_CELL_US / 1e6f / TX_CYCLES_PER_CELL);
    dma_ch = dma_claim_unused_channel(true);
}

void flux_tx_abort(void) {
    pio_sm_set_enabled(TX_PIO, TX_SM, false);
    if (dma_channel_is_busy(dma_ch)) dma_channel_abort(dma_ch);
    gpio_put(PIN_MECH_WRTGATE, 1);                              // SIO latch high before taking the pin back
    gpio_set_function(PIN_MECH_WRTGATE, GPIO_FUNC_SIO);
    gpio_set_function(PIN_MECH_WRTDATA, GPIO_FUNC_SIO);
}

void flux_tx_load(const uint8_t *buf, size_t len) {
    flux_tx_abort();
    pio_sm_init(TX_PIO, TX_SM, offset, &cfg);                  // stopped, FIFOs cleared, PC at start
    uint32_t level = (1u << PIN_MECH_WRTGATE) | ((uint32_t)gpio_get_out_level(PIN_MECH_WRTDATA) << PIN_MECH_WRTDATA);
    pio_sm_set_pins_with_mask(TX_PIO, TX_SM, level, TX_PINS);  // PIO outputs match the SIO levels
    pio_sm_set_pindirs_with_mask(TX_PIO, TX_SM, TX_PINS, TX_PINS);
    pio_gpio_init(TX_PIO, PIN_MECH_WRTGATE);                   // pad follows the PIO from here: gate high
    pio_gpio_init(TX_PIO, PIN_MECH_WRTDATA);
    pio_sm_put(TX_PIO, TX_SM, (uint32_t)(len * 8u - 1u));
    dma_channel_config c = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);     // byte lands in all lanes; MSB-first shift takes it
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(TX_PIO, TX_SM, true));
    dma_channel_configure(dma_ch, &c, &TX_PIO->txf[TX_SM], buf, len, true);   // pre-fills the FIFO now
}

void flux_tx_fire(void) { pio_sm_set_enabled(TX_PIO, TX_SM, true); }

bool flux_tx_busy(void) {
    bool enabled = (TX_PIO->ctrl & (1u << (PIO_CTRL_SM_ENABLE_LSB + TX_SM))) != 0;
    return enabled && pio_sm_get_pc(TX_PIO, TX_SM) != offset + flux_tx_offset_park;
}
