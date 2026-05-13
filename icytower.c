#include <allegro5/allegro.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_acodec.h>
#include <stdio.h>

#include "icytower.h"
#include "gfx.h"
#include "sfx.h"
#include "menu.h"
#include "options.h"
#include "characters.h"
#include "floor_types.h"
#include "game.h"
#include "fullscreen.h"
#include "highscores.h"

static ALLEGRO_TIMER *game_tick_timer;

static char initials_buf[4];
static int initials_cursor;
static unsigned initials_board_mask;

static unsigned leader_board_mask(void)
{
	return highscores_get_board_mask((unsigned)(it_state.floor * 10
					       + it_state.score),
			(unsigned)it_state.floor, (unsigned)it_state.combo);
}

static void initials_adjust_letter(char *c, int delta)
{
	int i = *c - 'A';

	i = (i + delta) % 26;
	if (i < 0)
		i += 26;
	*c = (char)(i + 'A');
}
void icytower_sync_game_speed(void)
{
	double period;

	if (!game_tick_timer)
		return;
	period = (1.0 / 50.0) * (double)OPTIONS_BPM_REFERENCE
			/ (double)option_bpm;
	al_set_timer_speed(game_tick_timer, period);
	sfx_apply_playback_speed();
}

bool initialize(void) {
	if (!al_init()) {
		printf("Failed to initialize the Allegro system\n");
		return false;
	}
	if (!al_init_image_addon()) {
		printf("Failed to initialize the image i/o addon\n");
		return false;
	}
	if (!al_init_primitives_addon()) {
		printf("Failed to initialize the primitives addon\n");
		return false;
	}
	if (!al_init_font_addon()) {
		printf("Failed to initialize the font addons\n");
		return false;
	}
	if (!al_install_keyboard()) {
		printf("Failed to install the keyboard driver\n");
		return false;
	}
	if (!al_install_audio()) {
		printf("Failed to install the audio subsystem\n");
		return false;
	}
	if (!al_init_acodec_addon()) {
		printf("Failed to initialize the audio codecs\n");
		return false;
	}
	if (!al_reserve_samples(16)) {
		printf("Failed to reserve audio sample instances\n");
		return false;
	}
	return true;
}

void start_game(void) {
	sfx_bgm_stop_menu();
	sfx_bgm_play_character(character_index);
	initialize_game();
	draw_game();
	sfx_play_sample(characters[character_index].sfx.greeting,
			volume_sfx / 10.0f, ALLEGRO_PLAYMODE_ONCE);
}

void pause_game(void) {
	sfx_play_sample(characters[character_index].sfx.pause,
			volume_sfx / 10.0f, ALLEGRO_PLAYMODE_ONCE);
}

void main_menu(void) {
	sfx_bgm_stop_character(character_index);
	sfx_bgm_play_menu();
}

enum GAME_STATE game_state;

