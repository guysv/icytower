#include <allegro5/allegro.h>
#include <allegro5/keycodes.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "characters.h"
#include "floor_types.h"
#include "options.h"

unsigned int character_index = 0;
unsigned int start_floor = 0;
unsigned int eye_candy = 0;
bool fullscreen = false;
unsigned int volume_sfx = 10, volume_music = 10;
int key_left = ALLEGRO_KEY_LEFT;
int key_right = ALLEGRO_KEY_RIGHT;
int key_jump = ALLEGRO_KEY_SPACE;
int key_pause = ALLEGRO_KEY_P;
bool rejump = true;
char player_initials[4] = "AAA";
unsigned int option_bpm = OPTIONS_BPM_DEFAULT;

#define OPTIONS_SECTION "options"

/*
 * Non-macOS: persisting under XDG_CONFIG_HOME or %AppData% is not implemented yet;
 * load/save are no-ops there so the project still builds on Linux/Windows.
 */

#if defined(__APPLE__)

static bool fill_config_dir(char *buf, size_t len)
{
	const char *home = getenv("HOME");
	int n;

	if (!home || home[0] == '\0')
		return false;
	n = snprintf(buf, len, "%s/Library/Application Support/IcyTower", home);
	return n > 0 && (size_t)n < len;
}

static bool fill_config_path(char *buf, size_t len)
{
	const char *home = getenv("HOME");
	int n;

	if (!home || home[0] == '\0')
		return false;
	n = snprintf(buf, len,
			"%s/Library/Application Support/IcyTower/options.cfg", home);
	return n > 0 && (size_t)n < len;
}

static bool mkdir_p(char *path)
{
	for (char *p = path + 1; *p != '\0'; ++p) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(path, 0755) != 0 && errno != EEXIST) {
			*p = '/';
			return false;
		}
		*p = '/';
	}
	return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static bool parse_uint(const char *s, unsigned int *out, unsigned int max_val)
{
	char *end;
	unsigned long v;

	if (!s || s[0] == '-')
		return false;
	v = strtoul(s, &end, 10);
	if (end == s || *end != '\0')
		return false;
	if (v > max_val)
		return false;
	*out = (unsigned int)v;
	return true;
}

static bool parse_keycode(const char *s, int *out)
{
	char *end;
	long v;

	if (!s)
		return false;
	v = strtol(s, &end, 10);
	if (end == s || *end != '\0')
		return false;
	if (v <= 0 || v >= (long)ALLEGRO_KEY_MAX)
		return false;
	*out = (int)v;
	return true;
}

static void load_bool(const ALLEGRO_CONFIG *cfg, const char *key, bool *out)
{
	const char *s = al_get_config_value(cfg, OPTIONS_SECTION, key);

	if (!s)
		return;
	if (strcmp(s, "1") == 0)
		*out = true;
	else if (strcmp(s, "0") == 0)
		*out = false;
}

static void load_player_initials(const ALLEGRO_CONFIG *cfg)
{
	const char *s = al_get_config_value(cfg, OPTIONS_SECTION,
			"player_initials");

	if (!s || strlen(s) != 3) {
		strcpy(player_initials, "AAA");
		return;
	}
	memcpy(player_initials, s, 3);
	player_initials[3] = '\0';
}

void options_load(void)
{
	char path[PATH_MAX];
	ALLEGRO_CONFIG *cfg;

	if (!fill_config_path(path, sizeof(path)))
		return;
	cfg = al_load_config_file(path);
	if (!cfg)
		return;

	(void)parse_uint(al_get_config_value(cfg, OPTIONS_SECTION,
					    "character_index"),
			&character_index, UINT_MAX);
	(void)parse_uint(al_get_config_value(cfg, OPTIONS_SECTION, "start_floor"),
			&start_floor, UINT_MAX);
	(void)parse_uint(al_get_config_value(cfg, OPTIONS_SECTION, "eye_candy"),
			&eye_candy, 2u);
	load_bool(cfg, "fullscreen", &fullscreen);
	(void)parse_uint(al_get_config_value(cfg, OPTIONS_SECTION, "volume_sfx"),
			&volume_sfx, 10u);
	(void)parse_uint(al_get_config_value(cfg, OPTIONS_SECTION, "volume_music"),
			&volume_music, 10u);
	{
		int k;

		if (parse_keycode(al_get_config_value(cfg, OPTIONS_SECTION, "key_left"),
				&k))
			key_left = k;
		if (parse_keycode(al_get_config_value(cfg, OPTIONS_SECTION, "key_right"),
				&k))
			key_right = k;
		if (parse_keycode(al_get_config_value(cfg, OPTIONS_SECTION, "key_jump"),
				&k))
			key_jump = k;
		if (parse_keycode(al_get_config_value(cfg, OPTIONS_SECTION, "key_pause"),
				&k))
			key_pause = k;
	}
	load_bool(cfg, "rejump", &rejump);
	load_player_initials(cfg);

	al_destroy_config(cfg);
}

