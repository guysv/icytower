/*
 * Two localhost UDP sockets: duplicate READY datagrams must each produce READY_ACK
 * (receiver-side reliability contract).
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "lan_internal.h"
#include "lan_msg.h"
#include "lan_net.h"

#define TEST_ROOM  0xbaddcafef00df00dull

static int fail(const char *what)
{
	fprintf(stderr, "[lan_ready_dup_ack] FAIL: %s\n", what);
	return 1;
}

static bool tx_ready(int fd, LanAddr to, const LanMsgReady *rdy)
{
	uint8_t py[64], fr[LAN_FRAME_HEADER_LEN + 64 + LAN_FRAME_CRC_LEN];
	size_t z, fn;

	z = lan_enc_ready(py, sizeof py, rdy);
	if (!z)
		return false;
	fn = lan_frame_encode(fr, sizeof fr, LAN_MSG_READY, py, z);
	return fn > 0 && lan_net_send(fd, fr, fn, to);
}

static bool bob_try_ack_one(int fb)
{
	uint8_t b[768];
	LanAddr fr;
	int n;
	LanMsgType ty;
	const uint8_t *py;
	size_t pl;
	LanMsgReady r;
	LanMsgReadyAck ack;
	uint8_t apy[64];
	size_t az;
	size_t fn;

	memset(&ack, 0, sizeof ack);

	n = lan_net_recv(fb, b, sizeof b, &fr);
	if (n <= 0)
		return false;
	if (!lan_frame_decode(b, (size_t)n, &ty, &py, &pl))
		return false;
	if (ty != LAN_MSG_READY)
		return false;
	if (!lan_dec_ready(py, pl, &r))
		return false;
	if (r.room_id != TEST_ROOM || r.spec_version != LAN_SPEC_VERSION)
		return false;

	ack.room_id = TEST_ROOM;
	ack.spec_version = LAN_SPEC_VERSION;
	memset(ack.ack_sender.b, 0x77, LAN_UUID_BYTES);
	ack.for_peer = r.sender;
	ack.seq = r.seq;
	az = lan_enc_ready_ack(apy, sizeof apy, &ack);
	if (!az)
		return false;
	fn = lan_frame_encode(b, sizeof b, LAN_MSG_READY_ACK, apy, az);
	return fn > 0 && lan_net_send(fb, b, fn, fr);
}

static unsigned alice_drain_acks(int fa)
{
	unsigned got = 0;
	unsigned spins;
	uint8_t b[768];
	LanAddr fr;
	int n;
	LanMsgType ty;
	const uint8_t *py;
	size_t pl;

	for (spins = 0; spins < 800u; ++spins) {
		n = lan_net_recv(fa, b, sizeof b, &fr);
		if (n > 0) {
			if (lan_frame_decode(b, (size_t)n, &ty, &py, &pl)
					&& ty == LAN_MSG_READY_ACK)
				got++;
			continue;
		}
		usleep(2000);
	}
	return got;
}

int main(void)
{
	int fa = -1, fb = -1;
	uint16_t pa = 0, pb = 0;
	LanAddr bob_addr;
	LanMsgReady rdy;
	unsigned acks;

	memset(&rdy, 0, sizeof rdy);
	rdy.room_id = TEST_ROOM;
	rdy.spec_version = LAN_SPEC_VERSION;
	memset(rdy.sender.b, 0x33, LAN_UUID_BYTES);
	rdy.seq = 42u;
	rdy.ready = 1u;

	if (!lan_net_open_game(0, &fa, &pa) || !lan_net_open_game(0, &fb, &pb))
		return fail("lan_net_open_game");

	bob_addr.ip = ntohl(inet_addr("127.0.0.1"));
	bob_addr.port = pb;

	if (!tx_ready(fa, bob_addr, &rdy))
		return fail("tx READY 1");

	while (!bob_try_ack_one(fb))
		usleep(1000);

	if (!tx_ready(fa, bob_addr, &rdy))
		return fail("tx READY duplicate");

	while (!bob_try_ack_one(fb))
		usleep(1000);

	acks = alice_drain_acks(fa);
	lan_net_close(fa);
	lan_net_close(fb);

	if (acks != 2u)
		return fail("expected 2 READY_ACK datagrams");

	puts("[lan_ready_dup_ack] PASS");
	return 0;
}
