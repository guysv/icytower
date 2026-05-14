#include "lan_clock.h"

#include <allegro5/allegro.h>

uint64_t lan_now_ms(void)
{
	double t = al_get_time();

	if (t < 0.0)
		t = 0.0;
	return (uint64_t)(t * 1000.0);
}
