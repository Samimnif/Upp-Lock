/**
 * protocol.h — Shared UART command/response protocol
 *
 * Frame format (both directions):
 *   [STX 0x02] [CMD/RSP byte] [LEN byte] [payload: LEN bytes] [CRC8]
 *
 * Base → Sensor: commands
 * Sensor → Base: responses
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── Frame delimiters ─────────────────────────────────── */
#define PROTO_STX        0x02u   /* Start of frame        */
#define PROTO_MAX_PAYLOAD  16u   /* Max payload bytes     */

/* ── Commands  (Base → Sensor) ───────────────────────── */
typedef enum {
    CMD_PING          = 0x01,   /* No payload; sensor echoes ACK        */
    CMD_READ_TEMP     = 0x02,   /* No payload; sensor returns temp data  */
    CMD_READ_HUMIDITY = 0x03,   /* No payload; sensor returns hum data   */
    CMD_READ_ALL      = 0x04,   /* No payload; sensor returns all data   */
    CMD_SET_INTERVAL  = 0x05,   /* Payload: uint16_t ms between readings */
    CMD_RESET         = 0xFF,   /* No payload; sensor soft-resets        */
} proto_cmd_t;

/* ── Responses (Sensor → Base) ───────────────────────── */
typedef enum {
    RSP_ACK           = 0x80,   /* Generic acknowledgement               */
    RSP_NACK          = 0x81,   /* Command rejected / unknown            */
    RSP_TEMP          = 0x82,   /* Payload: int16_t centidegrees (°C×100)*/
    RSP_HUMIDITY      = 0x83,   /* Payload: uint16_t centi-percent       */
    RSP_ALL           = 0x84,   /* Payload: temp(int16) + hum(uint16)    */
    RSP_ERROR         = 0xFF,   /* Payload: uint8_t error code           */
} proto_rsp_t;

/* ── Error codes ─────────────────────────────────────── */
typedef enum {
    ERR_NONE          = 0x00,
    ERR_SENSOR_FAULT  = 0x01,
    ERR_BAD_CRC       = 0x02,
    ERR_UNKNOWN_CMD   = 0x03,
} proto_err_t;

/* ── Frame struct ────────────────────────────────────── */
typedef struct {
    uint8_t  stx;                          /* Always PROTO_STX           */
    uint8_t  cmd;                          /* proto_cmd_t or proto_rsp_t */
    uint8_t  len;                          /* Payload length (0..16)     */
    uint8_t  payload[PROTO_MAX_PAYLOAD];   /* Payload bytes              */
    uint8_t  crc;                          /* CRC-8/MAXIM over cmd+len+payload */
} proto_frame_t;

/* Total wire bytes for a frame */
#define PROTO_FRAME_SIZE(f)  (3u + (f)->len + 1u)   /* stx+cmd+len+payload+crc */

/* ── CRC-8 (Dallas/Maxim polynomial 0x31) ────────────── */
static inline uint8_t proto_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* Compute CRC over the cmd, len, and payload fields */
static inline uint8_t proto_frame_crc(const proto_frame_t *f)
{
    uint8_t buf[2 + PROTO_MAX_PAYLOAD];
    buf[0] = f->cmd;
    buf[1] = f->len;
    for (uint8_t i = 0; i < f->len; i++) {
        buf[2 + i] = f->payload[i];
    }
    return proto_crc8(buf, 2u + f->len);
}

/* ── Encode/decode helpers ───────────────────────────── */

/**
 * Serialize a frame into `out` buffer.
 * Returns number of bytes written, or 0 on error.
 */
static inline size_t proto_encode(const proto_frame_t *f, uint8_t *out, size_t out_size)
{
    size_t sz = PROTO_FRAME_SIZE(f);
    if (sz > out_size) return 0;
    out[0] = PROTO_STX;
    out[1] = f->cmd;
    out[2] = f->len;
    for (uint8_t i = 0; i < f->len; i++) {
        out[3 + i] = f->payload[i];
    }
    out[3 + f->len] = proto_frame_crc(f);
    return sz;
}

/**
 * Deserialize `in` buffer into frame `f`.
 * Returns true if valid STX and CRC match.
 */
static inline bool proto_decode(const uint8_t *in, size_t in_size, proto_frame_t *f)
{
    if (in_size < 4) return false;
    if (in[0] != PROTO_STX) return false;

    f->stx = in[0];
    f->cmd = in[1];
    f->len = in[2];

    if (f->len > PROTO_MAX_PAYLOAD) return false;
    if (in_size < (size_t)(3u + f->len + 1u)) return false;

    for (uint8_t i = 0; i < f->len; i++) {
        f->payload[i] = in[3 + i];
    }
    f->crc = in[3 + f->len];

    return f->crc == proto_frame_crc(f);
}

/* ── Payload helpers ─────────────────────────────────── */

static inline void proto_put_i16(proto_frame_t *f, int16_t val)
{
    f->payload[0] = (uint8_t)(val >> 8);
    f->payload[1] = (uint8_t)(val & 0xFF);
    f->len = 2;
}

static inline void proto_put_u16(proto_frame_t *f, uint16_t val)
{
    f->payload[0] = (uint8_t)(val >> 8);
    f->payload[1] = (uint8_t)(val & 0xFF);
    f->len = 2;
}

static inline int16_t proto_get_i16(const proto_frame_t *f)
{
    return (int16_t)((f->payload[0] << 8) | f->payload[1]);
}

static inline uint16_t proto_get_u16(const proto_frame_t *f)
{
    return (uint16_t)((f->payload[0] << 8) | f->payload[1]);
}

#endif /* PROTOCOL_H */
