/*
 * Wire-format encode/decode for LAN messages.
 *
 * Frame layout (header is 8 bytes, all multi-byte fields little-endian):
 *
 *   off  size  field
 *   0    4     magic        = "ITW1"
 *   4    1     spec_version (LAN_SPEC_VERSION, must match peer payloads)
 *   5    1     msg_type     (LanMsgType)
 *   6    2     length       (bytes of payload between header and CRC)
 *   8    L     payload
 *   8+L  4     crc32_le     (IEEE polynomial, computed over header+payload)
 *
 * HELLO: u16 rel_seq (0 = request WELCOME; non-zero = mesh update only),
 * room_id, spec_version, uuid, skin_id, name, feature_flags.
 * Periodic HELLO mesh gossip doubles as lobby liveness / reachability refresh.
 *
 * GOODBYE: room_id, spec_version, uuid.
 *
 * WELCOME: u16 rel_seq (per sender, dedupe), room_id, spec_version, sender uuid,
 * u32 level_seed (host wall time at room creation), u8 peer_count, then
 * peer_count × (uuid, u32 ip host order, u16 udp port host order, name).
 *
 * SESSION_ADVERT: room_id, spec_version, room_name, port. Lobby peers multicast
 * periodically so discovery works from any member; mDNS uses the same payload.
 *
 * READY (LAN_MSG_READY): room_id, spec_version, sender uuid, u32 seq (per sender,
 * increments each commit), u8 ready (0 or 1). Full mesh: sender unicasts to each
 * remote roster peer until every peer sends READY_ACK for this seq (UDP retries).
 *
 * READY_ACK (LAN_MSG_READY_ACK): room_id, spec_version, ack_sender uuid,
 * for_peer uuid (= READY sender echoed), u32 seq. Duplicate READY datagrams
 * still provoke READY_ACK again before duplicate suppression so retries converge.
 *
 * STEADY (LAN_MSG_STEADY): room_id, spec_version, sender uuid (senior survivor),
 * u32 steady_seq (per wave), u32 countdown_ms (remaining until go; shrinks on retries).
 *
 * STEADY_ACK (LAN_MSG_STEADY_ACK): room_id, spec_version, ack_sender uuid,
 * for_peer uuid (= STEADY sender echoed), u32 steady_seq. Duplicate STEADY still
 * provoke STEADY_ACK again before suppression.
 *
 * POSE (LAN_MSG_POSE): u32 seq (per sender, monotonic; receivers drop seq <= last seen),
 * room_id, spec_version, sender uuid, then sender kinematics as fixed-point
 * (i32 scaled by LAN_POSE_FIXED_SCALE = 10000): x, y, dx, dy. Values mirror the
 * sender's IT_STATE tower/world coordinates (same space local physics uses), not a
 * camera-local transform; only x/y/dx/dy use the fixed-point scale. A trailing u8
 * keys_lr carries bit0=KEY_LEFT, bit1=KEY_RIGHT for cheap extrapolation.
 * i32 screen_y (integer game pixels, not scaled) matches IT_STATE.screen_y for gameplay sync.
 * u32 client_time_ms (sender wall clock, arbitrary origin; receivers estimate
 * offset on first packet) is used to replay forward by stale age before
 * snap/lerp reconcile (lobby and gameplay).
 * Each peer is authoritative for their own character; POSE is fan-out unicast
 * over the game socket. In lobby, typical spacing is LAN_POSE_PERIOD_MS,
 * with immediate sends on keys_lr edges while roaming floor 0 with jump disabled.
 *
 * JUMP (LAN_MSG_JUMP): unreliable; room_id, spec_version, sender uuid, u32 jump_seq.
 * DIE (LAN_MSG_DIE): room_id, spec_version, sender uuid, u32 die_seq, snapshot x_fp,
 * y_fp, dx_fp, dy_fp, screen_y, status, keys_lr; mesh DIE_ACK like READY.
 * DIE_ACK (LAN_MSG_DIE_ACK): ack_sender, for_peer, die_seq.
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
	char     room_name[LAN_ROOM_LEN];
	uint16_t port;
} LanMsgSessionAdvert;

typedef struct {
	uint16_t rel_seq; /* 0: request WELCOME; non-zero: mesh gossip only. */
	uint64_t room_id;
	uint16_t spec_version;
	LanUuid  uuid;
	uint8_t  skin_id;
	char     name[LAN_NAME_LEN];
	uint32_t feature_flags;
} LanMsgHello;

typedef struct {
	uint64_t room_id;
	uint16_t spec_version;
	LanUuid  uuid;
} LanMsgGoodbye;

typedef struct {
	LanUuid  uuid;
	uint32_t ip;    /* host order */
	uint16_t udp_port;
	char     name[LAN_NAME_LEN];
} LanMsgRosterEntry;

typedef struct {
	uint16_t rel_seq;
	uint64_t room_id;
	uint16_t spec_version;
	LanUuid  sender;
	uint32_t level_seed;
	uint8_t count;
	LanMsgRosterEntry peer[LAN_MAX_PEERS];
} LanMsgWelcome;

