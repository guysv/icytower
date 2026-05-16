/*
 * Phase 1 LAN: SESSION_ADVERT on discovery (multicast+broadcast) carries room_id,
 * room name, and game port. HELLO (mesh + liveness), WELCOME, GOODBYE on the
 * game socket maintain mesh roster and liveness. While in lobby, every peer
 * multicasts SESSION_ADVERT and may register mDNS for the same room (Bonjour
 * disambiguates duplicate names). The lexicographically smallest UUID peer is
 * still senior for STEADY and LAN bookkeeping (advert/session discovery no
 * longer depends on that role).
 */

#include "lan_party.h"

#include <allegro5/allegro.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fullscreen.h"
#include "gfx.h"
#include "game.h"
#include "icytower.h"
#include "options.h"
#include "physics.h"

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
	uint64_t    last_hb_ms;
} Row;

typedef struct {
	bool     ok;
	LanUuid  id;
	bool     have_seq;
	uint32_t last_seq;
	bool     clock_known;
	uint32_t clock_offset;
	uint8_t  keys_lr;
	int      anim_frame;
	bool     ghost;
	bool     have_jump_rx_seq;
	uint32_t last_jump_rx_seq;
	bool     have_die_rx_seq;
	uint32_t last_die_rx_seq;
	bool     have_pkt_src;
	LanAddr  pkt_src;
	IT_STATE it;
} LobbyRemoteSlot;

typedef struct {
	bool        ok;
	LanUuid     id;
	uint32_t    last_ready_seq;
	uint8_t     ready_flag;
} ReadyRemSlot;

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
	bool        join_leader_announced;
	bool        lobby_level_seeded;
	uint32_t    level_seed;
	uint64_t    room_id;
	char        room[LAN_ROOM_LEN];

	Adv         adv[ADV_CAP];
	int         ord[ADV_CAP];
	int         nord;

	Row         rw[LAN_MAX_PEERS];

	bool        rrx_use[LAN_MAX_PEERS];
	LanUuid     rrx_id[LAN_MAX_PEERS];
	uint16_t    rrx_seq[LAN_MAX_PEERS];

	LobbyRemoteSlot pose[LAN_MAX_PEERS];
	LanLobbyRemote  pose_view[LAN_MAX_PEERS];
	uint32_t    pose_tx_seq;
	uint64_t    wpose;
	uint8_t     last_tx_keys_lr;
	bool        last_tx_keys_valid;

	uint16_t    welcome_tx_seq;
	uint16_t    hello_rel_mesh;
	/* HELLO rel_seq: 0 = request WELCOME reply; non-zero = mesh gossip. */

	uint32_t    ready_tx_seq;
	bool        ready_pending;
	bool        ready_failed;
	bool        ready_acknowledged_sticky;
	uint32_t    ready_pending_seq;
	uint8_t     ready_pending_flag;
	uint8_t     local_ready_committed;
	unsigned    ready_retries_remaining;
	uint64_t    ready_next_tx_ms;
	bool        ready_need_ack[LAN_MAX_PEERS];
	bool        ready_got_ack[LAN_MAX_PEERS];
	ReadyRemSlot ready_rem[LAN_MAX_PEERS];

	uint64_t    steady_go_at_ms;
	uint32_t    steady_wave_seq;
	bool        steady_pending;
	bool        steady_active;
	uint64_t    steady_end_ms;
	uint32_t    last_steady_rx_seq;
	unsigned    steady_retries_remaining;
	uint64_t    steady_next_tx_ms;
	bool        steady_need_ack[LAN_MAX_PEERS];
	bool        steady_got_ack[LAN_MAX_PEERS];

	bool        jump_prev_down;
	uint32_t    jump_tx_seq;

	bool        die_pending;
	uint32_t    die_pending_seq;
	unsigned    die_retries_remaining;
	uint64_t    die_next_tx_ms;
	bool        die_need_ack[LAN_MAX_PEERS];
	bool        die_got_ack[LAN_MAX_PEERS];

	int         cursor;
	uint64_t    wadv;
	uint64_t    wgsp;
	LanAddr     jpeer;
	unsigned    jleft;
	uint64_t    jnext;


	bool        mdns_registered;
} Ctx;

static Ctx X;

extern enum GAME_STATE game_state;

static void lan_party_puts_ts(const char *msg)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) == 0)
		printf("[%lld.%03lld] %s\n",
		    (long long)tv.tv_sec,
		    (long long)(tv.tv_usec / 1000LL),
		    msg);
	else
		printf("[%llu] %s\n",
		    (unsigned long long)lan_now_ms(), msg);
	fflush(stdout);
}

static bool lan_peer_play_frame_mode(void)
{
	return X.ph == LAN_PARTY_PHASE_GAME
		&& (game_state == PLAYING || game_state == ESCAPE);
}

static bool lan_party_should_tx_pose_playing(void)
{
	return game_state == PLAYING || game_state == ESCAPE;
}

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

static void pose_clear_all(void)
{
	memset(X.pose, 0, sizeof X.pose);
	X.pose_tx_seq = 0;
	X.wpose = 0;
	X.last_tx_keys_valid = false;
}

static void pose_drop(const LanUuid *id)
{
	int i;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.pose[i].ok && memcmp(X.pose[i].id.b, id->b,
				LAN_UUID_BYTES) == 0) {
			memset(&X.pose[i], 0, sizeof X.pose[i]);
			return;
		}
	}
}

static uint32_t puppet_room_seed(void)
{
	if (X.host_flag)
		return X.level_seed;
	if (X.lobby_level_seeded)
		return X.level_seed;
	return 0u;
}

static void puppet_init_fresh(LobbyRemoteSlot *s)
{
	init_state(&s->it, rejump, puppet_room_seed());
	s->it.dx = 0;
	s->it.dy = 0;
	s->it.status = STATUS_IDLE;
	s->keys_lr = 0;
	s->clock_known = false;
	s->clock_offset = 0;
	s->anim_frame = 0;
}

static void puppets_reseed(uint32_t seed)
{
	int i;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.pose[i].ok)
			seed_state(&X.pose[i].it, seed);
	}
}

static uint8_t keys_to_lr(int keys)
{
	uint8_t u = 0;

	if (keys & KEY_LEFT)
		u |= LAN_POSE_KEY_LEFT_BIT;
	if (keys & KEY_RIGHT)
		u |= LAN_POSE_KEY_RIGHT_BIT;
	return u;
}

static void steady_state_clear(void)
{
	X.steady_go_at_ms = 0;
	X.steady_pending = false;
	X.steady_active = false;
	X.steady_end_ms = 0;
	X.last_steady_rx_seq = 0;
	X.steady_retries_remaining = 0;
	X.steady_next_tx_ms = 0;
	memset(X.steady_need_ack, 0, sizeof X.steady_need_ack);
	memset(X.steady_got_ack, 0, sizeof X.steady_got_ack);
}

/*
 * Follower receiving authoritative STEADY must discard any half-local steady
 * handshake state; steady_tick() otherwise clears everything when
 * steady_pending && !senior, wiping follower countdown.
 */
static void steady_follow_rx_cancel_originator(void)
{
	X.steady_pending = false;
	X.steady_retries_remaining = 0;
	X.steady_next_tx_ms = 0;
	memset(X.steady_need_ack, 0, sizeof X.steady_need_ack);
	memset(X.steady_got_ack, 0, sizeof X.steady_got_ack);
}

static void die_party_reset(void)
{
	X.die_pending = false;
	X.die_pending_seq = 0;
	X.die_retries_remaining = 0;
	X.die_next_tx_ms = 0;
	memset(X.die_need_ack, 0, sizeof X.die_need_ack);
	memset(X.die_got_ack, 0, sizeof X.die_got_ack);
	X.jump_prev_down = false;
	X.jump_tx_seq = 0;
}

