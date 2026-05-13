extern enum GAME_STATE {
	TITLE, INSTRUCTIONS, PLAYING, PAUSE, ESCAPE, GAMEOVER, ENTER_INITIALS,
			EXIT
} game_state;

void start_game(void);
void icytower_sync_game_speed(void);
