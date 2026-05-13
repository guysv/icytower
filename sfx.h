#include <allegro5/allegro.h>
#include <allegro5/allegro_audio.h>
#include <stdbool.h>

extern ALLEGRO_SAMPLE *sample_aight;
extern ALLEGRO_SAMPLE *sample_amazing;
extern ALLEGRO_SAMPLE *sample_cheer;
extern ALLEGRO_SAMPLE *sample_extreme;
extern ALLEGRO_SAMPLE *sample_fantastic;
extern ALLEGRO_SAMPLE *sample_gameover;
extern ALLEGRO_SAMPLE *sample_good;
extern ALLEGRO_SAMPLE *sample_great;
extern ALLEGRO_SAMPLE *sample_hurryup;
extern ALLEGRO_SAMPLE *sample_menu_change;
extern ALLEGRO_SAMPLE *sample_menu_choose;
extern ALLEGRO_SAMPLE *sample_ring;
extern ALLEGRO_SAMPLE *sample_splat;
extern ALLEGRO_SAMPLE *sample_splendid;
extern ALLEGRO_SAMPLE *sample_step;
extern ALLEGRO_SAMPLE *sample_super;
extern ALLEGRO_SAMPLE *sample_sweet;
extern ALLEGRO_SAMPLE *sample_tryagain;
extern ALLEGRO_SAMPLE *sample_unbelievable;
extern ALLEGRO_SAMPLE *sample_wow;

extern ALLEGRO_SAMPLE *sample_harold_edge;
extern ALLEGRO_SAMPLE *sample_harold_falling;
extern ALLEGRO_SAMPLE *sample_harold_jump_hi;
extern ALLEGRO_SAMPLE *sample_harold_jump_lo;
extern ALLEGRO_SAMPLE *sample_harold_jump_mid;
extern ALLEGRO_SAMPLE *sample_harold_wazup;
extern ALLEGRO_SAMPLE *sample_harold_yo;

extern ALLEGRO_SAMPLE *sample_disco_dave_ahey;
extern ALLEGRO_SAMPLE *sample_disco_dave_cmonyo;
extern ALLEGRO_SAMPLE *sample_disco_dave_diggin;
extern ALLEGRO_SAMPLE *sample_disco_dave_goinon;
extern ALLEGRO_SAMPLE *sample_disco_dave_ho;
extern ALLEGRO_SAMPLE *sample_disco_dave_stayinalive;
extern ALLEGRO_SAMPLE *sample_disco_dave_watchit;

bool sfx_load_audio_streams_and_samples(void);
void sfx_register_bgm_event_sources(ALLEGRO_EVENT_QUEUE *queue);
void sfx_handle_audio_stream_fragment(const ALLEGRO_EVENT *event);
void sfx_destroy_audio_streams_and_samples(void);
void sfx_apply_music_volume(void);
void sfx_apply_playback_speed(void);
void sfx_play_sample(ALLEGRO_SAMPLE *spl, float gain,
		ALLEGRO_PLAYMODE mode);
void sfx_bgm_play_menu(void);
void sfx_bgm_stop_menu(void);
void sfx_bgm_play_character(unsigned character_index);
void sfx_bgm_stop_character(unsigned character_index);