static void ready_party_reset(void)
{
	die_party_reset();
	memset(X.ready_rem, 0, sizeof X.ready_rem);
	X.ready_pending = false;
	X.ready_failed = false;
	X.ready_acknowledged_sticky = false;
	X.ready_tx_seq = 0;
	X.ready_pending_seq = 0;
	X.ready_pending_flag = 0;
	X.local_ready_committed = 0;
	X.ready_retries_remaining = 0;
	X.ready_next_tx_ms = 0;
	memset(X.ready_need_ack, 0, sizeof X.ready_need_ack);
	memset(X.ready_got_ack, 0, sizeof X.ready_got_ack);

	X.steady_wave_seq = 0;
	steady_state_clear();
}

static void roster_local_only(void)
{
	int i;

	ready_party_reset();
	memset(X.rrx_use, 0, sizeof X.rrx_use);

	for (i = 0; i < LAN_MAX_PEERS; ++i)
		memset(&X.rw[i], 0, sizeof X.rw[i]);

	X.rw[0].ok = true;
	X.rw[0].loc = true;
	X.rw[0].id = X.me;
	strncpy(X.rw[0].nm, X.tag, LAN_NAME_LEN);
	X.rw[0].nm[LAN_NAME_LEN - 1] = '\0';
	X.rw[0].ua.ip = ntohl(inet_addr("127.0.0.1"));
	X.rw[0].ua.port = X.gport_bound;
	X.welcome_tx_seq = 0;
	X.hello_rel_mesh = 0;
	pose_clear_all();
}

static void roster_remove(const LanUuid *id)
{
	int i, k;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc
				&& memcmp(X.rw[i].id.b, id->b, LAN_UUID_BYTES)
						== 0) {
			X.ready_need_ack[i] = false;
			X.ready_got_ack[i] = false;
			X.steady_need_ack[i] = false;
			X.steady_got_ack[i] = false;
			for (k = 0; k < LAN_MAX_PEERS; ++k) {
				if (X.ready_rem[k].ok && memcmp(X.ready_rem[k].id.b,
							id->b, LAN_UUID_BYTES) == 0) {
					memset(&X.ready_rem[k], 0,
							sizeof X.ready_rem[k]);
					break;
				}
			}
			memset(&X.rw[i], 0, sizeof X.rw[i]);
			for (k = 0; k < LAN_MAX_PEERS; ++k) {
				if (X.rrx_use[k]
						&& memcmp(X.rrx_id[k].b, id->b,
							LAN_UUID_BYTES) == 0)
					X.rrx_use[k] = false;
			}
			pose_drop(id);
			return;
		}
	}
}

/*
 * Return true if this (sender, rel_seq) is an exact duplicate WELCOME wire
 * duplicate; false if new or updated seq for that sender.
 */
static bool roster_rx_is_dup(const LanUuid *sender, uint16_t seq)
{
	int i, e = -1;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.rrx_use[i]) {
			if (e < 0)
				e = i;
			continue;
		}
		if (memcmp(X.rrx_id[i].b, sender->b, LAN_UUID_BYTES) == 0) {
			if (X.rrx_seq[i] == seq)
				return true;
			X.rrx_seq[i] = seq;
			return false;
		}
	}
	if (e < 0)
		e = 0;
	X.rrx_use[e] = true;
	X.rrx_id[e] = *sender;
	X.rrx_seq[e] = seq;
	return false;
}

static void roster_hb_evict(uint64_t now)
{
	uint64_t thr =
			(uint64_t)LAN_HELLO_GOSSIP_MS * LAN_PRESENCE_MISS_CAP;
	int i;

	/*
	 * During STEADY countdown the mesh is mid-flight; avoid evicting peers on
	 * HELLO timing alone (game traffic proves liveness via roster_refresh_hb).
	 */
	if (X.ph == LAN_PARTY_PHASE_LOBBY && X.steady_active)
		return;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.rw[i].ok || X.rw[i].loc)
			continue;
		if (now - X.rw[i].last_hb_ms >= thr)
			roster_remove(&X.rw[i].id);
	}
}

static LanUuid row_uuid(const Row *r)
{
	return r->loc ? X.me : r->id;
}

static int seniority_cmp(const LanUuid *a, const LanUuid *b)
{
	return memcmp(a->b, b->b, LAN_UUID_BYTES);
}

static bool roster_senior_uuid(LanUuid *out)
{
	int i;
	bool any = false;
	LanUuid best;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		LanUuid id;

		if (!X.rw[i].ok)
			continue;
		id = row_uuid(&X.rw[i]);
		if (!any || seniority_cmp(&id, &best) < 0) {
			best = id;
			any = true;
		}
	}
	if (!any || !out)
		return false;
	*out = best;
	return true;
}

static int roster_remote_count(void)
{
	int i, n = 0;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc)
			n++;
	}
	return n;
}

static void roster_add(LanUuid id, LanAddr ua, const char *nm, uint64_t now_ts)
{
	int i;
	LanUuid prev_leader;
	bool have_prev_leader;

	if (memcmp(id.b, X.me.b, LAN_UUID_BYTES) == 0)
		return;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc
				&& memcmp(X.rw[i].id.b, id.b, LAN_UUID_BYTES)
						== 0) {
			X.rw[i].ua = ua;
			X.rw[i].last_hb_ms = now_ts;
			if (nm && nm[0]) {
				strncpy(X.rw[i].nm, nm, LAN_NAME_LEN);
				X.rw[i].nm[LAN_NAME_LEN - 1] = '\0';
			}
			return;
		}
	}
	have_prev_leader = roster_senior_uuid(&prev_leader);

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.rw[i].ok) {
			memset(&X.rw[i], 0, sizeof X.rw[i]);
			X.rw[i].ok = true;
			X.rw[i].id = id;
			X.rw[i].ua = ua;
			X.rw[i].last_hb_ms = now_ts;
			strncpy(X.rw[i].nm, nm ? nm : "?", LAN_NAME_LEN);
			X.rw[i].nm[LAN_NAME_LEN - 1] = '\0';
			if (have_prev_leader
					&& seniority_cmp(&id, &prev_leader) < 0)
				lan_party_puts_ts(
					"leader changed (new peer has lower UUID)");
			return;
		}
	}
}

static void roster_refresh_hb(const LanUuid *id, uint64_t now_ts)
{
	int i;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.rw[i].ok || X.rw[i].loc)
			continue;
		if (memcmp(X.rw[i].id.b, id->b, LAN_UUID_BYTES) == 0) {
			X.rw[i].last_hb_ms = now_ts;
			return;
		}
	}
}

static bool i_am_senior_survivor(void)
{
	LanUuid best;

	return roster_senior_uuid(&best)
			&& memcmp(best.b, X.me.b, LAN_UUID_BYTES) == 0;
}

static bool roster_all_ready(void)
{
	int i;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		int k;
		bool found;

		if (!X.rw[i].ok)
			continue;
		if (X.rw[i].loc) {
			if (X.local_ready_committed != 1 || X.ready_failed)
				return false;
			continue;
		}
		found = false;
		for (k = 0; k < LAN_MAX_PEERS; ++k) {
			if (!X.ready_rem[k].ok)
				continue;
			if (memcmp(X.ready_rem[k].id.b, X.rw[i].id.b,
						LAN_UUID_BYTES) != 0)
				continue;
			found = true;
			if (X.ready_rem[k].ready_flag != 1u)
				return false;
			break;
		}
		if (!found)
			return false;
	}
	return true;
}

