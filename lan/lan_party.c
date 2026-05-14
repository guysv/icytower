/*
 * Phase 1 LAN: SESSION_ADVERT on discovery (multicast+broadcast) carries room_id,
 * room name, and game port. HELLO on the game socket carries the same room_id
 * for roster. Primary (creator) adverts use peer_hint=1 so discovery merges
 * preserve the host address when joiners also multicast SESSION_ADVERT.
 */

#include "lan_party.h"

#include <allegro5/allegro.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gfx.h"
#include "icytower.h"

#include "lan_clock.h"
#include "lan_internal.h"
#include "lan_msg.h"
#include "lan_mdns.h"
#include "lan_net.h"

#define ADV_CAP      24
#define STALE_MS     6200ull

typedef struct {
	bool        ok;
	uint64_t    room_id;
	bool        from_primary_host;
	char        rm[LAN_ROOM_LEN];
	uint64_t    tms;
	LanAddr     src;
	uint16_t    gprt;
	bool        mdns;
	char        mdns_inst[64];
} Adv;

typedef struct {
	bool        ok;
	bool        loc;
	LanUuid     id;
	LanAddr     ua;
	char        nm[LAN_NAME_LEN];
} Row;

typedef struct {
	bool        net;
	int         dfd;
	int         gfd;
	uint16_t    dport_bound;
	uint16_t    gport_bound;

	LanUuid     me;
	char        tag[LAN_NAME_LEN];

	LanPartyPhase ph;

	bool        host_flag;
	uint64_t    room_id;
	char        room[LAN_ROOM_LEN];

	Adv         adv[ADV_CAP];
	int         ord[ADV_CAP];
	int         nord;

	Row         rw[LAN_MAX_PEERS];

	int         cursor;
	uint64_t    wadv;
	uint64_t    wgsp;
	LanAddr     jpeer;
	unsigned    jleft;
	uint64_t    jnext;
} Ctx;

static Ctx X;

extern enum GAME_STATE game_state;

static uint16_t e16(const char *ev, uint16_t def)
{
	const char *s;
	char *z;
	unsigned long v;

	s = getenv(ev);
	if (!s || !*s)
		return def;
	v = strtoul(s, &z, 10);
	if (*z || v == 0 || v > 65535)
		return def;
	return (uint16_t)v;
}

static void uuid_mk(LanUuid *u)
{
	int fd;

	memset(u, 0, sizeof *u);
	fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		if (read(fd, u->b, LAN_UUID_BYTES) == LAN_UUID_BYTES) {
			close(fd);
			return;
		}
		close(fd);
	}
	memset(u->b, 0x6B, LAN_UUID_BYTES);
}

static uint64_t rnd64(void)
{
	uint64_t v = 1;
	int fd = open("/dev/urandom", O_RDONLY);

	if (fd >= 0) {
		if (read(fd, &v, sizeof v) != (ssize_t)sizeof v)
			v = ((uint64_t)rand() << 32) | (unsigned)rand();
		close(fd);
	} else {
		v = ((uint64_t)rand() << 32) | (unsigned)rand();
	}
	return v ? v : 1ull;
}

