#include "physics.h"

extern IT_STATE it_state;

void initialize_game(void);
void game_reset_for_lobby_preview(void);

void press_left(void);
void press_right(void);
void press_jump(void);
void release_left(void);
void release_right(void);
void release_jump(void);

void do_tick(void);

void draw_game(void);
void draw_pause(void);
void draw_escape(void);
void draw_gameover(void);
void draw_enter_initials(const char letters[3], int cursor_index,
		unsigned board_mask);
