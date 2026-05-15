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
 * SESSION_ADVERT: room_id, spec_version, room_name, port. Multicast adverts are
 * emitted only by the senior surviving peer — lexicographically smallest peer
 * UUID in the roster (local row counts as X.me).
 *
 * LOBBY_POSE: u32 seq (per sender, monotonic; receivers drop seq <= last seen),
 * room_id, spec_version, sender uuid, then sender kinematics as fixed-point
 * (i32 scaled by LAN_POSE_FIXED_SCALE = 10000): x, y, dx, dy. A trailing u8
 * keys_lr carries bit0=KEY_LEFT, bit1=KEY_RIGHT for cheap extrapolation.
 * u32 client_time_ms (sender wall clock, arbitrary origin; receivers estimate
 * offset on first packet) is used to replay play_lobby_frame forward by stale
 * age before snap/lerp reconcile.
 * Each peer is authoritative for its own avatar; LOBBY_POSE is fan-out
 * unicast over the game socket at LAN_LOBBY_POSE_PERIOD_MS or immediately on
 * keys_lr edges while a peer roams floor 0 with jump disabled.
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
 * Fixed-point scale for LOBBY_POSE kinematics. IT_STATE.{x,y,dx,dy} are doubles
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
	uint32_t client_time_ms;
} LanMsgLobbyPose;

/* --- Payload encoders/decoders ------------------------------------------ */

size_t lan_enc_session_advert(uint8_t *out, size_t cap,
		const LanMsgSessionAdvert *m);
size_t lan_enc_hello(uint8_t *out, size_t cap, const LanMsgHello *m);
size_t lan_enc_goodbye(uint8_t *out, size_t cap, const LanMsgGoodbye *m);
size_t lan_enc_welcome(uint8_t *out, size_t cap, const LanMsgWelcome *m);
size_t lan_enc_lobby_pose(uint8_t *out, size_t cap, const LanMsgLobbyPose *m);

bool lan_dec_session_advert(const uint8_t *buf, size_t len,
		LanMsgSessionAdvert *m);
bool lan_dec_hello(const uint8_t *buf, size_t len, LanMsgHello *m);
bool lan_dec_goodbye(const uint8_t *buf, size_t len, LanMsgGoodbye *m);
bool lan_dec_welcome(const uint8_t *buf, size_t len, LanMsgWelcome *m);
bool lan_dec_lobby_pose(const uint8_t *buf, size_t len, LanMsgLobbyPose *m);

#endif /* ICYTOWER_LAN_MSG_H */
