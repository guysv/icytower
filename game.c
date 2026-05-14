#include <stdio.h>

#include <allegro5/allegro.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_primitives.h>
#include <time.h>

#include "game.h"
#include "icytower.h"
#include "gfx.h"
#include "sfx.h"
#include "options.h"
#include "characters.h"
#include "floor_types.h"
#include "highscores.h"
#include "combo_trail.h"

IT_STATE it_state;
int keys;

enum {
	ANIMATION_IDLE,
	ANIMATION_CHOCK,
	ANIMATION_WALK_LEFT,
	ANIMATION_WALK_RIGHT,
	ANIMATION_FLY,
	ANIMATION_FLY_LEFT,
	ANIMATION_FLY_RIGHT,
	ANIMATION_ROTATE
} animation;

int animation_frame;

void initialize_game(void) {
	init_state(&it_state, rejump, time(NULL));
	keys = 0;
	animation = ANIMATION_IDLE;
	animation_frame = 0;
	combo_trail_init();
}

void game_reset_for_lobby_preview(void)
{
	keys = 0;
	animation = ANIMATION_IDLE;
	animation_frame = 0;
	combo_trail_init();
}

void press_left(void) { keys |= KEY_LEFT; }
void press_right(void) { keys |= KEY_RIGHT; }
void press_jump(void) { keys |= KEY_JUMP; }
void release_left(void) { keys &= ~KEY_LEFT; }
void release_right(void) { keys &= ~KEY_RIGHT; }
void release_jump(void) { keys &= ~KEY_JUMP; }

