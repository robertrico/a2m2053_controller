#include <string.h>
#include "test.h"
#include "sync_core1.h"

static fake_drive_t D;
static mech_bus_t B;
static flux_src_t S;
static flux_tx_t T;
static mech_server_t SRV;
static fstore_t F;
static sp_store_t st;

static void setup(void) {
    fake_drive_init(&D);
    B = fake_drive_bus(&D);
    S = fake_flux_src(&D);
    T = fake_flux_tx(&D);
    mech_server_init(&SRV, &B, &S, &T);
    sync_srv = &SRV;
    sync_off = false;
    fstore_init(&F, sync_transact, NULL);
    st = fstore_store(&F);
}

static bool data_is(uint32_t block, const uint8_t *d) {
    int t, s, sec;
    if (!gcr_block_to_tss((int)block, &t, &s, &sec)) return false;
    for (int i = 0; i < 512; i++) if (d[i] != fd_payload_byte(t, s, sec, GCR_TAG_LEN + i)) return false;
    return true;
}

int main(void) {
    uint8_t b[512];

    // status mapping
    CHECK(fstore_code(ST_OK, 0, true) == SP_E_OK);
    CHECK(fstore_code(ST_SOFT, SOFT_BADDATA, true) == SP_E_SOFT_IO);       // ROM Z57 $08/$10
    CHECK(fstore_code(ST_SOFT, SOFT_BADADDR, true) == SP_E_SOFT_IO);       // ROM Z57 $20
    CHECK(fstore_code(ST_SOFT, SOFT_RECAL, true) == SP_E_OK);              // Le4f7 leaves Z57
    CHECK(fstore_code(ST_SOFT, SOFT_OVERRUN, true) == SP_E_OK);
    CHECK(fstore_code(ST_SOFT, SOFT_FOREIGN | SOFT_BADDATA, true) == SP_E_SOFT_IO);
    CHECK(fstore_code(ST_SOFT, SOFT_RECAL, false) == SP_E_OK);
    CHECK(fstore_code(ST_IOERR, 0, true) == SP_E_IOERROR);
    CHECK(fstore_code(ST_NODISK, 0, true) == SP_E_OFFLINE);
    CHECK(fstore_code(ST_BADBLK, 0, true) == SP_E_BADBLOCK);
    CHECK(fstore_code(ST_WRPROT, 0, false) == SP_E_NOWRITE);

    // reads off the fake floppy: the 512 data bytes, not the tags
    setup();
    CHECK(st.read(st.ctx, 0, b) == SP_E_OK && data_is(0, b));
    CHECK(st.read(st.ctx, 777, b) == SP_E_OK && data_is(777, b));
    CHECK(st.read(st.ctx, 1599, b) == SP_E_OK && data_is(1599, b));
    CHECK(st.read(st.ctx, 1600, b) == SP_E_BADBLOCK);

    // media
    CHECK(st.media(st.ctx) == SP_MEDIA_OK);
    D.wprot = true;
    CHECK(st.media(st.ctx) == SP_MEDIA_WRPROT);
    uint8_t w[512];
    for (int i = 0; i < 512; i++) w[i] = (uint8_t)(i * 3 + 1);
    CHECK(st.write(st.ctx, 40, w) == SP_E_NOWRITE);
    D.wprot = false;

    // write, then read it back through the store
    CHECK(st.write(st.ctx, 40, w) == SP_E_OK);
    memset(b, 0, sizeof b);
    CHECK(st.read(st.ctx, 40, b) == SP_E_OK && memcmp(b, w, 512) == 0);
    uint8_t u[8];
    st.unistat(st.ctx, u);
    CHECK(u[0] == 0 && u[3] == 0 && u[7] == 0xFF);

    // head lands on the wrong cylinder once (ROM Z57 case): $67 with the data
    setup();
    D.skew = 1;
    uint8_t code = st.read(st.ctx, 300, b);
    CHECK(code == SP_E_SOFT_IO && data_is(300, b));
    st.unistat(st.ctx, u);
    CHECK(u[1] & SOFT_FOREIGN);
    // bad data on early revolutions only: $67 with the data, as the ROM's $E7
    setup();
    D.bad_sector = 3; D.bad_revs = 2;
    CHECK(st.read(st.ctx, 3, b) == SP_E_SOFT_IO && data_is(3, b));
    // order (ROM Le8b2): no disk before a bad block; write-protect before a bad block
    setup();
    D.disk_in = false;
    CHECK(st.read(st.ctx, 1600, b) == SP_E_OFFLINE);
    setup();
    D.wprot = true;
    CHECK(st.write(st.ctx, 1600, b) == SP_E_NOWRITE);
    // a sector that never reads: $27 after the retries and a recal
    setup();
    D.bad_sector = 3; D.bad_revs = 1000;
    CHECK(st.read(st.ctx, 3, b) == SP_E_IOERROR);
    st.unistat(st.ctx, u);
    CHECK(u[1] & SOFT_RECAL);

    // no disk
    setup();
    D.disk_in = false;
    CHECK(st.read(st.ctx, 5, b) == SP_E_OFFLINE);
    CHECK(st.media(st.ctx) == SP_MEDIA_NODISK);
    D.wprot = true;
    CHECK(st.media(st.ctx) == SP_MEDIA_NODISK);      // no disk wins

    // eject
    setup();
    CHECK(st.read(st.ctx, 100, b) == SP_E_OK);
    CHECK(st.eject(st.ctx) == SP_E_OK && !D.disk_in && D.ejects == 1);
    CHECK(st.media(st.ctx) == SP_MEDIA_NODISK);

    // server off: reads fail $27, media says offline
    setup();
    sync_off = true;
    CHECK(st.read(st.ctx, 0, b) == SP_E_IOERROR);
    CHECK(st.media(st.ctx) == SP_MEDIA_NODISK);
    CHECK(st.write(st.ctx, 0, w) == SP_E_IOERROR);

    // format: write-protected refuses at the first track side; then the whole disk reads zeros
    setup();
    D.wprot = true;
    sync_calls = 0;
    CHECK(st.format(st.ctx) == SP_E_NOWRITE && sync_calls == 1);
    D.wprot = false;
    sync_calls = 0;
    CHECK(st.format(st.ctx) == SP_E_OK && sync_calls == 160);
    bool zero = true;
    for (uint32_t blk = 0; blk < 1600; blk += 97) {
        CHECK(st.read(st.ctx, blk, b) == SP_E_OK);
        for (int i = 0; i < 512; i++) if (b[i]) zero = false;
    }
    CHECK(zero);
    TEST_END();
}
