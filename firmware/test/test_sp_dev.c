#include <string.h>
#include "test.h"
#include "sp_dev.h"
#include "ramdisk.h"

static uint8_t base[2 * RD_BLOCK_LEN];
static uint8_t slots[2][RD_BLOCK_LEN];
static ramdisk_t rd;
static sp_store_t store;
static sp_dev_t dev;
static sp_reply_t r;
static bool lastone = true;

static bool lastone_cb(void *ctx) { (void)ctx; return lastone; }

// Fake store with knobs (shape of the floppy store).
static sp_media_t f_media_v = SP_MEDIA_OK;
static uint8_t f_read_v, f_write_v, f_format_v;
static int f_reads, f_ejects;
static uint8_t f_read(void *c, uint32_t b, uint8_t *o) { (void)c; (void)b; f_reads++; memset(o, 0x5A, 512); return f_read_v; }
static uint8_t f_write(void *c, uint32_t b, const uint8_t *i) { (void)c; (void)b; (void)i; return f_write_v; }
static uint8_t f_format(void *c) { (void)c; return f_format_v; }
static uint8_t f_eject(void *c) { (void)c; f_ejects++; return 0; }
static sp_media_t f_media(void *c) { (void)c; return f_media_v; }
static void f_unistat(void *c, uint8_t o[8]) { (void)c; memset(o, 0, 8); o[1] = 0x11; o[2] = 3; }

static void run(uint8_t dest, uint8_t cmd, uint8_t pc, uint8_t p2, uint8_t p3, uint8_t p4,
                const uint8_t *data, size_t dlen) {
    uint8_t c[SP_CMD_LEN] = { cmd, pc, 0x00, 0x20, p2, p3, p4, 0, 0 };
    sp_dev_execute(&dev, dest, c, sizeof c, data, dlen, &r);
}

static sp_hdr_t hdr(uint8_t dest, uint8_t type) {
    sp_hdr_t h = { dest, 0, type, 0, 0, 9 };
    return h;
}