void do_tick(void) {
	int prev_status = it_state.status;
	int prev_floor = it_state.floor;
	int prev_combo_timer = it_state.combo_timer;
	if (!play_frame(&it_state, keys)) {
		sfx_play_sample(characters[character_index].sfx.death,
				volume_sfx / 10.0f, ALLEGRO_PLAYMODE_ONCE);
		sfx_play_sample(sample_gameover, volume_sfx / 10.0f,
				ALLEGRO_PLAYMODE_ONCE);
		combo_trail_kill();
		game_state = GAMEOVER;
		return;
	}

	if (it_state.speed_counter == 1500) {
		sfx_play_sample(sample_ring, volume_sfx / 10.0f,
				ALLEGRO_PLAYMODE_ONCE);
		sfx_play_sample(sample_hurryup, volume_sfx / 10.0f,
				ALLEGRO_PLAYMODE_ONCE);
	}

	if (it_state.floor <= 1000 ? it_state.floor / 50 > prev_floor / 50 :
			it_state.floor / 500 > prev_floor / 500) {
		int milestone;

		sfx_play_sample(sample_aight, volume_sfx / 10.0f,
				ALLEGRO_PLAYMODE_ONCE);
		milestone = it_state.floor <= 1000
				? (it_state.floor / 50) * 50
				: (it_state.floor / 500) * 500;
		combo_trail_milestone_burst(&it_state, milestone);
	}

	if (prev_combo_timer > 0 && it_state.combo_timer == 0
			&& it_state.combo_count > 1) {
		if (it_state.combo_floor >= 200)
			sfx_play_sample(sample_unbelievable, volume_sfx / 10.0f,
					ALLEGRO_PLAYMODE_ONCE);
		else if (it_state.combo_floor >= 140)
			sfx_play_sample(sample_splendid, volume_sfx / 10.0f,
					ALLEGRO_PLAYMODE_ONCE);
		else if (it_state.combo_floor >= 100)
			sfx_play_sample(sample_fantastic, volume_sfx / 10.0f,
					ALLEGRO_PLAYMODE_ONCE);
		else if (it_state.combo_floor >= 70)
			sfx_play_sample(sample_extreme, volume_sfx / 10.0f,
					ALLEGRO_PLAYMODE_ONCE);
		else if (it_state.combo_floor >= 50)
			sfx_play_sample(sample_amazing, volume_sfx / 10.0f,
					ALLEGRO_PLAYMODE_ONCE);
		else if (it_state.combo_floor >= 35)
			sfx_play_sample(sample_wow, volume_sfx / 10.0f,
					ALLEGRO_PLAYMODE_ONCE);
		else if (it_state.combo_floor >= 25)
			sfx_play_sample(sample_super, volume_sfx / 10.0f,
					ALLEGRO_PLAYMODE_ONCE);
		else if (it_state.combo_floor >= 15)
			sfx_play_sample(sample_great, volume_sfx / 10.0f,
					ALLEGRO_PLAYMODE_ONCE);
		else if (it_state.combo_floor >= 7)
			sfx_play_sample(sample_sweet, volume_sfx / 10.0f,
					ALLEGRO_PLAYMODE_ONCE);
		else
			sfx_play_sample(sample_good, volume_sfx / 10.0f,
					ALLEGRO_PLAYMODE_ONCE);
	}

	switch (it_state.status) {
	case STATUS_IDLE:
		if (it_state.dx > 0.045) {
			if (animation != ANIMATION_WALK_RIGHT) {
				animation = ANIMATION_WALK_RIGHT;
				animation_frame = 0;
			}
		} else if (it_state.dx < -0.045) {
			if (animation != ANIMATION_WALK_LEFT) {
				animation = ANIMATION_WALK_LEFT;
				animation_frame = 0;
			}
		} else if (it_state.y >= 406 && it_state.speed > 0) {
			if (animation != ANIMATION_CHOCK) {
				animation = ANIMATION_CHOCK;
				animation_frame = 0;
			}
		} else {
			if (animation != ANIMATION_IDLE) {
				animation = ANIMATION_IDLE;
				animation_frame = 0;
			}
		}
		if (prev_status != STATUS_IDLE)
			sfx_play_sample(sample_step, volume_sfx / 10.0f,
					ALLEGRO_PLAYMODE_ONCE);
		break;
	case STATUS_FLY_UP:
		if (prev_status == STATUS_IDLE) {
			if (it_state.dy < -22.2)
				sfx_play_sample(characters[character_index].sfx.jumphi,
						volume_sfx / 10.0f,
						ALLEGRO_PLAYMODE_ONCE);
			else if (it_state.dy < -15.6)
				sfx_play_sample(characters[character_index].sfx.jumpmed,
						volume_sfx / 10.0f,
						ALLEGRO_PLAYMODE_ONCE);
			else
				sfx_play_sample(characters[character_index].sfx.jumplo,
						volume_sfx / 10.0f,
						ALLEGRO_PLAYMODE_ONCE);
		}
	case STATUS_FLY_IDLE:
	case STATUS_FLY_DOWN:
		if (animation == ANIMATION_ROTATE)
			break;
		if (it_state.dy < -22.2) {
			animation = ANIMATION_ROTATE;
			animation_frame = 0;
		} else if (it_state.dx > 0.045) {
			if (animation != ANIMATION_FLY_RIGHT) {
				animation = ANIMATION_FLY_RIGHT;
				animation_frame = 0;
			}
		} else if (it_state.dx < -0.045) {
			if (animation != ANIMATION_FLY_LEFT) {
				animation = ANIMATION_FLY_LEFT;
				animation_frame = 0;
			}
		} else {
			if (animation != ANIMATION_FLY &&
					animation != ANIMATION_FLY_LEFT &&
					animation != ANIMATION_FLY_RIGHT) {
				animation = ANIMATION_FLY;
				animation_frame = 0;
			}
		}
		break;
	}
	++animation_frame;
	combo_trail_tick(&it_state, animation == ANIMATION_ROTATE);
}

void draw_background(void) {
	al_clear_to_color(al_map_rgb(0, 0, 0));
}

