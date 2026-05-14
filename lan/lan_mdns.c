#include "lan_mdns.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lan_internal.h"

#ifdef ICYTOWER_HAVE_MDNS

#include <arpa/inet.h>
#include <dns_sd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>

struct resolve_ctx {
	char service_name[64];
};

#define MDNS_MAX_RESOLVE 12

typedef struct {
	DNSServiceRef ref;
} RSlot;

static DNSServiceRef g_browse;
static DNSServiceRef g_reg;
static RSlot         g_rl[MDNS_MAX_RESOLVE];

static lan_mdns_browse_cb g_cb;
static void              *g_ctx;

static void browse_reply(DNSServiceRef sdRef, DNSServiceFlags flags,
		uint32_t interface_index, DNSServiceErrorType err,
		const char *service_name, const char *regtype,
		const char *reply_domain, void *context);

static void resolve_reply(DNSServiceRef sdRef, DNSServiceFlags flags,
		uint32_t interface_index, DNSServiceErrorType err,
		const char *full_name, const char *host_target, uint16_t port,
		uint16_t txt_len, const unsigned char *txt, void *context);

static void store_resolve_ref(DNSServiceRef ref)
{
	int i;

	if (!ref)
		return;
	for (i = 0; i < MDNS_MAX_RESOLVE; ++i) {
		if (g_rl[i].ref == 0) {
			g_rl[i].ref = ref;
			return;
		}
	}
	DNSServiceRefDeallocate(ref);
}

static void forget_resolve_ref(DNSServiceRef ref)
{
	int i;

	if (!ref)
		return;
	for (i = 0; i < MDNS_MAX_RESOLVE; ++i) {
		if (g_rl[i].ref == ref) {
			g_rl[i].ref = 0;
			return;
		}
	}
}

static void clear_resolve_refs(void)
{
	int i;

	for (i = 0; i < MDNS_MAX_RESOLVE; ++i) {
		if (g_rl[i].ref) {
			DNSServiceRefDeallocate(g_rl[i].ref);
			g_rl[i].ref = 0;
		}
	}
}

static bool txt_get_u64(const unsigned char *txt, uint16_t txt_len,
		const char *key, uint64_t *out)
{
	const unsigned char *p = txt;
	const unsigned char *end = txt + txt_len;
	size_t kl = strlen(key);

	while (p < end) {
		unsigned seg = *p++;

		if (seg == 0 || p + seg > end)
			break;
		if (seg > kl && memcmp(p, key, kl) == 0 && p[kl] == '=') {
			const char *s = (const char *)p + kl + 1;
			unsigned vl = seg - (unsigned)(kl + 1);
			char buf[32];
			unsigned long long v;

			if (vl >= sizeof buf)
				return false;
			memcpy(buf, s, vl);
			buf[vl] = '\0';
			v = strtoull(buf, NULL, 0);
			*out = (uint64_t)v;
			return true;
		}
		p += seg;
	}
	return false;
}

static bool txt_get_u16(const unsigned char *txt, uint16_t txt_len,
		const char *key, uint16_t *out)
{
	uint64_t v;

	if (!txt_get_u64(txt, txt_len, key, &v) || v > 65535)
		return false;
	*out = (uint16_t)v;
	return true;
}

static bool txt_get_room(const unsigned char *txt, uint16_t txt_len,
		char *out, size_t out_cap)
{
	const unsigned char *p = txt;
	const unsigned char *end = txt + txt_len;
	const char *key = "room";
	size_t kl = strlen(key);

	while (p < end) {
		unsigned seg = *p++;

		if (seg == 0 || p + seg > end)
			break;
		if (seg > kl && memcmp(p, key, kl) == 0 && p[kl] == '=') {
			const char *s = (const char *)p + kl + 1;
			size_t vl = seg - (kl + 1);

			if (vl >= out_cap)
				vl = out_cap - 1;
			memcpy(out, s, vl);
			out[vl] = '\0';
			return true;
		}
		p += seg;
	}
	return false;
}

static int txt_append(uint8_t *buf, int pos, int cap, const char *kv)
{
	size_t n = strlen(kv);

	if (n > 255 || pos + 1 + (int)n > cap)
		return pos;
	buf[pos++] = (uint8_t)n;
	memcpy(buf + pos, kv, n);
	return pos + (int)n;
}