int main(void) {
    for (size_t i = 0; i < sizeof base; i++) base[i] = (uint8_t)(i ^ (i >> 8));
    ramdisk_init(&rd, base, sizeof base, slots, 2);
    store = ramdisk_store(&rd);
    sp_dev_init(&dev, &store, lastone_cb, NULL);

    // no ID: INIT to any destination (the card walks the chain), nothing else; data never
    sp_hdr_t h = hdr(7, SP_TYPE_CMD);
    CHECK(sp_dev_accepts(&dev, &h, SP_INIT));
    CHECK(!sp_dev_accepts(&dev, &h, SP_READBLOCK));
    CHECK(!sp_dev_accepts(&dev, &h, SP_STATUS));
    h = hdr(1, SP_TYPE_DATA);
    CHECK(!sp_dev_accepts(&dev, &h, SP_INIT));
    h = hdr(1, SP_TYPE_CMD);
    h.len = 0;
    CHECK(!sp_dev_accepts(&dev, &h, SP_INIT));

    // INIT with LASTONE/ high: ID taken, status $40, pass-through on
    run(1, SP_INIT, 2, 0, 0, 0, NULL, 0);
    CHECK(dev.id == 1 && r.stat == SP_E_LAST && r.len == 0 && dev.passthrough && dev.last);
    h = hdr(1, SP_TYPE_CMD);
    CHECK(sp_dev_accepts(&dev, &h, SP_READBLOCK));
    CHECK(sp_dev_accepts(&dev, &h, SP_INIT));
    h = hdr(2, SP_TYPE_CMD);
    CHECK(!sp_dev_accepts(&dev, &h, SP_READBLOCK));
    CHECK(!sp_dev_accepts(&dev, &h, SP_INIT));
    // INIT again to our ID: OK, ID kept
    run(1, SP_INIT, 2, 0, 0, 0, NULL, 0);
    CHECK(dev.id == 1 && r.stat == SP_E_OK);
    // bus reset clears it; INIT with LASTONE/ low (a drive behind us): status $00
    sp_dev_bus_reset(&dev);
    CHECK(dev.id == 0 && !dev.passthrough);
    lastone = false;
    run(3, SP_INIT, 2, 0, 0, 0, NULL, 0);
    CHECK(dev.id == 3 && r.stat == SP_E_OK && !dev.last);
    // INIT with the wrong parameter count: $04, no ID
    sp_dev_bus_reset(&dev);
    run(1, SP_INIT, 1, 0, 0, 0, NULL, 0);
    CHECK(r.stat == SP_E_BADPCNT && dev.id == 0);
    run(1, SP_INIT, 2, 0, 0, 0, NULL, 0);

    // STATUS
    run(1, SP_STATUS, 3, 0x00, 0, 0, NULL, 0);
    CHECK(r.stat == 0 && r.len == 4 && r.data[0] == 0xF8 && r.data[1] == 0x40 && r.data[2] == 0x06 && r.data[3] == 0);
    run(1, SP_STATUS, 3, 0x03, 0, 0, NULL, 0);
    CHECK(r.stat == 0 && r.len == 25 && r.data[0] == 0xF8 && r.data[4] == 8);
    CHECK(memcmp(r.data + 5, "DISK 3.5        ", 16) == 0);
    CHECK(r.data[21] == 0x01 && r.data[22] == 0x00 && r.data[23] == 0x00 && r.data[24] == 0x10);
    run(1, SP_STATUS, 3, 0x05, 0, 0, NULL, 0);
    CHECK(r.stat == 0 && r.len == 8);
    for (uint8_t code = 1; code < 10; code++) {
        if (code == 3 || code == 5) continue;
        run(1, SP_STATUS, 3, code, 0, 0, NULL, 0);
        CHECK(r.stat == SP_E_BADCTL && r.len == 0);
    }
    run(1, SP_STATUS, 2, 0x00, 0, 0, NULL, 0);
    CHECK(r.stat == SP_E_BADPCNT);

    // READBLOCK
    run(1, SP_READBLOCK, 3, 1, 0, 0, NULL, 0);
    CHECK(r.stat == 0 && r.len == 512 && memcmp(r.data, base + 512, 512) == 0);
    run(1, SP_READBLOCK, 3, 0x40, 0x06, 0, NULL, 0);           // block 1600
    CHECK(r.stat == SP_E_BADBLOCK && r.len == 0);
    run(1, SP_READBLOCK, 3, 0x3F, 0x06, 0, NULL, 0);           // block 1599
    CHECK(r.stat == 0 && r.len == 512);
    run(1, SP_READBLOCK, 3, 0, 0, 1, NULL, 0);                 // block 65536
    CHECK(r.stat == SP_E_BADBLOCK);

    // WRITEBLOCK
    uint8_t w[512];
    memset(w, 0x77, sizeof w);
    CHECK(sp_dev_needs_data(SP_WRITEBLOCK) && sp_dev_needs_data(SP_CONTROL) && sp_dev_needs_data(SP_WRITE));
    CHECK(!sp_dev_needs_data(SP_STATUS) && !sp_dev_needs_data(SP_READBLOCK) && !sp_dev_needs_data(SP_INIT));
    run(1, SP_WRITEBLOCK, 3, 0, 0, 0, w, 512);
    CHECK(r.stat == 0 && r.len == 0);
    run(1, SP_READBLOCK, 3, 0, 0, 0, NULL, 0);
    CHECK(r.stat == 0 && memcmp(r.data, w, 512) == 0);
    run(1, SP_WRITEBLOCK, 3, 0x40, 0x06, 0, w, 512);
    CHECK(r.stat == SP_E_BADBLOCK);
    run(1, SP_WRITEBLOCK, 3, 5, 0, 0, w, 100);                 // short data packet
    CHECK(r.stat == SP_E_IOERROR);
    run(1, SP_WRITEBLOCK, 3, 9, 0, 0, w, 512);
    CHECK(r.stat == 0);
    run(1, SP_WRITEBLOCK, 3, 10, 0, 0, w, 512);                // overlay (2 slots) full
    CHECK(r.stat == SP_E_IOERROR);

    // FORMAT: everything reads zero
    run(1, SP_FORMAT, 1, 0, 0, 0, NULL, 0);
    CHECK(r.stat == 0);
    run(1, SP_READBLOCK, 3, 1, 0, 0, NULL, 0);
    CHECK(r.stat == 0 && r.data[5] == 0 && r.data[511] == 0);

    // CONTROL
    run(1, SP_CONTROL, 3, 0x00, 0, 0, w, 2);
    CHECK(r.stat == 0);
    run(1, SP_CONTROL, 3, 0x04, 0, 0, w, 2);
    CHECK(r.stat == 0);
    for (uint8_t code = 1; code < 10; code++) {
        if (code == 4) continue;
        run(1, SP_CONTROL, 3, code, 0, 0, w, 2);
        CHECK(r.stat == SP_E_BADCTL);
    }

    // open, close, read, write, out of range
    run(1, 0x06, 1, 0, 0, 0, NULL, 0); CHECK(r.stat == SP_E_BADCMD);
    run(1, 0x07, 1, 0, 0, 0, NULL, 0); CHECK(r.stat == SP_E_BADCMD);
    run(1, 0x08, 4, 0, 0, 0, NULL, 0); CHECK(r.stat == SP_E_BADCMD);
    run(1, 0x09, 4, 0, 0, 0, w, 512);  CHECK(r.stat == SP_E_BADCMD);
    run(1, 0x0A, 0, 0, 0, 0, NULL, 0); CHECK(r.stat == SP_E_BADCMD);
    run(1, 0xFF, 0, 0, 0, 0, NULL, 0); CHECK(r.stat == SP_E_BADCMD);
    CHECK(dev.cmds[SP_NCMDS] == 2 && dev.cmds[SP_READBLOCK] >= 6);

    // a store with media state and soft errors (the floppy store shape)
    {
        sp_store_t fs = { f_read, f_write, f_format, f_eject, f_media, f_unistat, NULL };
        sp_dev_t d2;
        sp_dev_init(&d2, &fs, lastone_cb, NULL);
        uint8_t ini[SP_CMD_LEN] = { SP_INIT, 2 };
        sp_dev_execute(&d2, 1, ini, sizeof ini, NULL, 0, &r);
        f_media_v = SP_MEDIA_NODISK;
        uint8_t st[SP_CMD_LEN] = { SP_STATUS, 3, 0, 0, 0x00 };
        sp_dev_execute(&d2, 1, st, sizeof st, NULL, 0, &r);
        CHECK(r.stat == 0 && r.len == 4 && r.data[0] == 0xE8);
        f_media_v = SP_MEDIA_WRPROT;
        st[4] = 0x03;
        sp_dev_execute(&d2, 1, st, sizeof st, NULL, 0, &r);
        CHECK(r.stat == 0 && r.len == 25 && r.data[0] == 0xFC);
        st[4] = 0x05;
        sp_dev_execute(&d2, 1, st, sizeof st, NULL, 0, &r);
        CHECK(r.stat == 0 && r.len == 8 && r.data[1] == 0x11 && r.data[2] == 3);
        uint8_t rb[SP_CMD_LEN] = { SP_READBLOCK, 3, 0, 0, 5 };
        f_read_v = SP_E_SOFT_IO;
        sp_dev_execute(&d2, 1, rb, sizeof rb, NULL, 0, &r);
        CHECK(r.stat == SP_E_SOFT_IO && r.len == 512 && r.data[0] == 0x5A);   // data with $67
        f_read_v = SP_E_OFFLINE;
        sp_dev_execute(&d2, 1, rb, sizeof rb, NULL, 0, &r);
        CHECK(r.stat == SP_E_OFFLINE && r.len == 0);
        rb[4] = 0x40; rb[5] = 0x06;                                           // 1600: before the store
        f_read_v = SP_E_OK;
        int calls = f_reads;
        sp_dev_execute(&d2, 1, rb, sizeof rb, NULL, 0, &r);
        CHECK(r.stat == SP_E_BADBLOCK && f_reads == calls);
        uint8_t wb[SP_CMD_LEN] = { SP_WRITEBLOCK, 3, 0, 0, 5 };
        uint8_t blk[512] = {0};
        f_write_v = SP_E_NOWRITE;
        sp_dev_execute(&d2, 1, wb, sizeof wb, blk, 512, &r);
        CHECK(r.stat == SP_E_NOWRITE);
        uint8_t fm[SP_CMD_LEN] = { SP_FORMAT, 1 };
        f_format_v = SP_E_IOERROR;
        sp_dev_execute(&d2, 1, fm, sizeof fm, NULL, 0, &r);
        CHECK(r.stat == SP_E_IOERROR);
        uint8_t ej[SP_CMD_LEN] = { SP_CONTROL, 3, 0, 0, 0x04 };
        sp_dev_execute(&d2, 1, ej, sizeof ej, blk, 2, &r);
        CHECK(r.stat == 0 && f_ejects == 1);
    }

    // short command packet is padded with zeros
    uint8_t shortc[2] = { SP_FORMAT, 1 };
    sp_dev_execute(&dev, 1, shortc, sizeof shortc, NULL, 0, &r);
    CHECK(r.stat == 0);
    TEST_END();
}
