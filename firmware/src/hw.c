#include <stddef.h>
#include "hw.h"
#include "pins.h"
#include "hardware/gpio.h"

typedef struct { unsigned pin; bool level; } pin_level_t;

static const pin_level_t idle_outputs[] = {
    {PIN_CA0, 0}, {PIN_CA1, 0}, {PIN_CA2, 0}, {PIN_LSTRB, 0}, {PIN_SEL, 0},
    {PIN_MECH_ENBL, 1},
    {PIN_MECH_WRTGATE, 1},
    {PIN_MECH_WRTDATA, 0},
    {PIN_PH3_OUT, 0}, {PIN_ENBL_OUT, 1}, {PIN_LED, 0},
    {PIN_HOST_RD, 1}, {PIN_ACK, 1},      // host bus idle levels (RD idles high, M6); U3 off
};

static const unsigned input_pins[] = {
    PIN_MECH_RD, PIN_PH0, PIN_PH1, PIN_PH2, PIN_PH3,
    PIN_HOST_WRREQ, PIN_HOST_HSTEN, PIN_HOST_WR, PIN_LASTONE,
};

static void out_pin(unsigned pin, bool level) {
    gpio_init(pin);            // input, latch 0, SIO function
    gpio_put(pin, level);      // set the latch before driving: no glitch
    gpio_set_dir(pin, GPIO_OUT);
}

void hw_init_safe(void) {
    out_pin(PIN_MECH_OE, 1);       // U2 + U4 stay Hi-Z (the 10k pull-up held it through reset)
    out_pin(PIN_HOST_RD_OE, 1);    // U3 Hi-Z
    for (size_t i = 0; i < sizeof idle_outputs / sizeof idle_outputs[0]; i++)
        out_pin(idle_outputs[i].pin, idle_outputs[i].level);
    for (size_t i = 0; i < sizeof input_pins / sizeof input_pins[0]; i++) {
        gpio_init(input_pins[i]);
        gpio_disable_pulls(input_pins[i]);   // driven push-pull by an 8T245
    }
    gpio_init(PIN_EJECT_BTN);
    gpio_pull_up(PIN_EJECT_BTN);
}

void hw_mech_outputs_enable(bool on) { gpio_put(PIN_MECH_OE, !on); }
void hw_mech_enbl(bool on) { gpio_put(PIN_MECH_ENBL, !on); }
void hw_led(bool on) { gpio_put(PIN_LED, on); }
bool hw_mech_rd(void) { return gpio_get(PIN_MECH_RD); }

#include "pico/stdlib.h"

#define ADDR_MASK ((1u << PIN_CA0) | (1u << PIN_CA1) | (1u << PIN_CA2) | (1u << PIN_SEL))
_Static_assert((ADDR_MASK & (1u << PIN_MECH_WRTGATE)) == 0, "mech bus must never drive /WRTGATE");

static void bus_set_addr(void *ctx, uint8_t a) {
    (void)ctx;
    uint32_t v = (((a >> 1) & 1u) << PIN_CA0) | (((a >> 2) & 1u) << PIN_CA1) |
                 (((a >> 3) & 1u) << PIN_CA2) | ((a & 1u) << PIN_SEL);
    gpio_put_masked(ADDR_MASK, v);
}
static void bus_set_lstrb(void *ctx, bool high) { (void)ctx; gpio_put(PIN_LSTRB, high); }
static bool bus_read_rd(void *ctx) { (void)ctx; return gpio_get(PIN_MECH_RD); }
static void bus_delay_us(void *ctx, uint32_t us) { (void)ctx; busy_wait_us_32(us); }

static const mech_bus_t gpio_bus = { bus_set_addr, bus_set_lstrb, bus_read_rd, bus_delay_us, NULL };

const mech_bus_t *hw_mech_bus(void) { return &gpio_bus; }
