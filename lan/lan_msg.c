#include "lan_msg.h"

#include <string.h>

/* --- CRC32 IEEE (poly 0xEDB88320), table-driven, no init globals required -- */

static uint32_t crc32_table[256];
static int crc32_table_ready = 0;

static void crc32_init_table(void)
{
	uint32_t i, j, c;
	for (i = 0; i < 256; ++i) {
		c = i;
		for (j = 0; j < 8; ++j)
			c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
		crc32_table[i] = c;
	}
	crc32_table_ready = 1;
}

uint32_t lan_crc32(const uint8_t *data, size_t len)
{
	uint32_t c = 0xFFFFFFFFu;
	size_t i;
	if (!crc32_table_ready)
		crc32_init_table();
	for (i = 0; i < len; ++i)
		c = crc32_table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

/* --- LE write/read helpers ----------------------------------------------- */

void lan_w_u16(uint8_t **p, uint16_t v)
{
	(*p)[0] = (uint8_t)(v);
	(*p)[1] = (uint8_t)(v >> 8);
	*p += 2;
}

void lan_w_u32(uint8_t **p, uint32_t v)
{
	(*p)[0] = (uint8_t)(v);
	(*p)[1] = (uint8_t)(v >> 8);
	(*p)[2] = (uint8_t)(v >> 16);
	(*p)[3] = (uint8_t)(v >> 24);
	*p += 4;
}

void lan_w_u64(uint8_t **p, uint64_t v)
{
	int i;
	for (i = 0; i < 8; ++i)
		(*p)[i] = (uint8_t)(v >> (i * 8));
	*p += 8;
}

void lan_w_bytes(uint8_t **p, const uint8_t *bytes, size_t n)
{
	if (n)
		memcpy(*p, bytes, n);
	*p += n;
}

uint16_t lan_r_u16(const uint8_t **p, const uint8_t *end, bool *ok)
{
	uint16_t v;
	if (*p + 2 > end) { *ok = false; return 0; }
	v = (uint16_t)((*p)[0] | ((uint16_t)(*p)[1] << 8));
	*p += 2;
	return v;
}

uint32_t lan_r_u32(const uint8_t **p, const uint8_t *end, bool *ok)
{
	uint32_t v;
	if (*p + 4 > end) { *ok = false; return 0; }
	v =  (uint32_t)(*p)[0]
	   | ((uint32_t)(*p)[1] << 8)
	   | ((uint32_t)(*p)[2] << 16)
	   | ((uint32_t)(*p)[3] << 24);
	*p += 4;
	return v;
}

uint64_t lan_r_u64(const uint8_t **p, const uint8_t *end, bool *ok)
{
	uint64_t v = 0;
	int i;
	if (*p + 8 > end) { *ok = false; return 0; }
	for (i = 0; i < 8; ++i)
		v |= (uint64_t)(*p)[i] << (i * 8);
	*p += 8;
	return v;
}

void lan_r_bytes(const uint8_t **p, const uint8_t *end, uint8_t *out, size_t n,
		bool *ok)
{
	if (*p + n > end) { *ok = false; return; }
	if (n)
		memcpy(out, *p, n);
	*p += n;
}

/* --- Frame encode/decode ------------------------------------------------- */

size_t lan_frame_encode(uint8_t *out, size_t out_cap, LanMsgType msg_type,
		const uint8_t *payload, size_t payload_len)
{
	uint8_t *p;
	uint32_t crc;
	size_t total;

	if (payload_len > LAN_FRAME_MAX_PAYLOAD)
		return 0;
	total = LAN_FRAME_HEADER_LEN + payload_len + LAN_FRAME_CRC_LEN;
	if (total > out_cap)
		return 0;
	p = out;
	*p++ = LAN_FRAME_MAGIC0;
	*p++ = LAN_FRAME_MAGIC1;
	*p++ = LAN_FRAME_MAGIC2;
	*p++ = LAN_FRAME_MAGIC3;
	*p++ = (uint8_t)LAN_SPEC_VERSION;
	*p++ = (uint8_t)msg_type;
	lan_w_u16(&p, (uint16_t)payload_len);
	if (payload_len)
		memcpy(p, payload, payload_len);
	p += payload_len;
	crc = lan_crc32(out, LAN_FRAME_HEADER_LEN + payload_len);
	lan_w_u32(&p, crc);
	return total;
}

bool lan_frame_decode(const uint8_t *buf, size_t buf_len, LanMsgType *out_type,
		const uint8_t **out_payload, size_t *out_payload_len)
{
	uint16_t payload_len;
	uint32_t crc_actual, crc_expected;
	const uint8_t *p = buf;

	if (buf_len < (size_t)(LAN_FRAME_HEADER_LEN + LAN_FRAME_CRC_LEN))
		return false;
	if (buf[0] != LAN_FRAME_MAGIC0 || buf[1] != LAN_FRAME_MAGIC1
			|| buf[2] != LAN_FRAME_MAGIC2 || buf[3] != LAN_FRAME_MAGIC3)
		return false;
	if (buf[4] != LAN_SPEC_VERSION)
		return false;
	*out_type = (LanMsgType)buf[5];
	payload_len = (uint16_t)(buf[6] | ((uint16_t)buf[7] << 8));
	if ((size_t)payload_len + LAN_FRAME_HEADER_LEN + LAN_FRAME_CRC_LEN
			!= buf_len)
		return false;
	crc_expected =  (uint32_t)buf[LAN_FRAME_HEADER_LEN + payload_len]
	             | ((uint32_t)buf[LAN_FRAME_HEADER_LEN + payload_len + 1] << 8)
	             | ((uint32_t)buf[LAN_FRAME_HEADER_LEN + payload_len + 2] << 16)
	             | ((uint32_t)buf[LAN_FRAME_HEADER_LEN + payload_len + 3] << 24);
	crc_actual = lan_crc32(buf, LAN_FRAME_HEADER_LEN + payload_len);
	if (crc_expected != crc_actual)
		return false;
	*out_payload = p + LAN_FRAME_HEADER_LEN;
	*out_payload_len = payload_len;
	return true;
}

bool lan_msg_is_reliable(LanMsgType t)
{
	switch (t) {
	case LAN_MSG_HELLO:
	case LAN_MSG_ROSTER_SNAPSHOT:
	case LAN_MSG_APPEARANCE:
	case LAN_MSG_LOBBY_READY:
	case LAN_MSG_LOBBY_PHASE_NONCE:
	case LAN_MSG_JOIN_NONCE:
	case LAN_MSG_COUNTDOWN_ANCHOR:
	case LAN_MSG_ELIMINATE:
	case LAN_MSG_LEAVE:
		return true;
	default:
		return false;
	}
}

/* --- Helpers for fixed-length strings ------------------------------------ */

static void put_fixed_string(uint8_t **p, const char *s, size_t cap)
{
	size_t i, n = 0;
	if (s)
		while (s[n] && n < cap) ++n;
	for (i = 0; i < n; ++i)
		(*p)[i] = (uint8_t)s[i];
	for (i = n; i < cap; ++i)
		(*p)[i] = 0;
	*p += cap;
}

static void get_fixed_string(const uint8_t **p, const uint8_t *end, char *out,
		size_t cap, bool *ok)
{
	size_t i;
	if (*p + cap > end) { *ok = false; return; }
	for (i = 0; i < cap; ++i)
		out[i] = (char)(*p)[i];
	*p += cap;
	out[cap - 1] = '\0';
}

/* --- Encoders ------------------------------------------------------------ */

size_t lan_enc_session_advert(uint8_t *out, size_t cap,
		const LanMsgSessionAdvert *m)
{
	uint8_t *p = out;
	if (cap < 8u + 2u + 1u + LAN_ROOM_LEN + 2u) return 0;
	lan_w_u64(&p, m->session_id);
	lan_w_u16(&p, m->spec_version);
	lan_w_u8 (&p, m->peer_hint);
	put_fixed_string(&p, m->room_name, LAN_ROOM_LEN);
	lan_w_u16(&p, m->port);
	return (size_t)(p - out);
}

bool lan_dec_session_advert(const uint8_t *buf, size_t len,
		LanMsgSessionAdvert *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->session_id   = lan_r_u64(&p, end, &ok);
	m->spec_version = lan_r_u16(&p, end, &ok);
	m->peer_hint    = lan_r_u8 (&p, end);
	get_fixed_string(&p, end, m->room_name, LAN_ROOM_LEN, &ok);
	m->port         = lan_r_u16(&p, end, &ok);
	return ok;
}

size_t lan_enc_hello(uint8_t *out, size_t cap, const LanMsgHello *m)
{
	uint8_t *p = out;
	if (cap < 2u + 8u + 2u + LAN_UUID_BYTES + 1u + LAN_NAME_LEN + 4u) return 0;
	lan_w_u16(&p, m->rel_seq);
	lan_w_u64(&p, m->session_id);
	lan_w_u16(&p, m->spec_version);
	lan_w_bytes(&p, m->uuid.b, LAN_UUID_BYTES);
	lan_w_u8(&p, m->skin_id);
	put_fixed_string(&p, m->name, LAN_NAME_LEN);
	lan_w_u32(&p, m->feature_flags);
	return (size_t)(p - out);
}

bool lan_dec_hello(const uint8_t *buf, size_t len, LanMsgHello *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->rel_seq      = lan_r_u16(&p, end, &ok);
	m->session_id   = lan_r_u64(&p, end, &ok);
	m->spec_version = lan_r_u16(&p, end, &ok);
	lan_r_bytes(&p, end, m->uuid.b, LAN_UUID_BYTES, &ok);
	m->skin_id      = lan_r_u8 (&p, end);
	get_fixed_string(&p, end, m->name, LAN_NAME_LEN, &ok);
	m->feature_flags = lan_r_u32(&p, end, &ok);
	return ok;
}

size_t lan_enc_roster_snapshot(uint8_t *out, size_t cap,
		const LanMsgRosterSnapshot *m)
{
	uint8_t *p = out;
	size_t i;
	if (m->count > LAN_MAX_PEERS) return 0;
	if (cap < 2u + 8u + 1u + (size_t)m->count * LAN_UUID_BYTES + 2u) return 0;
	lan_w_u16(&p, m->rel_seq);
	lan_w_u64(&p, m->session_id);
	lan_w_u8 (&p, m->count);
	for (i = 0; i < m->count; ++i)
		lan_w_bytes(&p, m->uuids[i].b, LAN_UUID_BYTES);
	lan_w_u16(&p, m->spec_version);
	return (size_t)(p - out);
}

bool lan_dec_roster_snapshot(const uint8_t *buf, size_t len,
		LanMsgRosterSnapshot *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	uint8_t i;
	m->rel_seq    = lan_r_u16(&p, end, &ok);
	m->session_id = lan_r_u64(&p, end, &ok);
	m->count      = lan_r_u8 (&p, end);
	if (m->count > LAN_MAX_PEERS) return false;
	for (i = 0; i < m->count; ++i)
		lan_r_bytes(&p, end, m->uuids[i].b, LAN_UUID_BYTES, &ok);
	m->spec_version = lan_r_u16(&p, end, &ok);
	return ok;
}

size_t lan_enc_appearance(uint8_t *out, size_t cap, const LanMsgAppearance *m)
{
	uint8_t *p = out;
	if (cap < 2u + 1u + 1u + LAN_NAME_LEN) return 0;
	lan_w_u16(&p, m->rel_seq);
	lan_w_u8 (&p, m->player_id);
	lan_w_u8 (&p, m->skin_id);
	put_fixed_string(&p, m->name, LAN_NAME_LEN);
	return (size_t)(p - out);
}

bool lan_dec_appearance(const uint8_t *buf, size_t len, LanMsgAppearance *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->rel_seq   = lan_r_u16(&p, end, &ok);
	m->player_id = lan_r_u8 (&p, end);
	m->skin_id   = lan_r_u8 (&p, end);
	get_fixed_string(&p, end, m->name, LAN_NAME_LEN, &ok);
	return ok;
}

size_t lan_enc_lobby_ready(uint8_t *out, size_t cap, const LanMsgLobbyReady *m)
{
	uint8_t *p = out;
	if (cap < 2u + 1u + 1u) return 0;
	lan_w_u16(&p, m->rel_seq);
	lan_w_u8 (&p, m->player_id);
	lan_w_u8 (&p, m->ready);
	return (size_t)(p - out);
}

bool lan_dec_lobby_ready(const uint8_t *buf, size_t len, LanMsgLobbyReady *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->rel_seq   = lan_r_u16(&p, end, &ok);
	m->player_id = lan_r_u8 (&p, end);
	m->ready     = lan_r_u8 (&p, end);
	return ok;
}

size_t lan_enc_lobby_phase_nonce(uint8_t *out, size_t cap,
		const LanMsgLobbyPhaseNonce *m)
{
	uint8_t *p = out;
	if (cap < 2u + 4u) return 0;
	lan_w_u16(&p, m->rel_seq);
	lan_w_u32(&p, m->round_id);
	return (size_t)(p - out);
}

bool lan_dec_lobby_phase_nonce(const uint8_t *buf, size_t len,
		LanMsgLobbyPhaseNonce *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->rel_seq  = lan_r_u16(&p, end, &ok);
	m->round_id = lan_r_u32(&p, end, &ok);
	return ok;
}

size_t lan_enc_join_nonce(uint8_t *out, size_t cap, const LanMsgJoinNonce *m)
{
	uint8_t *p = out;
	if (cap < 2u + 1u + 4u + 4u) return 0;
	lan_w_u16(&p, m->rel_seq);
	lan_w_u8 (&p, m->player_id);
	lan_w_u32(&p, m->round_id);
	lan_w_u32(&p, m->nonce);
	return (size_t)(p - out);
}

bool lan_dec_join_nonce(const uint8_t *buf, size_t len, LanMsgJoinNonce *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->rel_seq   = lan_r_u16(&p, end, &ok);
	m->player_id = lan_r_u8 (&p, end);
	m->round_id  = lan_r_u32(&p, end, &ok);
	m->nonce     = lan_r_u32(&p, end, &ok);
	return ok;
}

size_t lan_enc_countdown_anchor(uint8_t *out, size_t cap,
		const LanMsgCountdownAnchor *m)
{
	uint8_t *p = out;
	if (cap < 2u + 1u + 4u + 8u) return 0;
	lan_w_u16(&p, m->rel_seq);
	lan_w_u8 (&p, m->player_id);
	lan_w_u32(&p, m->round_id);
	lan_w_u64(&p, m->propose_ms);
	return (size_t)(p - out);
}

bool lan_dec_countdown_anchor(const uint8_t *buf, size_t len,
		LanMsgCountdownAnchor *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->rel_seq    = lan_r_u16(&p, end, &ok);
	m->player_id  = lan_r_u8 (&p, end);
	m->round_id   = lan_r_u32(&p, end, &ok);
	m->propose_ms = lan_r_u64(&p, end, &ok);
	return ok;
}

size_t lan_enc_sync_keys(uint8_t *out, size_t cap, const LanMsgSyncKeys *m)
{
	uint8_t *p = out;
	if (cap < 1u + 4u + 1u) return 0;
	lan_w_u8 (&p, m->player_id);
	lan_w_u32(&p, m->seq);
	lan_w_u8 (&p, m->keybits);
	return (size_t)(p - out);
}

bool lan_dec_sync_keys(const uint8_t *buf, size_t len, LanMsgSyncKeys *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->player_id = lan_r_u8 (&p, end);
	m->seq       = lan_r_u32(&p, end, &ok);
	m->keybits   = lan_r_u8 (&p, end);
	return ok;
}

size_t lan_enc_control_edge(uint8_t *out, size_t cap, const LanMsgControlEdge *m)
{
	uint8_t *p = out;
	if (cap < 1u + 4u + 1u + 8u) return 0;
	lan_w_u8 (&p, m->player_id);
	lan_w_u32(&p, m->seq);
	lan_w_u8 (&p, m->keybits_after);
	lan_w_u64(&p, m->timestamp_ms);
	return (size_t)(p - out);
}

bool lan_dec_control_edge(const uint8_t *buf, size_t len, LanMsgControlEdge *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->player_id     = lan_r_u8 (&p, end);
	m->seq           = lan_r_u32(&p, end, &ok);
	m->keybits_after = lan_r_u8 (&p, end);
	m->timestamp_ms  = lan_r_u64(&p, end, &ok);
	return ok;
}

size_t lan_enc_score_telemetry(uint8_t *out, size_t cap,
		const LanMsgScoreTelemetry *m)
{
	uint8_t *p = out;
	if (cap < 1u + 4u + 4u + 4u) return 0;
	lan_w_u8 (&p, m->player_id);
	lan_w_u32(&p, m->seq_t);
	lan_w_u32(&p, (uint32_t)m->floor);
	lan_w_u32(&p, m->score);
	return (size_t)(p - out);
}

bool lan_dec_score_telemetry(const uint8_t *buf, size_t len,
		LanMsgScoreTelemetry *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->player_id = lan_r_u8 (&p, end);
	m->seq_t     = lan_r_u32(&p, end, &ok);
	m->floor     = (int32_t)lan_r_u32(&p, end, &ok);
	m->score     = lan_r_u32(&p, end, &ok);
	return ok;
}

size_t lan_enc_eliminate(uint8_t *out, size_t cap, const LanMsgEliminate *m)
{
	uint8_t *p = out;
	if (cap < 2u + 1u + 4u + 1u) return 0;
	lan_w_u16(&p, m->rel_seq);
	lan_w_u8 (&p, m->player_id);
	lan_w_u32(&p, m->round_id);
	lan_w_u8 (&p, m->reason);
	return (size_t)(p - out);
}

bool lan_dec_eliminate(const uint8_t *buf, size_t len, LanMsgEliminate *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->rel_seq   = lan_r_u16(&p, end, &ok);
	m->player_id = lan_r_u8 (&p, end);
	m->round_id  = lan_r_u32(&p, end, &ok);
	m->reason    = lan_r_u8 (&p, end);
	return ok;
}

size_t lan_enc_leave(uint8_t *out, size_t cap, const LanMsgLeave *m)
{
	uint8_t *p = out;
	if (cap < 2u + 1u) return 0;
	lan_w_u16(&p, m->rel_seq);
	lan_w_u8 (&p, m->player_id);
	return (size_t)(p - out);
}

bool lan_dec_leave(const uint8_t *buf, size_t len, LanMsgLeave *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->rel_seq   = lan_r_u16(&p, end, &ok);
	m->player_id = lan_r_u8 (&p, end);
	return ok;
}

size_t lan_enc_rel_ack(uint8_t *out, size_t cap, const LanMsgRelAck *m)
{
	uint8_t *p = out;
	if (cap < 2u) return 0;
	lan_w_u16(&p, m->acked_seq);
	return (size_t)(p - out);
}

bool lan_dec_rel_ack(const uint8_t *buf, size_t len, LanMsgRelAck *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->acked_seq = lan_r_u16(&p, end, &ok);
	return ok;
}

size_t lan_enc_party_ack(uint8_t *out, size_t cap, const LanMsgPartyAck *m)
{
	uint8_t *p = out;
	if (cap < 8u + 2u + LAN_UUID_BYTES)
		return 0;
	lan_w_u64(&p, m->session_id);
	lan_w_u16(&p, m->spec_version);
	lan_w_bytes(&p, m->uuid.b, LAN_UUID_BYTES);
	return (size_t)(p - out);
}

bool lan_dec_party_ack(const uint8_t *buf, size_t len, LanMsgPartyAck *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->session_id   = lan_r_u64(&p, end, &ok);
	m->spec_version = lan_r_u16(&p, end, &ok);
	lan_r_bytes(&p, end, m->uuid.b, LAN_UUID_BYTES, &ok);
	return ok;
}
