#ifndef _ICY_TOWER_H_
#define _ICY_TOWER_H_

#define KEY_LEFT            0x01
#define KEY_RIGHT           0x02
#define KEY_JUMP            0x10

#define STATUS_IDLE         0
#define STATUS_FLY_UP       1
#define STATUS_FLY_IDLE     2
#define STATUS_FLY_DOWN     3

// Without (__int64), VS 2008 uses SSE optimisation, and the result is wrong
// Since we port to gcc, use int64_t instead
#include <stdint.h>

#define FTOI(f)             ((int)(int64_t)(f))

typedef struct {
	int start, end;
} FLOOR;

typedef struct {
	FLOOR floor[7];
	int start, count, pad;
	unsigned int seed;
	int floor_gen; /* 0: no new_floor while scrolling (ground only); 1: normal */
} FLOORS;

typedef struct {
	double x, y, dx, dy;
	int status;
	int screen_y;
	int speed, speed_counter, zero_speed_skip;
	int rejump, rejump_jumped;
	int combo_timer, combo_count, combo_floor;
	int score, floor, combo;
	FLOORS floors;
} IT_STATE;

void init_empty_state(IT_STATE *its, int rejump);
void seed_state(IT_STATE *its, unsigned int seed);
void init_state(IT_STATE *its, int rejump, unsigned int seed);
/*
 * Regenerate tower floors from seed for competitive play while keeping the
 * current avatar kinematics from lobby; resets score/combo/speed tempo fields.
 */
void play_begin_match_keep_pose(IT_STATE *its, unsigned int seed);
int play_frame(IT_STATE *its, int keys);
int play_frame_peer(IT_STATE *its, int keys);
void play_puppet_ghost_frame(IT_STATE *its);
/*
 * Lobby physics: horizontal movement and ground collision only, no scroll, no
 * new floors, no combo/score updates. Jump key is forced off. Always returns
 * non-zero (the lobby avatar cannot fail).
 */
int play_lobby_frame(IT_STATE *its, int keys);
void handle_keys(IT_STATE *its, int keys);
int jump(IT_STATE *its);
void handle_pos(IT_STATE *its);
void new_floor(FLOORS *floors);
int rand_p(unsigned int *seed);
void handle_collision(IT_STATE *its, int prev_x, int prev_y);
int floor_from_y(FLOORS *floors, int y, int screen_y, int *floor_y, int *floor_xl, int *floor_xr, int *floor_level);
int line_intersection(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, int *xi, int *yi);

#endif // _ICY_TOWER_H_
