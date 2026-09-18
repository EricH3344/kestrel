/**
 ******************************************************************************
 * @file    crsf.h
 * @brief   CRSF (Crossfire) protocol - framing, CRC, RC channel packing.
 *
 * The purpose of this file is to provide minimal implementation of CRSF framing
 * for MAVLink and RC packaging to be sent to an ELRS module.
 * This file is written in pure C with no freeRTOS or STM32 overhead.
 *
 * Any ISR or task in the STM32 project may call this
 * code to utilize the CRSF protocol.
 *
 * Frame on the wire:  [sync][len][type][ payload ... ][crc8]
 *   - sync : 1 byte (see CRSF_SYNC_* below)
 *   - len  : 1 byte, counts  type + payload + crc
 *   - crc8 : DVB-S2, poly 0xD5, computed over  type + payload
 *   Extended types (>= 0x28) start the payload with [dest][orig].
 ******************************************************************************
 */
#ifndef CRSF_H
#define CRSF_H

#include <stdint.h>
#include <stddef.h>

/* ---- Byte 0 (sync) and extended-frame addresses -------------------------
 * The module accepts 0xEE or 0xC8 as byte 0 from us and always sends 0xC8. */
#define CRSF_SYNC_TX          0xEEu // byte 0 of frames we send
#define CRSF_SYNC_RX          0xC8u // byte 0 of frames the module sends
#define CRSF_ADDR_MODULE      0xEEu // TX module: dest of our extended frames
#define CRSF_ADDR_HANDSET     0xEAu // us: orig of our extended frames

/* ---- CRSF Frame types of interest ---------------------------------------- */
#define CRSF_FRAMETYPE_LINK_STATISTICS     0x14u // module -> us, ~4/s
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED  0x16u // us -> module, every tick
#define CRSF_FRAMETYPE_PARAMETER_WRITE     0x2Du // extended: [dest][orig][idx][value]
#define CRSF_FRAMETYPE_ELRS_MAVLINK_RAW    0xAAu // both ways: 1..60 raw MAVLink bytes

/* ---- Sizes ---------------------------------------------------------------
 * 16 channels x 11 bits = 176 bits = exactly 22 bytes (byte-aligned).     */
#define CRSF_NUM_CHANNELS     16
#define CRSF_RC_PAYLOAD_BYTES 22

/* Full RC frame: [sync][len][type][22][crc] */
#define CRSF_RC_FRAME_SIZE    (3 + CRSF_RC_PAYLOAD_BYTES + 1)   /* = 26 */

/* Frame: [sync][len][type..data..crc]. Total <= 64, so payload <= 60. */
#define CRSF_MAX_PAYLOAD      60
#define CRSF_MAX_FRAME_SIZE   64

/* Channel value range - matches the ELRS ~988/1500/2012 us mapping. */
#define CRSF_CH_MIN   172u
#define CRSF_CH_MID   992u
#define CRSF_CH_MAX   1811u

/* DVB-S2 CRC-8 polynomial */
#define CRSF_CRC_POLY 0xD5u

/* LINK_STATISTICS payload. RSSI: v = (int8_t)b; dBm = v <= 0 ? v : -v. */
typedef struct {
    uint8_t up_rssi1, up_rssi2, up_lq;
    int8_t  up_snr;
    uint8_t active_antenna;
    uint8_t rf_mode;            /* ELRS rate enum, see ELRS_RF_MODE_* in freertos.c */
    uint8_t up_tx_power;
    uint8_t down_rssi, down_lq;
    int8_t  down_snr;
} crsf_link_stats_t;

/* Byte-stream deframer state. Zero-initialise; one per stream. */
typedef struct {
    uint8_t buf[CRSF_MAX_FRAME_SIZE];
    uint8_t n;                  /* bytes buffered */
    uint8_t done;               /* length of the frame returned last call */
} crsf_deframer_t;

/* ------------------------------------------------------------------------ */

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

/** Build an ELRS_MAVLINK_RAW (0xAA) frame wrapping @p n raw bytes at @p payload.
 *  @p n is clamped to CRSF_MAX_PAYLOAD. @p buf needs up to CRSF_MAX_FRAME_SIZE.
 *  @return bytes written (n + 4), or 0 if n == 0. */
size_t crsf_build_mavlink(uint8_t *buf, const uint8_t *payload, size_t n);

/** Build an extended frame [sync][len][type][dest][orig][p...][crc].
 *  @p n <= CRSF_MAX_PAYLOAD - 2. @return bytes written (n + 6). */
size_t crsf_build_ext(uint8_t *buf, uint8_t type, uint8_t dest, uint8_t orig,
                      const uint8_t *p, size_t n);

/** PARAMETER_WRITE to the module: set parameter @p idx to @p val. @return 8. */
size_t crsf_build_param_write(uint8_t *buf, uint8_t idx, uint8_t val);

/** Validate a complete frame [sync][len][type..][crc] of @p len bytes.
 *  On success returns the frame type (>= 0) and, if non-NULL, sets
 *  *payload / *payload_len to the data between type and crc.
 *  Returns -1 on a bad length field or CRC mismatch. */
int crsf_parse(const uint8_t *frame, size_t len,
               const uint8_t **payload, size_t *payload_len);

/** Feed one downlink byte. Returns 1 when a CRC-valid frame is complete;
 *  *frame / *len point into @p d and stay valid until the next push. */
int crsf_deframe_push(crsf_deframer_t *d, uint8_t b,
                      const uint8_t **frame, size_t *len);

#endif /* CRSF_H */