static int append_cre_txt(uint8_t *out, int pos, int cap, const LanUuid *u)
{
	char kv[8 + LAN_UUID_BYTES * 2 + 8];
	int i, p;

	if (!u)
		return pos;
	p = snprintf(kv, sizeof kv, "cre=");
	if (p < 0 || p >= (int)sizeof kv - (int)LAN_UUID_BYTES * 2)
		return pos;
	for (i = 0; i < LAN_UUID_BYTES; ++i) {
		int j = snprintf(kv + 4 + i * 2, sizeof kv - (size_t)(4 + i * 2),
				"%02x", u->b[i]);
		if (j != 2)
			return pos;
	}
	kv[4 + LAN_UUID_BYTES * 2] = '\0';
	return txt_append(out, pos, cap, kv);
}

static bool txt_get_uuid_hex(const unsigned char *txt, uint16_t txt_len,
		const char *key, LanUuid *out)
{
	const unsigned char *p = txt;
	const unsigned char *end = txt + txt_len;
	size_t kl = strlen(key);

	while (p < end) {
		unsigned seg = *p++;

		if (seg == 0 || p + seg > end)
			break;
		if (seg > kl && memcmp(p, key, kl) == 0 && p[kl] == '=') {
			const char *s = (const char *)p + kl + 1;
			unsigned vl = seg - (unsigned)(kl + 1);
			unsigned i;

			if (vl != LAN_UUID_BYTES * 2)
				return false;
			memset(out, 0, sizeof *out);
			for (i = 0; i < LAN_UUID_BYTES; ++i) {
				char hx[3];
				unsigned v;

				hx[0] = s[i * 2];
				hx[1] = s[i * 2 + 1];
				hx[2] = '\0';
				if (!isxdigit((unsigned char)hx[0])
						|| !isxdigit((unsigned char)hx[1]))
					return false;
				v = (unsigned)strtoul(hx, NULL, 16);
				out->b[i] = (uint8_t)v;
			}
			return true;
		}
		p += seg;
	}
	return false;
}

static int build_txt(uint8_t *out, int cap, uint64_t room_id, const char *room,
		uint16_t dport, const LanUuid *creator)
{
	char kv[LAN_ROOM_LEN + 48];
	int p = 0;

	snprintf(kv, sizeof kv, "sid=%llu",
			(unsigned long long)room_id);
	p = txt_append(out, p, cap, kv);
	snprintf(kv, sizeof kv, "room=%s", room);
	p = txt_append(out, p, cap, kv);
	snprintf(kv, sizeof kv, "d=%u", (unsigned)dport);
	p = txt_append(out, p, cap, kv);
	p = append_cre_txt(out, p, cap, creator);
	return p;
}

static void browse_reply(DNSServiceRef sdRef, DNSServiceFlags flags,
		uint32_t interface_index, DNSServiceErrorType err,
		const char *service_name, const char *regtype,
		const char *reply_domain, void *context)
{
	DNSServiceErrorType e;
	DNSServiceRef res = NULL;
	struct resolve_ctx *rctx;

	(void)sdRef;
	(void)context;

	if (err != kDNSServiceErr_NoError)
		return;

	if (!(flags & kDNSServiceFlagsAdd)) {
		if (g_cb)
			g_cb(g_ctx, false, service_name, 0, NULL, 0, 0, 0, NULL);
		return;
	}

	rctx = (struct resolve_ctx *)calloc(1, sizeof *rctx);
	if (!rctx)
		return;
	strncpy(rctx->service_name, service_name,
			sizeof rctx->service_name - 1);

	e = DNSServiceResolve(&res, 0, interface_index, service_name, regtype,
			reply_domain, resolve_reply, rctx);
	if (e != kDNSServiceErr_NoError) {
		free(rctx);
		return;
	}
	store_resolve_ref(res);
}

static void resolve_reply(DNSServiceRef sdRef, DNSServiceFlags flags,
		uint32_t interface_index, DNSServiceErrorType err,
		const char *full_name, const char *host_target, uint16_t port,
		uint16_t txt_len, const unsigned char *txt, void *context)
{
	uint64_t room_id = 0;
	uint16_t dport = LAN_DEFAULT_PORT;
	char room[LAN_ROOM_LEN];
	LanUuid cre;
	struct addrinfo hints, *ai = NULL;
	uint32_t ipv4 = 0;
	struct resolve_ctx *rctx = context;
	const char *inst;

	(void)flags;
	(void)interface_index;

	memset(&cre, 0, sizeof cre);

	if (err != kDNSServiceErr_NoError || !host_target || !g_cb) {
		goto done;
	}

	if (!txt_get_u64(txt, txt_len, "sid", &room_id)) {
		goto done;
	}
	(void)txt_get_uuid_hex(txt, txt_len, "cre", &cre);
	if (!txt_get_room(txt, txt_len, room, sizeof room)) {
		room[0] = '\0';
	}
	(void)txt_get_u16(txt, txt_len, "d", &dport);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo(host_target, NULL, &hints, &ai) == 0 && ai) {
		struct sockaddr_in *sin = (struct sockaddr_in *)ai->ai_addr;

		ipv4 = ntohl(sin->sin_addr.s_addr);
		freeaddrinfo(ai);
		ai = NULL;
	}

	inst = (rctx && rctx->service_name[0]) ? rctx->service_name
			: (full_name ? full_name : host_target);

	if (ipv4 != 0)
		g_cb(g_ctx, true, inst, room_id, room, ntohs(port), ipv4, dport,
				&cre);

