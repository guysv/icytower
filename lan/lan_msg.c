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
	if (cap < 8u + 2u + LAN_ROOM_LEN + 2u)
		return 0;
	lan_w_u64(&p, m->room_id);
	lan_w_u16(&p, m->spec_version);
	put_fixed_string(&p, m->room_name, LAN_ROOM_LEN);
	lan_w_u16(&p, m->port);
	return (size_t)(p - out);
}

bool lan_dec_session_advert(const uint8_t *buf, size_t len,
		LanMsgSessionAdvert *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->room_id      = lan_r_u64(&p, end, &ok);
	m->spec_version = lan_r_u16(&p, end, &ok);
	get_fixed_string(&p, end, m->room_name, LAN_ROOM_LEN, &ok);
	m->port = lan_r_u16(&p, end, &ok);
	return ok && p == end;
}

size_t lan_enc_hello(uint8_t *out, size_t cap, const LanMsgHello *m)
{
	uint8_t *p = out;
	if (cap < 2u + 8u + 2u + LAN_UUID_BYTES + 1u + LAN_NAME_LEN + 4u) return 0;
	lan_w_u16(&p, m->rel_seq);
	lan_w_u64(&p, m->room_id);
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
	m->room_id      = lan_r_u64(&p, end, &ok);
	m->spec_version = lan_r_u16(&p, end, &ok);
	lan_r_bytes(&p, end, m->uuid.b, LAN_UUID_BYTES, &ok);
	m->skin_id      = lan_r_u8 (&p, end);
	get_fixed_string(&p, end, m->name, LAN_NAME_LEN, &ok);
	m->feature_flags = lan_r_u32(&p, end, &ok);
	return ok;
}

size_t lan_enc_goodbye(uint8_t *out, size_t cap, const LanMsgGoodbye *m)
{
	uint8_t *p = out;
	if (cap < 8u + 2u + LAN_UUID_BYTES)
		return 0;
	lan_w_u64(&p, m->room_id);
	lan_w_u16(&p, m->spec_version);
	lan_w_bytes(&p, m->uuid.b, LAN_UUID_BYTES);
	return (size_t)(p - out);
}

bool lan_dec_goodbye(const uint8_t *buf, size_t len, LanMsgGoodbye *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	m->room_id      = lan_r_u64(&p, end, &ok);
	m->spec_version = lan_r_u16(&p, end, &ok);
	lan_r_bytes(&p, end, m->uuid.b, LAN_UUID_BYTES, &ok);
	return ok && p == end;
}

size_t lan_enc_welcome(uint8_t *out, size_t cap, const LanMsgWelcome *m)
{
	uint8_t *p = out;
	size_t need;
	unsigned i;

	if (m->count > LAN_MAX_PEERS)
		return 0;
	need = 2u + 8u + 2u + LAN_UUID_BYTES + 1u
		+ (size_t)m->count * (LAN_UUID_BYTES + 4u + 2u + LAN_NAME_LEN);
	if (cap < need)
		return 0;
	lan_w_u16(&p, m->rel_seq);
	lan_w_u64(&p, m->room_id);
	lan_w_u16(&p, m->spec_version);
	lan_w_bytes(&p, m->sender.b, LAN_UUID_BYTES);
	lan_w_u8(&p, m->count);
	for (i = 0; i < (unsigned)m->count; ++i) {
		lan_w_bytes(&p, m->peer[i].uuid.b, LAN_UUID_BYTES);
		lan_w_u32(&p, m->peer[i].ip);
		lan_w_u16(&p, m->peer[i].udp_port);
		put_fixed_string(&p, m->peer[i].name, LAN_NAME_LEN);
	}
	return (size_t)(p - out);
}

bool lan_dec_welcome(const uint8_t *buf, size_t len, LanMsgWelcome *m)
{
	bool ok = true;
	const uint8_t *p = buf, *end = buf + len;
	unsigned i;

	memset(m, 0, sizeof *m);
	m->rel_seq      = lan_r_u16(&p, end, &ok);
	m->room_id      = lan_r_u64(&p, end, &ok);
	m->spec_version = lan_r_u16(&p, end, &ok);
	lan_r_bytes(&p, end, m->sender.b, LAN_UUID_BYTES, &ok);
	m->count        = lan_r_u8(&p, end);
	if (m->count > LAN_MAX_PEERS) {
		ok = false;
		return false;
	}
	for (i = 0; i < (unsigned)m->count; ++i) {
		lan_r_bytes(&p, end, m->peer[i].uuid.b, LAN_UUID_BYTES, &ok);
		m->peer[i].ip = lan_r_u32(&p, end, &ok);
		m->peer[i].udp_port = lan_r_u16(&p, end, &ok);
		get_fixed_string(&p, end, m->peer[i].name, LAN_NAME_LEN, &ok);
	}
	return ok && p == end;
}