static void mdns_apply(void *ctx, bool add, const char *instance,
		uint64_t room_id, const char *room, uint16_t game_port,
		uint32_t ipv4, uint16_t dport)
{
	uint64_t now = lan_now_ms();
	int j, k;

	(void)ctx;

	if (!add) {
		for (k = 0; k < ADV_CAP; ++k) {
			if (X.adv[k].ok && X.adv[k].mdns
					&& strcmp(X.adv[k].mdns_inst, instance)
							== 0) {
				X.adv[k].ok = false;
				return;
			}
		}
		return;
	}
	if (ipv4 == 0u || game_port == 0u)
		return;

	j = -1;
	for (k = 0; k < ADV_CAP; ++k) {
		if (X.adv[k].ok && X.adv[k].room_id == room_id) {
			j = k;
			break;
		}
	}
	if (j < 0) {
		for (k = 0; k < ADV_CAP; ++k) {
			if (!X.adv[k].ok) {
				j = k;
				break;
			}
		}
		if (j < 0)
			return;
		memset(&X.adv[j], 0, sizeof X.adv[j]);
	}
	X.adv[j].ok = true;
	X.adv[j].room_id = room_id;
	X.adv[j].from_primary_host = true;
	X.adv[j].tms = now;
	X.adv[j].src.ip = ipv4;
	X.adv[j].src.port = dport;
	X.adv[j].gprt = game_port;
	X.adv[j].mdns = true;
	strncpy(X.adv[j].mdns_inst, instance, sizeof X.adv[j].mdns_inst - 1);
	X.adv[j].mdns_inst[sizeof X.adv[j].mdns_inst - 1] = '\0';
	if (room && room[0]) {
		strncpy(X.adv[j].rm, room, LAN_ROOM_LEN);
		X.adv[j].rm[LAN_ROOM_LEN - 1] = '\0';
	} else {
		strncpy(X.adv[j].rm, "?", LAN_ROOM_LEN);
		X.adv[j].rm[LAN_ROOM_LEN - 1] = '\0';
	}
}

static void net_dn(void)
{
	lan_net_close(X.dfd);
	lan_net_close(X.gfd);
	X.dfd = X.gfd = -1;
	X.net = false;
}

static bool net_up(void)
{
	uint16_t dq = e16(LAN_DISCOVERY_PORT_ENV, LAN_DEFAULT_PORT);
	uint16_t gq =
			getenv(LAN_GAME_PORT_ENV) ? e16(LAN_GAME_PORT_ENV, 0) : 0;
	unsigned char ttl = 1;

	if (X.net)
		return true;
	X.dfd = X.gfd = -1;
	if (!lan_net_open_discovery(dq, &X.dfd, &X.dport_bound)) {
		perror("[lan] discovery bind");
		return false;
	}
	if (setsockopt(X.dfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl)
			!= 0)
		perror("[lan] IP_MULTICAST_TTL");
	if (!lan_net_open_game(gq, &X.gfd, &X.gport_bound)) {
		perror("[lan] game bind");
		lan_net_close(X.dfd);
		X.dfd = -1;
		return false;
	}
	X.net = true;
	return true;
}

static int cmp_browse_ord(const void *a, const void *b)
{
	int ia = *(const int *)a;
	int ib = *(const int *)b;
	uint64_t ra = X.adv[ia].room_id;
	uint64_t rb = X.adv[ib].room_id;

	if (ra < rb)
		return -1;
	if (ra > rb)
		return 1;
	if (X.adv[ia].from_primary_host != X.adv[ib].from_primary_host) {
		if (X.adv[ia].from_primary_host)
			return -1;
		if (X.adv[ib].from_primary_host)
			return 1;
	}
	if (X.adv[ia].tms > X.adv[ib].tms)
		return -1;
	if (X.adv[ia].tms < X.adv[ib].tms)
		return 1;
	return 0;
}

static void refresh_browse(uint64_t now)
{
	int i, w;

	for (i = 0; i < ADV_CAP; ++i) {
		if (X.adv[i].ok && now > X.adv[i].tms + STALE_MS)
			X.adv[i].ok = false;
	}

	X.nord = 0;
	for (i = 0; i < ADV_CAP; ++i) {
		if (X.adv[i].ok)
			X.ord[X.nord++] = i;
	}
	qsort(X.ord, (size_t)X.nord, sizeof X.ord[0], cmp_browse_ord);

	w = 0;
	for (i = 0; i < X.nord; ++i) {
		if (w > 0 && X.adv[X.ord[i]].room_id
				== X.adv[X.ord[w - 1]].room_id)
			continue;
		X.ord[w++] = X.ord[i];
	}
	X.nord = w;

	i = X.nord + 1;

	if (X.cursor >= i)
		X.cursor = i - 1;
	if (X.cursor < 0)
		X.cursor = 0;
}

