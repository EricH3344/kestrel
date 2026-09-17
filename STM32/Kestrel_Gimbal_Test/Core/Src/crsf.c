/**
 ******************************************************************************
 * @file    crsf.c
 * @brief   CRSF framing / CRC / RC channel bit-packing. No HAL, host-testable.
 ******************************************************************************
 */
#include "crsf.h"
#include <string.h>
#include <assert.h>

/* Byte-aligned packing invariant + frame-size sanity (plan 2.1). */
_Static_assert(CRSF_NUM_CHANNELS * 11 == CRSF_RC_PAYLOAD_BYTES * 8,
               "11-bit channel packing must be byte-aligned");
_Static_assert(CRSF_RC_FRAME_SIZE == 26, "RC frame must be 26 bytes on the wire");
_Static_assert(CRSF_MAX_PAYLOAD + 4 <= CRSF_MAX_FRAME_SIZE,
               "wrapped MAVLink frame must fit CRSF_MAX_FRAME_SIZE");

/* --------------------------------------------------------------------------
 * CRC-8 / DVB-S2, poly 0xD5.
 * The 256-entry table is built once from the polynomial so there is no risk
 * of a mistyped constant table.
 * ------------------------------------------------------------------------ */
static uint8_t s_crc_tab[256];
static int     s_crc_ready = 0;

void crsf_init(void)
{
    for (int i = 0; i < 256; i++) {
        uint8_t c = (uint8_t)i;
        for (int b = 0; b < 8; b++) {
            c = (c & 0x80u) ? (uint8_t)((c << 1) ^ CRSF_CRC_POLY)
                            : (uint8_t)(c << 1);
        }
        s_crc_tab[i] = c;
    }
    s_crc_ready = 1;
}

uint8_t crsf_crc8(const uint8_t *d, size_t n)
{
    if (!s_crc_ready) {
        crsf_init();
    }
    uint8_t crc = 0;
    while (n--) {
        crc = s_crc_tab[crc ^ *d++];
    }
    return crc;
}

/* --------------------------------------------------------------------------
 * RC channel packing: 16 x 11 bits, LSB-first, little-endian bit order.
 * Explicit shift/accumulate (plan 2.1) - no compiler-defined bitfields.
 * ------------------------------------------------------------------------ */
void crsf_pack_channels(const uint16_t ch[CRSF_NUM_CHANNELS],
                        uint8_t out[CRSF_RC_PAYLOAD_BYTES])
{
    uint32_t acc = 0;
    unsigned nbits = 0;
    unsigned o = 0;

    for (unsigned i = 0; i < CRSF_NUM_CHANNELS; i++) {
        acc |= (uint32_t)(ch[i] & 0x07FFu) << nbits;
        nbits += 11;
        while (nbits >= 8) {
            out[o++] = (uint8_t)(acc & 0xFFu);
            acc >>= 8;
            nbits -= 8;
        }
    }
    /* 176 bits in, exactly 22 bytes out, nbits back to 0. */
    assert(o == CRSF_RC_PAYLOAD_BYTES && nbits == 0);
}

void crsf_unpack_channels(const uint8_t in[CRSF_RC_PAYLOAD_BYTES],
                          uint16_t ch[CRSF_NUM_CHANNELS])
{
    uint32_t acc = 0;
    unsigned nbits = 0;
    unsigned p = 0;

    for (unsigned i = 0; i < CRSF_NUM_CHANNELS; i++) {
        while (nbits < 11) {
            acc |= (uint32_t)in[p++] << nbits;
            nbits += 8;
        }
        ch[i] = (uint16_t)(acc & 0x07FFu);
        acc >>= 11;
        nbits -= 11;
    }
}

/* --------------------------------------------------------------------------
 * Frame builders
 * ------------------------------------------------------------------------ */
size_t crsf_build_rc(uint8_t *buf, const uint16_t ch[CRSF_NUM_CHANNELS])
{
    buf[0] = CRSF_ADDR_HANDSET;
    buf[1] = CRSF_RC_PAYLOAD_BYTES + 2;               /* type + payload + crc */
    buf[2] = CRSF_FRAMETYPE_RC_CHANNELS_PACKED;
    crsf_pack_channels(ch, &buf[3]);
    buf[3 + CRSF_RC_PAYLOAD_BYTES] =
        crsf_crc8(&buf[2], 1 + CRSF_RC_PAYLOAD_BYTES); /* over type + payload */
    return CRSF_RC_FRAME_SIZE;
}

size_t crsf_build_mavlink(uint8_t *buf, const uint8_t *payload, size_t n)
{
    if (n == 0) {
        return 0;
    }
    if (n > CRSF_MAX_PAYLOAD) {
        assert(0 && "crsf_build_mavlink: payload > CRSF_MAX_PAYLOAD");
        n = CRSF_MAX_PAYLOAD;
    }
    buf[0] = CRSF_ADDR_HANDSET;
    buf[1] = (uint8_t)(n + 2);                        /* type + payload + crc */
    buf[2] = CRSF_FRAMETYPE_MAVLINK_ENVELOPE;
    memcpy(&buf[3], payload, n);
    buf[3 + n] = crsf_crc8(&buf[2], 1 + n);
    return n + 4;                                     /* addr+len+type+n+crc */
}

/* --------------------------------------------------------------------------
 * Frame validation (shared by the downlink deframer and the tests)
 * ------------------------------------------------------------------------ */
int crsf_parse(const uint8_t *frame, size_t len,
               const uint8_t **payload, size_t *payload_len)
{
    if (len < 4) {                       /* addr + len + type + crc minimum */
        return -1;
    }
    unsigned lenfield = frame[1];        /* counts type + payload + crc */
    if (lenfield < 2 || lenfield > 62 || (size_t)(lenfield + 2) != len) {
        return -1;
    }
    uint8_t want = crsf_crc8(&frame[2], lenfield - 1); /* type .. end of payload */
    if (want != frame[len - 1]) {
        return -1;
    }
    if (payload) {
        *payload = &frame[3];
    }
    if (payload_len) {
        *payload_len = lenfield - 2;     /* strip type and crc */
    }
    return frame[2];                     /* frame type */
}
