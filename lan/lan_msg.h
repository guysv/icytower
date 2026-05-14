/*
 * Wire-format encode/decode for all LAN messages.
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
 * Reliable messages (HELLO, ROSTER_SNAPSHOT, APPEARANCE, LOBBY_*, JOIN_NONCE,
 * COUNTDOWN_ANCHOR, ELIMINATE, LEAVE) carry a u16 rel_seq as the first two
 * payload bytes; lan_rel handles ordering and ACKs.
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
	uint64_t session_id;
	uint16_t spec_version;
	uint8_t  peer_hint;
	char     room_name[LAN_ROOM_LEN];
	uint16_t port;
} LanMsgSessionAdvert;

typedef struct {
	uint16_t rel_seq;
	uint64_t session_id;
	uint16_t spec_version;
	LanUuid  uuid;
	uint8_t  skin_id;
	char     name[LAN_NAME_LEN];
	uint32_t feature_flags;
} LanMsgHello;

typedef struct {
	uint16_t rel_seq;
	uint64_t session_id;
	uint8_t  count;
	LanUuid  uuids[LAN_MAX_PEERS];
	uint16_t spec_version;
} LanMsgRosterSnapshot;

typedef struct {
	uint16_t rel_seq;
	LanPlayerId player_id;
	uint8_t  skin_id;
	char     name[LAN_NAME_LEN];
} LanMsgAppearance;

typedef struct {
	uint16_t rel_seq;
	LanPlayerId player_id;
	uint8_t  ready;
} LanMsgLobbyReady;

typedef struct {
	uint16_t rel_seq;
	uint32_t round_id;
} LanMsgLobbyPhaseNonce;

typedef struct {
	uint16_t rel_seq;
	LanPlayerId player_id;
	uint32_t round_id;
	uint32_t nonce;
} LanMsgJoinNonce;

typedef struct {
	uint16_t rel_seq;
	LanPlayerId player_id;
	uint32_t round_id;
	uint64_t propose_ms;
} LanMsgCountdownAnchor;

typedef struct {
	LanPlayerId player_id;
	uint32_t seq;
	uint8_t  keybits;
} LanMsgSyncKeys;

typedef struct {
	LanPlayerId player_id;
	uint32_t seq;
	uint8_t  keybits_after;
	uint64_t timestamp_ms;
} LanMsgControlEdge;

typedef struct {
	LanPlayerId player_id;
	uint32_t seq_t;
	int32_t  floor;
	uint32_t score;
} LanMsgScoreTelemetry;

typedef struct {
	uint16_t rel_seq;
	LanPlayerId player_id;
	uint32_t round_id;
	uint8_t  reason; /* LanEliminateReason */
} LanMsgEliminate;

typedef struct {
	uint16_t rel_seq;
	LanPlayerId player_id;
} LanMsgLeave;

typedef struct {
	uint16_t acked_seq;
} LanMsgRelAck;

typedef struct {
	uint64_t session_id;
	uint16_t spec_version;
	LanUuid  uuid;
} LanMsgPartyAck;

/* --- Payload encoders/decoders ------------------------------------------ */

size_t lan_enc_session_advert(uint8_t *out, size_t cap,
		const LanMsgSessionAdvert *m);
size_t lan_enc_hello(uint8_t *out, size_t cap, const LanMsgHello *m);
size_t lan_enc_roster_snapshot(uint8_t *out, size_t cap,
		const LanMsgRosterSnapshot *m);
size_t lan_enc_appearance(uint8_t *out, size_t cap, const LanMsgAppearance *m);
size_t lan_enc_lobby_ready(uint8_t *out, size_t cap, const LanMsgLobbyReady *m);
size_t lan_enc_lobby_phase_nonce(uint8_t *out, size_t cap,
		const LanMsgLobbyPhaseNonce *m);
size_t lan_enc_join_nonce(uint8_t *out, size_t cap, const LanMsgJoinNonce *m);
size_t lan_enc_countdown_anchor(uint8_t *out, size_t cap,
		const LanMsgCountdownAnchor *m);
size_t lan_enc_sync_keys(uint8_t *out, size_t cap, const LanMsgSyncKeys *m);
size_t lan_enc_control_edge(uint8_t *out, size_t cap,
		const LanMsgControlEdge *m);
size_t lan_enc_score_telemetry(uint8_t *out, size_t cap,
		const LanMsgScoreTelemetry *m);
size_t lan_enc_eliminate(uint8_t *out, size_t cap, const LanMsgEliminate *m);
size_t lan_enc_leave(uint8_t *out, size_t cap, const LanMsgLeave *m);
size_t lan_enc_rel_ack(uint8_t *out, size_t cap, const LanMsgRelAck *m);

bool lan_dec_session_advert(const uint8_t *buf, size_t len,
		LanMsgSessionAdvert *m);
bool lan_dec_hello(const uint8_t *buf, size_t len, LanMsgHello *m);
bool lan_dec_roster_snapshot(const uint8_t *buf, size_t len,
		LanMsgRosterSnapshot *m);
bool lan_dec_appearance(const uint8_t *buf, size_t len, LanMsgAppearance *m);
bool lan_dec_lobby_ready(const uint8_t *buf, size_t len, LanMsgLobbyReady *m);
bool lan_dec_lobby_phase_nonce(const uint8_t *buf, size_t len,
		LanMsgLobbyPhaseNonce *m);
bool lan_dec_join_nonce(const uint8_t *buf, size_t len, LanMsgJoinNonce *m);
bool lan_dec_countdown_anchor(const uint8_t *buf, size_t len,
		LanMsgCountdownAnchor *m);
bool lan_dec_sync_keys(const uint8_t *buf, size_t len, LanMsgSyncKeys *m);
bool lan_dec_control_edge(const uint8_t *buf, size_t len, LanMsgControlEdge *m);
bool lan_dec_score_telemetry(const uint8_t *buf, size_t len,
		LanMsgScoreTelemetry *m);
bool lan_dec_eliminate(const uint8_t *buf, size_t len, LanMsgEliminate *m);
bool lan_dec_leave(const uint8_t *buf, size_t len, LanMsgLeave *m);
bool lan_dec_rel_ack(const uint8_t *buf, size_t len, LanMsgRelAck *m);

size_t lan_enc_party_ack(uint8_t *out, size_t cap, const LanMsgPartyAck *m);
bool lan_dec_party_ack(const uint8_t *buf, size_t len, LanMsgPartyAck *m);

/* True if the message type uses reliable delivery (payload begins with u16 seq). */
bool lan_msg_is_reliable(LanMsgType t);

#endif /* ICYTOWER_LAN_MSG_H */