void draw_floor(FLOOR *floor, unsigned int n) {
	int x, y = it_state.screen_y + 426 - 80 * n;
	unsigned int index = start_floor + n / 100;
	const struct floor_type *type;
	if (index >= floor_types_count)
		index = floor_types_count - 1;
	type = &floor_types[index];
	x = (floor->start + 1) * 16;
	al_draw_bitmap(type->left,
			x - al_get_bitmap_width(type->left), y, 0);
	while (x < floor->end * 16) {
		al_draw_bitmap(type->mid, x, y, 0);
		x += 16;
	}
	al_draw_bitmap(type->right, x, y, 0);
	if (n > 0 && n % 10 == 0) {
		x = (floor->start + floor->end) * 8;
		y += 16;
		al_draw_bitmap(type->sign, x, y, 0);
		x += al_get_bitmap_width(type->sign) / 2;
		y += 6;
		al_draw_textf(font_native, al_map_rgb(0, 0, 0), x - 1, y,
				ALLEGRO_ALIGN_CENTRE, "%u", n);
		al_draw_textf(font_native, al_map_rgb(0, 0, 0), x + 1, y,
				ALLEGRO_ALIGN_CENTRE, "%u", n);
		al_draw_textf(font_native, al_map_rgb(0, 0, 0), x, y - 1,
				ALLEGRO_ALIGN_CENTRE, "%u", n);
		al_draw_textf(font_native, al_map_rgb(0, 0, 0), x, y + 1,
				ALLEGRO_ALIGN_CENTRE, "%u", n);
		al_draw_textf(font_native, al_map_rgb(255, 255, 255), x, y,
				ALLEGRO_ALIGN_CENTRE, "%u", n);
	}
}

void draw_floors(void) {
	unsigned int i;
	FLOORS *floors = &it_state.floors;
	for (i = 1; i <= floors->count; ++i) {
		if (i > 7)
			break;
		draw_floor(&floors->floor[(floors->start - i + 7) % 7],
				floors->count - i);
	}
}

void draw_character(void) {
	ALLEGRO_BITMAP *character;
	int width, height;
	switch (animation) {
	case ANIMATION_IDLE:
		switch ((animation_frame / 13) % 4) {
		case 0:
		case 2:
			character = characters[character_index].gfx.idle1;
			break;
		case 1:
			character = characters[character_index].gfx.idle2;
			break;
		case 3:
			character = characters[character_index].gfx.idle3;
			break;
		}
		width = al_get_bitmap_width(character);
		height = al_get_bitmap_height(character);
		al_draw_bitmap(character,
				it_state.x - width / 2 + 1,
				it_state.y - height + 1,
				0);
		break;
	case ANIMATION_CHOCK:
		character = characters[character_index].gfx.chock;
		width = al_get_bitmap_width(character);
		height = al_get_bitmap_height(character);
		al_draw_bitmap(character,
				it_state.x - width / 2 + 1,
				it_state.y - height + 1,
				0);
		break;
	case ANIMATION_WALK_LEFT:
		switch ((animation_frame / 10) % 4) {
		case 0:
			character = characters[character_index].gfx.walk1;
			break;
		case 1:
			character = characters[character_index].gfx.walk2;
			break;
		case 2:
			character = characters[character_index].gfx.walk3;
			break;
		case 3:
			character = characters[character_index].gfx.walk4;
			break;
		}
		width = al_get_bitmap_width(character);
		height = al_get_bitmap_height(character);
		al_draw_bitmap(character,
				it_state.x - width / 2 + 1,
				it_state.y - height + 1,
				ALLEGRO_FLIP_HORIZONTAL);
		break;
	case ANIMATION_WALK_RIGHT:
		switch ((animation_frame / 10) % 4) {
		case 0:
			character = characters[character_index].gfx.walk1;
			break;
		case 1:
			character = characters[character_index].gfx.walk2;
			break;
		case 2:
			character = characters[character_index].gfx.walk3;
			break;
		case 3:
			character = characters[character_index].gfx.walk4;
			break;
		}
		width = al_get_bitmap_width(character);
		height = al_get_bitmap_height(character);
		al_draw_bitmap(character,
				it_state.x - width / 2 + 1,
				it_state.y - height + 1,
				0);
		break;
	case ANIMATION_FLY:
		character = characters[character_index].gfx.jump;
		width = al_get_bitmap_width(character);
		height = al_get_bitmap_height(character);
		al_draw_bitmap(character,
				it_state.x - width / 2 + 1,
				it_state.y - height + 1,
				0);
		break;
	case ANIMATION_FLY_LEFT:
		switch (it_state.status) {
		case STATUS_FLY_UP:
			character = characters[character_index].gfx.jump1;
			break;
		case STATUS_FLY_IDLE:
			character = characters[character_index].gfx.jump2;
			break;
		case STATUS_FLY_DOWN:
			character = characters[character_index].gfx.jump3;
			break;
		}
		width = al_get_bitmap_width(character);
		height = al_get_bitmap_height(character);
		al_draw_bitmap(character,
				it_state.x - width / 2 + 1,
				it_state.y - height + 1,
				ALLEGRO_FLIP_HORIZONTAL);
		break;
	case ANIMATION_FLY_RIGHT:
		switch (it_state.status) {
		case STATUS_FLY_UP:
			character = characters[character_index].gfx.jump1;
			break;
		case STATUS_FLY_IDLE:
			character = characters[character_index].gfx.jump2;
			break;
		case STATUS_FLY_DOWN:
			character = characters[character_index].gfx.jump3;
			break;
		}
		width = al_get_bitmap_width(character);
		height = al_get_bitmap_height(character);
		al_draw_bitmap(character,
				it_state.x - width / 2 + 1,
				it_state.y - height + 1,
				0);
		break;
	case ANIMATION_ROTATE:
		character = characters[character_index].gfx.rotate;
		width = al_get_bitmap_width(character);
		height = al_get_bitmap_height(character);
		al_draw_rotated_bitmap(character,
				width / 2, height / 2,
				it_state.x, it_state.y - height / 2,
				ALLEGRO_PI * (animation_frame % 30) / 15.0, 0);
		break;
	}
}

