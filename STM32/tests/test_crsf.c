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
    CHECK(f[0] == CRSF_ADDR_HANDSET);        /* 0xEE */
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
        CHECK(f[0] == CRSF_ADDR_HANDSET);
        CHECK(f[1] == n + 2);
        CHECK(f[2] == CRSF_FRAMETYPE_MAVLINK_ENVELOPE);

        const uint8_t *pl; size_t pl_len;
        int type = crsf_parse(f, total, &pl, &pl_len);
        CHECK(type == CRSF_FRAMETYPE_MAVLINK_ENVELOPE);
        CHECK(pl_len == n);
        CHECK(memcmp(pl, payload, n) == 0);
    }

    CHECK(crsf_build_mavlink(f, payload, 0) == 0);          /* empty -> nothing */
}

int main(void)
{
    crsf_init();
    test_crc();
    test_pack_bitorder();
    test_pack_roundtrip();
    test_build_rc();
    test_build_mavlink();

    if (g_fail == 0) { printf("crsf: all tests passed\n"); return 0; }
    printf("crsf: %d check(s) failed\n", g_fail);
    return 1;
}
