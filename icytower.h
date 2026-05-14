extern enum GAME_STATE {
	TITLE, INSTRUCTIONS, PLAYING, PAUSE, ESCAPE, GAMEOVER, ENTER_INITIALS,
			EXIT
#ifdef ICYTOWER_LAN
	, LAN_PARTY_BROWSE, LAN_PARTY_LOBBY
#endif
} game_state;

void start_game(void);
void icytower_sync_game_speed(void);