void draw_walls(void) {
	int y = -((80 - it_state.screen_y % 80) % 80) * 1.55;
	for (; y < 480; y += 124) {
		al_draw_bitmap(bitmap_sideblock, 640 - 75, y, 0);
		al_draw_bitmap(bitmap_sideblock,
				75 - al_get_bitmap_width(bitmap_sideblock), y,
				ALLEGRO_FLIP_HORIZONTAL);
	}
}

void draw_hud(void) {
	static unsigned int combo_timeout_count = 0;
	al_draw_bitmap(bitmap_clock, 4, 10, 0);
	al_draw_rotated_bitmap(bitmap_clock_hand, 8, 29, 40, 57,
			ALLEGRO_PI * it_state.speed_counter / 750.0, 0);

	al_draw_textf(font_color, al_map_rgb(255, 255, 255),
			8, 440, 0, "SCORE: %d",
			it_state.floor * 10 + it_state.score);

	al_draw_bitmap(bitmap_combo_meter, 20, 100, 0);
	if (it_state.combo_timer > 0) {
		al_draw_bitmap_region(bitmap_combo_liquid,
				0, 100 - it_state.combo_timer,
				16, it_state.combo_timer,
				31, 219 - it_state.combo_timer, 0);
		al_draw_bitmap(bitmap_combo_count, -10, 210, 0);
		al_draw_textf(font_color, al_map_rgb(255, 255, 255),
				40, 214, ALLEGRO_ALIGN_CENTRE,
				"%u", it_state.combo_floor);
		combo_timeout_count = 0;
	} else if (it_state.combo_count > 1) {
		if (combo_timeout_count++ < 75) {
			al_draw_bitmap(bitmap_combo_count, -10, 210, 0);
			al_draw_textf(font_color, al_map_rgb(255, 255, 255),
					40, 214, ALLEGRO_ALIGN_CENTRE,
					"%u", it_state.combo_floor);
		}
	}
}

void draw_game(void) {
	if (fullscreen)
		al_clear_to_color(al_map_rgb(20, 20, 20));
	draw_background();
	draw_floors();
	combo_trail_draw((unsigned)animation_frame);
	draw_character();
	draw_walls();
	draw_hud();
}

