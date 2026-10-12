#pragma once
// SmartPort device logic (M6 spec sec. 6): ID, INIT, command dispatch, status replies.
// No bus timing here: sp_bus feeds decoded packets in and sends the reply out.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ramdisk.h"
#include "sp_wire.h"

#define SP_CMD_LEN 9                     // cmd, pcount, 7 parameter bytes

enum {
    SP_STATUS = 0x00, SP_READBLOCK = 0x01, SP_WRITEBLOCK = 0x02, SP_FORMAT = 0x03,
    SP_CONTROL = 0x04, SP_INIT = 0x05, SP_WRITE = 0x09, SP_NCMDS = 0x0A,
};

// Error codes (liron.asm err_*); the reply STAT byte is $80 | code.
enum {
    SP_E_OK = 0x00, SP_E_BADCMD = 0x01, SP_E_BADPCNT = 0x04, SP_E_BADCTL = 0x21,
    SP_E_IOERROR = 0x27, SP_E_BADBLOCK = 0x2D, SP_E_LAST = 0x40,
};

typedef struct {
    uint8_t stat;                        // code without bit 7
    size_t len;
    uint8_t data[RD_BLOCK_LEN];
} sp_reply_t;

typedef struct {
    uint8_t id;                          // 0 = no ID yet
    bool last;                           // INIT found LASTONE/ high
    bool passthrough;                    // would enable PH3 to the rear port (ROM $44); no pins in M6
    ramdisk_t *rd;
    bool (*lastone_high)(void *ctx);
    void *ctx;
    uint32_t cmds[SP_NCMDS + 1];         // per command; [SP_NCMDS] = out of range
    uint32_t inits;
} sp_dev_t;

void sp_dev_init(sp_dev_t *d, ramdisk_t *rd, bool (*lastone_high)(void *), void *ctx);
void sp_dev_bus_reset(sp_dev_t *d);
// Is this command packet for us? Without an ID every destination is (INIT); with one, ours only.
bool sp_dev_accepts(const sp_dev_t *d, const sp_hdr_t *h);
// Does the host follow this command with a data packet? (ROM expected_param_count_tab MSB.)
bool sp_dev_needs_data(uint8_t cmd);
// Run one command. cmd is the decoded command packet (len bytes, padded with zeros to 9);
// data/dlen the data packet when sp_dev_needs_data(). Fills the reply.
void sp_dev_execute(sp_dev_t *d, uint8_t dest, const uint8_t *cmd, size_t len,
                    const uint8_t *data, size_t dlen, sp_reply_t *r);
