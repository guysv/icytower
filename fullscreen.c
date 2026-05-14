#include <allegro5/allegro.h>

#include "fullscreen.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static void viewport_fit_logical(ALLEGRO_DISPLAY *display)
{
	float width = (float)al_get_display_width(display);
	float height = (float)al_get_display_height(display);
	float scale = MIN(width / (float)ICYTOWER_LOGICAL_W,
			height / (float)ICYTOWER_LOGICAL_H);
	float offset_x = 0.5f * (width - scale * (float)ICYTOWER_LOGICAL_W);
	float offset_y = 0.5f * (height - scale * (float)ICYTOWER_LOGICAL_H);
	ALLEGRO_TRANSFORM t;

	al_identity_transform(&t);
	al_scale_transform(&t, scale, scale);
	al_translate_transform(&t, offset_x, offset_y);
	al_use_transform(&t);

	al_set_clipping_rectangle(
			(int)(offset_x + 0.5f),
			(int)(offset_y + 0.5f),
			(int)((float)ICYTOWER_LOGICAL_W * scale + 0.5f),
			(int)((float)ICYTOWER_LOGICAL_H * scale + 0.5f));
}

void icytower_apply_window_viewport(void)
{
	ALLEGRO_DISPLAY *display = al_get_current_display();

	if (display != NULL)
		viewport_fit_logical(display);
}

void enable_fullscreen(void) {
	ALLEGRO_DISPLAY *display = al_get_current_display();

	if (display == NULL)
		return;
	al_set_display_flag(display, ALLEGRO_FULLSCREEN_WINDOW, true);
	viewport_fit_logical(display);

	al_clear_to_color(al_map_rgb(0, 0, 0));
	al_flip_display();
	al_clear_to_color(al_map_rgb(0, 0, 0));
}

void disable_fullscreen(void) {
	ALLEGRO_DISPLAY *display = al_get_current_display();

	if (display == NULL)
		return;
	al_set_display_flag(display, ALLEGRO_FULLSCREEN_WINDOW, false);
	(void)al_resize_display(display, ICYTOWER_WINDOW_W, ICYTOWER_WINDOW_H);
	viewport_fit_logical(display);
}
