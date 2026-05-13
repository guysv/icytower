#ifndef OPTIONS_H
#define OPTIONS_H

/* Reference tempo: gameplay and audio match this baseline at option_bpm. */
#define OPTIONS_BPM_REFERENCE 130u
#define OPTIONS_BPM_DEFAULT OPTIONS_BPM_REFERENCE
#define OPTIONS_BPM_MIN 60u
#define OPTIONS_BPM_MAX 240u

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
extern unsigned int option_bpm;

float options_playback_speed(void);

void options_load(void);
void options_save(void);
void options_clamp_indices(void);
void options_normalize_player_initials(void);

#endif