void options_save(void)
{
	char path[PATH_MAX];
	char dir[PATH_MAX];
	ALLEGRO_CONFIG *cfg;
	char buf[32];

	if (!fill_config_path(path, sizeof(path)))
		return;
	if (!fill_config_dir(dir, sizeof(dir)))
		return;
	if (!mkdir_p(dir))
		return;

	cfg = al_create_config();
	if (!cfg)
		return;

	al_set_config_value(cfg, OPTIONS_SECTION, "version", "1");
	snprintf(buf, sizeof(buf), "%u", character_index);
	al_set_config_value(cfg, OPTIONS_SECTION, "character_index", buf);
	snprintf(buf, sizeof(buf), "%u", start_floor);
	al_set_config_value(cfg, OPTIONS_SECTION, "start_floor", buf);
	snprintf(buf, sizeof(buf), "%u", eye_candy);
	al_set_config_value(cfg, OPTIONS_SECTION, "eye_candy", buf);
	al_set_config_value(cfg, OPTIONS_SECTION, "fullscreen",
			fullscreen ? "1" : "0");
	snprintf(buf, sizeof(buf), "%u", volume_sfx);
	al_set_config_value(cfg, OPTIONS_SECTION, "volume_sfx", buf);
	snprintf(buf, sizeof(buf), "%u", volume_music);
	al_set_config_value(cfg, OPTIONS_SECTION, "volume_music", buf);
	snprintf(buf, sizeof(buf), "%d", key_left);
	al_set_config_value(cfg, OPTIONS_SECTION, "key_left", buf);
	snprintf(buf, sizeof(buf), "%d", key_right);
	al_set_config_value(cfg, OPTIONS_SECTION, "key_right", buf);
	snprintf(buf, sizeof(buf), "%d", key_jump);
	al_set_config_value(cfg, OPTIONS_SECTION, "key_jump", buf);
	snprintf(buf, sizeof(buf), "%d", key_pause);
	al_set_config_value(cfg, OPTIONS_SECTION, "key_pause", buf);
	al_set_config_value(cfg, OPTIONS_SECTION, "rejump", rejump ? "1" : "0");
	al_set_config_value(cfg, OPTIONS_SECTION, "player_initials",
			player_initials);

	if (!al_save_config_file(path, cfg))
		fprintf(stderr, "Warning: could not save options to %s\n", path);

	al_destroy_config(cfg);
}

#else /* !__APPLE__ */

void options_load(void)
{
}

void options_save(void)
{
}

#endif /* __APPLE__ */

float options_playback_speed(void)
{
	return (float)option_bpm / (float)OPTIONS_BPM_REFERENCE;
}

void options_clamp_indices(void)
{
	if (characters_count > 0 && character_index >= characters_count)
		character_index = (unsigned int)(characters_count - 1);
	if (floor_types_count > 0 && start_floor >= floor_types_count)
		start_floor = (unsigned int)(floor_types_count - 1);
	if (eye_candy > 2u)
		eye_candy = 2u;
	if (volume_sfx > 10u)
		volume_sfx = 10u;
	if (volume_music > 10u)
		volume_music = 10u;
	if (option_bpm < OPTIONS_BPM_MIN)
		option_bpm = OPTIONS_BPM_MIN;
	else if (option_bpm > OPTIONS_BPM_MAX)
		option_bpm = OPTIONS_BPM_MAX;
	options_normalize_player_initials();
}

void options_normalize_player_initials(void)
{
	for (unsigned int i = 0; i < 3; ++i) {
		char c = player_initials[i];

		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		if (c < 'A' || c > 'Z') {
			strcpy(player_initials, "AAA");
			return;
		}
		player_initials[i] = c;
	}
	player_initials[3] = '\0';
}
