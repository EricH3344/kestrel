/**
 ******************************************************************************
 * @file    crsf.c
 * @brief   CRSF framing / CRC / RC channel bit-packing. No HAL, host-testable.
 ******************************************************************************
 */
#include "crsf.h"
#include <string.h>
#include <assert.h>
#include <stdbool.h>

/* Check that the channel packing is byte-aligned and frame sizes are correct */
_Static_assert(CRSF_NUM_CHANNELS * 11 == CRSF_RC_PAYLOAD_BYTES * 8,
               "11-bit channel packing must be byte-aligned");
_Static_assert(CRSF_RC_FRAME_SIZE == 26, "RC frame must be 26 bytes on the wire");
_Static_assert(CRSF_MAX_PAYLOAD + 4 <= CRSF_MAX_FRAME_SIZE,
               "wrapped MAVLink frame must fit CRSF_MAX_FRAME_SIZE");
_Static_assert(sizeof(crsf_link_stats_t) == 10, "LINK_STATISTICS payload is 10 bytes");

/* --------------------------------------------------------------------------
 * The 256-byte lookup table is built dynamically from the polynomial 0xD5
 * so there is no risk of a mistyped constant table.
 * ------------------------------------------------------------------------ */
static uint8_t s_crc_tab[256];
static bool    s_crc_ready = false;

uint8_t crsf_crc8(const uint8_t *d, size_t n)
{
    if (!s_crc_ready) {
        for (int i = 0; i < 256; i++) {
            uint8_t c = (uint8_t)i;
            for (int b = 0; b < 8; b++) {
                c = (c & 0x80u) ? (uint8_t)((c << 1) ^ CRSF_CRC_POLY)
                                : (uint8_t)(c << 1);
            }
            s_crc_tab[i] = c;
        }
        s_crc_ready = true;
    }
    uint8_t crc = 0;
    while (n--) {
        crc = s_crc_tab[crc ^ *d++];
    }
    return crc;
}

/* --------------------------------------------------------------------------
 * RC channel packing:
 * 16 x 11 bits = 22 bytes, LSB-first/little-endian bit order.
 * ------------------------------------------------------------------------ */
void crsf_pack_channels(const uint16_t ch[CRSF_NUM_CHANNELS],
                        uint8_t out[CRSF_RC_PAYLOAD_BYTES])
{
    uint32_t acc = 0; // bit accumulator
    unsigned nbits = 0; // number of bits in acc
    unsigned o = 0; // output byte index

    // for every channel, take the 11 bits and shift them into the acc until
    // acc has at least 8 bits, then write out a byte and shift acc down
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

// Inverse of crsf_pack_channels() for testing
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
 * Frame builders:
 * Builds the frames inside the buffer provided by the caller.
 * The caller must ensure that the buffer is large enough to hold the frame.
 * ------------------------------------------------------------------------ */
size_t crsf_build_rc(uint8_t *buf, const uint16_t ch[CRSF_NUM_CHANNELS])
{
    buf[0] = CRSF_SYNC_TX;                            /* handset sync byte */
    buf[1] = CRSF_RC_PAYLOAD_BYTES + 2;               /* type(1) + payload(22) + crc(1) */
    buf[2] = CRSF_FRAMETYPE_RC_CHANNELS_PACKED;       /* frame type */
    crsf_pack_channels(ch, &buf[3]);                  /* pack 16 channels into 22 bytes */
    buf[3 + CRSF_RC_PAYLOAD_BYTES] =
        crsf_crc8(&buf[2], 1 + CRSF_RC_PAYLOAD_BYTES); /* crc over type + payload */
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
    buf[0] = CRSF_SYNC_TX;                            /* handset sync byte */
    buf[1] = (uint8_t)(n + 2);                        /* type(1) + payload(n) + crc(1) */
    buf[2] = CRSF_FRAMETYPE_ELRS_MAVLINK_RAW;         /* mavlink frame type */
    memcpy(&buf[3], payload, n);                      /* copy mavlink payload */
    buf[3 + n] = crsf_crc8(&buf[2], 1 + n);           /* crc over type + payload */
    return n + 4;                                     /* sync+len+type+payload+crc */
}

size_t crsf_build_ext(uint8_t *buf, uint8_t type, uint8_t dest, uint8_t orig,
                      const uint8_t *p, size_t n)
{
    if (n > CRSF_MAX_PAYLOAD - 2) {
        assert(0 && "crsf_build_ext: payload too long");
        n = CRSF_MAX_PAYLOAD - 2;
    }
    buf[0] = CRSF_SYNC_TX;
    buf[1] = (uint8_t)(n + 4);                        /* type + dest + orig + n + crc */
    buf[2] = type;
    buf[3] = dest;
    buf[4] = orig;
    memcpy(&buf[5], p, n);
    buf[5 + n] = crsf_crc8(&buf[2], 3 + n);           /* crc over type + dest + orig + p */
    return n + 6;
}

size_t crsf_build_param_write(uint8_t *buf, uint8_t idx, uint8_t val)
{
    const uint8_t p[2] = { idx, val };
    return crsf_build_ext(buf, CRSF_FRAMETYPE_PARAMETER_WRITE,
                          CRSF_ADDR_MODULE, CRSF_ADDR_HANDSET, p, sizeof p);
}

/* --------------------------------------------------------------------------
 * Frame validation
 * ------------------------------------------------------------------------ */
int crsf_parse(const uint8_t *frame, size_t len,
               const uint8_t **payload, size_t *payload_len)
{
    if (len < 4) {                       /* addr + len + type + crc (at least 4 bytes) */
        return -1;
    }
    unsigned lenfield = frame[1];        /* type + payload + crc; frame = lenfield + 2 */
    if (lenfield < 2 || lenfield > 62 || (size_t)(lenfield + 2) != len) {
        return -1;
    }
    uint8_t want = crsf_crc8(&frame[2], lenfield - 1); /* validate crc over type + payload */
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

/* --------------------------------------------------------------------------
 * Downlink deframer: hunt 0xC8, check the length byte, validate the CRC.
 * On a bad frame the leading byte is dropped and the buffered bytes are
 * re-scanned, so a real frame hiding inside garbage is still delivered.
 * ------------------------------------------------------------------------ */
int crsf_deframe_push(crsf_deframer_t *d, uint8_t b,
                      const uint8_t **frame, size_t *len)
{
    if (d->done) {                                   /* discard last call's frame */
        d->n -= d->done;
        memmove(d->buf, d->buf + d->done, d->n);
        d->done = 0;
    }
    if (d->n == 0 && b != CRSF_SYNC_RX) {
        return 0;                                    /* hunting for sync */
    }
    d->buf[d->n++] = b;

    for (;;) {
        if (d->n < 2) {
            return 0;
        }
        unsigned total = (unsigned)d->buf[1] + 2;    /* sync + len + counted bytes */
        if (d->buf[1] >= 2 && total <= CRSF_MAX_FRAME_SIZE) {
            if (d->n < total) {
                return 0;                            /* wait for the rest */
            }
            if (crsf_parse(d->buf, total, NULL, NULL) >= 0) {
                *frame = d->buf;
                *len   = total;
                d->done = (uint8_t)total;
                return 1;
            }
        }
        /* bad length or CRC: drop one byte, re-sync on the next 0xC8 in the buffer */
        unsigned i = 1;
        while (i < d->n && d->buf[i] != CRSF_SYNC_RX) {
            i++;
        }
        d->n -= i;
        memmove(d->buf, d->buf + i, d->n);
    }
}
