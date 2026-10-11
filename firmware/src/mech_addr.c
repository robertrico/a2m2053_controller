#include "mech_addr.h"

const char *const mech_reg_names[16] = {
    "DIRTN", "CSTIN", "STEP", "WRPROT", "MOTORON", "TK0", "EJECT", "TACH",
    "RDDATA0", "RDDATA1", "SUPERDRIVE", "REG_B", "SIDES", "READY", "DRVIN", "REVISED",
};

uint8_t mech_addr_from_rom(uint8_t a) {
    uint8_t ca2 = a & 1u, sel = (a >> 1) & 1u, ca0 = (a >> 2) & 1u, ca1 = (a >> 3) & 1u;
    return (uint8_t)((ca2 << 3) | (ca1 << 2) | (ca0 << 1) | sel);
}
