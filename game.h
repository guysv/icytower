#include <stdbool.h>

#include "physics.h"

extern IT_STATE it_state;

void initialize_game(void);
void game_reset_for_lobby_preview(void);
/*
 * Used after play_begin_match_keep_pose: reset walk animation + combo trail for
 * a fresh ladder match without clearing inputs (keyboard holds do not replay
 * Allegro KEY_DOWN until release).
 */
void game_reset_match_visual_only(void);
/*
 * Lobby physics + animation tick for the local avatar. Driven from
 * lan_party_tick at the same 50 Hz cadence as PLAYING. Jump is suppressed and
 * scoring/scroll are skipped; the avatar roams floor 0 only.
 */
void game_lobby_tick(int keys_in);

void press_left(void);
void press_right(void);
void press_jump(void);
void release_left(void);
void release_right(void);
void release_jump(void);

int game_current_keys(void);

void do_tick(void);

void draw_game(void);
void draw_pause(void);
void draw_escape(void);
void draw_gameover(void);
void draw_enter_initials(const char letters[3], int cursor_index,
		unsigned board_mask);

/*
 * Render a single lobby avatar (local or remote puppet) using the local
 * character_index bitmaps. Animation is selected from horizontal velocity and
 * a frame counter; remotes do not need fly/rotate states.
 */
void draw_lobby_avatar(double x, double y, double dx, int anim_frame,
		bool walking_left_held, bool walking_right_held);
