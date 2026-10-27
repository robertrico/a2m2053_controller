#pragma once
// SmartPort device logic (M6 spec sec. 6): ID, INIT, command dispatch, status replies.
// No bus timing here: sp_bus feeds decoded packets in and sends the reply out.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sp_store.h"
#include "sp_wire.h"

#define SP_CMD_LEN 9                     // cmd, pcount, 7 parameter bytes

enum {
    SP_STATUS = 0x00, SP_READBLOCK = 0x01, SP_WRITEBLOCK = 0x02, SP_FORMAT = 0x03,
    SP_CONTROL = 0x04, SP_INIT = 0x05, SP_WRITE = 0x09, SP_NCMDS = 0x0A,
};

#define SP_BLOCKS 1600                   // 800K double-sided (ROM block_count_tab)

typedef struct {
    uint8_t stat;                        // code without bit 7
    size_t len;
    uint8_t data[SP_BLOCK_LEN];
} sp_reply_t;

typedef struct {
    uint8_t id;                          // 0 = no ID yet
    bool last;                           // INIT found LASTONE/ high
    bool passthrough;                    // rear-port pass-through requested (ROM $44); sp_bus mirrors it to the pins
    const sp_store_t *store;             // swapped at run time (sp disk ram|floppy)
    bool (*lastone_high)(void *ctx);
    void *ctx;
    uint32_t cmds[SP_NCMDS + 1];         // per command; [SP_NCMDS] = out of range
    uint32_t inits;
} sp_dev_t;

void sp_dev_init(sp_dev_t *d, const sp_store_t *store, bool (*lastone_high)(void *), void *ctx);
void sp_dev_bus_reset(sp_dev_t *d);
// Is this command packet for us? Without an ID only INIT, to any destination (the card walks
// the chain); with one, every command to our ID. cmd = first data byte (command code).
bool sp_dev_accepts(const sp_dev_t *d, const sp_hdr_t *h, uint8_t cmd);
// Does the host follow this command with a data packet? (ROM expected_param_count_tab MSB.)
bool sp_dev_needs_data(uint8_t cmd);
// Run one command. cmd is the decoded command packet (len bytes, padded with zeros to 9);
// data/dlen the data packet when sp_dev_needs_data(). Fills the reply.
void sp_dev_execute(sp_dev_t *d, uint8_t dest, const uint8_t *cmd, size_t len,
                    const uint8_t *data, size_t dlen, sp_reply_t *r);
