#ifndef COMBO_TRAIL_H
#define COMBO_TRAIL_H

#include "physics.h"

void combo_trail_init(void);
void combo_trail_kill(void);
void combo_trail_tick(const IT_STATE *its, int rotating_animation);
void combo_trail_draw(unsigned animation_frame);

#endif
