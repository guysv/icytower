#include "combo_trail.h"

#include <allegro5/allegro.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include "gfx.h"
#include "options.h"
#include "physics.h"

extern IT_STATE it_state;

#define COMBO_TRAIL_MAX 48
#define SPAWN_EVERY 3
#define CULL_MARGIN 32
#define FEET_JITTER 10
#define STAR_GRAVITY 0.75f
#define STAR_SPAWN_SPEED 3.0f
#define VEL_EPS 1e-4f
#define SPAWN_SPRAY_DEG 5.0f

typedef struct {
	float x, y, vx, vy;
	unsigned char phase;
	bool active;
} ComboTrailStar;

static ComboTrailStar stars[COMBO_TRAIL_MAX];
static int prev_screen_y;

static void combo_trail_clear(void)
{
	int i;

	for (i = 0; i < COMBO_TRAIL_MAX; i++)
		stars[i].active = false;
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

static void spawn_one(float x, float y, float vx, float vy)
{
	int i;
	ComboTrailStar *s;

	i = find_free_slot();
	if (i < 0)
		return;
	s = &stars[i];
	s->active = true;
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
	spawn_one(x, y, bx * c5 - by * s5, bx * s5 + by * c5);
	spawn_one(x, y, bx * c5 + by * s5, -bx * s5 + by * c5);
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
	combo_trail_clear();
	prev_screen_y = it_state.screen_y;
}

void combo_trail_kill(void)
{
	combo_trail_clear();
}

void combo_trail_tick(const IT_STATE *its, int rotating_animation)
{
	int dy;
	static unsigned tick;

	if (eye_candy != 2u) {
		combo_trail_clear();
		prev_screen_y = its->screen_y;
		return;
	}

	if (its->combo_timer <= 0 || its->combo_count <= 0) {
		combo_trail_clear();
		prev_screen_y = its->screen_y;
		return;
	}

	dy = its->screen_y - prev_screen_y;
	prev_screen_y = its->screen_y;

	apply_scroll_gravity_cull(dy);

	tick++;
	if (tick % SPAWN_EVERY == 0 && rotating_animation)
		try_spawn(its);
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
