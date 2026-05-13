#include "combo_trail.h"

#include <allegro5/allegro.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include "gfx.h"
#include "options.h"
#include "physics.h"

extern IT_STATE it_state;

#define COMBO_TRAIL_MAX 96
#define SPAWN_EVERY 3
#define CULL_MARGIN 32
#define FEET_JITTER 10
#define STAR_GRAVITY 0.375f
#define STAR_SPAWN_SPEED 3.0f
#define VEL_EPS 1e-4f
#define SPAWN_SPRAY_DEG 5.0f
#define MILESTONE_BURST_SPEED 4.25f
#define MILESTONE_BURST_STRONG_MUL 3.0f
#define MILESTONE_SPEED_MUL_MIN 1.0f
#define MILESTONE_SPEED_MUL_MAX 3.0f
#define MILESTONE_SPAWN_X_MARGIN 16.0f
#define MILESTONE_SPAWN_Y 476.0f
#define MILESTONE_ANGLE_MIN -20.0f
#define MILESTONE_ANGLE_SPAN 40.0f

typedef struct {
	float x, y, vx, vy;
	unsigned char phase;
	bool active;
	bool combo_owned;
} ComboTrailStar;

static ComboTrailStar stars[COMBO_TRAIL_MAX];
static int prev_screen_y;

static void trail_clear_all(void)
{
	int i;

	for (i = 0; i < COMBO_TRAIL_MAX; i++)
		stars[i].active = false;
}

static void trail_clear_combo(void)
{
	int i;

	for (i = 0; i < COMBO_TRAIL_MAX; i++) {
		if (stars[i].active && stars[i].combo_owned)
			stars[i].active = false;
	}
}

static ALLEGRO_BITMAP *star_bitmap(unsigned ix)
{
	switch (ix % 8u) {
	case 0:
		return bitmap_star01;
	case 1:
		return bitmap_star02;
	case 2:
		return bitmap_star03;
	case 3:
		return bitmap_star04;
	case 4:
		return bitmap_star05;
	case 5:
		return bitmap_star06;
	case 6:
		return bitmap_star07;
	default:
		return bitmap_star08;
	}
}

static int find_free_slot(void)
{
	int i;

	for (i = 0; i < COMBO_TRAIL_MAX; i++) {
		if (!stars[i].active)
			return i;
	}
	return -1;
}

static void spawn_one(float x, float y, float vx, float vy, bool combo_owned)
{
	int i;
	ComboTrailStar *s;

	i = find_free_slot();
	if (i < 0)
		return;
	s = &stars[i];
	s->active = true;
	s->combo_owned = combo_owned;
	s->x = x;
	s->y = y;
	s->vx = vx;
	s->vy = vy;
	s->phase = (unsigned char)(rand() % 8);
}

static void try_spawn(const IT_STATE *its)
{
	float jx, jy;
	float dx, dy, mag, inv;
	float bx, by;
	float c5, s5;
	float x, y;

	jx = (float)(rand() % (FEET_JITTER * 2 + 1) - FEET_JITTER);
	jy = (float)(rand() % (FEET_JITTER + 1));
	x = (float)its->x + jx;
	y = (float)its->y + jy - (float)FEET_JITTER;

	dx = (float)its->dx;
	dy = (float)its->dy;
	mag = sqrtf(dx * dx + dy * dy);
	if (mag < VEL_EPS) {
		bx = 0.0f;
		by = 0.0f;
	} else {
		inv = STAR_SPAWN_SPEED / mag;
		bx = -dx * inv;
		by = -dy * inv;
	}

	c5 = cosf(SPAWN_SPRAY_DEG * ALLEGRO_PI / 180.0f);
	s5 = sinf(SPAWN_SPRAY_DEG * ALLEGRO_PI / 180.0f);

	/* +5 deg and -5 deg from base kick velocity */
	spawn_one(x, y, bx * c5 - by * s5, bx * s5 + by * c5, true);
	spawn_one(x, y, bx * c5 + by * s5, -bx * s5 + by * c5, true);
}