static void sync_room_mdns(void)
{
	if (!X.mdns_registered) {
		if (lan_mdns_register(X.room, X.room_id, X.gport_bound,
				X.dport_bound))
			X.mdns_registered = true;
	}
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

static bool tx_hello(LanAddr to, uint16_t rel_seq)
{
	LanMsgHello h;
	uint8_t py[256];
	size_t z;

	memset(&h, 0, sizeof h);
	h.rel_seq = rel_seq;
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

static bool tx_goodbye(uint64_t room_id, LanAddr to)
{
	LanMsgGoodbye g;
	uint8_t py[64];
	size_t z;

	memset(&g, 0, sizeof g);
	g.room_id = room_id;
	g.spec_version = LAN_SPEC_VERSION;
	g.uuid = X.me;
	z = lan_enc_goodbye(py, sizeof py, &g);
	return z && tx_raw(X.gfd, LAN_MSG_GOODBYE, py, z, to);
}

static bool tx_welcome(LanAddr to)
{
	LanMsgWelcome r;
	uint8_t py[LAN_FRAME_MAX_PAYLOAD];
	size_t z;
	unsigned n = 0;
	int i;

	memset(&r, 0, sizeof r);
	X.welcome_tx_seq++;
	r.rel_seq = X.welcome_tx_seq;
	r.room_id = X.room_id;
	r.spec_version = LAN_SPEC_VERSION;
	r.sender = X.me;
	r.level_seed = X.level_seed;
	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		LanMsgRosterEntry *e;

		if (!X.rw[i].ok || X.rw[i].loc)
			continue;
		if (n >= LAN_MAX_PEERS)
			break;
		e = &r.peer[n];
		e->uuid = X.rw[i].id;
		e->ip = X.rw[i].ua.ip;
		e->udp_port = X.rw[i].ua.port;
		strncpy(e->name, X.rw[i].nm, LAN_NAME_LEN);
		e->name[LAN_NAME_LEN - 1] = '\0';
		n++;
	}
	r.count = (uint8_t)n;
	z = lan_enc_welcome(py, sizeof py, &r);
	return z && tx_raw(X.gfd, LAN_MSG_WELCOME, py, z, to);
}

static bool tx_pose_one(LanAddr to, const LanMsgPose *m)
{
	uint8_t py[64];
	size_t z = lan_enc_pose(py, sizeof py, m);

	return z && tx_raw(X.gfd, LAN_MSG_POSE, py, z, to);
}

static void tx_pose_mesh(uint64_t now)
{
	LanMsgPose m;
	int local_keys;
	int i;

	if (X.ph != LAN_PARTY_PHASE_LOBBY
			&& !(X.ph == LAN_PARTY_PHASE_GAME
				&& lan_party_should_tx_pose_playing()))
		return;
	if (X.ph == LAN_PARTY_PHASE_GAME && game_state == GAMEOVER)
		return;

	X.pose_tx_seq++;
	local_keys = game_current_keys();

	memset(&m, 0, sizeof m);
	m.seq          = X.pose_tx_seq;
	m.room_id      = X.room_id;
	m.spec_version = LAN_SPEC_VERSION;
	m.sender       = X.me;
	m.client_time_ms = (uint32_t)now;
	m.x_fp  = (int32_t)(it_state.x  * (double)LAN_POSE_FIXED_SCALE);
	m.y_fp  = (int32_t)(it_state.y  * (double)LAN_POSE_FIXED_SCALE);
	m.dx_fp = (int32_t)(it_state.dx * (double)LAN_POSE_FIXED_SCALE);
	m.dy_fp = (int32_t)(it_state.dy * (double)LAN_POSE_FIXED_SCALE);
	m.screen_y = (int32_t)it_state.screen_y;
	if (local_keys & KEY_LEFT)
		m.keys_lr |= LAN_POSE_KEY_LEFT_BIT;
	if (local_keys & KEY_RIGHT)
		m.keys_lr |= LAN_POSE_KEY_RIGHT_BIT;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc)
			(void)tx_pose_one(X.rw[i].ua, &m);
	}
}

static void pose_apply(const LanMsgPose *m, uint64_t now, LanAddr fr)
{
	int i, slot = -1, free_slot = -1;
	double scale = (double)LAN_POSE_FIXED_SCALE;
	bool newborn;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.pose[i].ok) {
			if (free_slot < 0)
				free_slot = i;
			continue;
		}
		if (memcmp(X.pose[i].id.b, m->sender.b, LAN_UUID_BYTES) == 0) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		if (free_slot < 0)
			return;
		slot = free_slot;
		memset(&X.pose[slot], 0, sizeof X.pose[slot]);
		X.pose[slot].ok = true;
		X.pose[slot].id = m->sender;
		X.pose[slot].have_seq = false;
	}

	newborn = !X.pose[slot].have_seq;

	if (X.pose[slot].have_seq) {
		uint32_t last = X.pose[slot].last_seq;
		uint32_t cur = m->seq;
		uint32_t delta = cur - last;

		if (delta == 0u || delta > 0x80000000u)
			return;
	}

	X.pose[slot].last_seq = m->seq;
	X.pose[slot].have_seq = true;
	X.pose[slot].have_pkt_src = true;
	X.pose[slot].pkt_src = fr;

	if (newborn)
		puppet_init_fresh(&X.pose[slot]);

	if (X.pose[slot].ghost)
		return;

	{
		uint32_t now32 = (uint32_t)now;
		int64_t adv;
		unsigned t, ticks_to_replay;
		IT_STATE tmp;
		int k = 0;
		double dx_err, dy_err;

		if (!X.pose[slot].clock_known) {
			X.pose[slot].clock_offset = now32 - m->client_time_ms;
			X.pose[slot].clock_known = true;
		}
		adv = (int64_t)now32 - (int64_t)m->client_time_ms
				- (int64_t)X.pose[slot].clock_offset;
		if (adv < 0)
			adv = 0;
		if (adv > 200)
			adv = 200;
		ticks_to_replay = (unsigned)(adv / 20);

		X.pose[slot].keys_lr = m->keys_lr;
		tmp = X.pose[slot].it;
		tmp.x = (double)m->x_fp / scale;
		tmp.y = (double)m->y_fp / scale;
		tmp.dx = (double)m->dx_fp / scale;
		tmp.dy = (double)m->dy_fp / scale;
		tmp.screen_y = (int)m->screen_y;
		if (m->keys_lr & LAN_POSE_KEY_LEFT_BIT)
			k |= KEY_LEFT;
		if (m->keys_lr & LAN_POSE_KEY_RIGHT_BIT)
			k |= KEY_RIGHT;
		for (t = 0; t < ticks_to_replay; ++t) {
			if (lan_peer_play_frame_mode())
				(void)play_frame_peer(&tmp, k);
			else
				play_lobby_frame(&tmp, k);
		}

		dx_err = tmp.x - X.pose[slot].it.x;
		dy_err = tmp.y - X.pose[slot].it.y;
		if (fabs(dx_err) > 40.0 || fabs(dy_err) > 40.0)
			X.pose[slot].it = tmp;
		else {
			X.pose[slot].it.x += dx_err * 0.30;
			X.pose[slot].it.y += dy_err * 0.30;
			X.pose[slot].it.dx = tmp.dx;
			X.pose[slot].it.dy = tmp.dy;
			X.pose[slot].it.screen_y = tmp.screen_y;
		}
	}
}

