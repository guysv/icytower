/*
 * Roundtrip LAN_MSG_READY and LAN_MSG_READY_ACK payloads through ITW1 framing.
 */
#include <stdio.h>
#include <string.h>

#include "lan_internal.h"
#include "lan_msg.h"

static int fail(const char *what)
{
	fprintf(stderr, "[lan_ready_roundtrip] FAIL: %s\n", what);
	return 1;
}

int main(void)
{
	LanMsgReady rtx, rrx;
	LanMsgReadyAck atx, arx;
	uint8_t py[64];
	uint8_t fr[LAN_FRAME_HEADER_LEN + 64 + LAN_FRAME_CRC_LEN];
	size_t pl_tx, pl_rx, fn;
	LanMsgType t;
	const uint8_t *pyp;
	int i;

	if (LAN_SPEC_VERSION < 10u)
		return fail("LAN_SPEC_VERSION < 10 (READY messages)");

	memset(&rtx, 0, sizeof rtx);
	rtx.room_id = 0xfeedface12345678ull;
	rtx.spec_version = LAN_SPEC_VERSION;
	for (i = 0; i < LAN_UUID_BYTES; ++i)
		rtx.sender.b[i] = (uint8_t)(0xC0 + i);
	rtx.seq = 0x11223344u;
	rtx.ready = 1u;

	pl_tx = lan_enc_ready(py, sizeof py, &rtx);
	if (!pl_tx)
		return fail("lan_enc_ready returned 0");

	fn = lan_frame_encode(fr, sizeof fr, LAN_MSG_READY, py, pl_tx);
	if (!fn)
		return fail("lan_frame_encode READY returned 0");

	if (!lan_frame_decode(fr, fn, &t, &pyp, &pl_rx))
		return fail("lan_frame_decode rejected READY frame");
	if (t != LAN_MSG_READY)
		return fail("READY frame type mismatch");
	if (!lan_dec_ready(pyp, pl_rx, &rrx))
		return fail("lan_dec_ready rejected payload");

	if (rrx.room_id != rtx.room_id)
		return fail("READY room_id mismatch");
	if (rrx.spec_version != rtx.spec_version)
		return fail("READY spec_version mismatch");
	if (memcmp(rrx.sender.b, rtx.sender.b, LAN_UUID_BYTES) != 0)
		return fail("READY sender mismatch");
	if (rrx.seq != rtx.seq)
		return fail("READY seq mismatch");
	if (rrx.ready != rtx.ready)
		return fail("READY flag mismatch");

	memset(&atx, 0, sizeof atx);
	atx.room_id = rtx.room_id;
	atx.spec_version = LAN_SPEC_VERSION;
	for (i = 0; i < LAN_UUID_BYTES; ++i)
		atx.ack_sender.b[i] = (uint8_t)(0xD0 + i);
	atx.for_peer = rtx.sender;
	atx.seq = rtx.seq;

	pl_tx = lan_enc_ready_ack(py, sizeof py, &atx);
	if (!pl_tx)
		return fail("lan_enc_ready_ack returned 0");

	fn = lan_frame_encode(fr, sizeof fr, LAN_MSG_READY_ACK, py, pl_tx);
	if (!fn)
		return fail("lan_frame_encode READY_ACK returned 0");

	if (!lan_frame_decode(fr, fn, &t, &pyp, &pl_rx))
		return fail("lan_frame_decode rejected READY_ACK frame");
	if (t != LAN_MSG_READY_ACK)
		return fail("READY_ACK frame type mismatch");
	if (!lan_dec_ready_ack(pyp, pl_rx, &arx))
		return fail("lan_dec_ready_ack rejected payload");

	if (arx.room_id != atx.room_id)
		return fail("READY_ACK room_id mismatch");
	if (arx.spec_version != atx.spec_version)
		return fail("READY_ACK spec_version mismatch");
	if (memcmp(arx.ack_sender.b, atx.ack_sender.b, LAN_UUID_BYTES) != 0)
		return fail("READY_ACK ack_sender mismatch");
	if (memcmp(arx.for_peer.b, atx.for_peer.b, LAN_UUID_BYTES) != 0)
		return fail("READY_ACK for_peer mismatch");
	if (arx.seq != atx.seq)
		return fail("READY_ACK seq mismatch");

	puts("[lan_ready_roundtrip] PASS");
	return 0;
}
