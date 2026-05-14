/*
 * Forked localhost test: ITW1 framing + HELLO (spec v2) on game UDP, parent
 * replies with ROSTER as the real lobby would.
 *
 * Discovery uses two binds on one host conflict on Darwin; only the game path
 * is exercised via a pipe-published port.
 */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "lan_internal.h"
#include "lan_msg.h"
#include "lan_net.h"

#define TEST_ROOM_ID  0xcafebeef12345678ull

enum { PIPE_RD, PIPE_WR };

/* Parent / child use fixed UUIDs so the child can assert ROSTER.sender. */
static void uuid_parent(LanUuid *u) { memset(u->b, 0x77, LAN_UUID_BYTES); }
static void uuid_joiner(LanUuid *u) { memset(u->b, 0x33, LAN_UUID_BYTES); }

static bool tx_hi(int gf, LanAddr to, LanUuid const *id)
{
	LanMsgHello h;
	uint8_t py[256], fr[768];
	size_t z, tn;

	memset(&h, 0, sizeof h);
	h.rel_seq = 0;
	h.room_id = TEST_ROOM_ID;
	h.spec_version = LAN_SPEC_VERSION;
	h.uuid = *id;
	h.skin_id = 0;
	strncpy(h.name, "joiner", LAN_NAME_LEN);
	h.name[LAN_NAME_LEN - 1] = '\0';
	z = lan_enc_hello(py, sizeof py, &h);
	if (!z)
		return false;
	tn = lan_frame_encode(fr, sizeof fr, LAN_MSG_HELLO, py, z);
	return tn > 0 && lan_net_send(gf, fr, tn, to);
}

static bool tx_roster_reply(int gf, LanAddr to, const LanUuid *parent_id,
		const LanMsgHello *jh, LanAddr peer_addr)
{
	LanMsgRoster r;
	uint8_t py[LAN_FRAME_MAX_PAYLOAD], fr[LAN_FRAME_HEADER_LEN
		+ LAN_FRAME_MAX_PAYLOAD + LAN_FRAME_CRC_LEN];
	size_t z, tn;

	memset(&r, 0, sizeof r);
	r.rel_seq = 1;
	r.room_id = TEST_ROOM_ID;
	r.spec_version = LAN_SPEC_VERSION;
	r.sender = *parent_id;
	r.count = 1;
	r.peer[0].uuid = jh->uuid;
	r.peer[0].ip = peer_addr.ip;
	r.peer[0].udp_port = peer_addr.port;
	strncpy(r.peer[0].name, jh->name, LAN_NAME_LEN);
	r.peer[0].name[LAN_NAME_LEN - 1] = '\0';

	z = lan_enc_roster(py, sizeof py, &r);
	if (!z)
		return false;
	tn = lan_frame_encode(fr, sizeof fr, LAN_MSG_ROSTER, py, z);
	return tn > 0 && lan_net_send(gf, fr, tn, to);
}

static int child_main(int rd_pipe)
{
	int gf;
	uint16_t gparent;
	LanAddr to;
	LanUuid ju;
	LanUuid expect_sender;
	unsigned try;

	uuid_joiner(&ju);
	uuid_parent(&expect_sender);

	if (read(rd_pipe, &gparent, sizeof gparent) != (ssize_t)sizeof gparent)
		return 22;

	if (!lan_net_open_game(0, &gf, NULL)) {
		perror("child game bind");
		return 21;
	}

	to.ip = ntohl(inet_addr("127.0.0.1"));
	to.port = gparent;

	for (try = 0; try < 60; ++try) {
		uint8_t b[768];
		LanAddr fr;
		int rn;
		LanMsgType t;
		const uint8_t *py;
		size_t pl;

		if (!tx_hi(gf, to, &ju))
			return 23;
		usleep(15000);
		while ((rn = lan_net_recv(gf, b, sizeof b, &fr)) > 0) {
			if (!lan_frame_decode(b, (size_t)rn, &t, &py, &pl))
				continue;
			if (t != LAN_MSG_ROSTER)
				continue;
			{
				LanMsgRoster r;
				if (!lan_dec_roster(py, pl, &r))
					continue;
				if (r.room_id != TEST_ROOM_ID
						|| r.spec_version
							!= LAN_SPEC_VERSION)
					continue;
				if (memcmp(r.sender.b, expect_sender.b,
					    LAN_UUID_BYTES) != 0)
					continue;
				if (r.count < 1
						|| memcmp(r.peer[0].uuid.b,
							ju.b, LAN_UUID_BYTES)
							!= 0)
					continue;
				lan_net_close(gf);
				return 0;
			}
		}
		if (rn < 0) {
			perror("child recv");
			break;
		}
	}

	lan_net_close(gf);
	return 41;
}

static int parent_main(int wr_pipe)
{
	int gf;
	uint16_t gpr;
	LanUuid ju, pu;
	LanUuid expect;

	uuid_joiner(&ju);
	uuid_parent(&pu);
	expect = ju;

	if (!lan_net_open_game(0, &gf, &gpr)) {
		perror("parent game bind");
		return 10;
	}
	if ((ssize_t)write(wr_pipe, &gpr, sizeof gpr) != (ssize_t)sizeof gpr)
		return 11;

	for (unsigned tr = 0; tr < 400; ++tr) {
		uint8_t b[768];
		LanAddr fr;
		int rn = lan_net_recv(gf, b, sizeof b, &fr);
		LanMsgType t;
		const uint8_t *py;
		size_t pl;

		if (rn < 0) {
			perror("recv");
			break;
		}
		if (rn == 0) {
			usleep(12500);
			continue;
		}
		if (!lan_frame_decode(b, (size_t)rn, &t, &py, &pl))
			continue;
		if (t != LAN_MSG_HELLO)
			continue;
		{
			LanMsgHello h;
			if (!lan_dec_hello(py, pl, &h))
				continue;
			if (h.room_id != TEST_ROOM_ID
					|| h.spec_version != LAN_SPEC_VERSION)
				continue;
			if (memcmp(h.uuid.b, expect.b, LAN_UUID_BYTES) != 0)
				continue;
			if (h.rel_seq != 0)
				continue;

			LanAddr back;

			back.ip = fr.ip;
			back.port = fr.port;

			if (!tx_roster_reply(gf, back, &pu, &h, fr)) {
				lan_net_close(gf);
				return 42;
			}
			lan_net_close(gf);
			return 0;
		}
	}

	lan_net_close(gf);
	return 31;
}

int main(void)
{
	int fds[2];
	pid_t pid;
	int st_parent, st_child, wst;

	st_child = 0;

	if (pipe(fds) != 0) {
		perror("pipe");
		return 1;
	}
	pid = fork();
	if (pid < 0) {
		perror("fork");
		return 1;
	}
	if (!pid) {
		close(fds[PIPE_WR]);
		st_child = child_main(fds[PIPE_RD]);
		close(fds[PIPE_RD]);
		_exit(st_child);
	}

	close(fds[PIPE_RD]);
	st_parent = parent_main(fds[PIPE_WR]);
	close(fds[PIPE_WR]);

	if (waitpid(pid, &wst, 0) < 0) {
		perror("waitpid");
		return 1;
	}
	if (WIFEXITED(wst))
		st_child = WEXITSTATUS(wst);
	else
		st_child = 99;

	if (!st_parent && !st_child)
		puts("[lan_loopback_connect] PASS (HELLO + ROSTER game path)");
	else
		fprintf(stderr, "[lan_loopback_connect] FAIL parent=%d child=%d\n",
				st_parent, st_child);

	return st_parent ? st_parent : st_child;
}