static void puppets_tick_all(void)
{
	int i, k;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.pose[i].ok)
			continue;
		if (X.pose[i].ghost) {
			double view_bot;
			play_puppet_ghost_frame(&X.pose[i].it);
			/*
			 * Match draw_game remotes: viewport y so we only drop the slot after
			 * the ghost clears the bottom of the *local* screen (purge was
			 * comparing tower world_y to LOGICAL_H, which vanished mid-air).
			 */
			view_bot = X.pose[i].it.y - (double)X.pose[i].it.screen_y
					+ (double)it_state.screen_y;
			if (view_bot > (double)(ICYTOWER_LOGICAL_H + 96))
				memset(&X.pose[i], 0, sizeof X.pose[i]);
			else
				X.pose[i].anim_frame++;
			continue;
		}
		k = 0;
		if (X.pose[i].keys_lr & LAN_POSE_KEY_LEFT_BIT)
			k |= KEY_LEFT;
		if (X.pose[i].keys_lr & LAN_POSE_KEY_RIGHT_BIT)
			k |= KEY_RIGHT;
		if (lan_peer_play_frame_mode())
			(void)play_frame_peer(&X.pose[i].it, k);
		else
			play_lobby_frame(&X.pose[i].it, k);
		X.pose[i].anim_frame++;
	}
}

static bool roster_has_remote_uuid(const LanUuid *id)
{
	int k;

	for (k = 0; k < LAN_MAX_PEERS; ++k) {
		if (!X.rw[k].ok || X.rw[k].loc)
			continue;
		if (memcmp(X.rw[k].id.b, id->b, LAN_UUID_BYTES) == 0)
			return true;
	}
	return false;
}

static void goodbye_mesh(uint64_t room_id)
{
	int i;

	lan_party_puts_ts("sending goodbye");

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc)
			(void)tx_goodbye(room_id, X.rw[i].ua);
	}

	/*
	 * Fall back to the source address of recent POSE packets when a peer is
	 * simulating correctly but roster/WELCOME bookkeeping missed their row.
	 */
	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.pose[i].ok || !X.pose[i].have_seq || !X.pose[i].have_pkt_src)
			continue;
		if (roster_has_remote_uuid(&X.pose[i].id))
			continue;
		(void)tx_goodbye(room_id, X.pose[i].pkt_src);
	}
}

static void hi_mesh(void)
{
	int i;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc)
			(void)tx_hello(X.rw[i].ua, X.hello_rel_mesh);
	}
}

static void ready_rem_apply(const LanUuid *who, uint32_t seq, uint8_t ready_bit)
{
	int i, slot = -1, free_slot = -1;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.ready_rem[i].ok) {
			if (free_slot < 0)
				free_slot = i;
			continue;
		}
		if (memcmp(X.ready_rem[i].id.b, who->b, LAN_UUID_BYTES) == 0) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		if (free_slot < 0)
			return;
		slot = free_slot;
		memset(&X.ready_rem[slot], 0, sizeof X.ready_rem[slot]);
		X.ready_rem[slot].ok = true;
		X.ready_rem[slot].id = *who;
	}
	if (seq <= X.ready_rem[slot].last_ready_seq)
		return;
	X.ready_rem[slot].last_ready_seq = seq;
	X.ready_rem[slot].ready_flag = ready_bit ? 1u : 0u;
	lan_party_puts_ts("ready received");
}

static void ready_send_burst(void)
{
	LanMsgReady m;
	uint8_t py[64];
	size_t z;
	int i;

	if (!X.ready_pending)
		return;
	memset(&m, 0, sizeof m);
	m.room_id = X.room_id;
	m.spec_version = LAN_SPEC_VERSION;
	m.sender = X.me;
	m.seq = X.ready_pending_seq;
	m.ready = X.ready_pending_flag;
	z = lan_enc_ready(py, sizeof py, &m);
	if (!z)
		return;
	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.ready_need_ack[i] || X.ready_got_ack[i])
			continue;
		if (!X.rw[i].ok || X.rw[i].loc)
			continue;
		(void)tx_raw(X.gfd, LAN_MSG_READY, py, z, X.rw[i].ua);
	}
}

static void ready_tick(uint64_t now)
{
	int i;
	bool waiting = false;

	if (!X.ready_pending)
		return;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.ready_need_ack[i] && !X.ready_got_ack[i]) {
			waiting = true;
			break;
		}
	}
	if (!waiting) {
		X.ready_pending = false;
		X.ready_acknowledged_sticky = true;
		X.local_ready_committed = X.ready_pending_flag;
		return;
	}
	if (X.ready_retries_remaining == 0u && now >= X.ready_next_tx_ms) {
		fprintf(stderr, "[lan] READY handshake timed out\n");
		X.ready_pending = false;
		X.ready_failed = true;
		return;
	}
	if (now >= X.ready_next_tx_ms && X.ready_retries_remaining > 0u) {
		ready_send_burst();
		X.ready_retries_remaining--;
		X.ready_next_tx_ms = now + LAN_READY_RETRY_MS;
	}
}

static void steady_abort_if_needed(void)
{
	if (!X.steady_active && !X.steady_pending)
		return;
	if (!roster_all_ready()) {
		/*
		 * UDP reorder: STEADY can arrive before READY on followers. Senior
		 * only sends STEADY after their roster_all_ready; skip tearing down
		 * follower RX countdown until READY catches up.
		 * Originators (!follower mode) still abort when mesh unreadies.
		 */
		if (X.steady_active && !X.steady_pending
				&& !i_am_senior_survivor())
			return;
		steady_state_clear();
		return;
	}
	if (X.steady_pending && !i_am_senior_survivor())
		steady_state_clear();
}

static void steady_send_burst(uint64_t now)
{
	LanMsgSteady m;
	uint8_t py[64];
	size_t z;
	int i;
	uint64_t rem_ms;

	if (!i_am_senior_survivor() || !X.steady_active)
		return;
	memset(&m, 0, sizeof m);
	m.room_id = X.room_id;
	m.spec_version = LAN_SPEC_VERSION;
	m.sender = X.me;
	m.steady_seq = X.steady_wave_seq;
	rem_ms = X.steady_go_at_ms > now ? X.steady_go_at_ms - now : 0;
	m.countdown_ms = rem_ms > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)rem_ms;
	z = lan_enc_steady(py, sizeof py, &m);
	if (!z)
		return;
	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.steady_need_ack[i] || X.steady_got_ack[i])
			continue;
		if (!X.rw[i].ok || X.rw[i].loc)
			continue;
		(void)tx_raw(X.gfd, LAN_MSG_STEADY, py, z, X.rw[i].ua);
	}
}

static void steady_tick(uint64_t now)
{
	int i;
	bool waiting = false;

	if (!X.steady_pending)
		return;
	if (!i_am_senior_survivor()) {
		steady_state_clear();
		return;
	}

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.steady_need_ack[i] && !X.steady_got_ack[i]) {
			waiting = true;
			break;
		}
	}
	if (!waiting) {
		X.steady_pending = false;
		memset(X.steady_need_ack, 0, sizeof X.steady_need_ack);
		memset(X.steady_got_ack, 0, sizeof X.steady_got_ack);
		X.steady_retries_remaining = 0;
		X.steady_next_tx_ms = 0;
		return;
	}
	if (X.steady_retries_remaining == 0u && now >= X.steady_next_tx_ms) {
		fprintf(stderr, "[lan] STEADY handshake timed out "
				"(giving up retries; countdown continues)\n");
		X.steady_pending = false;
		memset(X.steady_need_ack, 0, sizeof X.steady_need_ack);
		memset(X.steady_got_ack, 0, sizeof X.steady_got_ack);
		X.steady_retries_remaining = 0;
		X.steady_next_tx_ms = 0;
		return;
	}
	if (now >= X.steady_next_tx_ms && X.steady_retries_remaining > 0u) {
		steady_send_burst(now);
		X.steady_retries_remaining--;
		X.steady_next_tx_ms = now + LAN_STEADY_RETRY_MS;
	}
}

