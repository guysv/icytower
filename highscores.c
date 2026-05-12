/*
 * High scores persistence: macOS uses ~/Library/Application Support/IcyTower/highscores.cfg
 * via ALLEGRO_CONFIG. Non-Apple: load/save are no-ops (same as options.c).
 */
#include <allegro5/allegro.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "highscores.h"

typedef struct {
	char name[4];
	unsigned floor;
	unsigned score;
	unsigned combo;
	bool used;
} HighScoreRow;

static HighScoreRow boards[HIGHSCORE_LEADER_COUNT][HIGHSCORES_COUNT];

#define HIGHSCORES_SECTION "highscores"

static int cmp_score_board(const void *a, const void *b)
{
	const HighScoreRow *ra = a;
	const HighScoreRow *rb = b;

	if (ra->used != rb->used)
		return (int)rb->used - (int)ra->used;
	if (ra->score != rb->score)
		return ra->score > rb->score ? -1 : 1;
	if (ra->floor != rb->floor)
		return ra->floor > rb->floor ? -1 : 1;
	if (ra->combo != rb->combo)
		return ra->combo > rb->combo ? -1 : 1;
	return strcmp(ra->name, rb->name);
}

static int cmp_floor_board(const void *a, const void *b)
{
	const HighScoreRow *ra = a;
	const HighScoreRow *rb = b;

	if (ra->used != rb->used)
		return (int)rb->used - (int)ra->used;
	if (ra->floor != rb->floor)
		return ra->floor > rb->floor ? -1 : 1;
	if (ra->score != rb->score)
		return ra->score > rb->score ? -1 : 1;
	if (ra->combo != rb->combo)
		return ra->combo > rb->combo ? -1 : 1;
	return strcmp(ra->name, rb->name);
}

static int cmp_combo_board(const void *a, const void *b)
{
	const HighScoreRow *ra = a;
	const HighScoreRow *rb = b;

	if (ra->used != rb->used)
		return (int)rb->used - (int)ra->used;
	if (ra->combo != rb->combo)
		return ra->combo > rb->combo ? -1 : 1;
	if (ra->score != rb->score)
		return ra->score > rb->score ? -1 : 1;
	if (ra->floor != rb->floor)
		return ra->floor > rb->floor ? -1 : 1;
	return strcmp(ra->name, rb->name);
}

static void sort_board(HighscoreLeader which)
{
	int (*cmp)(const void *, const void *);

	switch (which) {
	case HIGHSCORE_LEADER_SCORE:
		cmp = cmp_score_board;
		break;
	case HIGHSCORE_LEADER_FLOOR:
		cmp = cmp_floor_board;
		break;
	case HIGHSCORE_LEADER_COMBO:
		cmp = cmp_combo_board;
		break;
	default:
		return;
	}
	qsort(boards[which], HIGHSCORES_COUNT, sizeof(boards[which][0]), cmp);
}

static void clear_board(HighscoreLeader which)
{
	for (int i = 0; i < HIGHSCORES_COUNT; ++i) {
		boards[which][i].used = false;
		strcpy(boards[which][i].name, "---");
		boards[which][i].floor = 0;
		boards[which][i].score = 0;
		boards[which][i].combo = 0;
	}
}

static void clear_everything(void)
{
	for (int w = 0; w < HIGHSCORE_LEADER_COUNT; ++w)
		clear_board((HighscoreLeader)w);
}

static int count_used(HighscoreLeader which)
{
	int n = 0;

	for (int i = 0; i < HIGHSCORES_COUNT; ++i) {
		if (boards[which][i].used)
			++n;
	}
	return n;
}

static bool valid_initial_char(int c)
{
	return c >= 'A' && c <= 'Z';
}

static bool parse_row(const char *s, HighScoreRow *out)
{
	char name[8];
	unsigned fl, sc, cb = 0;
	int nfields;

	memset(out, 0, sizeof(*out));
	if (!s)
		return false;
	nfields = sscanf(s, "%3[^,],%u,%u,%u", name, &fl, &sc, &cb);
	if (nfields < 3)
		return false;
	if (strlen(name) != 3)
		return false;
	if (strcmp(name, "---") == 0) {
		out->used = false;
		strcpy(out->name, "---");
		out->floor = 0;
		out->score = 0;
		out->combo = 0;
		return true;
	}
	for (int i = 0; i < 3; ++i) {
		if (!valid_initial_char((unsigned char)name[i]))
			return false;
	}
	strcpy(out->name, name);
	out->floor = fl;
	out->score = sc;
	out->combo = (nfields >= 4) ? cb : 0u;
	out->used = true;
	return true;
}

