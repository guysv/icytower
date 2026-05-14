/*
 * Phase 1 LAN: SESSION_ADVERT on discovery (multicast+broadcast) +
 * HELLO mesh on the game UDP socket.
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
#include "lan_net.h"

#define ADV_CAP      24
#define STALE_MS     6200ull

typedef struct {
	bool        ok;
	uint64_t    sid;
	char        rm[LAN_ROOM_LEN];
	uint64_t    tms;
	LanAddr     src;
	uint16_t    gprt;
} Adv;

typedef struct {
	bool        ok;
	bool        loc;
	LanUuid     id;
	LanAddr     ua;
	char        nm[LAN_NAME_LEN];
	bool        recv_party_ack;
	bool        sent_party_ack;
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
	uint64_t    sid;
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

static int cmp_ord(const void *a, const void *b)
{
	int ia = *(const int *)a;
	int ib = *(const int *)b;
	uint64_t sa = X.adv[ia].sid;
	uint64_t sb = X.adv[ib].sid;

	if (sa < sb)
		return -1;
	return sa > sb ? 1 : 0;
}

static void refresh_browse(uint64_t now)
{
	int i;

	for (i = 0; i < ADV_CAP; ++i) {
		if (X.adv[i].ok && now > X.adv[i].tms + STALE_MS)
			X.adv[i].ok = false;
	}

	X.nord = 0;
	for (i = 0; i < ADV_CAP; ++i) {
		if (X.adv[i].ok)
			X.ord[X.nord++] = i;
	}
	qsort(X.ord, (size_t)X.nord, sizeof X.ord[0], cmp_ord);

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

static void roster_mark_party_ack(LanUuid id, LanAddr fr)
{
	int i;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc
				&& memcmp(X.rw[i].id.b, id.b, LAN_UUID_BYTES)
						== 0) {
			X.rw[i].ua = fr;
			X.rw[i].recv_party_ack = true;
			return;
		}
	}
	roster_add(id, fr, "?");
	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc
				&& memcmp(X.rw[i].id.b, id.b, LAN_UUID_BYTES)
						== 0) {
			X.rw[i].recv_party_ack = true;
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
	m.session_id = X.sid;
	m.spec_version = LAN_SPEC_VERSION;
	m.peer_hint = 1;
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
	h.session_id = X.sid;
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

static bool party_every_remote_acked(void)
{
	int i;
	int n_remote = 0;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.rw[i].ok || X.rw[i].loc)
			continue;
		n_remote++;
		if (!X.rw[i].recv_party_ack || !X.rw[i].sent_party_ack)
			return false;
	}
	return n_remote >= 1;
}

static void ack_mesh(void)
{
	LanMsgPartyAck pa;
	uint8_t py[64];
	size_t z;
	int i;

	memset(&pa, 0, sizeof pa);
	pa.session_id = X.sid;
	pa.spec_version = LAN_SPEC_VERSION;
	pa.uuid = X.me;
	z = lan_enc_party_ack(py, sizeof py, &pa);
	if (!z)
		return;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.rw[i].ok || X.rw[i].loc)
			continue;
		if (tx_raw(X.gfd, LAN_MSG_PARTY_ACK, py, z, X.rw[i].ua))
			X.rw[i].sent_party_ack = true;
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

			if (!lan_dec_session_advert(py, pl, &m))
				continue;
			if (m.spec_version != LAN_SPEC_VERSION)
				continue;

			j = -1;
			for (k = 0; k < ADV_CAP; ++k) {
				if (X.adv[k].ok && X.adv[k].sid == m.session_id) {
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
			X.adv[j].sid = m.session_id;
			X.adv[j].tms = now;
			X.adv[j].src = fr;
			X.adv[j].gprt = m.port;
			strncpy(X.adv[j].rm, m.room_name, LAN_ROOM_LEN);
			X.adv[j].rm[LAN_ROOM_LEN - 1] = '\0';
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
					|| h.session_id != X.sid)
				continue;
			roster_add(h.uuid, fr, h.name);
			continue;
		}
		if (t != LAN_MSG_PARTY_ACK)
			continue;
		{
			LanMsgPartyAck pa;

			if (!lan_dec_party_ack(py, pl, &pa))
				continue;
			if (pa.spec_version != LAN_SPEC_VERSION
					|| pa.session_id != X.sid)
				continue;
			if (memcmp(pa.uuid.b, X.me.b, LAN_UUID_BYTES) == 0)
				continue;
			roster_mark_party_ack(pa.uuid, fr);
		}
	}
}

static void lobby_create(void)
{
	X.sid = rnd64();
	strncpy(X.room, X.tag, LAN_ROOM_LEN);
	X.room[LAN_ROOM_LEN - 1] = '\0';
	X.host_flag = true;
	roster_local_only();
	X.wadv = X.wgsp = 0;
	X.jleft = 0;
	X.ph = LAN_PARTY_PHASE_LOBBY;
	game_state = LAN_PARTY_LOBBY;
	(void)tx_advert();
}

static void lobby_join(int ord_idx)
{
	int ai = X.ord[ord_idx];
	const Adv *a = &X.adv[ai];

	X.sid = a->sid;
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
	net_dn();
	memset(X.adv, 0, sizeof X.adv);
	X.nord = 0;
	X.cursor = 0;
	X.ph = LAN_PARTY_PHASE_NONE;
	X.sid = 0;
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
}

void lan_party_tick(void)
{
	uint64_t now = lan_now_ms();

	if (!X.net)
		return;

	refresh_browse(now);
	rx_disc(now);

	if (X.ph == LAN_PARTY_PHASE_LOBBY) {
		rx_game();

		if (party_every_remote_acked()) {
			lan_party_shutdown_all();
			game_state = TITLE;
			return;
		}

		if (now >= X.wadv) {
			(void)tx_advert();
			X.wadv = now + LAN_ADVERT_PERIOD_MS;
		}
		if (now >= X.wgsp) {
			hi_mesh();
			ack_mesh();
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
			snprintf(line, sizeof line, " %c  %s  sid=%016llx",
					(i == X.cursor) ? '>' : ' ',
					X.adv[X.ord[i]].rm,
					(unsigned long long)X.adv[X.ord[i]].sid);
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

	snprintf(line, sizeof line, "Session %016llx   %s  (%s)",
			(unsigned long long)X.sid, X.room,
			X.host_flag ? "creator" : "joined");
	al_draw_text(font_color, al_map_rgb(200, 200, 200), 12, y, 0, line);
	y += 28;
	al_draw_text(font_color, al_map_rgb(200, 200, 200), 12, y, 0,
			"Peers (HELLO), then PARTY_ACK handshake:");
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
			snprintf(line, sizeof line, "  %s  %s  tx=%s rx=%s",
					tmp[i].nm, ap,
					tmp[i].sent_party_ack ? "y" : "n",
					tmp[i].recv_party_ack ? "y" : "n");
		}
		al_draw_text(font_color, al_map_rgb(255, 255, 255), 16, y, 0,
				line);
		y += 22;
	}
	y += 8;
	al_draw_text(font_native, al_map_rgb(160, 160, 160), 12, y, 0,
			"When tx/rx ACK both yes for every peer → return to "
			"main menu");
	y += 16;
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
			X.ph = LAN_PARTY_PHASE_BROWSE;
			X.sid = 0;
			X.jleft = 0;
			memset(X.adv, 0, sizeof X.adv);
			X.cursor = 0;
			game_state = LAN_PARTY_BROWSE;
		}
	}
}

void lan_party_key_up(int kc)
{
	(void)kc;
}