static void try_arm_steady(uint64_t now)
{
	int i;
	bool any_ack_need = false;

	if (X.ph != LAN_PARTY_PHASE_LOBBY)
		return;
	if (!i_am_senior_survivor())
		return;
	if (X.steady_pending || X.steady_active)
		return;
	if (!roster_all_ready())
		return;

	lan_party_puts_ts("all ready, sending steady");
	X.steady_wave_seq++;
	X.steady_go_at_ms = now + LAN_STEADY_INITIAL_MS;
	X.steady_active = true;
	X.steady_end_ms = X.steady_go_at_ms;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		X.steady_need_ack[i] = X.rw[i].ok && !X.rw[i].loc;
		X.steady_got_ack[i] = false;
		if (X.steady_need_ack[i])
			any_ack_need = true;
	}

	X.steady_pending = any_ack_need;
	if (any_ack_need) {
		X.steady_retries_remaining = LAN_STEADY_MAX_TRIES > 0u
				? LAN_STEADY_MAX_TRIES - 1u : 0u;
		X.steady_next_tx_ms = now + LAN_STEADY_RETRY_MS;
	} else {
		X.steady_retries_remaining = 0;
		X.steady_next_tx_ms = 0;
	}

	steady_send_burst(now);
}

static void steady_completion_maybe(uint64_t now)
{
	if (!X.steady_active || now < X.steady_end_ms)
		return;

	steady_state_clear();
	X.ph = LAN_PARTY_PHASE_GAME;
	game_state = PLAYING;
	X.jump_prev_down = false;
	start_game_with_seed(X.level_seed);
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
			X.adv[j].src = fr;
			X.adv[j].gprt = m.port;
		}
	}
}

static void apply_die_to_puppet(const LanMsgDie *d)
{
	double scale = (double)LAN_POSE_FIXED_SCALE;
	int slot = -1, free_slot = -1, i;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.pose[i].ok) {
			if (free_slot < 0)
				free_slot = i;
			continue;
		}
		if (memcmp(X.pose[i].id.b, d->sender.b, LAN_UUID_BYTES) == 0) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		if (free_slot < 0)
			return;
		slot = free_slot;
		memset(&X.pose[slot], 0, sizeof X.pose[slot]);
		X.pose[slot].ok = true;
		X.pose[slot].id = d->sender;
		puppet_init_fresh(&X.pose[slot]);
	}

	if (X.pose[slot].have_die_rx_seq) {
		uint32_t last = X.pose[slot].last_die_rx_seq;
		uint32_t cur = d->die_seq;
		uint32_t delta = cur - last;

		if (delta == 0u || delta > 0x80000000u)
			return;
	}
	X.pose[slot].have_die_rx_seq = true;
	X.pose[slot].last_die_rx_seq = d->die_seq;

	X.pose[slot].ghost = true;
	X.pose[slot].it.x = (double)d->x_fp / scale;
	X.pose[slot].it.y = (double)d->y_fp / scale;
	X.pose[slot].it.dx = (double)d->dx_fp / scale;
	X.pose[slot].it.dy = (double)d->dy_fp / scale;
	X.pose[slot].it.screen_y = (int)d->screen_y;
	X.pose[slot].it.status = (int)d->status;
	X.pose[slot].keys_lr = d->keys_lr;
	X.pose[slot].have_seq = true;
}

static void jump_apply_rx(const LanMsgJump *m)
{
	int slot = -1, free_slot = -1, i;

	if (X.ph != LAN_PARTY_PHASE_GAME)
		return;
	if (memcmp(m->sender.b, X.me.b, LAN_UUID_BYTES) == 0)
		return;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.pose[i].ok) {
			if (free_slot < 0)
				free_slot = i;
			continue;
		}
		if (memcmp(X.pose[i].id.b, m->sender.b, LAN_UUID_BYTES) == 0) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		if (free_slot < 0)
			return;
		slot = free_slot;
		memset(&X.pose[slot], 0, sizeof X.pose[slot]);
		X.pose[slot].ok = true;
		X.pose[slot].id = m->sender;
		puppet_init_fresh(&X.pose[slot]);
	}

	if (X.pose[slot].ghost)
		return;

	if (X.pose[slot].have_jump_rx_seq) {
		uint32_t last = X.pose[slot].last_jump_rx_seq;
		uint32_t cur = m->jump_seq;
		uint32_t delta = cur - last;

		if (delta == 0u || delta > 0x80000000u)
			return;
	}
	X.pose[slot].have_jump_rx_seq = true;
	X.pose[slot].last_jump_rx_seq = m->jump_seq;

	(void)jump(&X.pose[slot].it);
}

static void tx_jump_mesh(void)
{
	LanMsgJump m;
	uint8_t py[64];
	size_t z;
	int i;

	if (X.ph != LAN_PARTY_PHASE_GAME || !lan_party_should_tx_pose_playing())
		return;

	X.jump_tx_seq++;
	memset(&m, 0, sizeof m);
	m.room_id = X.room_id;
	m.spec_version = LAN_SPEC_VERSION;
	m.sender = X.me;
	m.jump_seq = X.jump_tx_seq;
	z = lan_enc_jump(py, sizeof py, &m);
	if (!z)
		return;
	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc)
			(void)tx_raw(X.gfd, LAN_MSG_JUMP, py, z, X.rw[i].ua);
	}
}

static void die_send_burst(void)
{
	LanMsgDie m;
	uint8_t py[96];
	size_t z;
	int i;
	double sc = (double)LAN_POSE_FIXED_SCALE;

	if (!X.die_pending)
		return;
	memset(&m, 0, sizeof m);
	m.room_id = X.room_id;
	m.spec_version = LAN_SPEC_VERSION;
	m.sender = X.me;
	m.die_seq = X.die_pending_seq;
	m.x_fp = (int32_t)(it_state.x * sc);
	m.y_fp = (int32_t)(it_state.y * sc);
	m.dx_fp = (int32_t)(it_state.dx * sc);
	m.dy_fp = (int32_t)(it_state.dy * sc);
	m.screen_y = (int32_t)it_state.screen_y;
	m.status = (uint8_t)it_state.status;
	m.keys_lr = keys_to_lr(game_current_keys());

	z = lan_enc_die(py, sizeof py, &m);
	if (!z)
		return;
	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.die_need_ack[i] || X.die_got_ack[i])
			continue;
		if (!X.rw[i].ok || X.rw[i].loc)
			continue;
		(void)tx_raw(X.gfd, LAN_MSG_DIE, py, z, X.rw[i].ua);
	}
}

static void die_tick(uint64_t now)
{
	int i;
	bool waiting = false;

	if (!X.die_pending)
		return;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.die_need_ack[i] && !X.die_got_ack[i]) {
			waiting = true;
			break;
		}
	}
	if (!waiting) {
		X.die_pending = false;
		memset(X.die_need_ack, 0, sizeof X.die_need_ack);
		memset(X.die_got_ack, 0, sizeof X.die_got_ack);
		X.die_retries_remaining = 0;
		X.die_next_tx_ms = 0;
		return;
	}
	if (X.die_retries_remaining == 0u && now >= X.die_next_tx_ms) {
		fprintf(stderr, "[lan] DIE handshake timed out\n");
		X.die_pending = false;
		memset(X.die_need_ack, 0, sizeof X.die_need_ack);
		memset(X.die_got_ack, 0, sizeof X.die_got_ack);
		return;
	}
	if (now >= X.die_next_tx_ms && X.die_retries_remaining > 0u) {
		die_send_burst();
		X.die_retries_remaining--;
		X.die_next_tx_ms = now + LAN_DIE_RETRY_MS;
	}
}

