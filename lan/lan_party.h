#ifndef LAN_LAN_PARTY_H
#define LAN_LAN_PARTY_H

#include <stdbool.h>

void lan_party_init(void);
void lan_party_shutdown_all(void);

bool lan_party_busy(void);
void lan_party_enter_browse(void);

void lan_party_tick(void);
void lan_party_draw(void);

void lan_party_key_down(int keycode);
void lan_party_key_up(int keycode);

#endif
