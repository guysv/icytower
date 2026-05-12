#ifndef HIGHSCORES_H
#define HIGHSCORES_H

#include <stdbool.h>

#define HIGHSCORES_COUNT 5

typedef enum {
	HIGHSCORE_LEADER_FLOOR,
	HIGHSCORE_LEADER_SCORE,
	HIGHSCORE_LEADER_COMBO,
	HIGHSCORE_LEADER_COUNT
} HighscoreLeader;

#define HIGHSCORE_BOARD_MASK(which) ((unsigned)(1u << (unsigned)(which)))

void highscores_load(void);
void highscores_save(void);

void highscores_get_entry(HighscoreLeader which, int rank, char *name3,
		unsigned *floor, unsigned *total_score, unsigned *combo,
		bool *used);

unsigned highscores_get_board_mask(unsigned total_score, unsigned floor,
		unsigned combo);

void highscores_submit(const char *initials3, unsigned floor,
		unsigned total_score, unsigned combo);

#endif