static void apply_scroll_gravity_cull(int dy)
{
	int i;
	ALLEGRO_BITMAP *b;
	int h, maxy;

	for (i = 0; i < COMBO_TRAIL_MAX; i++) {
		if (!stars[i].active)
			continue;
		stars[i].y += (float)dy;
		stars[i].x += stars[i].vx;
		stars[i].vy += STAR_GRAVITY;
		stars[i].y += stars[i].vy;
		b = star_bitmap(stars[i].phase);
		h = al_get_bitmap_height(b);
		maxy = 480 + CULL_MARGIN + h;
		if (stars[i].y > maxy || stars[i].y < -(float)CULL_MARGIN
				|| stars[i].x < -(float)CULL_MARGIN
				|| stars[i].x > 640.0f + (float)CULL_MARGIN)
			stars[i].active = false;
	}
}

void combo_trail_init(void)
{
	trail_clear_all();
	prev_screen_y = it_state.screen_y;
}

void combo_trail_kill(void)
{
	trail_clear_all();
}

void combo_trail_tick(const IT_STATE *its, int rotating_animation)
{
	int dy;
	static unsigned tick;

	if (eye_candy != 2u) {
		trail_clear_all();
		prev_screen_y = its->screen_y;
		return;
	}

	dy = its->screen_y - prev_screen_y;
	prev_screen_y = its->screen_y;
	apply_scroll_gravity_cull(dy);

	if (its->combo_timer > 0 && its->combo_count > 0) {
		tick++;
		if (tick % SPAWN_EVERY == 0 && rotating_animation)
			try_spawn(its);
	} else {
		trail_clear_combo();
	}
}

void combo_trail_milestone_burst(const IT_STATE *its, int milestone_floor)
{
	int j, n;
	float xl, xr, span, sx, ang_deg, ang_rad, speed_base, mul, speed;

	if (eye_candy != 2u || milestone_floor <= 0)
		return;

	(void)its;

	xl = MILESTONE_SPAWN_X_MARGIN;
	xr = 640.0f - MILESTONE_SPAWN_X_MARGIN;
	span = xr - xl;
	n = (int)(span / 12.0f);
	if (n < 10)
		n = 10;
	if (n > 64)
		n = 64;

	speed_base = MILESTONE_BURST_SPEED;
	if (milestone_floor == 50)
		speed_base *= MILESTONE_BURST_STRONG_MUL;

	for (j = 0; j < n; j++) {
		float u = ((float)j + (float)rand() / (float)RAND_MAX) / (float)n;

		sx = xl + u * span;
		mul = MILESTONE_SPEED_MUL_MIN
				+ ((float)rand() / (float)RAND_MAX)
					* (MILESTONE_SPEED_MUL_MAX - MILESTONE_SPEED_MUL_MIN);
		speed = speed_base * mul;
		ang_deg = MILESTONE_ANGLE_MIN
				+ ((float)rand() / (float)RAND_MAX) * MILESTONE_ANGLE_SPAN;
		ang_rad = ang_deg * ALLEGRO_PI / 180.0f;
		spawn_one(sx, MILESTONE_SPAWN_Y,
				speed * sinf(ang_rad),
				-speed * cosf(ang_rad),
				false);
	}
}

void combo_trail_draw(unsigned animation_frame)
{
	int i;
	ALLEGRO_BITMAP *b;
	int w, h;

	if (eye_candy != 2u)
		return;

	for (i = 0; i < COMBO_TRAIL_MAX; i++) {
		if (!stars[i].active)
			continue;
		b = star_bitmap((unsigned)stars[i].phase + animation_frame);
		w = al_get_bitmap_width(b);
		h = al_get_bitmap_height(b);
		al_draw_bitmap(b,
				(int)(stars[i].x - (float)(w / 2)),
				(int)(stars[i].y - (float)h),
				0);
	}
}
