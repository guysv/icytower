/*
 * Roundtrip LAN_MSG_STEADY and LAN_MSG_STEADY_ACK payloads through ITW1 framing.
 */
#include <stdio.h>
#include <string.h>

#include "lan_internal.h"
#include "lan_msg.h"

static int fail(const char *what)
{
	fprintf(stderr, "[lan_steady_roundtrip] FAIL: %s\n", what);
	return 1;
}

int main(void)
{
	LanMsgSteady rtx, rrx;
	LanMsgSteadyAck atx, arx;
	uint8_t py[64];
	uint8_t fr[LAN_FRAME_HEADER_LEN + 64 + LAN_FRAME_CRC_LEN];
	size_t pl_tx, pl_rx, fn;
	LanMsgType t;
	const uint8_t *pyp;
	int i;

	if (LAN_SPEC_VERSION < 11u)
		return fail("LAN_SPEC_VERSION < 11 (STEADY messages)");

	memset(&rtx, 0, sizeof rtx);
	rtx.room_id = 0xfeedface12345678ull;
	rtx.spec_version = LAN_SPEC_VERSION;
	for (i = 0; i < LAN_UUID_BYTES; ++i)
		rtx.sender.b[i] = (uint8_t)(0xE0 + i);
	rtx.steady_seq = 0x11223344u;
	rtx.countdown_ms = 3000u;

	pl_tx = lan_enc_steady(py, sizeof py, &rtx);
	if (!pl_tx)
		return fail("lan_enc_steady returned 0");

	fn = lan_frame_encode(fr, sizeof fr, LAN_MSG_STEADY, py, pl_tx);
	if (!fn)
		return fail("lan_frame_encode STEADY returned 0");

	if (!lan_frame_decode(fr, fn, &t, &pyp, &pl_rx))
		return fail("lan_frame_decode rejected STEADY frame");
	if (t != LAN_MSG_STEADY)
		return fail("STEADY frame type mismatch");
	if (!lan_dec_steady(pyp, pl_rx, &rrx))
		return fail("lan_dec_steady rejected payload");

	if (rrx.room_id != rtx.room_id)
		return fail("STEADY room_id mismatch");
	if (rrx.spec_version != rtx.spec_version)
		return fail("STEADY spec_version mismatch");
	if (memcmp(rrx.sender.b, rtx.sender.b, LAN_UUID_BYTES) != 0)
		return fail("STEADY sender mismatch");
	if (rrx.steady_seq != rtx.steady_seq)
		return fail("STEADY steady_seq mismatch");
	if (rrx.countdown_ms != rtx.countdown_ms)
		return fail("STEADY countdown_ms mismatch");

	memset(&atx, 0, sizeof atx);
	atx.room_id = rtx.room_id;
	atx.spec_version = LAN_SPEC_VERSION;
	for (i = 0; i < LAN_UUID_BYTES; ++i)
		atx.ack_sender.b[i] = (uint8_t)(0xF0 + i);
	atx.for_peer = rtx.sender;
	atx.steady_seq = rtx.steady_seq;

	pl_tx = lan_enc_steady_ack(py, sizeof py, &atx);
	if (!pl_tx)
		return fail("lan_enc_steady_ack returned 0");

	fn = lan_frame_encode(fr, sizeof fr, LAN_MSG_STEADY_ACK, py, pl_tx);
	if (!fn)
		return fail("lan_frame_encode STEADY_ACK returned 0");

	if (!lan_frame_decode(fr, fn, &t, &pyp, &pl_rx))
		return fail("lan_frame_decode rejected STEADY_ACK frame");
	if (t != LAN_MSG_STEADY_ACK)
		return fail("STEADY_ACK frame type mismatch");
	if (!lan_dec_steady_ack(pyp, pl_rx, &arx))
		return fail("lan_dec_steady_ack rejected payload");

	if (arx.room_id != atx.room_id)
		return fail("STEADY_ACK room_id mismatch");
	if (arx.spec_version != atx.spec_version)
		return fail("STEADY_ACK spec_version mismatch");
	if (memcmp(arx.ack_sender.b, atx.ack_sender.b, LAN_UUID_BYTES) != 0)
		return fail("STEADY_ACK ack_sender mismatch");
	if (memcmp(arx.for_peer.b, atx.for_peer.b, LAN_UUID_BYTES) != 0)
		return fail("STEADY_ACK for_peer mismatch");
	if (arx.steady_seq != atx.steady_seq)
		return fail("STEADY_ACK steady_seq mismatch");

	puts("[lan_steady_roundtrip] PASS");
	return 0;
}