static void rx_game(uint64_t now)
{
	uint8_t b[LAN_FRAME_HEADER_LEN + LAN_FRAME_MAX_PAYLOAD
			+ LAN_FRAME_CRC_LEN];

	if (X.ph != LAN_PARTY_PHASE_LOBBY && X.ph != LAN_PARTY_PHASE_GAME)
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

			if (X.ph != LAN_PARTY_PHASE_LOBBY)
				continue;
			if (!lan_dec_hello(py, pl, &h))
				continue;
			if (h.spec_version != LAN_SPEC_VERSION
					|| h.room_id != X.room_id)
				continue;
			roster_add(h.uuid, fr, h.name, now);
			if (h.rel_seq == 0)
				(void)tx_welcome(fr);
			continue;
		}
		if (t == LAN_MSG_WELCOME) {
			LanMsgWelcome r;
			unsigned i;

			if (X.ph != LAN_PARTY_PHASE_LOBBY)
				continue;
			if (!lan_dec_welcome(py, pl, &r))
				continue;
			if (r.spec_version != LAN_SPEC_VERSION
					|| r.room_id != X.room_id)
				continue;
			if (roster_rx_is_dup(&r.sender, r.rel_seq))
				continue;
			if (!X.host_flag) {
				X.level_seed = r.level_seed;
				if (!X.lobby_level_seeded) {
					seed_state(&it_state, r.level_seed);
					puppets_reseed(r.level_seed);
					X.lobby_level_seeded = true;
				}
			}
			for (i = 0; i < (unsigned)r.count; ++i) {
				LanMsgRosterEntry *e = &r.peer[i];
				LanAddr ua;

				ua.ip = e->ip;
				ua.port = e->udp_port;
				roster_add(e->uuid, ua, e->name, now);
			}
			if (X.hello_rel_mesh == 0) {
				X.hello_rel_mesh = 1;
				hi_mesh();
			}
			continue;
		}
		if (t == LAN_MSG_GOODBYE) {
			LanMsgGoodbye g;
			LanUuid prev_senior, next_senior;
			bool had_prev, had_next;

			if (!lan_dec_goodbye(py, pl, &g))
				continue;
			if (g.spec_version != LAN_SPEC_VERSION
					|| g.room_id != X.room_id)
				continue;

			lan_party_puts_ts("goodbye received");

			had_prev = roster_senior_uuid(&prev_senior);
			roster_remove(&g.uuid);
			had_next = roster_senior_uuid(&next_senior);

			if (had_prev && had_next
					&& memcmp(prev_senior.b, next_senior.b,
						LAN_UUID_BYTES) != 0)
				lan_party_puts_ts(
					"leader changed (session senior left via goodbye)");

			continue;
		}
		if (t == LAN_MSG_READY) {
			LanMsgReady r;
			LanMsgReadyAck ack;
			uint8_t apy[64];
			size_t az;

			if (!lan_dec_ready(py, pl, &r))
				continue;
			if (r.spec_version != LAN_SPEC_VERSION
					|| r.room_id != X.room_id)
				continue;
			if (memcmp(r.sender.b, X.me.b, LAN_UUID_BYTES) == 0)
				continue;

			roster_refresh_hb(&r.sender, now);

			memset(&ack, 0, sizeof ack);
			ack.room_id = X.room_id;
			ack.spec_version = LAN_SPEC_VERSION;
			ack.ack_sender = X.me;
			ack.for_peer = r.sender;
			ack.seq = r.seq;
			az = lan_enc_ready_ack(apy, sizeof apy, &ack);
			if (az)
				(void)tx_raw(X.gfd, LAN_MSG_READY_ACK, apy, az,
						fr);

			ready_rem_apply(&r.sender, r.seq, r.ready);
			continue;
		}
		if (t == LAN_MSG_READY_ACK) {
			LanMsgReadyAck a;
			int i;

			if (!lan_dec_ready_ack(py, pl, &a))
				continue;
			if (a.spec_version != LAN_SPEC_VERSION
					|| a.room_id != X.room_id)
				continue;
			if (memcmp(a.for_peer.b, X.me.b, LAN_UUID_BYTES) != 0)
				continue;
			if (!X.ready_pending || a.seq != X.ready_pending_seq)
				continue;

			roster_refresh_hb(&a.ack_sender, now);

			for (i = 0; i < LAN_MAX_PEERS; ++i) {
				if (!X.ready_need_ack[i])
					continue;
				if (!X.rw[i].ok || X.rw[i].loc)
					continue;
				if (memcmp(X.rw[i].id.b, a.ack_sender.b,
							LAN_UUID_BYTES) == 0) {
					X.ready_got_ack[i] = true;
					break;
				}
			}
			continue;
		}
		if (t == LAN_MSG_STEADY) {
			LanMsgSteady r;
			LanMsgSteadyAck ack;
			LanUuid sen;
			uint8_t apy[64];
			size_t az;

			if (!lan_dec_steady(py, pl, &r))
				continue;
			if (r.spec_version != LAN_SPEC_VERSION
					|| r.room_id != X.room_id)
				continue;
			if (!roster_senior_uuid(&sen))
				continue;
			if (memcmp(r.sender.b, sen.b, LAN_UUID_BYTES) != 0)
				continue;
			if (memcmp(r.sender.b, X.me.b, LAN_UUID_BYTES) == 0)
				continue;

			roster_refresh_hb(&r.sender, now);

			memset(&ack, 0, sizeof ack);
			ack.room_id = X.room_id;
			ack.spec_version = LAN_SPEC_VERSION;
			ack.ack_sender = X.me;
			ack.for_peer = r.sender;
			ack.steady_seq = r.steady_seq;
			az = lan_enc_steady_ack(apy, sizeof apy, &ack);
			if (az)
				(void)tx_raw(X.gfd, LAN_MSG_STEADY_ACK, apy, az,
						fr);

			steady_follow_rx_cancel_originator();

			if (r.steady_seq < X.last_steady_rx_seq)
				continue;
			if (r.steady_seq != X.last_steady_rx_seq)
				lan_party_puts_ts("steady received");
			X.last_steady_rx_seq = r.steady_seq;
			X.steady_wave_seq = r.steady_seq;
			X.steady_active = true;
			X.steady_end_ms = now + (uint64_t)r.countdown_ms;
			continue;
		}
		if (t == LAN_MSG_STEADY_ACK) {
			LanMsgSteadyAck a;
			int i;

			if (!lan_dec_steady_ack(py, pl, &a))
				continue;
			if (a.spec_version != LAN_SPEC_VERSION
					|| a.room_id != X.room_id)
				continue;
			if (memcmp(a.for_peer.b, X.me.b, LAN_UUID_BYTES) != 0)
				continue;
			if (!X.steady_pending || a.steady_seq != X.steady_wave_seq)
				continue;

			roster_refresh_hb(&a.ack_sender, now);

			for (i = 0; i < LAN_MAX_PEERS; ++i) {
				if (!X.steady_need_ack[i])
					continue;
				if (!X.rw[i].ok || X.rw[i].loc)
					continue;
				if (memcmp(X.rw[i].id.b, a.ack_sender.b,
							LAN_UUID_BYTES) == 0) {
					X.steady_got_ack[i] = true;
					break;
				}
			}
			continue;
		}
		if (t == LAN_MSG_POSE) {
			LanMsgPose p;

			if (X.ph != LAN_PARTY_PHASE_LOBBY && X.ph != LAN_PARTY_PHASE_GAME)
				continue;
			if (!lan_dec_pose(py, pl, &p))
				continue;
			if (p.spec_version != LAN_SPEC_VERSION
					|| p.room_id != X.room_id)
				continue;
			if (memcmp(p.sender.b, X.me.b, LAN_UUID_BYTES) == 0)
				continue;
			roster_refresh_hb(&p.sender, now);
			pose_apply(&p, now, fr);
			continue;
		}
		if (t == LAN_MSG_JUMP) {
			LanMsgJump j;

			if (X.ph != LAN_PARTY_PHASE_GAME)
				continue;
			if (!lan_dec_jump(py, pl, &j))
				continue;
			if (j.spec_version != LAN_SPEC_VERSION
					|| j.room_id != X.room_id)
				continue;
			roster_refresh_hb(&j.sender, now);
			jump_apply_rx(&j);
			continue;
		}
		if (t == LAN_MSG_DIE) {
			LanMsgDie d;
			LanMsgDieAck ack;
			uint8_t apy[80];
			size_t az;

			if (!lan_dec_die(py, pl, &d))
				continue;
			if (d.spec_version != LAN_SPEC_VERSION
					|| d.room_id != X.room_id)
				continue;
			if (memcmp(d.sender.b, X.me.b, LAN_UUID_BYTES) == 0)
				continue;

			roster_refresh_hb(&d.sender, now);

			memset(&ack, 0, sizeof ack);
			ack.room_id = X.room_id;
			ack.spec_version = LAN_SPEC_VERSION;
			ack.ack_sender = X.me;
			ack.for_peer = d.sender;
			ack.die_seq = d.die_seq;
			az = lan_enc_die_ack(apy, sizeof apy, &ack);
			if (az)
				(void)tx_raw(X.gfd, LAN_MSG_DIE_ACK, apy, az, fr);

			apply_die_to_puppet(&d);
			continue;
		}
		if (t == LAN_MSG_DIE_ACK) {
			LanMsgDieAck a;
			int i;

			if (!lan_dec_die_ack(py, pl, &a))
				continue;
			if (a.spec_version != LAN_SPEC_VERSION
					|| a.room_id != X.room_id)
				continue;
			if (memcmp(a.for_peer.b, X.me.b, LAN_UUID_BYTES) != 0)
				continue;
			if (!X.die_pending || a.die_seq != X.die_pending_seq)
				continue;

			roster_refresh_hb(&a.ack_sender, now);

			for (i = 0; i < LAN_MAX_PEERS; ++i) {
				if (!X.die_need_ack[i])
					continue;
				if (!X.rw[i].ok || X.rw[i].loc)
					continue;
				if (memcmp(X.rw[i].id.b, a.ack_sender.b,
							LAN_UUID_BYTES) == 0) {
					X.die_got_ack[i] = true;
					break;
				}
			}
			continue;
		}
	}
}

