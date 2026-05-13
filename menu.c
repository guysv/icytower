#include <allegro5/allegro.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_font.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#include "menu.h"
#include "icytower.h"
#include "gfx.h"
#include "sfx.h"
#include "options.h"
#include "characters.h"
#include "floor_types.h"
#include "fullscreen.h"
#include "highscores.h"

enum {
	MAIN_MENU, OPTIONS, GAME_OPTIONS, GFX_OPTIONS, SOUND_OPTIONS, CONTROLS
} menu_page = MAIN_MENU;

unsigned int menu_bullet = 0;

void menu_up(void) {
	al_play_sample(sample_menu_choose, volume_sfx / 10.0, 0, 1,
			ALLEGRO_PLAYMODE_ONCE, NULL);
	if (menu_bullet != 0)
		menu_bullet -= 1;
	else switch (menu_page) {
	case MAIN_MENU:
	case OPTIONS:
		menu_bullet = 4;
		break;
	case GAME_OPTIONS:
	case GFX_OPTIONS:
	case SOUND_OPTIONS:
		menu_bullet = 2;
		break;
	case CONTROLS:
		menu_bullet = 5;
		break;
	}
}

void menu_down(void) {
	al_play_sample(sample_menu_choose, volume_sfx / 10.0, 0, 1,
			ALLEGRO_PLAYMODE_ONCE, NULL);
	menu_bullet += 1;
	switch (menu_page) {
	case MAIN_MENU:
	case OPTIONS:
		menu_bullet %= 5;
		break;
	case GAME_OPTIONS:
	case GFX_OPTIONS:
	case SOUND_OPTIONS:
		menu_bullet %= 3;
		break;
	case CONTROLS:
		menu_bullet %= 6;
		break;
	}
}

void menu_enter(void) {
	switch (menu_page) {
	case MAIN_MENU:
		switch (menu_bullet) {
		case 0:
			/* START GAME */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			game_state = PLAYING;
			start_game();
			break;
		case 1:
			/* LOAD REPLAY */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			printf("Loading replays is not implemented yet\n");
			break;
		case 2:
			/* INSTRUCTIONS */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			game_state = INSTRUCTIONS;
			break;
		case 3:
			/* OPTIONS */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = OPTIONS;
			menu_bullet = 0;
			break;
		case 4:
			/* EXIT */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			game_state = EXIT;
			break;
		}
		break;
	case OPTIONS:
		switch (menu_bullet) {
		case 0:
			/* GAME OPTIONS */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = GAME_OPTIONS;
			menu_bullet = 0;
			break;
		case 1:
			/* GFX OPTIONS */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = GFX_OPTIONS;
			menu_bullet = 0;
			break;
		case 2:
			/* SOUND OPTIONS */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = SOUND_OPTIONS;
			menu_bullet = 0;
			break;
		case 3:
			/* CONTROLS */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = CONTROLS;
			menu_bullet = 0;
			break;
		case 4:
			/* BACK */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = MAIN_MENU;
			menu_bullet = 3;
			break;
		}
		break;
	case GAME_OPTIONS:
		switch (menu_bullet) {
		case 0:
			/* CHARACTER */
			break;
		case 1:
			/* START FLOOR */
			break;
		case 2:
			/* BACK */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = OPTIONS;
			menu_bullet = 0;
			break;
		}
		break;
	case GFX_OPTIONS:
		switch (menu_bullet) {
		case 0:
			/* EYE CANDY */
			break;
		case 1:
			/* FULLSCREEN */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			fullscreen = !fullscreen;
			if (fullscreen)
				enable_fullscreen();
			else
				disable_fullscreen();
			break;
		case 2:
			/* BACK */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = OPTIONS;
			menu_bullet = 1;
			break;
		}
		break;
	case SOUND_OPTIONS:
		switch (menu_bullet) {
		case 0:
			/* SOUND */
			break;
		case 1:
			/* MUSIC */
			break;
		case 2:
			/* BACK */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = OPTIONS;
			menu_bullet = 2;
			break;
		}
		break;
	case CONTROLS:
		switch (menu_bullet) {
		case 0:
			/* LEFT */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			printf("Changing controls is not implemented yet\n");
			break;
		case 1:
			/* RIGHT */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			printf("Changing controls is not implemented yet\n");
			break;
		case 2:
			/* JUMP */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			printf("Changing controls is not implemented yet\n");
			break;
		case 3:
			/* PAUSE */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			printf("Changing controls is not implemented yet\n");
			break;
		case 4:
			/* REJUMP */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			rejump = !rejump;
			break;
		case 5:
			/* BACK */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = OPTIONS;
			menu_bullet = 3;
			break;
		}
		break;
	}
}

