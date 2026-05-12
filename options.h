#ifndef OPTIONS_H
#define OPTIONS_H

extern unsigned int character_index;
extern unsigned int start_floor;
extern unsigned int eye_candy;
extern bool fullscreen;
extern unsigned int volume_sfx, volume_music;
extern int key_left;
extern int key_right;
extern int key_jump;
extern int key_pause;
extern bool rejump;
extern char player_initials[4];

void options_load(void);
void options_save(void);
void options_clamp_indices(void);
void options_normalize_player_initials(void);

#endif