/*
 * Fixed-point scale for POSE (LAN_MSG_POSE) kinematics. IT_STATE.{x,y,dx,dy} are doubles
 * that comfortably fit i32/scale = +/- ~214748 in world units; the play field
 * stays well inside that. Keeping the wire integer keeps the encoder
 * Allegro-free and CRC-stable across host endianness.
 */
#define LAN_POSE_FIXED_SCALE   10000

#define LAN_POSE_KEY_LEFT_BIT  0x01u
#define LAN_POSE_KEY_RIGHT_BIT 0x02u

typedef struct {
	uint32_t seq;
	uint64_t room_id;
	uint16_t spec_version;
	LanUuid  sender;
	int32_t  x_fp;
	int32_t  y_fp;
	int32_t  dx_fp;
	int32_t  dy_fp;
	uint8_t  keys_lr;
	int32_t  screen_y;
	uint32_t client_time_ms;
} LanMsgPose;

typedef struct {
	uint64_t room_id;
	uint16_t spec_version;
	LanUuid  sender;
	uint32_t seq;
	uint8_t  ready;
} LanMsgReady;

typedef struct {
	uint64_t room_id;
	uint16_t spec_version;
	LanUuid  ack_sender;
	LanUuid  for_peer;
	uint32_t seq;
} LanMsgReadyAck;

typedef struct {
	uint64_t room_id;
	uint16_t spec_version;
	LanUuid  sender;
	uint32_t steady_seq;
	uint32_t countdown_ms;
} LanMsgSteady;

typedef struct {
	uint64_t room_id;
	uint16_t spec_version;
	LanUuid  ack_sender;
	LanUuid  for_peer;
	uint32_t steady_seq;
} LanMsgSteadyAck;

typedef struct {
	uint64_t room_id;
	uint16_t spec_version;
	LanUuid  sender;
	uint32_t jump_seq;
} LanMsgJump;

typedef struct {
	uint64_t room_id;
	uint16_t spec_version;
	LanUuid  sender;
	uint32_t die_seq;
	int32_t  x_fp;
	int32_t  y_fp;
	int32_t  dx_fp;
	int32_t  dy_fp;
	int32_t  screen_y;
	uint8_t  status;
	uint8_t  keys_lr;
} LanMsgDie;

typedef struct {
	uint64_t room_id;
	uint16_t spec_version;
	LanUuid  ack_sender;
	LanUuid  for_peer;
	uint32_t die_seq;
} LanMsgDieAck;

/* --- Payload encoders/decoders ------------------------------------------ */

size_t lan_enc_session_advert(uint8_t *out, size_t cap,
		const LanMsgSessionAdvert *m);
size_t lan_enc_hello(uint8_t *out, size_t cap, const LanMsgHello *m);
size_t lan_enc_goodbye(uint8_t *out, size_t cap, const LanMsgGoodbye *m);
size_t lan_enc_welcome(uint8_t *out, size_t cap, const LanMsgWelcome *m);
size_t lan_enc_pose(uint8_t *out, size_t cap, const LanMsgPose *m);
size_t lan_enc_ready(uint8_t *out, size_t cap, const LanMsgReady *m);
size_t lan_enc_ready_ack(uint8_t *out, size_t cap, const LanMsgReadyAck *m);
size_t lan_enc_steady(uint8_t *out, size_t cap, const LanMsgSteady *m);
size_t lan_enc_steady_ack(uint8_t *out, size_t cap, const LanMsgSteadyAck *m);
size_t lan_enc_jump(uint8_t *out, size_t cap, const LanMsgJump *m);
size_t lan_enc_die(uint8_t *out, size_t cap, const LanMsgDie *m);
size_t lan_enc_die_ack(uint8_t *out, size_t cap, const LanMsgDieAck *m);

bool lan_dec_session_advert(const uint8_t *buf, size_t len,
		LanMsgSessionAdvert *m);
bool lan_dec_hello(const uint8_t *buf, size_t len, LanMsgHello *m);
bool lan_dec_goodbye(const uint8_t *buf, size_t len, LanMsgGoodbye *m);
bool lan_dec_welcome(const uint8_t *buf, size_t len, LanMsgWelcome *m);
bool lan_dec_pose(const uint8_t *buf, size_t len, LanMsgPose *m);
bool lan_dec_ready(const uint8_t *buf, size_t len, LanMsgReady *m);
bool lan_dec_ready_ack(const uint8_t *buf, size_t len, LanMsgReadyAck *m);
bool lan_dec_steady(const uint8_t *buf, size_t len, LanMsgSteady *m);
bool lan_dec_steady_ack(const uint8_t *buf, size_t len, LanMsgSteadyAck *m);
bool lan_dec_jump(const uint8_t *buf, size_t len, LanMsgJump *m);
bool lan_dec_die(const uint8_t *buf, size_t len, LanMsgDie *m);
bool lan_dec_die_ack(const uint8_t *buf, size_t len, LanMsgDieAck *m);

#endif /* ICYTOWER_LAN_MSG_H */
