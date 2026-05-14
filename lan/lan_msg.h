/*
 * Wire-format encode/decode for LAN messages.
 *
 * Frame layout (header is 8 bytes, all multi-byte fields little-endian):
 *
 *   off  size  field
 *   0    4     magic        = "ITW1"
 *   4    1     spec_version = 1
 *   5    1     msg_type     (LanMsgType)
 *   6    2     length       (bytes of payload between header and CRC)
 *   8    L     payload
 *   8+L  4     crc32_le     (IEEE polynomial, computed over header+payload)
 *
 * HELLO payload starts with u16 rel_seq (reserved for a future reliability layer).
 *
 * This file is intentionally Allegro-free so it can be linked into stand-alone
 * test binaries.
 */

#ifndef ICYTOWER_LAN_MSG_H
#define ICYTOWER_LAN_MSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lan_internal.h"

/* --- Frame primitives ---------------------------------------------------- */

/*
 * Encode a frame. payload may be NULL iff payload_len == 0. Returns total
 * encoded length, or 0 on overflow.
 */
size_t lan_frame_encode(uint8_t *out, size_t out_cap, LanMsgType msg_type,
		const uint8_t *payload, size_t payload_len);

/*
 * Decode a frame header from buf[buf_len]. On success, sets *out_type,
 * *out_payload (pointer into buf) and *out_payload_len; returns true.
 * Validates magic, spec_version and CRC32. Returns false on any error.
 */
bool lan_frame_decode(const uint8_t *buf, size_t buf_len, LanMsgType *out_type,
		const uint8_t **out_payload, size_t *out_payload_len);

uint32_t lan_crc32(const uint8_t *data, size_t len);

/* --- Read/write helpers (little-endian) ---------------------------------- */

static inline void lan_w_u8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static inline uint8_t lan_r_u8(const uint8_t **p, const uint8_t *end)
{
	if (*p >= end) return 0;
	return *(*p)++;
}
void lan_w_u16(uint8_t **p, uint16_t v);
void lan_w_u32(uint8_t **p, uint32_t v);
void lan_w_u64(uint8_t **p, uint64_t v);
void lan_w_bytes(uint8_t **p, const uint8_t *bytes, size_t n);
uint16_t lan_r_u16(const uint8_t **p, const uint8_t *end, bool *ok);
uint32_t lan_r_u32(const uint8_t **p, const uint8_t *end, bool *ok);
uint64_t lan_r_u64(const uint8_t **p, const uint8_t *end, bool *ok);
void     lan_r_bytes(const uint8_t **p, const uint8_t *end, uint8_t *out,
		size_t n, bool *ok);

/* --- Payload structs ----------------------------------------------------- */

typedef struct {
	uint64_t room_id;
	uint16_t spec_version;
	/* Non-zero: primary (creator) discovery endpoint; zero: member/joiner. */
	uint8_t  peer_hint;
	char     room_name[LAN_ROOM_LEN];
	uint16_t port;
} LanMsgSessionAdvert;

typedef struct {
	uint16_t rel_seq;
	uint64_t room_id;
	uint16_t spec_version;
	LanUuid  uuid;
	uint8_t  skin_id;
	char     name[LAN_NAME_LEN];
	uint32_t feature_flags;
} LanMsgHello;

/* --- Payload encoders/decoders ------------------------------------------ */

size_t lan_enc_session_advert(uint8_t *out, size_t cap,
		const LanMsgSessionAdvert *m);
size_t lan_enc_hello(uint8_t *out, size_t cap, const LanMsgHello *m);

bool lan_dec_session_advert(const uint8_t *buf, size_t len,
		LanMsgSessionAdvert *m);
bool lan_dec_hello(const uint8_t *buf, size_t len, LanMsgHello *m);

#endif /* ICYTOWER_LAN_MSG_H */