done:
	if (rctx)
		free(rctx);
	forget_resolve_ref(sdRef);
	DNSServiceRefDeallocate(sdRef);
}

static bool pump_ref(DNSServiceRef ref)
{
	int fd;
	struct pollfd p;

	if (!ref)
		return false;
	fd = DNSServiceRefSockFD(ref);
	if (fd < 0)
		return false;
	p.fd = fd;
	p.events = POLLIN;
	p.revents = 0;
	if (poll(&p, 1, 0) != 1 || !(p.revents & POLLIN))
		return false;
	return DNSServiceProcessResult(ref) == kDNSServiceErr_NoError;
}

bool lan_mdns_browse_start(void *ctx, lan_mdns_browse_cb cb)
{
	DNSServiceErrorType e;

	lan_mdns_browse_stop();
	g_ctx = ctx;
	g_cb = cb;

	e = DNSServiceBrowse(&g_browse, 0, kDNSServiceInterfaceIndexAny,
			LAN_MDNS_REGTYPE, NULL, browse_reply, NULL);
	return e == kDNSServiceErr_NoError && g_browse != 0;
}

void lan_mdns_browse_stop(void)
{
	g_cb = NULL;
	g_ctx = NULL;
	clear_resolve_refs();
	if (g_browse) {
		DNSServiceRefDeallocate(g_browse);
		g_browse = 0;
	}
}

bool lan_mdns_register(const char *room, uint64_t room_id, uint16_t game_port,
		uint16_t discovery_port, const LanUuid *creator_uuid)
{
	uint8_t txt[256];
	int tln;
	char name_label[64];
	DNSServiceErrorType e;

	lan_mdns_unregister();
	if (!room)
		room = "";

	memset(txt, 0, sizeof txt);
	tln = build_txt(txt, sizeof txt, room_id, room, discovery_port,
			creator_uuid);

	strncpy(name_label, room, sizeof name_label - 1);
	name_label[sizeof name_label - 1] = '\0';

	e = DNSServiceRegister(&g_reg, 0, kDNSServiceInterfaceIndexAny,
			name_label[0] ? name_label : NULL, LAN_MDNS_REGTYPE, NULL,
			NULL, htons(game_port), (uint16_t)tln, txt,
			NULL, NULL);
	return e == kDNSServiceErr_NoError && g_reg != 0;
}

void lan_mdns_unregister(void)
{
	if (g_reg) {
		DNSServiceRefDeallocate(g_reg);
		g_reg = 0;
	}
}

void lan_mdns_poll(void)
{
	int n;

	/* Drain each ref until idle. */
	for (n = 0; n < 3; ++n) {
		bool any = false;
		int i;

		any |= pump_ref(g_browse);
		any |= pump_ref(g_reg);
		for (i = 0; i < MDNS_MAX_RESOLVE; ++i)
			any |= pump_ref(g_rl[i].ref);
		if (!any)
			break;
	}
}

#else /* !ICYTOWER_HAVE_MDNS */

bool lan_mdns_browse_start(void *ctx, lan_mdns_browse_cb cb)
{
	(void)ctx;
	(void)cb;
	return false;
}

void lan_mdns_browse_stop(void)
{
}

bool lan_mdns_register(const char *room, uint64_t room_id, uint16_t game_port,
		uint16_t discovery_port, const LanUuid *creator_uuid)
{
	(void)room;
	(void)room_id;
	(void)game_port;
	(void)discovery_port;
	(void)creator_uuid;
	return false;
}

void lan_mdns_unregister(void)
{
}

void lan_mdns_poll(void)
{
}

#endif /* ICYTOWER_HAVE_MDNS */