static bool beats_worst_score(const HighScoreRow *w, const HighScoreRow *t)
{
	if (t->score > w->score)
		return true;
	if (t->score < w->score)
		return false;
	if (t->floor > w->floor)
		return true;
	if (t->floor < w->floor)
		return false;
	if (t->combo > w->combo)
		return true;
	if (t->combo < w->combo)
		return false;
	return strcmp(t->name, w->name) < 0;
}

static bool beats_worst_floor(const HighScoreRow *w, const HighScoreRow *t)
{
	if (t->floor > w->floor)
		return true;
	if (t->floor < w->floor)
		return false;
	if (t->score > w->score)
		return true;
	if (t->score < w->score)
		return false;
	if (t->combo > w->combo)
		return true;
	if (t->combo < w->combo)
		return false;
	return strcmp(t->name, w->name) < 0;
}

static bool beats_worst_combo(const HighScoreRow *w, const HighScoreRow *t)
{
	if (t->combo > w->combo)
		return true;
	if (t->combo < w->combo)
		return false;
	if (t->score > w->score)
		return true;
	if (t->score < w->score)
		return false;
	if (t->floor > w->floor)
		return true;
	if (t->floor < w->floor)
		return false;
	return strcmp(t->name, w->name) < 0;
}

static bool board_qualifies(HighscoreLeader which, const HighScoreRow *t)
{
	const HighScoreRow *w;

	sort_board(which);
	if (count_used(which) < HIGHSCORES_COUNT)
		return true;
	w = &boards[which][HIGHSCORES_COUNT - 1];
	switch (which) {
	case HIGHSCORE_LEADER_SCORE:
		return beats_worst_score(w, t);
	case HIGHSCORE_LEADER_FLOOR:
		return beats_worst_floor(w, t);
	case HIGHSCORE_LEADER_COMBO:
		return beats_worst_combo(w, t);
	default:
		return false;
	}
}

static bool insert_row(HighscoreLeader which, const HighScoreRow *new_row,
		int (*cmp)(const void *, const void *),
		bool (*beats)(const HighScoreRow *w, const HighScoreRow *t))
{
	int i;

	sort_board(which);
	if (count_used(which) < HIGHSCORES_COUNT) {
		for (i = 0; i < HIGHSCORES_COUNT; ++i) {
			if (!boards[which][i].used) {
				boards[which][i] = *new_row;
				boards[which][i].used = true;
				qsort(boards[which], HIGHSCORES_COUNT,
						sizeof(boards[which][0]), cmp);
				return true;
			}
		}
	}
	if (!beats(&boards[which][HIGHSCORES_COUNT - 1], new_row))
		return false;
	boards[which][HIGHSCORES_COUNT - 1] = *new_row;
	boards[which][HIGHSCORES_COUNT - 1].used = true;
	qsort(boards[which], HIGHSCORES_COUNT, sizeof(boards[which][0]), cmp);
	return true;
}

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