static void lobby_create(void)
{
	X.room_id = rnd64();
	X.level_seed = (uint32_t)time(NULL);
	strncpy(X.room, X.tag, LAN_ROOM_LEN);
	X.room[LAN_ROOM_LEN - 1] = '\0';
	X.host_flag = true;
	X.join_leader_announced = false;
	X.mdns_registered = false;
	roster_local_only();
	X.hello_rel_mesh = 1;
	X.wadv = X.wgsp = 0;
	X.jleft = 0;
	X.ph = LAN_PARTY_PHASE_LOBBY;
	game_state = LAN_PARTY_LOBBY;
	init_state(&it_state, rejump, X.level_seed);
	game_reset_for_lobby_preview();
	X.lobby_level_seeded = true;
	lan_mdns_browse_stop();
}

static void lobby_join(int ord_idx)
{
	int ai = X.ord[ord_idx];
	const Adv *a = &X.adv[ai];

	X.room_id = a->room_id;
	strncpy(X.room, a->rm, LAN_ROOM_LEN);
	X.room[LAN_ROOM_LEN - 1] = '\0';
	X.host_flag = false;
	X.mdns_registered = false;
	roster_local_only();
	X.jpeer.ip = a->src.ip;
	X.jpeer.port = a->gprt;
	X.join_leader_announced = false;
	X.jleft = 24;
	X.jnext = 0;
	X.wadv = X.wgsp = 0;
	X.ph = LAN_PARTY_PHASE_LOBBY;
	game_state = LAN_PARTY_LOBBY;
	init_state(&it_state, rejump, 0);
	game_reset_for_lobby_preview();
	X.lobby_level_seeded = false;
	lan_mdns_browse_stop();
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
	if (X.net && X.room_id != 0
			&& (X.ph == LAN_PARTY_PHASE_LOBBY
				|| X.ph == LAN_PARTY_PHASE_GAME))
		goodbye_mesh(X.room_id);
	lan_mdns_browse_stop();
	lan_mdns_unregister();
	X.mdns_registered = false;
	net_dn();
	memset(X.adv, 0, sizeof X.adv);
	X.nord = 0;
	X.cursor = 0;
	X.ph = LAN_PARTY_PHASE_NONE;
	X.room_id = 0;
	X.jleft = 0;
	ready_party_reset();
}

bool lan_party_busy(void)
{
	return X.ph == LAN_PARTY_PHASE_BROWSE
		|| X.ph == LAN_PARTY_PHASE_LOBBY
		|| X.ph == LAN_PARTY_PHASE_GAME;
}

bool lan_party_is_network_game(void)
{
	return X.ph == LAN_PARTY_PHASE_GAME;
}

void lan_party_leave_room_to_browse(void)
{
	uint64_t rid;

	if (!X.net)
		return;
	if (X.ph != LAN_PARTY_PHASE_LOBBY && X.ph != LAN_PARTY_PHASE_GAME)
		return;
	rid = X.room_id;
	goodbye_mesh(rid);
	lan_mdns_unregister();
	X.mdns_registered = false;
	X.ph = LAN_PARTY_PHASE_BROWSE;
	X.room_id = 0;
	X.level_seed = 0;
	X.lobby_level_seeded = false;
	X.jleft = 0;
	memset(X.adv, 0, sizeof X.adv);
	X.cursor = 0;
	ready_party_reset();
	game_state = LAN_PARTY_BROWSE;
	(void)lan_mdns_browse_start(NULL, mdns_apply);
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

	if (X.ph == LAN_PARTY_PHASE_GAME) {
		/*
		 * A loss (GAMEOVER) is not leaving the match: GOODBYE is only sent from
		 * explicit leave/browse/shutdown (see goodbye_mesh callers). Older logic
		 * tied phase lifetime to DIE_ACK handshakes which looked like a quit.
		 */
		bool keep_phase = game_state == PLAYING || game_state == PAUSE
				|| game_state == ESCAPE || game_state == GAMEOVER
				|| game_state == ENTER_INITIALS;

		if (!keep_phase) {
			goodbye_mesh(X.room_id);
			X.ph = LAN_PARTY_PHASE_NONE;
			steady_state_clear();
			die_party_reset();
			return;
		}
		rx_game(now);
		die_tick(now);
		if (game_state == PLAYING) {
			int lk = game_current_keys();
			bool jd = (lk & KEY_JUMP) != 0;
			bool jedge = jd && !X.jump_prev_down;

			X.jump_prev_down = jd;
			if (jedge)
				tx_jump_mesh();
		}

		if (lan_party_should_tx_pose_playing()) {
			uint8_t cur_lr = keys_to_lr(game_current_keys());
			bool keys_edge = !X.last_tx_keys_valid
					|| cur_lr != X.last_tx_keys_lr;

			if (keys_edge || now >= X.wpose) {
				tx_pose_mesh(now);
				X.last_tx_keys_lr = cur_lr;
				X.last_tx_keys_valid = true;
				X.wpose = now + LAN_POSE_PERIOD_MS;
			}
		}

		puppets_tick_all();
		return;
	}

	if (X.ph == LAN_PARTY_PHASE_LOBBY) {
		rx_game(now);
		if (!X.host_flag && !X.join_leader_announced
				&& roster_remote_count() > 0
				&& i_am_senior_survivor()) {
			lan_party_puts_ts(
				"joined room: we are now the leaders");
			X.join_leader_announced = true;
		}
		roster_hb_evict(now);
		steady_abort_if_needed();
		ready_tick(now);
		steady_tick(now);
		steady_completion_maybe(now);
		if (X.ph != LAN_PARTY_PHASE_LOBBY)
			return;
		try_arm_steady(now);
		if (X.ph != LAN_PARTY_PHASE_LOBBY)
			return;

		sync_room_mdns();

		/*
		 * Local-authority lobby physics + puppet simulation (see pose_apply
		 * reconcile + puppets_tick).
		 */
		game_lobby_tick(game_current_keys());
		puppets_tick_all();

		if (now >= X.wadv) {
			(void)tx_advert();
			X.wadv = now + LAN_ADVERT_PERIOD_MS;
		}
		if (now >= X.wgsp) {
			hi_mesh();
			X.wgsp = now + LAN_HELLO_GOSSIP_MS;
		}
		if (X.jleft > 0 && now >= X.jnext) {
			(void)tx_hello(X.jpeer, 0);
			X.jleft--;
			X.jnext = now + 40;
		}
		{
			uint8_t cur_lr = keys_to_lr(game_current_keys());
			bool keys_edge = !X.last_tx_keys_valid
					|| cur_lr != X.last_tx_keys_lr;

			if (keys_edge || now >= X.wpose) {
				tx_pose_mesh(now);
				X.last_tx_keys_lr = cur_lr;
				X.last_tx_keys_valid = true;
				X.wpose = now + LAN_POSE_PERIOD_MS;
			}
		}
	}
}