void menu_left(void) {
	switch (menu_page) {
	case GAME_OPTIONS:
		switch (menu_bullet) {
		case 0:
			/* CHARACTER */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			if (character_index > 0)
				--character_index;
			break;
		case 1:
			/* START FLOOR */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			if (start_floor > 0)
				--start_floor;
			break;
		}
		break;
	case GFX_OPTIONS:
		switch (menu_bullet) {
		case 0:
			/* EYE CANDY */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			if (eye_candy > 0)
				--eye_candy;
			break;
		}
		break;
	case SOUND_OPTIONS:
		switch (menu_bullet) {
		case 0:
			/* SOUND */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			if (volume_sfx > 0)
				--volume_sfx;
			break;
		case 1:
			/* MUSIC */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			if (volume_music > 0)
				--volume_music;
			sfx_apply_music_volume();
			break;
		}
		break;
	default:
		break;
	}
}

void menu_right(void) {
	switch (menu_page) {
	case GAME_OPTIONS:
		switch (menu_bullet) {
		case 0:
			/* CHARACTER */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			if (character_index < characters_count - 1)
				++character_index;
			break;
		case 1:
			/* START FLOOR */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			if (start_floor < floor_types_count - 1)
				++start_floor;
			break;
		}
		break;
	case GFX_OPTIONS:
		switch (menu_bullet) {
		case 0:
			/* EYE CANDY */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			if (eye_candy < 2)
				++eye_candy;
			break;
		}
		break;
	case SOUND_OPTIONS:
		switch (menu_bullet) {
		case 0:
			/* SOUND */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			if (volume_sfx < 10)
				++volume_sfx;
			break;
		case 1:
			/* MUSIC */
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			if (volume_music < 10)
				++volume_music;
			sfx_apply_music_volume();
			break;
		}
		break;
	default:
		break;
	}
}

