/*
 * Standalone roundtrip test for LAN_MSG_POSE: encode payload, wrap in
 * an ITW1 frame, decode the frame back, decode the payload, and verify every
 * field. Also asserts that LAN_SPEC_VERSION matches what the pose path
 * was bumped to 9 when client_time_ms was added.
 */
#include <stdio.h>
#include <string.h>

#include "lan_internal.h"
#include "lan_msg.h"

static int fail(const char *what)
{
	fprintf(stderr, "[lan_pose_roundtrip] FAIL: %s\n", what);
	return 1;
}

int main(void)
{
	LanMsgPose tx, rx;
	uint8_t py[64];
	uint8_t fr[LAN_FRAME_HEADER_LEN + 64 + LAN_FRAME_CRC_LEN];
	size_t pl_tx, pl_rx, fn;
	LanMsgType t;
	const uint8_t *pyp;
	int i;

	if (LAN_SPEC_VERSION < 12u)
		return fail("LAN_SPEC_VERSION expected >= 12 (POSE screen_y)");

	memset(&tx, 0, sizeof tx);
	tx.seq          = 0xdeadbeefu;
	tx.room_id      = 0xcafebeef12345678ull;
	tx.spec_version = LAN_SPEC_VERSION;
	for (i = 0; i < LAN_UUID_BYTES; ++i)
		tx.sender.b[i] = (uint8_t)(0xA0 + i);
	tx.x_fp  =  2000000;
	tx.y_fp  = -3500000;
	tx.dx_fp =  -42500;
	tx.dy_fp =       0;
	tx.keys_lr = LAN_POSE_KEY_LEFT_BIT | LAN_POSE_KEY_RIGHT_BIT;
	tx.screen_y = -42000;
	tx.client_time_ms = 0x12345678u;

	pl_tx = lan_enc_pose(py, sizeof py, &tx);
	if (!pl_tx)
		return fail("lan_enc_pose returned 0");

	fn = lan_frame_encode(fr, sizeof fr, LAN_MSG_POSE, py, pl_tx);
	if (!fn)
		return fail("lan_frame_encode returned 0");

	if (!lan_frame_decode(fr, fn, &t, &pyp, &pl_rx))
		return fail("lan_frame_decode rejected the frame (CRC?)");
	if (t != LAN_MSG_POSE)
		return fail("frame type mismatch on decode");
	if (pl_rx != pl_tx)
		return fail("payload length mismatch on decode");

	if (!lan_dec_pose(pyp, pl_rx, &rx))
		return fail("lan_dec_pose rejected payload");

	if (rx.seq != tx.seq)               return fail("seq mismatch");
	if (rx.room_id != tx.room_id)       return fail("room_id mismatch");
	if (rx.spec_version != tx.spec_version)
		return fail("spec_version mismatch");
	if (memcmp(rx.sender.b, tx.sender.b, LAN_UUID_BYTES) != 0)
		return fail("sender uuid mismatch");
	if (rx.x_fp  != tx.x_fp)  return fail("x_fp mismatch");
	if (rx.y_fp  != tx.y_fp)  return fail("y_fp mismatch");
	if (rx.dx_fp != tx.dx_fp) return fail("dx_fp mismatch");
	if (rx.dy_fp != tx.dy_fp) return fail("dy_fp mismatch");
	if (rx.keys_lr != tx.keys_lr) return fail("keys_lr mismatch");
	if (rx.screen_y != tx.screen_y) return fail("screen_y mismatch");
	if (rx.client_time_ms != tx.client_time_ms)
		return fail("client_time_ms mismatch");

	puts("[lan_pose_roundtrip] PASS (POSE encode/decode/CRC)");
	return 0;
}