void draw_grid(void) {
}

void draw_pause(void) {
	draw_game();
	draw_grid();
	al_draw_text(font_color, al_map_rgb(255, 255, 255),
			190, 160, 0, "GAME PAUSED");
	al_draw_text(font_mono, al_map_rgb(255, 255, 255),
			132, 210, 0, "PRESS ANY KEY TO RESUME");
}

void draw_escape(void) {
	draw_game();
	draw_grid();
	al_draw_text(font_color, al_map_rgb(255, 255, 255),
			23, 160, 0, "DO YOU REALLY WANT TO EXIT?");
	al_draw_text(font_mono, al_map_rgb(255, 255, 255),
			132, 210, 0, "PRESS ANY KEY TO RESUME");
	al_draw_text(font_mono, al_map_rgb(255, 255, 255),
			190, 240, 0, "PRESS ESC TO EXIT");
}

void draw_gameover(void)
{
	draw_game();
	al_draw_bitmap(bitmap_gameover, 96, 175, 0);
	al_draw_text(font_color, al_map_rgb(255, 255, 255),
			140, 275, 0, "SCORE:");
	al_draw_textf(font_color, al_map_rgb(255, 255, 255),
			500, 275, ALLEGRO_ALIGN_RIGHT,
			"%u", it_state.floor * 10 + it_state.score);
	al_draw_text(font_color, al_map_rgb(255, 255, 255),
			140, 315, 0, "LEVEL:");
	al_draw_textf(font_color, al_map_rgb(255, 255, 255),
			500, 315, ALLEGRO_ALIGN_RIGHT,
			"%u", it_state.floor);
	al_draw_text(font_color, al_map_rgb(255, 255, 255),
			140, 355, 0, "BEST COMBO:");
	al_draw_textf(font_color, al_map_rgb(255, 255, 255),
			500, 355, ALLEGRO_ALIGN_RIGHT,
			"%u", it_state.combo);
}

void draw_enter_initials(const char letters[3], int cursor_index,
		unsigned board_mask)
{
	static const char *const titles[HIGHSCORE_LEADER_COUNT] = {
		"BEST FLOOR",
		"BEST SCORE",
		"BEST COMBO",
	};
	char cat[112];
	size_t ci;
	char buf[2];
	int bmp_x = 96;
	int bmp_y = 175;
	int text_x = bmp_x + al_get_bitmap_width(bitmap_highscore) / 2;
	int text_y = bmp_y + al_get_bitmap_height(bitmap_highscore) + 6;
	int leader;
	bool first = true;
	int i;
	const int initials_x = 280;

	draw_game();
	al_draw_bitmap(bitmap_highscore, bmp_x, bmp_y, 0);
	cat[0] = '\0';
	ci = 0;
	for (leader = 0; leader < (int)HIGHSCORE_LEADER_COUNT;
			++leader) {
		size_t avail;
		int n;
		HighscoreLeader lb = (HighscoreLeader)leader;

		if (!(board_mask & HIGHSCORE_BOARD_MASK(lb)))
			continue;
		if (!first && ci + 2 < sizeof(cat)) {
			cat[ci++] = ',';
			cat[ci++] = ' ';
			cat[ci] = '\0';
		}
		first = false;
		avail = sizeof(cat) - ci;
		n = snprintf(cat + ci, avail, "%s", titles[leader]);
		if (n < 0 || (size_t)n >= avail)
			break;
		ci += (size_t)n;
	}
	if (!first)
		al_draw_text(font_mono, al_map_rgb(255, 255, 255),
				text_x, text_y, ALLEGRO_ALIGN_CENTRE, cat);

	buf[1] = '\0';
	for (i = 0; i < 3; ++i) {
		int x = initials_x + i * 36;

		buf[0] = letters[i];
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				x, 392, 0, buf);
		if (i == cursor_index)
			al_draw_line(x, 422, x + 22, 422,
					al_map_rgb(255, 255, 255), 2.0f);
	}
}
