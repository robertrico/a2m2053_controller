#include "sp_dev.h"
#include <string.h>

// liron.asm expected_param_count_tab, low 7 bits, commands $00-$09.
static const uint8_t pcount_tab[SP_NCMDS] = { 3, 3, 3, 1, 3, 2, 1, 1, 4, 4 };

// liron.asm Ded81: DIB after the 4 general status bytes.
static const uint8_t dib_tail[21] = {
    8, 'D', 'I', 'S', 'K', ' ', '3', '.', '5',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    0x01, 0x00, 0x00, 0x10,
};

void sp_dev_init(sp_dev_t *d, ramdisk_t *rd, bool (*lastone_high)(void *), void *ctx) {
    memset(d, 0, sizeof *d);
    d->rd = rd;
    d->lastone_high = lastone_high;
    d->ctx = ctx;
}

void sp_dev_bus_reset(sp_dev_t *d) {
    d->id = 0;
    d->last = false;
    d->passthrough = false;
}

bool sp_dev_accepts(const sp_dev_t *d, const sp_hdr_t *h) {
    if (h->type != SP_TYPE_CMD) return false;
    return d->id == 0 || h->dest == d->id;
}

bool sp_dev_needs_data(uint8_t cmd) {
    return cmd == SP_WRITEBLOCK || cmd == SP_CONTROL || cmd == SP_WRITE;
}

// Leda3: general status. Online, read/write, format allowed; 1600 blocks.
static void gen_status(sp_reply_t *r) {
    r->data[0] = 0xF8;
    r->data[1] = RD_BLOCKS & 0xFF;
    r->data[2] = RD_BLOCKS >> 8;
    r->data[3] = 0x00;
    r->len = 4;
}

static void do_status(uint8_t code, sp_reply_t *r) {
    switch (code) {
    case 0x00:
        gen_status(r);
        break;
    case 0x03:
        gen_status(r);
        memcpy(r->data + 4, dib_tail, sizeof dib_tail);
        r->len = 4 + sizeof dib_tail;
        break;
    case 0x05:                           // Led96: 8 bytes of drive status; nothing to report
        memset(r->data, 0, 8);
        r->len = 8;
        break;
    default:
        r->stat = SP_E_BADCTL;
    }
}

void sp_dev_execute(sp_dev_t *d, uint8_t dest, const uint8_t *cmd, size_t len,
                    const uint8_t *data, size_t dlen, sp_reply_t *r) {
    uint8_t c[SP_CMD_LEN] = {0};
    memcpy(c, cmd, len < SP_CMD_LEN ? len : SP_CMD_LEN);
    uint32_t block = (uint32_t)c[4] | ((uint32_t)c[5] << 8) | ((uint32_t)c[6] << 16);
    r->stat = SP_E_OK;
    r->len = 0;
    d->cmds[c[0] < SP_NCMDS ? c[0] : SP_NCMDS]++;
    if (c[0] >= SP_NCMDS) { r->stat = SP_E_BADCMD; return; }
    if (c[1] != pcount_tab[c[0]]) { r->stat = SP_E_BADPCNT; return; }
    switch (c[0]) {
    case SP_STATUS:
        do_status(c[4], r);
        break;
    case SP_READBLOCK:
        if (ramdisk_read(d->rd, block, r->data) != RD_OK) { r->stat = SP_E_BADBLOCK; break; }
        r->len = RD_BLOCK_LEN;
        break;
    case SP_WRITEBLOCK:
        if (dlen != RD_BLOCK_LEN) { r->stat = SP_E_IOERROR; break; }
        switch (ramdisk_write(d->rd, block, data)) {
        case RD_OK: break;
        case RD_BADBLOCK: r->stat = SP_E_BADBLOCK; break;
        case RD_FULL: r->stat = SP_E_IOERROR; break;
        }
        break;
    case SP_FORMAT:
        ramdisk_format(d->rd);
        break;
    case SP_CONTROL:
        if (c[4] != 0x00 && c[4] != 0x04) r->stat = SP_E_BADCTL;   // reset, eject (no-op in M6)
        break;
    case SP_INIT:
        d->inits++;
        if (d->id == 0) {
            d->id = dest;
            d->last = d->lastone_high ? d->lastone_high(d->ctx) : true;
            d->passthrough = true;
            if (d->last) r->stat = SP_E_LAST;
        }
        break;
    default:                             // open, close, read, write: not in v1
        r->stat = SP_E_BADCMD;
    }
}
