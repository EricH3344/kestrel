/**
 * Host unit tests for Core/Src/crsf.c  (plan Part 3 / Verification step 1).
 *
 * Build & run without CMake:
 *   cc -I../Kestrel_Gimbal_Test/Core/Inc -Wall -Wextra -O2 \
 *      test_crsf.c ../Kestrel_Gimbal_Test/Core/Src/crsf.c -o test_crsf && ./test_crsf
 *
 * Or via CTest:  cmake -B build && cmake --build build && ctest --test-dir build
 */
#include "crsf.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_fail = 0;
#define CHECK(cond) do {                                                   \
    if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
                   g_fail++; }                                             \
} while (0)

/* ---- CRC-8 / DVB-S2 golden values -------------------------------------- */
static void test_crc(void)
{
    CHECK(crsf_crc8((const uint8_t[]){0x00}, 1) == 0x00);
    CHECK(crsf_crc8((const uint8_t[]){0x01}, 1) == CRSF_CRC_POLY);   /* 0xD5 */
    /* Canonical CRC-8/DVB-S2 check value for ASCII "123456789". */
    CHECK(crsf_crc8((const uint8_t *)"123456789", 9) == 0xBC);
}

/* ---- Channel bit-packing --------------------------------------------------
 * LSB-first, 11 bits per channel. Verify bit order with isolated channels. */
static void test_pack_bitorder(void)
{
    uint16_t ch[CRSF_NUM_CHANNELS] = {0};
    uint8_t  buf[CRSF_RC_PAYLOAD_BYTES];

    ch[0] = 0x001;                       /* only LSB of channel 0 set */
    crsf_pack_channels(ch, buf);
    CHECK(buf[0] == 0x01);
    CHECK(buf[1] == 0x00);

    ch[0] = 0x7FF;                       /* all 11 bits of channel 0 */
    crsf_pack_channels(ch, buf);
    CHECK(buf[0] == 0xFF);
    CHECK((buf[1] & 0x07) == 0x07);      /* top 3 bits spill into byte 1 */
    CHECK((buf[1] & 0xF8) == 0x00);      /* channel 1 is zero */

    memset(ch, 0, sizeof ch);
    ch[15] = 0x7FF;                      /* last channel -> top of the buffer */
    crsf_pack_channels(ch, buf);
    CHECK(buf[CRSF_RC_PAYLOAD_BYTES - 1] == 0xFF);
    CHECK((buf[CRSF_RC_PAYLOAD_BYTES - 2] & 0xE0) == 0xE0);
}

/* ---- pack -> unpack round trip over pseudo-random channel sets --------- */
static void test_pack_roundtrip(void)
{
    uint32_t seed = 0x1234567u;
    for (int iter = 0; iter < 20000; iter++) {
        uint16_t in[CRSF_NUM_CHANNELS], out[CRSF_NUM_CHANNELS];
        uint8_t  packed[CRSF_RC_PAYLOAD_BYTES];
        for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
            seed = seed * 1664525u + 1013904223u;
            in[i] = (uint16_t)((seed >> 9) & 0x07FFu);
        }
        crsf_pack_channels(in, packed);
        crsf_unpack_channels(packed, out);
        CHECK(memcmp(in, out, sizeof in) == 0);
    }
}

/* ---- RC frame build + parse ----------------------------------------------- */
static void test_build_rc(void)
{
    uint16_t ch[CRSF_NUM_CHANNELS];
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) ch[i] = CRSF_CH_MID;

    uint8_t f[CRSF_RC_FRAME_SIZE];
    size_t  n = crsf_build_rc(f, ch);

    CHECK(n == CRSF_RC_FRAME_SIZE);          /* 26 */
    CHECK(f[0] == CRSF_SYNC_TX);             /* 0xEE */
    CHECK(f[1] == 24);                       /* type + 22 + crc */
    CHECK(f[2] == CRSF_FRAMETYPE_RC_CHANNELS_PACKED);

    const uint8_t *pl; size_t pl_len;
    int type = crsf_parse(f, n, &pl, &pl_len);
    CHECK(type == CRSF_FRAMETYPE_RC_CHANNELS_PACKED);
    CHECK(pl_len == CRSF_RC_PAYLOAD_BYTES);
    CHECK(pl == &f[3]);

    uint16_t back[CRSF_NUM_CHANNELS];
    crsf_unpack_channels(pl, back);
    CHECK(memcmp(ch, back, sizeof ch) == 0);

    f[10] ^= 0xFF;                           /* corrupt payload */
    CHECK(crsf_parse(f, n, NULL, NULL) == -1);
    f[10] ^= 0xFF;
    f[n - 1] ^= 0x01;                        /* corrupt crc */
    CHECK(crsf_parse(f, n, NULL, NULL) == -1);
}

/* ---- MAVLink envelope build + parse ------------------------------------- */
static void test_build_mavlink(void)
{
    uint8_t payload[CRSF_MAX_PAYLOAD];
    for (int i = 0; i < (int)sizeof payload; i++) payload[i] = (uint8_t)(i * 7 + 1);

    uint8_t f[CRSF_MAX_FRAME_SIZE];

    for (size_t n = 1; n <= CRSF_MAX_PAYLOAD; n++) {
        size_t total = crsf_build_mavlink(f, payload, n);
        CHECK(total == n + 4);
        CHECK(f[0] == CRSF_SYNC_TX);
        CHECK(f[1] == n + 2);
        CHECK(f[2] == CRSF_FRAMETYPE_ELRS_MAVLINK_RAW);

        const uint8_t *pl; size_t pl_len;
        int type = crsf_parse(f, total, &pl, &pl_len);
        CHECK(type == CRSF_FRAMETYPE_ELRS_MAVLINK_RAW);
        CHECK(pl_len == n);
        CHECK(memcmp(pl, payload, n) == 0);
    }

    CHECK(crsf_build_mavlink(f, payload, 0) == 0);          /* empty -> nothing */
}

