/*
 * Shared constants for LAN Phase 1 (connectivity). Wire format aligns with v1 spec;
 * LAN_SPEC_VERSION bumps when payloads change.
 */

#ifndef LAN_LAN_INTERNAL_H
#define LAN_LAN_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAN_SPEC_VERSION            1u

#define LAN_DEFAULT_PORT            51812u /* discovery + legacy default */
#define LAN_MULTICAST_GROUP         "239.43.137.251"
#define LAN_PORT_ENV                "ICYTOWER_LAN_PORT"
#define LAN_DISCOVERY_PORT_ENV      "ICYTOWER_LAN_DISCOVERY_PORT"
#define LAN_GAME_PORT_ENV           "ICYTOWER_LAN_GAME_PORT"
#define LAN_PEERS_ENV               "ICYTOWER_LAN_PEERS"
#define LAN_NAME_ENV                "ICYTOWER_LAN_NAME"

/* Bonjour / DNS-SD (see lan_mdns.c). */
#define LAN_MDNS_REGTYPE            "_icytower._udp"

#define LAN_MAX_PEERS               8
#define LAN_NAME_LEN                16
#define LAN_ROOM_LEN                24
#define LAN_UUID_BYTES              16

#define LAN_ADVERT_PERIOD_MS        900u
#define LAN_HELLO_GOSSIP_MS        280u

#define LAN_FRAME_MAGIC0            'I'
#define LAN_FRAME_MAGIC1            'T'
#define LAN_FRAME_MAGIC2            'W'
#define LAN_FRAME_MAGIC3            '1'
#define LAN_FRAME_HEADER_LEN        8
#define LAN_FRAME_CRC_LEN           4
#define LAN_FRAME_MAX_PAYLOAD       1024

typedef enum {
	LAN_MSG_SESSION_ADVERT      = 0x00,
	LAN_MSG_HELLO               = 0x01,
	LAN_MSG_ROSTER_SNAPSHOT     = 0x02,
	LAN_MSG_APPEARANCE          = 0x03,
	LAN_MSG_LOBBY_READY         = 0x04,
	LAN_MSG_LOBBY_PHASE_NONCE   = 0x05,
	LAN_MSG_JOIN_NONCE          = 0x06,
	LAN_MSG_COUNTDOWN_ANCHOR    = 0x07,
	LAN_MSG_SYNC_KEYS           = 0x08,
	LAN_MSG_CONTROL_EDGE        = 0x09,
	LAN_MSG_SCORE_TELEMETRY     = 0x0A,
	LAN_MSG_ELIMINATE           = 0x0B,
	LAN_MSG_LEAVE               = 0x0C,
	/* Brief connectivity check (unreliable); game UDP channel only. */
	LAN_MSG_PARTY_ACK           = 0x0D,
	LAN_MSG_REL_ACK             = 0x80
} LanMsgType;

typedef enum {
	LAN_PARTY_PHASE_NONE       = 0,
	LAN_PARTY_PHASE_BROWSE     = 1,
	LAN_PARTY_PHASE_LOBBY      = 2
} LanPartyPhase;

typedef uint8_t LanPlayerId;

typedef struct {
	uint8_t b[LAN_UUID_BYTES];
} LanUuid;

#endif /* LAN_LAN_INTERNAL_H */