int main() {
	ALLEGRO_DISPLAY *display = NULL;
	ALLEGRO_TIMER *timer = NULL;
	ALLEGRO_EVENT_QUEUE *event_queue = NULL;
	ALLEGRO_EVENT event;
	bool redraw;
	bool game_running;
	bool paused;
	bool dont_draw;

	if (!initialize())
		goto cleanup;

	options_load();
	highscores_load();

	al_set_new_display_flags(ALLEGRO_WINDOWED);
	display = al_create_display(640, 480);
	if (display == NULL) {
		printf("Failed to create a display\n");
		goto cleanup;
	}

	if (fullscreen)
		enable_fullscreen();

	timer = al_create_timer(1.0 / 50.0);
	if (timer == NULL) {
		printf("Failed to create a timer\n");
		goto cleanup;
	}
	game_tick_timer = timer;

	event_queue = al_create_event_queue();
	if (event_queue == NULL) {
		printf("Failed to create an event queue\n");
		goto cleanup;
	}

	al_register_event_source(event_queue,
			al_get_keyboard_event_source());
	al_register_event_source(event_queue,
			al_get_display_event_source(display));
	al_register_event_source(event_queue,
			al_get_timer_event_source(timer));

	printf("Loading gfx resources...\n");
	if (!gfx_load_bitmaps()) {
		printf("Failed to load gfx resources\n");
		goto cleanup;
	}
	printf("Loading fonts...\n");
	if (!gfx_load_fonts()) {
		printf("Failed to load fonts\n");
		goto cleanup;
	}
	printf("Loading sfx resources...\n");
	if (!sfx_load_audio_streams_and_samples()) {
		printf("Failed to load sfx resources\n");
		goto cleanup;
	}

	sfx_register_bgm_event_sources(event_queue);

	initialize_characters();
	initialize_floor_types();

	options_clamp_indices();

	sfx_apply_music_volume();
	icytower_sync_game_speed();

	game_state = TITLE;
	main_menu();

	game_running = true;
	redraw = true;
	paused = false;
	dont_draw = false;
	al_start_timer(timer);
	while (game_running) {
		al_wait_for_event(event_queue, &event);
		switch (event.type) {
		case ALLEGRO_EVENT_DISPLAY_CLOSE:
			goto cleanup;
		case ALLEGRO_EVENT_DISPLAY_HALT_DRAWING:
			al_acknowledge_drawing_halt(display);
			dont_draw = true;
			break;
		case ALLEGRO_EVENT_DISPLAY_RESUME_DRAWING:
			al_acknowledge_drawing_resume(display);
			dont_draw = false;
			break;
		case ALLEGRO_EVENT_DISPLAY_SWITCH_OUT:
			/* pause the game */
			paused = true;
			break;
		case ALLEGRO_EVENT_DISPLAY_SWITCH_IN:
			/* resume the game */
			paused = false;
			break;
		case ALLEGRO_EVENT_AUDIO_STREAM_FRAGMENT:
			sfx_handle_audio_stream_fragment(&event);
			break;
		case ALLEGRO_EVENT_TIMER:
			switch (game_state) {
			case PLAYING:
				if (!paused)
					do_tick();
				break;
			case EXIT:
				game_running = false;
				break;
			}
			// currently, redraw is coupled to physics tick
			// but it shouldn't be that way!
			redraw = true;
			break;
		case ALLEGRO_EVENT_KEY_DOWN:
			switch (game_state) {
			case TITLE:
				switch (event.keyboard.keycode) {
				case ALLEGRO_KEY_UP:
					menu_up();
					break;
				case ALLEGRO_KEY_DOWN:
					menu_down();
					break;
				case ALLEGRO_KEY_ENTER:
				case ALLEGRO_KEY_SPACE:
					menu_enter();
					break;
				case ALLEGRO_KEY_ESCAPE:
					menu_escape();
					break;
				case ALLEGRO_KEY_LEFT:
					menu_left();
					break;
				case ALLEGRO_KEY_RIGHT:
					menu_right();
					break;
				}
				break;
			case INSTRUCTIONS:
				switch (event.keyboard.keycode) {
				case ALLEGRO_KEY_ENTER:
				case ALLEGRO_KEY_SPACE:
				case ALLEGRO_KEY_ESCAPE:
					game_state = TITLE;
					break;
				}
				break;
			case PLAYING:
				if (event.keyboard.keycode == ALLEGRO_KEY_ESCAPE) {
					game_state = ESCAPE;
					pause_game();
				} else if (event.keyboard.keycode == key_pause) {
					game_state = PAUSE;
					pause_game();
				} else if (event.keyboard.keycode == key_left)
					press_left();
				else if (event.keyboard.keycode == key_right)
					press_right();
				else if (event.keyboard.keycode == key_jump)
					press_jump();
				break;
			case PAUSE:
				game_state = PLAYING;
				if (event.keyboard.keycode == key_left)
					press_left();
				else if (event.keyboard.keycode == key_right)
					press_right();
				else if (event.keyboard.keycode == key_jump)
					press_jump();
				break;
			case ESCAPE:
				if (event.keyboard.keycode == ALLEGRO_KEY_ESCAPE) {
					sfx_play_sample(sample_tryagain, volume_sfx / 10.0f,
							ALLEGRO_PLAYMODE_ONCE);
					game_state = TITLE;
					main_menu();
				} else {
					game_state = PLAYING;
					if (event.keyboard.keycode == key_left)
						press_left();
					else if (event.keyboard.keycode == key_right)
						press_right();
					else if (event.keyboard.keycode == key_jump)
						press_jump();
				}
				break;
			case GAMEOVER:
				switch (event.keyboard.keycode) {
				case ALLEGRO_KEY_ESCAPE:
					sfx_play_sample(sample_tryagain, volume_sfx / 10.0f,
							ALLEGRO_PLAYMODE_ONCE);
					game_state = TITLE;
					main_menu();
					break;
				case ALLEGRO_KEY_ENTER:
				case ALLEGRO_KEY_SPACE:
					initials_board_mask = leader_board_mask();
					if (initials_board_mask) {
						options_normalize_player_initials();
						memcpy(initials_buf, player_initials, 4);
						initials_cursor = 0;
						game_state = ENTER_INITIALS;
					} else {
						sfx_play_sample(sample_tryagain,
								volume_sfx / 10.0f,
								ALLEGRO_PLAYMODE_ONCE);
						game_state = TITLE;
						main_menu();
					}
					break;
				}
				break;
			case ENTER_INITIALS:
				switch (event.keyboard.keycode) {
				case ALLEGRO_KEY_UP:
					sfx_play_sample(sample_menu_change,
							volume_sfx / 10.0f,
							ALLEGRO_PLAYMODE_ONCE);
					initials_adjust_letter(
							&initials_buf[initials_cursor],
							1);
					break;
				case ALLEGRO_KEY_DOWN:
					sfx_play_sample(sample_menu_change,
							volume_sfx / 10.0f,
							ALLEGRO_PLAYMODE_ONCE);
					initials_adjust_letter(
							&initials_buf[initials_cursor],
							-1);
					break;
				case ALLEGRO_KEY_LEFT:
					sfx_play_sample(sample_menu_change,
							volume_sfx / 10.0f,
							ALLEGRO_PLAYMODE_ONCE);
					initials_cursor = (initials_cursor + 2) % 3;
					break;
				case ALLEGRO_KEY_RIGHT:
					sfx_play_sample(sample_menu_change,
							volume_sfx / 10.0f,
							ALLEGRO_PLAYMODE_ONCE);
					initials_cursor = (initials_cursor + 1) % 3;
					break;
				case ALLEGRO_KEY_ENTER:
				case ALLEGRO_KEY_SPACE:
					highscores_submit(initials_buf,
							(unsigned)it_state.floor,
							(unsigned)(it_state.floor * 10
									+ it_state.score),
							(unsigned)it_state.combo);
					memcpy(player_initials, initials_buf, 4);
					options_normalize_player_initials();
					options_save();
					sfx_play_sample(sample_menu_change,
							volume_sfx / 10.0f,
							ALLEGRO_PLAYMODE_ONCE);
					game_state = TITLE;
					main_menu();
					break;
				case ALLEGRO_KEY_ESCAPE:
					sfx_play_sample(sample_tryagain, volume_sfx / 10.0f,
							ALLEGRO_PLAYMODE_ONCE);
					game_state = TITLE;
					main_menu();
					break;
				}
				break;
			case EXIT:
				break;
			}
			break;
		case ALLEGRO_EVENT_KEY_UP:
			if (game_state == PLAYING) {
				if (event.keyboard.keycode == key_left)
					release_left();
				else if (event.keyboard.keycode == key_right)
					release_right();
				else if (event.keyboard.keycode == key_jump)
					release_jump();
			}
			break;
		}

		// if there are events in queue, continue to process them
		if (!al_is_event_queue_empty(event_queue))
			continue;

		if (redraw && !dont_draw) {
			switch (game_state) {
			case TITLE:
				draw_menu();
				break;
			case INSTRUCTIONS:
				draw_instructions();
				break;
			case PLAYING:
				draw_game();
				break;
			case PAUSE:
				draw_pause();
				break;
			case ESCAPE:
				draw_escape();
				break;
			case GAMEOVER:
				draw_gameover();
				break;
			case ENTER_INITIALS:
				draw_enter_initials(initials_buf, initials_cursor,
						initials_board_mask);
				break;
			}
			al_flip_display();
			redraw = false;
		}
	}

cleanup:
	options_save();
	highscores_save();

	sfx_destroy_audio_streams_and_samples();
	gfx_destroy_fonts();
	gfx_destroy_bitmaps();

	if (event_queue)
		al_destroy_event_queue(event_queue);
	if (timer) {
		al_destroy_timer(timer);
		game_tick_timer = NULL;
	}
	if (display)
		al_destroy_display(display);
	return 0;
}