static void roster_local_only(void)
{
	int i;

	for (i = 0; i < LAN_MAX_PEERS; ++i)
		memset(&X.rw[i], 0, sizeof X.rw[i]);

	X.rw[0].ok = true;
	X.rw[0].loc = true;
	X.rw[0].id = X.me;
	strncpy(X.rw[0].nm, X.tag, LAN_NAME_LEN);
	X.rw[0].nm[LAN_NAME_LEN - 1] = '\0';
	X.rw[0].ua.ip = ntohl(inet_addr("127.0.0.1"));
	X.rw[0].ua.port = X.gport_bound;
}

static void roster_add(LanUuid id, LanAddr ua, const char *nm)
{
	int i;

	if (memcmp(id.b, X.me.b, LAN_UUID_BYTES) == 0)
		return;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc
				&& memcmp(X.rw[i].id.b, id.b, LAN_UUID_BYTES)
						== 0) {
			X.rw[i].ua = ua;
			if (nm && nm[0]) {
				strncpy(X.rw[i].nm, nm, LAN_NAME_LEN);
				X.rw[i].nm[LAN_NAME_LEN - 1] = '\0';
			}
			return;
		}
	}
	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.rw[i].ok) {
			memset(&X.rw[i], 0, sizeof X.rw[i]);
			X.rw[i].ok = true;
			X.rw[i].id = id;
			X.rw[i].ua = ua;
			strncpy(X.rw[i].nm, nm ? nm : "?", LAN_NAME_LEN);
			X.rw[i].nm[LAN_NAME_LEN - 1] = '\0';
			return;
		}
	}
}

static int cmp_row(const void *a, const void *b)
{
	const Row *x = a;
	const Row *y = b;

	if (!x->ok)
		return 1;
	if (!y->ok)
		return -1;
	return memcmp(x->id.b, y->id.b, LAN_UUID_BYTES);
}

static bool tx_raw(int fd, LanMsgType ty, uint8_t *py, size_t pl,
		LanAddr to)
{
	uint8_t f[LAN_FRAME_HEADER_LEN + LAN_FRAME_MAX_PAYLOAD
			+ LAN_FRAME_CRC_LEN];
	size_t n = lan_frame_encode(f, sizeof f, ty, py, pl);

	return n > 0 && lan_net_send(fd, f, n, to);
}

static bool tx_advert(void)
{
	LanMsgSessionAdvert m;
	uint8_t py[128];
	size_t z;
	LanAddr mc;
	LanAddr bc;

	memset(&m, 0, sizeof m);
	m.room_id = X.room_id;
	m.spec_version = LAN_SPEC_VERSION;
	m.peer_hint = X.host_flag ? 1 : 0;
	m.port = X.gport_bound;
	strncpy(m.room_name, X.room, LAN_ROOM_LEN);
	m.room_name[LAN_ROOM_LEN - 1] = '\0';
	z = lan_enc_session_advert(py, sizeof py, &m);
	if (!z)
		return false;
	mc = lan_addr_multicast_peer(X.dport_bound);
	bc = lan_addr_broadcast(X.dport_bound);
	return tx_raw(X.dfd, LAN_MSG_SESSION_ADVERT, py, z, mc)
		&& tx_raw(X.dfd, LAN_MSG_SESSION_ADVERT, py, z, bc);
}

static bool tx_hello(LanAddr to)
{
	LanMsgHello h;
	uint8_t py[256];
	size_t z;

	memset(&h, 0, sizeof h);
	h.rel_seq = 0;
	h.room_id = X.room_id;
	h.spec_version = LAN_SPEC_VERSION;
	h.uuid = X.me;
	h.skin_id = 0;
	strncpy(h.name, X.tag, LAN_NAME_LEN);
	h.name[LAN_NAME_LEN - 1] = '\0';
	h.feature_flags = 0;
	z = lan_enc_hello(py, sizeof py, &h);
	return z && tx_raw(X.gfd, LAN_MSG_HELLO, py, z, to);
}