/* ---- Extended frame: PARAMETER_WRITE "Packet Rate"(1) = 19 (F1000) ------ */
static void test_build_param_write(void)
{
    uint8_t f[CRSF_MAX_FRAME_SIZE];
    size_t  n = crsf_build_param_write(f, 1, 19);

    const uint8_t want[] = { 0xEE, 0x06, 0x2D, 0xEE, 0xEA, 0x01, 0x13 };
    CHECK(n == 8);
    CHECK(memcmp(f, want, sizeof want) == 0);
    CHECK(f[7] == crsf_crc8(&f[2], 5));      /* crc covers type+dest+orig+idx+val */

    const uint8_t *pl; size_t pl_len;
    CHECK(crsf_parse(f, n, &pl, &pl_len) == CRSF_FRAMETYPE_PARAMETER_WRITE);
    CHECK(pl_len == 4 && pl[0] == CRSF_ADDR_MODULE && pl[1] == CRSF_ADDR_HANDSET);
}

/* ---- Deframer -------------------------------------------------------------
 * Frames from the module carry sync 0xC8; build with the TX helpers and patch
 * byte 0 (the CRC does not cover it). */
static size_t mk_rx_mavlink(uint8_t *f, uint8_t seed, size_t n)
{
    uint8_t p[CRSF_MAX_PAYLOAD];
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(seed + i * 13);
    size_t len = crsf_build_mavlink(f, p, n);
    f[0] = CRSF_SYNC_RX;
    return len;
}

/* Push bytes, return how many frames came out; copy the last one to *last. */
static int feed(crsf_deframer_t *d, const uint8_t *bytes, size_t n,
                uint8_t *last, size_t *last_len)
{
    int got = 0;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *fr; size_t fl;
        if (crsf_deframe_push(d, bytes[i], &fr, &fl)) {
            got++;
            memcpy(last, fr, fl);
            *last_len = fl;
        }
    }
    return got;
}

static void test_deframer(void)
{
    crsf_deframer_t d;
    uint8_t a[CRSF_MAX_FRAME_SIZE], b[CRSF_MAX_FRAME_SIZE];
    uint8_t out[CRSF_MAX_FRAME_SIZE]; size_t out_len = 0;
    uint8_t stream[256]; size_t sl;
    size_t la = mk_rx_mavlink(a, 0x10, 20);
    size_t lb = mk_rx_mavlink(b, 0x77, CRSF_MAX_PAYLOAD);   /* biggest frame: 64 B */

    /* clean frame: exactly one result, on the last byte */
    memset(&d, 0, sizeof d);
    CHECK(feed(&d, a, la, out, &out_len) == 1);
    CHECK(out_len == la && memcmp(out, a, la) == 0);

    /* two back to back, incl. the 64-byte maximum */
    memset(&d, 0, sizeof d);
    memcpy(stream, a, la); memcpy(stream + la, b, lb); sl = la + lb;
    CHECK(feed(&d, stream, sl, out, &out_len) == 2);
    CHECK(out_len == lb && memcmp(out, b, lb) == 0);

    /* garbage with fake syncs and plausible lengths (incl. a 64-byte claim
     * that swallows frame a until b's bytes force the resync), then a, b */
    memset(&d, 0, sizeof d);
    const uint8_t junk[] = { 0x00, 0xC8, 0x05, 0xC8, 0x18, 0xFF, 0xC8, 0x3E, 0x16, 0xC8, 0x02, 0x00 };
    memcpy(stream, junk, sizeof junk); memcpy(stream + sizeof junk, a, la);
    memcpy(stream + sizeof junk + la, b, lb); sl = sizeof junk + la + lb;
    CHECK(feed(&d, stream, sl, out, &out_len) == 2);
    CHECK(out_len == lb && memcmp(out, b, lb) == 0);

    /* corrupted CRC then a good frame: only the good one comes out */
    memset(&d, 0, sizeof d);
    memcpy(stream, a, la); stream[la - 1] ^= 0x01; memcpy(stream + la, b, lb); sl = la + lb;
    CHECK(feed(&d, stream, sl, out, &out_len) == 1);
    CHECK(out_len == lb && memcmp(out, b, lb) == 0);

    /* truncated frame (lost bytes) immediately followed by a good one:
     * the good one is found inside the failed candidate and still delivered */
    memset(&d, 0, sizeof d);
    memcpy(stream, a, 10); memcpy(stream + 10, b, lb); sl = 10 + lb;
    CHECK(feed(&d, stream, sl, out, &out_len) == 1);
    CHECK(out_len == lb && memcmp(out, b, lb) == 0);

    /* 0xC8 inside a payload must not break framing */
    memset(&d, 0, sizeof d);
    uint8_t p[8] = { 0xC8, 0x05, 0xC8, 0x18, 0xC8, 0xC8, 0x02, 0xC8 };
    size_t lc = crsf_build_mavlink(stream, p, sizeof p); stream[0] = CRSF_SYNC_RX;
    memcpy(stream + lc, a, la); sl = lc + la;
    CHECK(feed(&d, stream, sl, out, &out_len) == 2);
    CHECK(out_len == la && memcmp(out, a, la) == 0);
}

int main(void)
{
    test_crc();
    test_pack_bitorder();
    test_pack_roundtrip();
    test_build_rc();
    test_build_mavlink();
    test_build_param_write();
    test_deframer();

    if (g_fail == 0) { printf("crsf: all tests passed\n"); return 0; }
    printf("crsf: %d check(s) failed\n", g_fail);
    return 1;
}
