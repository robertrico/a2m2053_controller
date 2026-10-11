#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "flux_dec.h"

#define FIX "fixtures/"
#define M3  "../../docs/captures/m3/scan7_driveA_caps/"

typedef struct {
    uint8_t data[12][524];
    int got[12];
    int nsect, events[5], foreign_side1;
} sink_t;

static void on_sector(void *ctx, int sector, const uint8_t *p) {
    sink_t *s = ctx;
    if (sector >= 0 && sector < 12) { memcpy(s->data[sector], p, 524); s->got[sector]++; }
}
static void on_event(void *ctx, flux_ev_t ev, int track, int side, int sector) {
    sink_t *s = ctx; (void)track; (void)sector;
    s->events[ev]++;
    if (ev == FLUX_EV_FOREIGN && side == 1) s->foreign_side1++;
}

static uint8_t *load(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("missing fixture %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)*len);
    if (fread(buf, 1, (size_t)*len, f) != (size_t)*len) exit(2);
    fclose(f);
    return buf;
}

static uint16_t *load_u16(const char *path, int *n) {
    long len; uint8_t *b = load(path, &len);
    *n = (int)(len / 2);
    uint16_t *v = malloc(sizeof(uint16_t) * (size_t)*n);
    for (int i = 0; i < *n; i++) v[i] = (uint16_t)(b[2 * i] | (b[2 * i + 1] << 8));
    free(b);
    return v;
}

// Parse an M3 capture (FLUX BEGIN <n> <tick>\r\n + n x u16). Drops the first (partial) interval.
static uint16_t *load_flux(const char *path, int *n) {
    long len; uint8_t *b = load(path, &len);
    char *p = strstr((char *)b, "FLUX BEGIN ");
    if (!p) exit(2);
    int count = atoi(p + 11);
    char *nl = strchr(p, '\n');
    const uint8_t *d = (const uint8_t *)nl + 1;
    uint16_t *v = malloc(sizeof(uint16_t) * (size_t)count);
    for (int i = 1; i < count; i++) v[i - 1] = (uint16_t)(d[2 * i] | (d[2 * i + 1] << 8));
    *n = count - 1;
    free(b);
    return v;
}

static void decode(const uint16_t *v, int n, int track, int side, sink_t *s, int chunk) {
    memset(s, 0, sizeof *s);
    flux_dec_t d;
    flux_dec_sink_t k = { .sector = on_sector, .event = on_event, .ctx = s };
    flux_dec_reset(&d, track, side, &k);
    if (chunk <= 0) { for (int i = 0; i < n; i++) flux_dec_push(&d, v[i]); }
    else for (int i = 0; i < n; i += chunk) flux_dec_push_many(&d, v + i, n - i < chunk ? n - i : chunk);
    for (int i = 0; i < 12; i++) if (s->got[i]) s->nsect++;
}

static void test_clean(void) {
    int n; long el;
    uint16_t *v = load_u16(FIX "synth_clean_t00s0.u16", &n);
    uint8_t *exp = load(FIX "synth_clean_t00s0.exp", &el);
    sink_t s; decode(v, n, 0, 0, &s, 0);
    CHECK(s.nsect == 12);
    for (int i = 0; i < 12; i++) CHECK(memcmp(s.data[i], exp + 524 * i, 524) == 0);
    CHECK(s.events[FLUX_EV_BAD_DATA] == 0 && s.events[FLUX_EV_FOREIGN] == 0);
    free(v); free(exp);
}

static void test_jitter_and_drift(void) {
    int n; long el;
    uint16_t *v = load_u16(FIX "synth_drift_t70s1.u16", &n);
    uint8_t *exp = load(FIX "synth_drift_t70s1.exp", &el);
    sink_t s; decode(v, n, 70, 1, &s, 0);
    CHECK(s.nsect == 8);
    for (int i = 0; i < 8; i++) CHECK(memcmp(s.data[i], exp + 524 * i, 524) == 0);
    free(v); free(exp);
}

static void test_wrong_side_is_foreign(void) {
    int n;
    uint16_t *v = load_u16(FIX "synth_wrongside_t03s1.u16", &n);
    sink_t s; decode(v, n, 3, 0, &s, 0);
    CHECK(s.nsect == 0);
    CHECK(s.events[FLUX_EV_FOREIGN] == 12 && s.foreign_side1 == 12);
    free(v);
}

static void test_corrupt_sector_dropped(void) {
    int n; long el, bl;
    uint16_t *v = load_u16(FIX "synth_corrupt_t20s0.u16", &n);
    uint8_t *exp = load(FIX "synth_corrupt_t20s0.exp", &el);
    uint8_t *bad = load(FIX "synth_corrupt_t20s0.bad", &bl);
    sink_t s; decode(v, n, 20, 0, &s, 0);
    CHECK(s.nsect == 10 && s.got[bad[0]] == 0);
    CHECK(s.events[FLUX_EV_BAD_DATA] >= 1);
    int k = 0;
    for (int i = 0; i < 11; i++) {
        if (i == bad[0]) continue;
        CHECK(memcmp(s.data[i], exp + 524 * k, 524) == 0);
        k++;
    }
    free(v); free(exp); free(bad);
}

static void test_empty(void) {
    sink_t s; decode(NULL, 0, 0, 0, &s, 0);
    CHECK(s.nsect == 0);
    for (int e = 0; e < 5; e++) CHECK(s.events[e] == 0);
}

static const int real_ts[12][2] = {{0,0},{0,1},{5,0},{5,1},{20,0},{20,1},{40,0},{40,1},{60,0},{60,1},{79,0},{79,1}};

static void test_real_captures(void) {
    long el;
    uint8_t *exp = load(FIX "real_expected.bin", &el);
    CHECK(el == 124 * 512);
    long off = 0;
    for (int k = 0; k < 12; k++) {
        int t = real_ts[k][0], sd = real_ts[k][1], spt = gcr_sectors_per_track(t), n;
        char path[256];
        snprintf(path, sizeof path, M3 "t%02ds%d.flux", t, sd);
        uint16_t *v = load_flux(path, &n);
        sink_t s; decode(v, n, t, sd, &s, 0);
        CHECK(s.nsect == spt);
        for (int sec = 0; sec < spt; sec++)
            CHECK(memcmp(s.data[sec] + 12, exp + off + 512L * sec, 512) == 0);
        off += 512L * spt;
        free(v);
    }
    free(exp);
}

static void test_chunking_is_invisible(void) {
    int n;
    uint16_t *v = load_flux(M3 "t40s1.flux", &n);
    sink_t ref; decode(v, n, 40, 1, &ref, 0);
    static const int chunks[] = {1, 7, 500, 1 << 30};
    for (int c = 0; c < 4; c++) {
        sink_t s; decode(v, n, 40, 1, &s, chunks[c]);
        CHECK(s.nsect == ref.nsect);
        CHECK(memcmp(s.events, ref.events, sizeof s.events) == 0);
        for (int i = 0; i < 12; i++) CHECK(!ref.got[i] || memcmp(s.data[i], ref.data[i], 524) == 0);
    }
    free(v);
}

static void test_pll_recovers_from_noise(void) {
    int n; long el;
    uint16_t *v = load_u16(FIX "synth_clean_t00s0.u16", &n);
    uint8_t *exp = load(FIX "synth_clean_t00s0.exp", &el);
    sink_t s; memset(&s, 0, sizeof s);
    flux_dec_t d;
    flux_dec_sink_t k = { .sector = on_sector, .event = on_event, .ctx = &s };
    flux_dec_reset(&d, 0, 0, &k);
    for (int i = 0; i < 200; i++) flux_dec_push(&d, 0);
    flux_dec_push_many(&d, v, n);
    for (int i = 0; i < 12; i++) if (s.got[i]) s.nsect++;
    CHECK(s.nsect == 12);
    for (int i = 0; i < 12; i++) CHECK(memcmp(s.data[i], exp + 524 * i, 524) == 0);
    free(v); free(exp);
}

typedef struct { int n, nsec, track, side, sec[16], nsec_at[16]; } hdr_log_t;

static void hl_hdr(void *ctx, int track, int side, int sector) {
    hdr_log_t *h = ctx;
    if (h->n < 16) { h->sec[h->n] = sector; h->nsec_at[h->n] = h->nsec; }
    h->n++; h->track = track; h->side = side;
}
static void hl_sec(void *ctx, int sector, const uint8_t *p) { (void)sector; (void)p; ((hdr_log_t *)ctx)->nsec++; }

static void hdr_run(const char *path, int track, int side, hdr_log_t *h) {
    int n;
    uint16_t *v = load_u16(path, &n);
    memset(h, 0, sizeof *h);
    flux_dec_t d;
    flux_dec_sink_t k = { .sector = hl_sec, .ctx = h, .header = hl_hdr };
    flux_dec_reset(&d, track, side, &k);
    flux_dec_push_many(&d, v, n);
    free(v);
}

static void test_header_callback(void) {
    hdr_log_t h;
    hdr_run(FIX "synth_clean_t00s0.u16", 0, 0, &h);
    CHECK(h.n == 12 && h.track == 0 && h.side == 0);
    int seen = 0;
    for (int i = 0; i < 12; i++) seen |= 1 << h.sec[i];
    CHECK(seen == 0xFFF);
    for (int i = 0; i < 12; i++) CHECK(h.nsec_at[i] == i);   // fires before its own data field
}

static void test_header_callback_foreign_and_bad_data(void) {
    hdr_log_t h;
    hdr_run(FIX "synth_wrongside_t03s1.u16", 3, 0, &h);   // decoder expects side 0, disk says side 1
    CHECK(h.n == 12 && h.side == 1 && h.nsec == 0);       // reported with the header's own side
    hdr_run(FIX "synth_corrupt_t20s0.u16", 20, 0, &h);    // one bad data field, all headers good
    CHECK(h.n == 11 && h.nsec == 10);
}

// A header whose address checksum is bad must not fire the header callback: it gates M5 writes.
static int hdr_count_for_addr(const uint8_t addr[GCR_ADDR_FIELD_LEN]) {
    uint8_t bytes[64];
    size_t nb = 0;
    for (int i = 0; i < 8; i++) bytes[nb++] = 0xFF;
    memcpy(bytes + nb, addr, GCR_ADDR_FIELD_LEN); nb += GCR_ADDR_FIELD_LEN;
    for (int i = 0; i < 8; i++) bytes[nb++] = 0xFF;
    uint16_t iv[64 * 8];
    int ni = 0, run = 0;
    for (size_t k = 0; k < nb * 8; k++) {
        run++;
        if ((bytes[k >> 3] >> (7 - (k & 7))) & 1) {
            iv[ni++] = (uint16_t)((float)run * FLUX_DEC_CELL_COUNTS - FLUX_DEC_OVERHEAD + 0.5f);
            run = 0;
        }
    }
    hdr_log_t h;
    memset(&h, 0, sizeof h);
    flux_dec_t d;
    flux_dec_sink_t k = { .sector = hl_sec, .ctx = &h, .header = hl_hdr };
    flux_dec_reset(&d, 5, 0, &k);
    flux_dec_push_many(&d, iv, ni);
    return h.n;
}

static void test_header_bad_checksum_no_callback(void) {
    uint8_t a[GCR_ADDR_FIELD_LEN];
    gcr_encode_address(5, 3, 0, a);
    CHECK(hdr_count_for_addr(a) == 1);            // control: the good header is seen
    a[7] = a[7] == 0x96 ? 0x97 : 0x96;            // checksum nibble changed to another valid nibble
    CHECK(hdr_count_for_addr(a) == 0);
}

int main(void) {
    test_clean();
    test_jitter_and_drift();
    test_wrong_side_is_foreign();
    test_corrupt_sector_dropped();
    test_empty();
    test_real_captures();
    test_chunking_is_invisible();
    test_pll_recovers_from_noise();
    test_header_callback();
    test_header_callback_foreign_and_bad_data();
    test_header_bad_checksum_no_callback();
    TEST_END();
}
