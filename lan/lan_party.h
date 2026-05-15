#ifndef LAN_LAN_PARTY_H
#define LAN_LAN_PARTY_H

#include <stdbool.h>
#include <stddef.h>

#include "lan_internal.h"

void lan_party_init(void);
void lan_party_shutdown_all(void);

bool lan_party_busy(void);
void lan_party_enter_browse(void);

void lan_party_tick(void);
void lan_party_draw(void);

void lan_party_key_down(int keycode);
void lan_party_key_up(int keycode);

/*
 * Snapshot of a remote player's avatar received via LAN_MSG_POSE. Kept in
 * world-space doubles so the renderer matches the local IT_STATE units.
 * anim_frame is advanced locally (not from the wire) so each peer's animation
 * cycles independently; on the wire we only carry kinematics + held keys.
 */
typedef struct {
	LanUuid uuid;
	double  x, y;
	double  dx, dy;
	bool    key_left;
	bool    key_right;
	int     anim_frame;
} LanLobbyRemote;

/*
 * Iterate all known remote lobby avatars. Returns the count. The pointer is
 * valid until the next lan_party_tick (snapshot is rebuilt on demand).
 */
size_t lan_party_lobby_remotes(const LanLobbyRemote **out);

#endif
