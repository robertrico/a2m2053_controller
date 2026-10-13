#include "sp_dev.h"
#include "sp_ram.h"
#include <string.h>

// liron.asm expected_param_count_tab, low 7 bits, commands $00-$09.
static const uint8_t pcount_tab[SP_NCMDS] = { 3, 3, 3, 1, 3, 2, 1, 1, 4, 4 };

// liron.asm Ded81: DIB after the 4 general status bytes.
static const uint8_t dib_tail[21] = {
    8, 'D', 'I', 'S', 'K', ' ', '3', '.', '5',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    0x01, 0x00, 0x00, 0x10,
};

void sp_dev_init(sp_dev_t *d, const sp_store_t *store, bool (*lastone_high)(void *), void *ctx) {
    memset(d, 0, sizeof *d);
    d->store = store;
    d->lastone_high = lastone_high;
    d->ctx = ctx;
}

void sp_dev_bus_reset(sp_dev_t *d) {
    d->id = 0;
    d->last = false;
    d->passthrough = false;
}

bool SP_RAM(sp_dev_accepts)(const sp_dev_t *d, const sp_hdr_t *h, uint8_t cmd) {
    if (h->type != SP_TYPE_CMD || h->len == 0) return false;
    return d->id == 0 ? cmd == SP_INIT : h->dest == d->id;
}

bool sp_dev_needs_data(uint8_t cmd) {
    return cmd == SP_WRITEBLOCK || cmd == SP_CONTROL || cmd == SP_WRITE;
}

// Leda3: general status. $F8 online, read/write, format allowed; $E8 no disk (offline);
// $FC write-protected. 1600 blocks. The status code itself stays $00.
static void gen_status(const sp_dev_t *d, sp_reply_t *r) {
    sp_media_t m = d->store->media(d->store->ctx);
    r->data[0] = m == SP_MEDIA_NODISK ? 0xE8 : m == SP_MEDIA_WRPROT ? 0xFC : 0xF8;
    r->data[1] = SP_BLOCKS & 0xFF;
    r->data[2] = SP_BLOCKS >> 8;
    r->data[3] = 0x00;
    r->len = 4;
}

static void do_status(const sp_dev_t *d, uint8_t code, sp_reply_t *r) {
    switch (code) {
    case 0x00:
        gen_status(d, r);
        break;
    case 0x03:
        gen_status(d, r);
        memcpy(r->data + 4, dib_tail, sizeof dib_tail);
        r->len = 4 + sizeof dib_tail;
        break;
    case 0x05:                           // Led96: 8 bytes of drive status
        d->store->unistat(d->store->ctx, r->data);
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
        do_status(d, c[4], r);
        break;
    case SP_READBLOCK:
        r->stat = d->store->read(d->store->ctx, block, r->data);   // store checks media, then range (ROM)
        if (r->stat == SP_E_OK || r->stat == SP_E_SOFT_IO) r->len = SP_BLOCK_LEN;   // ROM sends data with $E7
        break;
    case SP_WRITEBLOCK:
        if (dlen != SP_BLOCK_LEN) { r->stat = SP_E_IOERROR; break; }
        r->stat = d->store->write(d->store->ctx, block, data);
        break;
    case SP_FORMAT:
        r->stat = d->store->format(d->store->ctx);
        break;
    case SP_CONTROL:
        if (c[4] == 0x04) r->stat = d->store->eject(d->store->ctx);   // ROM Se51b: $27 if it failed
        else if (c[4] != 0x00) r->stat = SP_E_BADCTL;                 // $00 reset: nothing to do
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
