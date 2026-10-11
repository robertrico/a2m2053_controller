#include "flux.h"
#include "pins.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "flux_rx.pio.h"

#define FLUX_PIO pio1
#define FLUX_SM  0

static uint16_t flux_buf[FLUX_MAX];
static uint offset;
static int dma_ch;

void flux_init(void) {
    offset = pio_add_program(FLUX_PIO, &flux_rx_program);
    pio_sm_claim(FLUX_PIO, FLUX_SM);
    flux_rx_program_init(FLUX_PIO, FLUX_SM, offset, PIN_MECH_RD, 3.0f);
    dma_ch = dma_claim_unused_channel(true);
}

uint32_t flux_capture(uint32_t count, uint32_t timeout_ms) {
    if (count > FLUX_MAX) count = FLUX_MAX;
    pio_sm_set_enabled(FLUX_PIO, FLUX_SM, false);
    pio_sm_clear_fifos(FLUX_PIO, FLUX_SM);
    pio_sm_restart(FLUX_PIO, FLUX_SM);
    pio_sm_exec(FLUX_PIO, FLUX_SM, pio_encode_jmp(offset));

    dma_channel_config cfg = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);   // low 16 bits of each count
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, pio_get_dreq(FLUX_PIO, FLUX_SM, false));
    dma_channel_configure(dma_ch, &cfg, flux_buf, &FLUX_PIO->rxf[FLUX_SM], count, true);

    pio_sm_set_enabled(FLUX_PIO, FLUX_SM, true);
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (dma_channel_is_busy(dma_ch) && !time_reached(deadline))
        tight_loop_contents();

    uint32_t remaining = dma_channel_hw_addr(dma_ch)->transfer_count & 0x0FFFFFFFu;
    if (dma_channel_is_busy(dma_ch)) dma_channel_abort(dma_ch);
    pio_sm_set_enabled(FLUX_PIO, FLUX_SM, false);
    return count - remaining;
}

const uint16_t *flux_data(void) { return flux_buf; }

static uint16_t ring[FLUX_RING_LEN] __attribute__((aligned(FLUX_RING_LEN * 2)));
static const uint32_t STREAM_COUNT = 0x0FFFFFFFu;
static uint32_t consumed;

static uint32_t stream_written(void) {
    return STREAM_COUNT - (dma_channel_hw_addr(dma_ch)->transfer_count & 0x0FFFFFFFu);
}

void flux_stream_start(void) {
    flux_stream_stop();
    pio_sm_clear_fifos(FLUX_PIO, FLUX_SM);
    pio_sm_restart(FLUX_PIO, FLUX_SM);
    pio_sm_exec(FLUX_PIO, FLUX_SM, pio_encode_jmp(offset));
    dma_channel_config cfg = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_ring(&cfg, true, 14);                 // wrap writes every 16 KB
    channel_config_set_dreq(&cfg, pio_get_dreq(FLUX_PIO, FLUX_SM, false));
    dma_channel_configure(dma_ch, &cfg, ring, &FLUX_PIO->rxf[FLUX_SM], STREAM_COUNT, true);
    consumed = 0;
    pio_sm_set_enabled(FLUX_PIO, FLUX_SM, true);
}

void flux_stream_stop(void) {
    pio_sm_set_enabled(FLUX_PIO, FLUX_SM, false);
    if (dma_channel_is_busy(dma_ch)) dma_channel_abort(dma_ch);
}

int flux_stream_read(uint16_t *buf, int max) {
    uint32_t written = stream_written();
    uint32_t avail = written - consumed;
    if (avail > FLUX_RING_LEN) { consumed = written; return -1; }     // writer lapped us
    int n = avail < (uint32_t)max ? (int)avail : max;
    for (int i = 0; i < n; i++) buf[i] = ring[(consumed + (uint32_t)i) % FLUX_RING_LEN];
    consumed += (uint32_t)n;
    return n;
}
