#include <stdio.h>
#include <string.h>

#include "lan_internal.h"
#include "lan_msg.h"

static int fail(const char *what)
{
	fprintf(stderr, "[lan_jump_die_roundtrip] FAIL: %s\n", what);
	return 1;
}

int main(void)
{
	LanMsgJump jt, jr;
	LanMsgDie dt, dr;
	LanMsgDieAck at, ar;
	uint8_t py[128];
	uint8_t fr[LAN_FRAME_HEADER_LEN + 128 + LAN_FRAME_CRC_LEN];
	size_t pl_tx, pl_rx, fn;
	LanMsgType t;
	const uint8_t *pyp;
	int i;

	memset(&jt, 0, sizeof jt);
	jt.room_id = 0x1122334455667788ull;
	jt.spec_version = LAN_SPEC_VERSION;
	for (i = 0; i < LAN_UUID_BYTES; ++i)
		jt.sender.b[i] = (uint8_t)(0x40 + i);
	jt.jump_seq = 0x99abcdefu;

	pl_tx = lan_enc_jump(py, sizeof py, &jt);
	if (!pl_tx)
		return fail("lan_enc_jump");
	fn = lan_frame_encode(fr, sizeof fr, LAN_MSG_JUMP, py, pl_tx);
	if (!fn || !lan_frame_decode(fr, fn, &t, &pyp, &pl_rx) || t != LAN_MSG_JUMP)
		return fail("JUMP frame");
	if (!lan_dec_jump(pyp, pl_rx, &jr))
		return fail("lan_dec_jump");
	if (jr.jump_seq != jt.jump_seq || jr.room_id != jt.room_id)
		return fail("JUMP field mismatch");

	memset(&dt, 0, sizeof dt);
	dt.room_id = jt.room_id;
	dt.spec_version = LAN_SPEC_VERSION;
	dt.sender = jt.sender;
	dt.die_seq = 7u;
	dt.x_fp = 1000;
	dt.y_fp = 2000;
	dt.dx_fp = -300;
	dt.dy_fp = 400;
	dt.screen_y = -777;
	dt.status = 3;
	dt.keys_lr = LAN_POSE_KEY_LEFT_BIT;

	pl_tx = lan_enc_die(py, sizeof py, &dt);
	if (!pl_tx)
		return fail("lan_enc_die");
	fn = lan_frame_encode(fr, sizeof fr, LAN_MSG_DIE, py, pl_tx);
	if (!fn || !lan_frame_decode(fr, fn, &t, &pyp, &pl_rx) || t != LAN_MSG_DIE)
		return fail("DIE frame");
	if (!lan_dec_die(pyp, pl_rx, &dr))
		return fail("lan_dec_die");
	if (dr.die_seq != dt.die_seq || dr.screen_y != dt.screen_y
			|| dr.status != dt.status || dr.keys_lr != dt.keys_lr)
		return fail("DIE field mismatch");

	memset(&at, 0, sizeof at);
	at.room_id = jt.room_id;
	at.spec_version = LAN_SPEC_VERSION;
	at.ack_sender = jt.sender;
	at.for_peer = jt.sender;
	at.for_peer.b[LAN_UUID_BYTES - 1] ^= 0xFF;
	at.die_seq = dt.die_seq;

	pl_tx = lan_enc_die_ack(py, sizeof py, &at);
	if (!pl_tx)
		return fail("lan_enc_die_ack");
	fn = lan_frame_encode(fr, sizeof fr, LAN_MSG_DIE_ACK, py, pl_tx);
	if (!fn || !lan_frame_decode(fr, fn, &t, &pyp, &pl_rx)
			|| t != LAN_MSG_DIE_ACK)
		return fail("DIE_ACK frame");
	if (!lan_dec_die_ack(pyp, pl_rx, &ar))
		return fail("lan_dec_die_ack");
	if (ar.die_seq != at.die_seq || memcmp(ar.for_peer.b, at.for_peer.b,
				LAN_UUID_BYTES) != 0)
		return fail("DIE_ACK field mismatch");

	puts("[lan_jump_die_roundtrip] PASS");
	return 0;
}
