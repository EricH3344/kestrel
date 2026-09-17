/**
 ******************************************************************************
 * @file    crsf.h
 * @brief   CRSF (Crossfire) protocol - framing, CRC, RC channel packing.
 *
 * Pure C, no HAL / no FreeRTOS dependencies, so it builds and unit-tests on
 * the host (see plan 2.1 / Part 3).
 *
 * Frame on the wire:  [addr][len][type][ payload ... ][crc8]
 *   - addr : 1 byte, not validated by ELRS (see CRSF_ADDR_* below)
 *   - len  : 1 byte, counts  type + payload + crc
 *   - crc8 : DVB-S2, poly 0xD5, computed over  type + payload
 ******************************************************************************
 */
#ifndef CRSF_H
#define CRSF_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Addresses -----------------------------------------------------------
 * The handset->module byte is 0xEE. The design doc's 0xC8 is the flight-
 * controller address (module->FC); ELRS does not check this byte, but we
 * send 0xEE for correctness and recognise both on the downlink deframer.  */
#define CRSF_ADDR_HANDSET     0xEEu
#define CRSF_ADDR_FC          0xC8u

/* ---- Frame types we care about ---------------------------------------- */
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED  0x16u
#define CRSF_FRAMETYPE_LINK_STATISTICS     0x14u
#define CRSF_FRAMETYPE_MAVLINK_ENVELOPE    0x3Au

/* ---- Sizes ---------------------------------------------------------------
 * 16 channels x 11 bits = 176 bits = exactly 22 bytes (byte-aligned).     */
#define CRSF_NUM_CHANNELS     16
#define CRSF_RC_PAYLOAD_BYTES 22

/* Full RC frame: [addr][len][type][22][crc] */
#define CRSF_RC_FRAME_SIZE    (3 + CRSF_RC_PAYLOAD_BYTES + 1)   /* = 26 */

/* CRSF caps the total frame at 64 bytes -> len field <= 62 -> the type+data
 * region is <= 61, so the biggest raw payload we ever wrap is 60.          */
#define CRSF_MAX_PAYLOAD      60
#define CRSF_MAX_FRAME_SIZE   64

/* Channel value range - matches the ELRS ~988/1500/2012 us mapping. */
#define CRSF_CH_MIN   172u
#define CRSF_CH_MID   992u
#define CRSF_CH_MAX   1811u

/* DVB-S2 CRC-8 polynomial */
#define CRSF_CRC_POLY 0xD5u

/* ------------------------------------------------------------------------ */

/** Optional: precompute the CRC table. Safe to skip - crsf_crc8() lazily
 *  builds it on first use - but call this once at init to avoid the first
 *  call being marginally slower / racy. */
void crsf_init(void);

/** Table-driven CRC-8 (poly 0xD5) over @p n bytes at @p d. */
uint8_t crsf_crc8(const uint8_t *d, size_t n);

/** Pack 16 channels (only low 11 bits of each used) LSB-first into 22 bytes. */
void crsf_pack_channels(const uint16_t ch[CRSF_NUM_CHANNELS],
                        uint8_t out[CRSF_RC_PAYLOAD_BYTES]);

/** Inverse of crsf_pack_channels(): 22 bytes -> 16 channels. */
void crsf_unpack_channels(const uint8_t in[CRSF_RC_PAYLOAD_BYTES],
                          uint16_t ch[CRSF_NUM_CHANNELS]);

/** Build an RC_CHANNELS_PACKED frame into @p buf (needs CRSF_RC_FRAME_SIZE).
 *  @return bytes written (always CRSF_RC_FRAME_SIZE). */
size_t crsf_build_rc(uint8_t *buf, const uint16_t ch[CRSF_NUM_CHANNELS]);

/** Build a MAVLINK_ENVELOPE frame wrapping @p n raw bytes at @p payload.
 *  @p n is clamped to CRSF_MAX_PAYLOAD. @p buf needs up to CRSF_MAX_FRAME_SIZE.
 *  @return bytes written (n + 4), or 0 if n == 0. */
size_t crsf_build_mavlink(uint8_t *buf, const uint8_t *payload, size_t n);

/** Validate a complete frame [addr][len][type..][crc] of @p len bytes.
 *  On success returns the frame type (>= 0) and, if non-NULL, sets
 *  *payload / *payload_len to the data between type and crc.
 *  Returns -1 on a bad length field or CRC mismatch. */
int crsf_parse(const uint8_t *frame, size_t len,
               const uint8_t **payload, size_t *payload_len);

#ifdef __cplusplus
}
#endif
#endif /* CRSF_H */