static void hi_mesh(void)
{
	int i;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc)
			(void)tx_hello(X.rw[i].ua);
	}
}

static void rx_disc(uint64_t now)
{
	uint8_t b[LAN_FRAME_HEADER_LEN + LAN_FRAME_MAX_PAYLOAD
			+ LAN_FRAME_CRC_LEN];

	for (;;) {
		LanMsgType t;
		const uint8_t *py;
		size_t pl;
		LanAddr fr;
		int n = lan_net_recv(X.dfd, b, sizeof b, &fr);

		if (n <= 0)
			break;
		if (!lan_frame_decode(b, (size_t)n, &t, &py, &pl))
			continue;
		if (t != LAN_MSG_SESSION_ADVERT)
			continue;
		{
			LanMsgSessionAdvert m;
			int j, k;
			bool primary;

			if (!lan_dec_session_advert(py, pl, &m))
				continue;
			if (m.spec_version != LAN_SPEC_VERSION)
				continue;

			primary = (m.peer_hint != 0);

			j = -1;
			for (k = 0; k < ADV_CAP; ++k) {
				if (X.adv[k].ok && X.adv[k].room_id == m.room_id) {
					j = k;
					break;
				}
			}
			if (j < 0) {
				for (k = 0; k < ADV_CAP; ++k) {
					if (!X.adv[k].ok) {
						j = k;
						break;
					}
				}
				if (j < 0)
					continue;
				memset(&X.adv[j], 0, sizeof X.adv[j]);
			}
			X.adv[j].ok = true;
			X.adv[j].room_id = m.room_id;
			X.adv[j].tms = now;
			X.adv[j].mdns = false;
			X.adv[j].mdns_inst[0] = '\0';
			strncpy(X.adv[j].rm, m.room_name, LAN_ROOM_LEN);
			X.adv[j].rm[LAN_ROOM_LEN - 1] = '\0';
			if (primary) {
				X.adv[j].from_primary_host = true;
				X.adv[j].src = fr;
				X.adv[j].gprt = m.port;
			} else if (!X.adv[j].from_primary_host) {
				X.adv[j].src = fr;
				X.adv[j].gprt = m.port;
			}
		}
	}
}

static void rx_game(void)
{
	uint8_t b[LAN_FRAME_HEADER_LEN + LAN_FRAME_MAX_PAYLOAD
			+ LAN_FRAME_CRC_LEN];

	if (X.ph != LAN_PARTY_PHASE_LOBBY)
		return;

	for (;;) {
		LanMsgType t;
		const uint8_t *py;
		size_t pl;
		LanAddr fr;
		int n = lan_net_recv(X.gfd, b, sizeof b, &fr);

		if (n <= 0)
			break;
		if (!lan_frame_decode(b, (size_t)n, &t, &py, &pl))
			continue;
		if (t == LAN_MSG_HELLO) {
			LanMsgHello h;

			if (!lan_dec_hello(py, pl, &h))
				continue;
			if (h.spec_version != LAN_SPEC_VERSION
					|| h.room_id != X.room_id)
				continue;
			roster_add(h.uuid, fr, h.name);
		}
	}
}

static void lobby_create(void)
{
	X.room_id = rnd64();
	strncpy(X.room, X.tag, LAN_ROOM_LEN);
	X.room[LAN_ROOM_LEN - 1] = '\0';
	X.host_flag = true;
	roster_local_only();
	X.wadv = X.wgsp = 0;
	X.jleft = 0;
	X.ph = LAN_PARTY_PHASE_LOBBY;
	game_state = LAN_PARTY_LOBBY;
	lan_mdns_browse_stop();
	(void)lan_mdns_register(X.room, X.room_id, X.gport_bound,
			X.dport_bound);
	(void)tx_advert();
}