void menu_escape(void) {
	switch (menu_page) {
	case MAIN_MENU:
		if (menu_bullet == 4) {
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			game_state = EXIT;
		} else {
			al_play_sample(sample_menu_choose, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_bullet = 4;
		}
		break;
	case OPTIONS:
		if (menu_bullet == 4) {
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = MAIN_MENU;
			menu_bullet = 3;
			break;
		} else {
			al_play_sample(sample_menu_choose, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_bullet = 4;
		}
		break;
	case GAME_OPTIONS:
		if (menu_bullet == 2) {
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = OPTIONS;
			menu_bullet = 0;
			break;
		} else {
			al_play_sample(sample_menu_choose, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_bullet = 2;
		}
		break;
	case GFX_OPTIONS:
		if (menu_bullet == 2) {
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = OPTIONS;
			menu_bullet = 1;
			break;
		} else {
			al_play_sample(sample_menu_choose, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_bullet = 2;
		}
		break;
	case SOUND_OPTIONS:
		if (menu_bullet == 2) {
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = OPTIONS;
			menu_bullet = 2;
			break;
		} else {
			al_play_sample(sample_menu_choose, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_bullet = 2;
		}
		break;
	case CONTROLS:
		if (menu_bullet == 5) {
			al_play_sample(sample_menu_change, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_page = OPTIONS;
			menu_bullet = 3;
			break;
		} else {
			al_play_sample(sample_menu_choose, volume_sfx / 10.0,
					0, 1, ALLEGRO_PLAYMODE_ONCE, NULL);
			menu_bullet = 5;
		}
		break;
	}
}

const char *get_volume_bar(unsigned int n) {
	static char volume_bar[11];
	unsigned int i;
	for (i = 0; i < 10; ++i)
		volume_bar[i] = i < n ? '}' : '{';
	return volume_bar;
}

static ALLEGRO_BITMAP *highscores_clip_scratch;

static void ensure_highscores_clip_scratch(int w, int h)
{
	int bw, bh;

	if (highscores_clip_scratch
			&& al_get_bitmap_width(highscores_clip_scratch) >= w
			&& al_get_bitmap_height(highscores_clip_scratch) >= h)
		return;
	if (highscores_clip_scratch) {
		al_destroy_bitmap(highscores_clip_scratch);
		highscores_clip_scratch = NULL;
	}
	bw = w + 8;
	if (bw < 256)
		bw = 256;
	bh = h + 4;
	if (bh < 24)
		bh = 24;
	highscores_clip_scratch = al_create_bitmap(bw, bh);
}

static void draw_highscores_cut_text(ALLEGRO_FONT *f, ALLEGRO_COLOR col,
		float x, float y, int flags, int clip_y, const char *text)
{
	int lh = al_get_font_line_height(f);
	int tw = al_get_text_width(f, text);
	int top = (int)y;
	int bot = top + lh;
	ALLEGRO_BITMAP *prev;
	int sy;

	if (bot <= clip_y)
		return;
	if (top >= clip_y) {
		al_draw_text(f, col, x, y, flags, text);
		return;
	}
	sy = clip_y - top;
	if (sy >= lh)
		return;
	ensure_highscores_clip_scratch(tw, lh);
	if (!highscores_clip_scratch)
		return;
	prev = al_get_target_bitmap();
	al_set_target_bitmap(highscores_clip_scratch);
	al_clear_to_color(al_map_rgba(0, 0, 0, 0));
	al_draw_text(f, col, 0, 0, flags, text);
	al_set_target_bitmap(prev);
	al_draw_bitmap_region(highscores_clip_scratch, 0, sy, tw, lh - sy, x,
			y + (float)sy, 0);
}

static void draw_highscores_cut_textf(ALLEGRO_FONT *f, ALLEGRO_COLOR col,
		float x, float y, int flags, int clip_y, const char *fmt, ...)
{
	char buf[48];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	draw_highscores_cut_text(f, col, x, y, flags, clip_y, buf);
}

static bool highscores_clip_row_vis(int y, int line_h, int clip_top,
		int clip_bottom)
{
	return y + line_h > clip_top && y < clip_bottom;
}

static void draw_highscores_section(int hx, float yf, int row_h,
		ALLEGRO_FONT *font_hs, HighscoreLeader lb, const char *subtitle,
		int cut_y, int clip_bottom)
{
	char name[4];
	unsigned fl, sc, cb;
	bool used;
	int row;
	int iy = (int)yf;
	int lh = al_get_font_line_height(font_hs);
	ALLEGRO_COLOR wht = al_map_rgb(255, 255, 255);

	if (highscores_clip_row_vis(iy, lh, cut_y, clip_bottom))
		draw_highscores_cut_text(font_hs, wht, hx, yf, 0, cut_y, subtitle);
	yf += row_h;
	for (row = 0; row < HIGHSCORES_COUNT; ++row) {
		iy = (int)yf;
		highscores_get_entry(lb, row, name, &fl, &sc, &cb, &used);
		if (!highscores_clip_row_vis(iy, lh, cut_y, clip_bottom)) {
			yf += row_h;
			continue;
		}
		lh = al_get_font_line_height(font_hs);
		if (used) {
			unsigned col3 = (lb == HIGHSCORE_LEADER_COMBO) ? cb : sc;

			draw_highscores_cut_text(font_hs, wht, hx, yf, 0, cut_y,
					name);
			draw_highscores_cut_textf(font_hs, wht, hx + 72, yf,
					0, cut_y, "%u", fl);
			draw_highscores_cut_textf(font_hs, wht, hx + 132, yf,
					0, cut_y, "%u", col3);
		} else {
			draw_highscores_cut_text(font_hs, wht, hx, yf, 0, cut_y,
					"---");
			draw_highscores_cut_text(font_hs, wht, hx + 72, yf, 0,
					cut_y, "0");
			draw_highscores_cut_text(font_hs, wht, hx + 132, yf, 0,
					cut_y, "0");
		}
		yf += row_h;
	}
}

static void draw_menu_highscores(int hx, int hy, int row_h,
		ALLEGRO_FONT *font_hs, ALLEGRO_FONT *font_hs_colhdr)
{
	static const HighscoreLeader lb_order[3] = {
		HIGHSCORE_LEADER_FLOOR,
		HIGHSCORE_LEADER_SCORE,
		HIGHSCORE_LEADER_COMBO,
	};
	static const char *const titles[3] = {
		"BEST FLOOR", "BEST SCORE", "BEST COMBO",
	};
	const int clip_top = hy + 42;
	const int clip_bottom = 472;
	const int section_gap = 10;
	const float section_h = (float)(row_h + HIGHSCORES_COUNT * row_h
			+ section_gap);
	const float cycle_h = section_h * 3.f;
	const double scroll_px = fmod(al_get_time() * 28.0, (double)cycle_h);
	float y0 = (float)clip_top - (float)scroll_px;
	int dup, s;

	for (dup = 0; dup < 2; ++dup) {
		float base = y0 + (float)dup * cycle_h;

		for (s = 0; s < 3; ++s) {
			draw_highscores_section(hx,
					base + (float)s * section_h, row_h,
					font_hs, lb_order[s], titles[s],
					clip_top, clip_bottom);
		}
	}

	al_draw_text(font_hs, al_map_rgb(255, 255, 255), hx, hy, 0,
			"HIGHSCORES");
	al_draw_text(font_hs_colhdr, al_map_rgb(255, 255, 255),
			hx, hy + 22, 0, "DUDE");
	al_draw_text(font_hs_colhdr, al_map_rgb(255, 255, 255),
			hx + 56, hy + 22, 0, "FLOOR");
	al_draw_text(font_hs_colhdr, al_map_rgb(255, 255, 255),
			hx + 128, hy + 22, 0, "SCORE");
}

/* Main menu hero head: vertical bob and tilt on different sine periods. */
static void draw_main_menu_heroface(void)
{
	ALLEGRO_BITMAP *frame;
	const double t = al_get_time();
	const double bob_period = 1.8;
	const double rot_period = 2.2;
	const double heroface_frame_period_sec = 0.22 * 4.0;
	const float bob_amp = 11.f;
	const double rot_max = 15.0 * ALLEGRO_PI / 180.0;
	double bob_phase, rot_phase;
	float cx, cy, dx, dy;
	int w, h, fi;

	if (!bitmap_heroface000 || !bitmap_heroface001 || !bitmap_heroface002)
		return;

	fi = (int)floor(fmod(t / heroface_frame_period_sec, 3.0));
	if (fi < 0)
		fi += 3;
	switch (fi) {
	case 0:
		frame = bitmap_heroface000;
		break;
	case 1:
		frame = bitmap_heroface001;
		break;
	default:
		frame = bitmap_heroface002;
		break;
	}
	w = al_get_bitmap_width(frame);
	h = al_get_bitmap_height(frame);
	cx = (float)w * 0.5f;
	cy = (float)h * 0.5f;
	bob_phase = sin(2.0 * ALLEGRO_PI * t / bob_period);
	rot_phase = sin(2.0 * ALLEGRO_PI * t / rot_period);
	dx = 8.f + cx;
	dy = 268.f + bob_amp * (float)bob_phase - cy;
	al_draw_rotated_bitmap(frame, cx, cy, dx, dy, rot_max * rot_phase, 0);
}

void draw_menu(void) {
	if (fullscreen)
		al_clear_to_color(al_map_rgb(0, 0, 0));
	al_draw_bitmap(bitmap_title_bg, 0, 0, 0);
	if (menu_page == MAIN_MENU)
		draw_main_menu_heroface();
	al_draw_bitmap(bitmap_title, 250, 20, 0);
	al_draw_bitmap(bitmap_menu_bullet, 4, 262 + 28 * menu_bullet, 0);
	switch (menu_page) {
	case MAIN_MENU:
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 0, 0, "START GAME");
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 1, 0, "LOAD REPLAY");
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 2, 0, "INSTRUCTIONS");
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 3, 0, "OPTIONS");
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 4, 0, "EXIT");
		draw_menu_highscores(400, 268, 18, font_mono, font_native);
		break;
	case OPTIONS:
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 0, 0, "GAME OPTIONS");
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 1, 0, "GFX OPTIONS");
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 2, 0, "SOUND OPTIONS");
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 3, 0, "CONTROLS");
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 4, 0, "BACK");
		break;
	case GAME_OPTIONS:
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 0, 0, "CHARACTER:");
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 1, 0, "START FLOOR:");
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 2, 0, "BACK");
		al_draw_bitmap(characters[character_index].gfx.idle1,
				330, 256, 0);
		al_draw_bitmap(floor_types[start_floor].left,
				315, 308, 0);
		al_draw_bitmap(floor_types[start_floor].mid,
				336, 308, 0);
		al_draw_bitmap(floor_types[start_floor].right,
				352, 308, 0);
		break;
	case GFX_OPTIONS:
		al_draw_textf(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 0, 0, "EYE CANDY: %s",
				eye_candy == 0 ? "NONE" :
				eye_candy == 1 ? "SOME" :
				eye_candy == 2 ? "LOTS" :
				"");
		al_draw_textf(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 1, 0, "FULLSCREEN: %s",
				fullscreen ? "YES" : "NO");
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 2, 0, "BACK");
		break;
	case SOUND_OPTIONS:
		al_draw_textf(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 0, 0, "SOUND:%s",
				get_volume_bar(volume_sfx));
		al_draw_textf(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 1, 0, "MUSIC:%s",
				get_volume_bar(volume_music));
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 2, 0, "BACK");
		break;
	case CONTROLS:
		al_draw_textf(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 0, 0, "LEFT:  (%s)",
				al_keycode_to_name(key_left));
		al_draw_textf(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 1, 0, "RIGHT: (%s)",
				al_keycode_to_name(key_right));
		al_draw_textf(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 2, 0, "JUMP:  (%s)",
				al_keycode_to_name(key_jump));
		al_draw_textf(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 3, 0, "PAUSE: (%s)",
				al_keycode_to_name(key_pause));
		al_draw_textf(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 4, 0, "REJUMP: %s",
				rejump ? "YES" : "NO");
		al_draw_text(font_color, al_map_rgb(255, 255, 255),
				40, 270 + 28 * 5, 0, "BACK");
		break;
	}
}

void draw_instructions(void) {
	if (fullscreen)
		al_clear_to_color(al_map_rgb(0, 0, 0));
	al_draw_bitmap(bitmap_title_bg, 0, 0, 0);
	al_draw_bitmap(bitmap_instructions, 0, 0, 0);
}