void lan_party_draw(void)
{
	int y = 48;
	int i;
	char line[128];

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
		snprintf(line, sizeof line, "discovery :%u  game :%u",
				(unsigned)X.dport_bound,
				(unsigned)X.gport_bound);
		al_draw_text(font_native, al_map_rgb(150, 150, 150), 12,
				ICYTOWER_LOGICAL_H - al_get_font_line_height(font_native) - 8,
				0, line);
		return;
	}

	if (X.ph != LAN_PARTY_PHASE_LOBBY)
		return;

	draw_game();

	if (X.steady_active) {
		uint64_t now_d = lan_now_ms();
		uint64_t rem_ms = X.steady_end_ms > now_d ? X.steady_end_ms - now_d : 0;
		int tw;

		if (rem_ms > 0u)
			snprintf(line, sizeof line, "%u",
					(unsigned)((rem_ms + 999ull) / 1000ull));
		else
			snprintf(line, sizeof line, "GO!");
		tw = al_get_text_width(font_color, line);
		al_draw_text(font_color, al_map_rgb(255, 220, 80),
				(ICYTOWER_LOGICAL_W - tw) / 2,
				(ICYTOWER_LOGICAL_H - al_get_font_line_height(font_color)) / 2,
				0, line);
	}
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
		if (kc == ALLEGRO_KEY_ESCAPE)
			lan_party_leave_room_to_browse();
	}
}

void lan_party_key_up(int kc)
{
	(void)kc;
}

void lan_party_notify_local_death(void)
{
	uint64_t now = lan_now_ms();
	int i;
	bool any_remote = false;

	if (!X.net || X.ph != LAN_PARTY_PHASE_GAME)
		return;
	if (X.die_pending)
		return;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc)
			any_remote = true;
	}

	if (!any_remote)
		return;

	X.die_pending_seq++;
	X.die_pending = true;
	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		X.die_need_ack[i] = X.rw[i].ok && !X.rw[i].loc;
		X.die_got_ack[i] = false;
	}
	X.die_retries_remaining = LAN_DIE_MAX_TRIES > 0u
			? LAN_DIE_MAX_TRIES - 1u : 0u;
	X.die_next_tx_ms = now + LAN_DIE_RETRY_MS;
	die_send_burst();
}

void lan_party_ready_commit(bool ready)
{
	uint64_t now = lan_now_ms();
	int i;
	bool any_remote = false;

	if (X.ph != LAN_PARTY_PHASE_LOBBY || !X.net)
		return;
	if (X.ready_pending)
		return;

	X.ready_tx_seq++;
	X.ready_pending_seq = X.ready_tx_seq;
	X.ready_pending_flag = ready ? 1u : 0u;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (X.rw[i].ok && !X.rw[i].loc)
			any_remote = true;
	}

	if (!any_remote) {
		X.local_ready_committed = X.ready_pending_flag;
		X.ready_acknowledged_sticky = true;
		X.ready_failed = false;
		return;
	}

	X.ready_pending = true;
	X.ready_failed = false;
	X.ready_acknowledged_sticky = false;

	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		X.ready_need_ack[i] = X.rw[i].ok && !X.rw[i].loc;
		X.ready_got_ack[i] = false;
	}

	X.ready_retries_remaining = LAN_READY_MAX_TRIES > 0u
			? LAN_READY_MAX_TRIES - 1u : 0u;
	X.ready_next_tx_ms = now + LAN_READY_RETRY_MS;
	lan_party_puts_ts("sending ready");
	ready_send_burst();
}

bool lan_party_ready_pending(void)
{
	return X.ready_pending;
}

bool lan_party_ready_acknowledged(void)
{
	return X.ready_acknowledged_sticky && !X.ready_failed
			&& !X.ready_pending;
}

static bool lobby_peer_ready_flag(const LanUuid *id)
{
	int k;

	for (k = 0; k < LAN_MAX_PEERS; ++k) {
		if (!X.ready_rem[k].ok)
			continue;
		if (memcmp(X.ready_rem[k].id.b, id->b, LAN_UUID_BYTES) != 0)
			continue;
		return X.ready_rem[k].ready_flag == 1u;
	}
	return false;
}

bool lan_party_lobby_local_ready_marker(void)
{
	if (X.ph != LAN_PARTY_PHASE_LOBBY)
		return false;
	if (X.ready_failed)
		return false;
	if (X.ready_pending && X.ready_pending_flag)
		return true;
	return X.local_ready_committed == 1;
}

size_t lan_party_lobby_remotes(const LanLobbyRemote **out)
{
	size_t n = 0;
	int i;

	if (X.ph != LAN_PARTY_PHASE_LOBBY && X.ph != LAN_PARTY_PHASE_GAME) {
		if (out)
			*out = NULL;
		return 0;
	}
	for (i = 0; i < LAN_MAX_PEERS; ++i) {
		if (!X.pose[i].ok || !X.pose[i].have_seq)
			continue;
		X.pose_view[n].uuid       = X.pose[i].id;
		X.pose_view[n].x          = X.pose[i].it.x;
		X.pose_view[n].y          = X.pose[i].it.y;
		X.pose_view[n].screen_y   = X.pose[i].it.screen_y;
		X.pose_view[n].dx         = X.pose[i].it.dx;
		X.pose_view[n].dy         = X.pose[i].it.dy;
		X.pose_view[n].key_left   = (X.pose[i].keys_lr
				& LAN_POSE_KEY_LEFT_BIT) != 0u;
		X.pose_view[n].key_right  = (X.pose[i].keys_lr
				& LAN_POSE_KEY_RIGHT_BIT) != 0u;
		X.pose_view[n].anim_frame = X.pose[i].anim_frame;
		X.pose_view[n].ready      = (X.ph == LAN_PARTY_PHASE_LOBBY)
				? lobby_peer_ready_flag(&X.pose[i].id) : false;
		X.pose_view[n].ghost      = X.pose[i].ghost;
		X.pose_view[n].phys_status = (uint8_t)X.pose[i].it.status;
		n++;
	}
	if (out)
		*out = X.pose_view;
	return n;
}