static void lobby_join(int ord_idx)
{
	int ai = X.ord[ord_idx];
	const Adv *a = &X.adv[ai];

	X.room_id = a->room_id;
	strncpy(X.room, a->rm, LAN_ROOM_LEN);
	X.room[LAN_ROOM_LEN - 1] = '\0';
	X.host_flag = false;
	roster_local_only();
	X.jpeer.ip = a->src.ip;
	X.jpeer.port = a->gprt;
	X.jleft = 24;
	X.jnext = 0;
	X.wadv = X.wgsp = 0;
	X.ph = LAN_PARTY_PHASE_LOBBY;
	game_state = LAN_PARTY_LOBBY;
	lan_mdns_browse_stop();
	(void)tx_advert();
}

void lan_party_init(void)
{
	const char *nm;

	memset(&X, 0, sizeof X);
	uuid_mk(&X.me);
	nm = getenv(LAN_NAME_ENV);
	if (nm && nm[0]) {
		strncpy(X.tag, nm, LAN_NAME_LEN);
		X.tag[LAN_NAME_LEN - 1] = '\0';
	} else {
		strncpy(X.tag, "Player", LAN_NAME_LEN);
		X.tag[LAN_NAME_LEN - 1] = '\0';
	}
	X.dfd = X.gfd = -1;
	X.ph = LAN_PARTY_PHASE_NONE;
}

void lan_party_shutdown_all(void)
{
	lan_mdns_browse_stop();
	lan_mdns_unregister();
	net_dn();
	memset(X.adv, 0, sizeof X.adv);
	X.nord = 0;
	X.cursor = 0;
	X.ph = LAN_PARTY_PHASE_NONE;
	X.room_id = 0;
	X.jleft = 0;
}

bool lan_party_busy(void)
{
	return X.ph == LAN_PARTY_PHASE_BROWSE
		|| X.ph == LAN_PARTY_PHASE_LOBBY;
}

void lan_party_enter_browse(void)
{
	if (!net_up()) {
		fprintf(stderr, "[lan] cannot open UDP (another process on "
				"discovery port? try %s)\n",
				LAN_DISCOVERY_PORT_ENV);
		return;
	}
	memset(X.adv, 0, sizeof X.adv);
	X.nord = 0;
	X.cursor = 0;
	X.ph = LAN_PARTY_PHASE_BROWSE;
	game_state = LAN_PARTY_BROWSE;
	(void)lan_mdns_browse_start(NULL, mdns_apply);
}

void lan_party_tick(void)
{
	uint64_t now = lan_now_ms();

	if (!X.net)
		return;

	lan_mdns_poll();

	refresh_browse(now);
	rx_disc(now);

	if (X.ph == LAN_PARTY_PHASE_LOBBY) {
		rx_game();

		if (now >= X.wadv) {
			(void)tx_advert();
			X.wadv = now + LAN_ADVERT_PERIOD_MS;
		}
		if (now >= X.wgsp) {
			hi_mesh();
			X.wgsp = now + LAN_HELLO_GOSSIP_MS;
		}
		if (X.jleft > 0 && now >= X.jnext) {
			(void)tx_hello(X.jpeer);
			X.jleft--;
			X.jnext = now + 40;
		}
	}
}

