#pragma once
#include <stdint.h>

// 4-bit register address: (CA2 << 3) | (CA1 << 2) | (CA0 << 1) | SEL.
// Status (sampled on RD): tashnotes MCI 800K table / Apple drive status codes.
// Names without "!" but most are active low: CSTIN low = disk in, TK0 low =
// at track 0, READY low = ready, DRVIN low = drive present, WRPROT low = protected.
typedef enum {
    MREG_DIRTN = 0x0, MREG_CSTIN = 0x1, MREG_STEP = 0x2, MREG_WRPROT = 0x3,
    MREG_MOTORON = 0x4, MREG_TK0 = 0x5, MREG_SWITCHED = 0x6, MREG_TACH = 0x7,
    MREG_RDDATA0 = 0x8, MREG_RDDATA1 = 0x9, MREG_SUPERDRIVE = 0xA, MREG_REG_B = 0xB,
    MREG_SIDES = 0xC, MREG_READY = 0xD, MREG_DRVIN = 0xE, MREG_REVISED = 0xF,
} mech_reg_t;

// Commands (act on the LSTRB rising edge, ERS 3.2.3).
typedef enum {
    MCMD_DIR_IN = 0x0,          // toward center, track number increases
    MCMD_STEP = 0x2,
    MCMD_MOTOR_ON = 0x4,
    MCMD_DIR_OUT = 0x8,         // toward rim
    MCMD_RESET_SWITCHED = 0x9,  // clears the disk-switched / eject latch
    MCMD_MOTOR_OFF = 0xC,
    MCMD_EJECT = 0xE,
} mech_cmd_t;

extern const char *const mech_reg_names[16];

// liron.asm Se654 packs the address as b0=CA2 (ph2), b1=SEL (HDSEL), b2=CA0 (ph0),
// b3=CA1 (ph1). Converts a ROM argument to our address. Oracle cross-check only.
uint8_t mech_addr_from_rom(uint8_t rom_arg);
