#ifndef LAN_LAN_PARTY_H
#define LAN_LAN_PARTY_H

#include <stdbool.h>
#include <stddef.h>

#include "lan_internal.h"

void lan_party_init(void);
void lan_party_shutdown_all(void);

bool lan_party_busy(void);
bool lan_party_is_network_game(void);
void lan_party_enter_browse(void);
void lan_party_leave_room_to_browse(void);

void lan_party_tick(void);
void lan_party_draw(void);

void lan_party_key_down(int keycode);
void lan_party_key_up(int keycode);

/*
 * Snapshot of a remote player's avatar received via LAN_MSG_POSE. x/y/dx/dy
 * match the sender's IT_STATE tower units; screen_y is their scroll offset.
 * Draw on the local viewport as y_draw = y - screen_y + local_it_state.screen_y
 * so peers ahead on the tower render above / off the top correctly.
 * anim_frame is advanced locally (not from the wire).
 */
typedef struct {
	LanUuid uuid;
	double  x, y;
	int     screen_y; /* sender scroll; composite draw as y - screen_y + local */
	double  dx, dy;
	bool    key_left;
	bool    key_right;
	int     anim_frame;
	bool    ready; /* last applied READY wire flag == 1 for this peer */
	bool    ghost;
	uint8_t phys_status;
} LanLobbyRemote;

/*
 * Iterate remote puppet snapshots for LAN_PARTY_LOBBY or GAME networking.
 * valid until the next lan_party_tick (snapshot is rebuilt on demand).
 */
size_t lan_party_lobby_remotes(const LanLobbyRemote **out);

/*
 * True when the local player should show the lobby “ready” marker (committed
 * ready, or ready commit in flight with flag 1); never when READY failed.
 */
bool lan_party_lobby_local_ready_marker(void);

/*
 * Lobby READY (LAN_MSG_READY / READY_ACK): commits local ready/unready to the
 * mesh; waits for ACK from every remote roster peer (UDP retransmits).
 */
void lan_party_ready_commit(bool ready);
bool lan_party_ready_pending(void);
bool lan_party_ready_acknowledged(void);

/*
 * Victim-only: announce GAMEOVER death with DIE+DIE_ACK mesh (no blocking).
 */
void lan_party_notify_local_death(void);

#endif