void lan_party_draw(void)
{
	int y = 48;
	int i;
	char line[128];
	Row tmp[LAN_MAX_PEERS];
	int nt;

	al_clear_to_color(al_map_rgb(0, 0, 0));
	al_draw_text(font_color, al_map_rgb(220, 220, 120), 12, 12, 0,
			"LAN PARTY");

	if (X.ph == LAN_PARTY_PHASE_BROWSE) {
		al_draw_text(font_color, al_map_rgb(200, 200, 200), 12, y, 0,
				"Room selection (UP/DOWN, ENTER, ESC = main menu)");
		y += 28;
		for (i = 0; i < X.nord; ++i) {
			snprintf(line, sizeof line, " %c  %s%s  rid=%016llx",
					(i == X.cursor) ? '>' : ' ',
					X.adv[X.ord[i]].rm,
					X.adv[X.ord[i]].mdns ? " [mDNS]" : "",
					(unsigned long long)X.adv[X.ord[i]].room_id);
			al_draw_text(font_color, al_map_rgb(255, 255, 255),
					20, y, 0, line);
			y += 22;
		}
		snprintf(line, sizeof line, " %c  CREATE NEW SESSION",
				(X.cursor == X.nord) ? '>' : ' ');
		al_draw_text(font_color, al_map_rgb(180, 255, 180), 20, y, 0,
				line);
		y += 26;
		snprintf(line, sizeof line, "discovery :%u  game :%u",
				(unsigned)X.dport_bound,
				(unsigned)X.gport_bound);
		al_draw_text(font_native, al_map_rgb(150, 150, 150), 12,
				y, 0, line);
		return;
	}

	if (X.ph != LAN_PARTY_PHASE_LOBBY)
		return;

	snprintf(line, sizeof line, "Room %016llx   %s  (%s)",
			(unsigned long long)X.room_id, X.room,
			X.host_flag ? "creator" : "joined");
	al_draw_text(font_color, al_map_rgb(200, 200, 200), 12, y, 0, line);
	y += 28;
	al_draw_text(font_color, al_map_rgb(200, 200, 200), 12, y, 0,
			"Peers (HELLO gossip to mesh):");
	y += 24;

	nt = 0;
	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok)
			tmp[nt++] = X.rw[i];
	}
	qsort(tmp, (size_t)nt, sizeof tmp[0], cmp_row);
	for (i = 0; i < nt; ++i) {
		char ap[64];

		if (tmp[i].loc) {
			snprintf(line, sizeof line, "  [LOCAL] %s  udp :%u",
					tmp[i].nm, (unsigned)X.gport_bound);
		} else {
			lan_addr_to_string(tmp[i].ua, ap, sizeof ap);
			snprintf(line, sizeof line, "  %s  %s",
					tmp[i].nm, ap);
		}
		al_draw_text(font_color, al_map_rgb(255, 255, 255), 16, y, 0,
				line);
		y += 22;
	}
	y += 8;
	al_draw_text(font_native, al_map_rgb(160, 160, 160), 12, y, 0,
			"ESC = room list  (second host: use different discovery "
			"port)");
}

void lan_party_key_down(int kc)
{
	int nrows;

	if (!X.net && kc != ALLEGRO_KEY_ESCAPE)
		return;

	if (X.ph == LAN_PARTY_PHASE_BROWSE) {
		nrows = X.nord + 1;
		if (kc == ALLEGRO_KEY_UP) {
			if (X.cursor > 0)
				X.cursor--;
			else
				X.cursor = nrows - 1;
		} else if (kc == ALLEGRO_KEY_DOWN) {
			X.cursor = (X.cursor + 1) % nrows;
		} else if (kc == ALLEGRO_KEY_ENTER || kc == ALLEGRO_KEY_SPACE) {
			if (X.cursor == X.nord)
				lobby_create();
			else
				lobby_join(X.cursor);
		} else if (kc == ALLEGRO_KEY_ESCAPE) {
			lan_party_shutdown_all();
			game_state = TITLE;
		}
		return;
	}

	if (X.ph == LAN_PARTY_PHASE_LOBBY) {
		if (kc == ALLEGRO_KEY_ESCAPE) {
			if (X.host_flag)
				lan_mdns_unregister();
			X.ph = LAN_PARTY_PHASE_BROWSE;
			X.room_id = 0;
			X.jleft = 0;
			memset(X.adv, 0, sizeof X.adv);
			X.cursor = 0;
			game_state = LAN_PARTY_BROWSE;
			(void)lan_mdns_browse_start(NULL, mdns_apply);
		}
	}
}

void lan_party_key_up(int kc)
{
	(void)kc;
}
