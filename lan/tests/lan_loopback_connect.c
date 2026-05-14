/*
 * Forked localhost test: verifies ITW1 framing + HELLO on game UDP ports.
 * (Discovery uses a single pinned port — two binds on one host conflict on
 * Darwin — so we only exercise the HELLO/game path via a pipe-published port.)
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

#define TEST_SID  0xcafebeef12345678ull

enum { PIPE_RD, PIPE_WR };

static bool tx_hi(int gf, LanAddr to, LanUuid const *id, uint16_t seq)
{
	LanMsgHello h;
	uint8_t py[256], fr[768];
	size_t z, tn;

	memset(&h, 0, sizeof h);
	h.rel_seq = seq;
	h.session_id = TEST_SID;
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

static int child_main(int rd_pipe)
{
	int gf;
	uint16_t gparent;
	LanAddr to;
	LanUuid ju;
	int k;

	if (read(rd_pipe, &gparent, sizeof gparent) != (ssize_t)sizeof gparent)
		return 22;

	if (!lan_net_open_game(0, &gf, NULL)) {
		perror("child game bind");
		return 21;
	}
	memset(&ju.b, 0x33, LAN_UUID_BYTES);
	to.ip = ntohl(inet_addr("127.0.0.1"));
	to.port = gparent;
	for (k = 0; k < 30; ++k) {
		(void)tx_hi(gf, to, &ju, (uint16_t)(k + 1));
		usleep(20000);
	}
	lan_net_close(gf);
	return 0;
}

static int parent_main(int wr_pipe)
{
	int gf;
	uint16_t gpr;
	LanUuid expect;
	LanUuid ju;
	memset(&ju.b, 0x33, LAN_UUID_BYTES);

	if (!lan_net_open_game(0, &gf, &gpr)) {
		perror("parent game bind");
		return 10;
	}
	if ((ssize_t)write(wr_pipe, &gpr, sizeof gpr) != (ssize_t)sizeof gpr)
		return 11;

	expect = ju;

	for (unsigned try = 0; try < 400; ++try) {
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
			if (h.session_id == TEST_SID
					&& memcmp(h.uuid.b, expect.b,
					    LAN_UUID_BYTES) == 0) {
				lan_net_close(gf);
				return 0;
			}
		}

		usleep(500);
	}

	lan_net_close(gf);
	return 31;
}

int main(void)
{
	int fds[2];
	pid_t pid;
	int st;

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
		st = child_main(fds[PIPE_RD]);
		close(fds[PIPE_RD]);
		_exit(st);
	}

	close(fds[PIPE_RD]);
	st = parent_main(fds[PIPE_WR]);
	close(fds[PIPE_WR]);
	waitpid(pid, NULL, 0);

	if (!st)
		puts("[lan_loopback_connect] PASS (HELLO game path)");
	else
		fprintf(stderr, "[lan_loopback_connect] FAIL parent=%d\n", st);
	return st;
}