static bool fill_highscores_path(char *buf, size_t len)
{
	const char *home = getenv("HOME");
	int n;

	if (!home || home[0] == '\0')
		return false;
	n = snprintf(buf, len,
			"%s/Library/Application Support/IcyTower/highscores.cfg", home);
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

static const char *board_key_prefix(HighscoreLeader w)
{
	switch (w) {
	case HIGHSCORE_LEADER_SCORE:
		return "score_e";
	case HIGHSCORE_LEADER_FLOOR:
		return "floor_e";
	case HIGHSCORE_LEADER_COMBO:
		return "combo_e";
	default:
		return "score_e";
	}
}

void highscores_load(void)
{
	char path[PATH_MAX];
	ALLEGRO_CONFIG *cfg;
	const char *ver;
	int w, i;

	clear_everything();
	if (!fill_highscores_path(path, sizeof(path)))
		return;
	cfg = al_load_config_file(path);
	if (!cfg)
		return;

	ver = al_get_config_value(cfg, HIGHSCORES_SECTION, "version");
	if (ver && strcmp(ver, "2") == 0) {
		for (w = 0; w < HIGHSCORE_LEADER_COUNT; ++w) {
			for (i = 0; i < HIGHSCORES_COUNT; ++i) {
				char key[20];
				const char *s;

				snprintf(key, sizeof(key), "%s%d", board_key_prefix(
							(HighscoreLeader)w), i);
				s = al_get_config_value(cfg, HIGHSCORES_SECTION, key);
				if (s && !parse_row(s, &boards[w][i])) {
					clear_everything();
					goto out;
				}
			}
		}
	} else {
		for (i = 0; i < HIGHSCORES_COUNT; ++i) {
			char key[8];
			const char *s;

			snprintf(key, sizeof(key), "e%d", i);
			s = al_get_config_value(cfg, HIGHSCORES_SECTION, key);
			if (s && !parse_row(s, &boards[HIGHSCORE_LEADER_SCORE][i])) {
				clear_everything();
				break;
			}
		}
	}
out:
	al_destroy_config(cfg);
	for (w = 0; w < HIGHSCORE_LEADER_COUNT; ++w)
		sort_board((HighscoreLeader)w);
}

void highscores_save(void)
{
	char path[PATH_MAX];
	char dir[PATH_MAX];
	ALLEGRO_CONFIG *cfg;
	int w, i;

	if (!fill_highscores_path(path, sizeof(path)))
		return;
	if (!fill_config_dir(dir, sizeof(dir)))
		return;
	if (!mkdir_p(dir))
		return;

	cfg = al_create_config();
	if (!cfg)
		return;

	al_set_config_value(cfg, HIGHSCORES_SECTION, "version", "2");
	for (w = 0; w < HIGHSCORE_LEADER_COUNT; ++w) {
		for (i = 0; i < HIGHSCORES_COUNT; ++i) {
			char key[20];
			char val[80];

			snprintf(key, sizeof(key), "%s%d", board_key_prefix(
						(HighscoreLeader)w), i);
			if (boards[w][i].used)
				snprintf(val, sizeof(val), "%s,%u,%u,%u",
						boards[w][i].name,
						boards[w][i].floor,
						boards[w][i].score,
						boards[w][i].combo);
			else
				snprintf(val, sizeof(val), "---,0,0,0");
			al_set_config_value(cfg, HIGHSCORES_SECTION, key, val);
		}
	}
	if (!al_save_config_file(path, cfg))
		fprintf(stderr, "Warning: could not save high scores to %s\n", path);
	al_destroy_config(cfg);
}

#else /* !__APPLE__ */

void highscores_load(void)
{
	clear_everything();
}

void highscores_save(void)
{
}

#endif /* __APPLE__ */

void highscores_get_entry(HighscoreLeader which, int rank, char *name3,
		unsigned *floor, unsigned *total_score, unsigned *combo,
		bool *used)
{
	if (which < 0 || which >= HIGHSCORE_LEADER_COUNT)
		return;
	if (rank < 0 || rank >= HIGHSCORES_COUNT)
		return;
	if (name3) {
		memcpy(name3, boards[which][rank].name, 3);
		name3[3] = '\0';
	}
	if (floor)
		*floor = boards[which][rank].floor;
	if (total_score)
		*total_score = boards[which][rank].score;
	if (combo)
		*combo = boards[which][rank].combo;
	if (used)
		*used = boards[which][rank].used;
}

unsigned highscores_get_board_mask(unsigned total_score, unsigned floor,
		unsigned combo)
{
	HighScoreRow t;
	unsigned m = 0;

	memset(&t, 0, sizeof(t));
	t.used = true;
	strcpy(t.name, "ZZZ");
	t.floor = floor;
	t.score = total_score;
	t.combo = combo;

	if (board_qualifies(HIGHSCORE_LEADER_FLOOR, &t))
		m |= HIGHSCORE_BOARD_MASK(HIGHSCORE_LEADER_FLOOR);
	if (board_qualifies(HIGHSCORE_LEADER_SCORE, &t))
		m |= HIGHSCORE_BOARD_MASK(HIGHSCORE_LEADER_SCORE);
	if (board_qualifies(HIGHSCORE_LEADER_COMBO, &t))
		m |= HIGHSCORE_BOARD_MASK(HIGHSCORE_LEADER_COMBO);
	return m;
}

void highscores_submit(const char *initials3, unsigned floor,
		unsigned total_score, unsigned combo)
{
	HighScoreRow new_row;
	bool changed = false;
	int i;

	if (!initials3 || strlen(initials3) != 3)
		return;
	for (i = 0; i < 3; ++i) {
		char c = initials3[i];

		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		if (c < 'A' || c > 'Z')
			return;
		new_row.name[i] = c;
	}
	new_row.name[3] = '\0';
	new_row.floor = floor;
	new_row.score = total_score;
	new_row.combo = combo;
	new_row.used = true;

	changed |= insert_row(HIGHSCORE_LEADER_SCORE, &new_row,
			cmp_score_board, beats_worst_score);
	changed |= insert_row(HIGHSCORE_LEADER_FLOOR, &new_row,
			cmp_floor_board, beats_worst_floor);
	changed |= insert_row(HIGHSCORE_LEADER_COMBO, &new_row,
			cmp_combo_board, beats_worst_combo);

	if (changed)
		highscores_save();
}
