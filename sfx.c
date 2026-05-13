#include <allegro5/allegro.h>
#include <allegro5/allegro_audio.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "options.h"
#include "third_party/sonic/sonic.h"
#include "sfx.h"

ALLEGRO_SAMPLE *sample_aight;
ALLEGRO_SAMPLE *sample_amazing;
ALLEGRO_SAMPLE *sample_cheer;
ALLEGRO_SAMPLE *sample_extreme;
ALLEGRO_SAMPLE *sample_fantastic;
ALLEGRO_SAMPLE *sample_gameover;
ALLEGRO_SAMPLE *sample_good;
ALLEGRO_SAMPLE *sample_great;
ALLEGRO_SAMPLE *sample_hurryup;
ALLEGRO_SAMPLE *sample_menu_change;
ALLEGRO_SAMPLE *sample_menu_choose;
ALLEGRO_SAMPLE *sample_ring;
ALLEGRO_SAMPLE *sample_splat;
ALLEGRO_SAMPLE *sample_splendid;
ALLEGRO_SAMPLE *sample_step;
ALLEGRO_SAMPLE *sample_super;
ALLEGRO_SAMPLE *sample_sweet;
ALLEGRO_SAMPLE *sample_tryagain;
ALLEGRO_SAMPLE *sample_unbelievable;
ALLEGRO_SAMPLE *sample_wow;

ALLEGRO_SAMPLE *sample_harold_edge;
ALLEGRO_SAMPLE *sample_harold_falling;
ALLEGRO_SAMPLE *sample_harold_jump_hi;
ALLEGRO_SAMPLE *sample_harold_jump_lo;
ALLEGRO_SAMPLE *sample_harold_jump_mid;
ALLEGRO_SAMPLE *sample_harold_wazup;
ALLEGRO_SAMPLE *sample_harold_yo;

ALLEGRO_SAMPLE *sample_disco_dave_ahey;
ALLEGRO_SAMPLE *sample_disco_dave_cmonyo;
ALLEGRO_SAMPLE *sample_disco_dave_diggin;
ALLEGRO_SAMPLE *sample_disco_dave_goinon;
ALLEGRO_SAMPLE *sample_disco_dave_ho;
ALLEGRO_SAMPLE *sample_disco_dave_stayinalive;
ALLEGRO_SAMPLE *sample_disco_dave_watchit;

typedef struct sfx_bg_stream {
	ALLEGRO_AUDIO_STREAM *stream;
	sonicStream sonic;
	short *src_pcm;
	unsigned src_frames;
	int ch;
	unsigned rate;
	unsigned src_pos;
	unsigned frag_frames;
} sfx_bg_stream;

static sfx_bg_stream bg_menu_ctx;
static sfx_bg_stream bg_beat_ctx;
static sfx_bg_stream bg_dave_ctx;

static ALLEGRO_EVENT_QUEUE *sfx_bgm_event_queue;

#define LOAD_SAMPLE(name) (sample_##name = al_load_sample("sfx/" #name ".ogg"))

#define LOAD_SAMPLE_CHARACTER(character, name)\
	(sample_##character##_##name = al_load_sample("sfx/" #character "/" #name ".ogg"))

#define DESTROY_SAMPLE(name) do {\
	al_destroy_sample(sample_##name);\
	sample_##name = NULL;\
} while (false)

#define DESTROY_SAMPLE_CHARACTER(character, name) do {\
	al_destroy_sample(sample_##character##_##name);\
	sample_##character##_##name = NULL;\
} while (false)

void sfx_apply_music_volume(void);

static ALLEGRO_CHANNEL_CONF sfx_chan_conf(int ch)
{
	return ch >= 2 ? ALLEGRO_CHANNEL_CONF_2 : ALLEGRO_CHANNEL_CONF_1;
}

static bool sfx_sample_to_s16(ALLEGRO_SAMPLE *s, short **outpcm,
		unsigned *frames, int *channels, unsigned *rate)
{
	void *data = al_get_sample_data(s);
	unsigned nf = al_get_sample_length(s);
	unsigned freq = al_get_sample_frequency(s);
	ALLEGRO_AUDIO_DEPTH depth = al_get_sample_depth(s);
	ALLEGRO_CHANNEL_CONF conf = al_get_sample_channels(s);
	int ch = (conf == ALLEGRO_CHANNEL_CONF_2) ? 2 : 1;
	size_t count = (size_t)nf * (size_t)ch;

	if (!data || nf == 0 || freq == 0)
		return false;

	*frames = nf;
	*channels = ch;
	*rate = freq;
	*outpcm = malloc(count * sizeof(short));
	if (!*outpcm)
		return false;

	switch (depth) {
	case ALLEGRO_AUDIO_DEPTH_INT16: {
		memcpy(*outpcm, data, count * sizeof(short));
		break;
	}
	case ALLEGRO_AUDIO_DEPTH_UINT16: {
		const unsigned short *u = data;
		short *d = *outpcm;

		for (size_t i = 0; i < count; ++i)
			d[i] = (short)((int)u[i] - 32768);
		break;
	}
	case ALLEGRO_AUDIO_DEPTH_FLOAT32: {
		const float *f = data;
		short *d = *outpcm;

		for (size_t i = 0; i < count; ++i) {
			float x = f[i] * 32767.0f;

			if (x > 32767.0f)
				x = 32767.0f;
			else if (x < -32768.0f)
				x = -32768.0f;
			d[i] = (short)x;
		}
		break;
	}
	default:
		free(*outpcm);
		*outpcm = NULL;
		return false;
	}
	return true;
}

static void sfx_feed_sonic_wrapped(sonicStream st, const short *pcm,
		unsigned total_frames, int ch, unsigned *pos, int feed_frames)
{
	int left = feed_frames;

	while (left > 0) {
		unsigned tail = total_frames - *pos;
		int chunk = (int)tail;

		if (chunk > left)
			chunk = left;
		sonicWriteShortToStream(st, pcm + (size_t)(*pos) * (size_t)ch, chunk);
		*pos = (*pos + (unsigned)chunk) % total_frames;
		left -= chunk;
	}
}

static void sfx_bg_fill_fragment_into(sfx_bg_stream *ctx, short *buf)
{
	unsigned need = ctx->frag_frames;
	unsigned filled = 0;
	int ch = ctx->ch;
	short tmp[8192];
	int max_chunk_frames = (int)(sizeof(tmp) / sizeof(tmp[0]) / (size_t)ch);
	int stall = 0;

	if (!ctx->sonic || max_chunk_frames <= 0)
		goto silence;

	while (filled < need) {
		unsigned remaining = need - filled;

		while (sonicSamplesAvailable(ctx->sonic) < (int)remaining) {
			sfx_feed_sonic_wrapped(ctx->sonic, ctx->src_pcm,
					ctx->src_frames, ch, &ctx->src_pos, 2048);
			if (++stall > 256) {
				stall = 0;
				break;
			}
		}

		{
			int want = (int)remaining;
			int avail = sonicSamplesAvailable(ctx->sonic);

			if (want > avail)
				want = avail;
			if (want > max_chunk_frames)
				want = max_chunk_frames;
			if (want <= 0)
				goto silence;
			{
				int n = sonicReadShortFromStream(ctx->sonic, tmp, want);

				if (n <= 0)
					goto silence;
				memcpy(buf + filled * (size_t)ch, tmp,
						(size_t)n * (size_t)ch * sizeof(short));
				filled += (unsigned)n;
				stall = 0;
			}
		}
	}

	al_set_audio_stream_fragment(ctx->stream, buf);
	return;

silence:
	memset(buf + filled * (size_t)ch, 0,
			(need - filled) * (size_t)ch * sizeof(short));
	al_set_audio_stream_fragment(ctx->stream, buf);
}

static sfx_bg_stream *sfx_bg_stream_for_event_source(
		const ALLEGRO_EVENT_SOURCE *src)
{
	if (bg_menu_ctx.stream
			&& src == al_get_audio_stream_event_source(bg_menu_ctx.stream))
		return &bg_menu_ctx;
	if (bg_beat_ctx.stream
			&& src == al_get_audio_stream_event_source(bg_beat_ctx.stream))
		return &bg_beat_ctx;
	if (bg_dave_ctx.stream
			&& src == al_get_audio_stream_event_source(bg_dave_ctx.stream))
		return &bg_dave_ctx;
	return NULL;
}

void sfx_register_bgm_event_sources(ALLEGRO_EVENT_QUEUE *queue)
{
	sfx_bgm_event_queue = queue;
	al_register_event_source(queue,
			al_get_audio_stream_event_source(bg_menu_ctx.stream));
	al_register_event_source(queue,
			al_get_audio_stream_event_source(bg_beat_ctx.stream));
	al_register_event_source(queue,
			al_get_audio_stream_event_source(bg_dave_ctx.stream));
}

static void sfx_unregister_bgm_event_sources(void)
{
	if (!sfx_bgm_event_queue)
		return;
	if (bg_menu_ctx.stream)
		al_unregister_event_source(sfx_bgm_event_queue,
				al_get_audio_stream_event_source(bg_menu_ctx.stream));
	if (bg_beat_ctx.stream)
		al_unregister_event_source(sfx_bgm_event_queue,
				al_get_audio_stream_event_source(bg_beat_ctx.stream));
	if (bg_dave_ctx.stream)
		al_unregister_event_source(sfx_bgm_event_queue,
				al_get_audio_stream_event_source(bg_dave_ctx.stream));
	sfx_bgm_event_queue = NULL;
}

void sfx_handle_audio_stream_fragment(const ALLEGRO_EVENT *event)
{
	sfx_bg_stream *ctx;
	void *frag;

	if (event->type != ALLEGRO_EVENT_AUDIO_STREAM_FRAGMENT)
		return;
	ctx = sfx_bg_stream_for_event_source(event->any.source);
	if (!ctx || !ctx->stream)
		return;
	frag = al_get_audio_stream_fragment(ctx->stream);
	if (!frag)
		return;
	sfx_bg_fill_fragment_into(ctx, frag);
}

static void sfx_bg_stream_stop(sfx_bg_stream *ctx)
{
	if (ctx->stream)
		al_set_audio_stream_playing(ctx->stream, false);
}

static void sfx_bg_stream_start(sfx_bg_stream *ctx)
{
	if (!ctx->stream || !ctx->src_pcm || ctx->src_frames == 0)
		return;

	if (ctx->sonic) {
		sonicDestroyStream(ctx->sonic);
		ctx->sonic = NULL;
	}

	ctx->sonic = sonicCreateStream((int)ctx->rate, ctx->ch);
	if (!ctx->sonic)
		return;

	sonicSetQuality(ctx->sonic, 0);
	sonicSetSpeed(ctx->sonic, options_playback_speed());
	ctx->src_pos = 0;
	al_rewind_audio_stream(ctx->stream);
	al_set_audio_stream_playing(ctx->stream, true);
}

static void sfx_bg_stream_destroy(sfx_bg_stream *ctx)
{
	if (ctx->sonic) {
		sonicDestroyStream(ctx->sonic);
		ctx->sonic = NULL;
	}
	if (ctx->stream) {
		al_set_audio_stream_playing(ctx->stream, false);
		al_detach_audio_stream(ctx->stream);
		al_destroy_audio_stream(ctx->stream);
		ctx->stream = NULL;
	}
	free(ctx->src_pcm);
	ctx->src_pcm = NULL;
	ctx->src_frames = 0;
	ctx->frag_frames = 0;
	ctx->src_pos = 0;
}

static bool sfx_bg_stream_create(sfx_bg_stream *ctx)
{
	ALLEGRO_AUDIO_STREAM *s;

	s = al_create_audio_stream(4u, 2048u, ctx->rate,
			ALLEGRO_AUDIO_DEPTH_INT16, sfx_chan_conf(ctx->ch));
	if (!s)
		return false;

	ctx->stream = s;
	ctx->frag_frames = al_get_audio_stream_length(s);
	al_set_audio_stream_playmode(s, ALLEGRO_PLAYMODE_ONCE);
	al_set_audio_stream_playing(s, false);

	if (!al_attach_audio_stream_to_mixer(s, al_get_default_mixer())) {
		al_destroy_audio_stream(s);
		ctx->stream = NULL;
		return false;
	}

	return true;
}

static bool sfx_load_bg_track(const char *path, sfx_bg_stream *ctx)
{
	ALLEGRO_SAMPLE *tmp = al_load_sample(path);

	if (!tmp)
		return false;
	if (!sfx_sample_to_s16(tmp, &ctx->src_pcm, &ctx->src_frames,
				&ctx->ch, &ctx->rate)) {
		al_destroy_sample(tmp);
		return false;
	}
	al_destroy_sample(tmp);

	ctx->src_pos = 0;
	ctx->sonic = NULL;

	if (!sfx_bg_stream_create(ctx)) {
		free(ctx->src_pcm);
		ctx->src_pcm = NULL;
		ctx->src_frames = 0;
		return false;
	}

	return true;
}

static void sfx_destroy_bgm_streams(void)
{
	sfx_unregister_bgm_event_sources();
	sfx_bg_stream_destroy(&bg_menu_ctx);
	sfx_bg_stream_destroy(&bg_beat_ctx);
	sfx_bg_stream_destroy(&bg_dave_ctx);
}

static bool sfx_init_bgm_streams(void)
{
	memset(&bg_menu_ctx, 0, sizeof(bg_menu_ctx));
	memset(&bg_beat_ctx, 0, sizeof(bg_beat_ctx));
	memset(&bg_dave_ctx, 0, sizeof(bg_dave_ctx));

	if (!sfx_load_bg_track("sfx/bg_menu.ogg", &bg_menu_ctx))
		return false;
	if (!sfx_load_bg_track("sfx/bg_beat.ogg", &bg_beat_ctx))
		return false;
	if (!sfx_load_bg_track("sfx/disco_dave/bg_dave.ogg", &bg_dave_ctx))
		return false;

	return true;
}

bool sfx_load_audio_streams_and_samples(void)
{
	if (!LOAD_SAMPLE(aight))
		goto destroy;
	if (!LOAD_SAMPLE(amazing))
		goto destroy;
	if (!LOAD_SAMPLE(cheer))
		goto destroy;
	if (!LOAD_SAMPLE(extreme))
		goto destroy;
	if (!LOAD_SAMPLE(fantastic))
		goto destroy;
	if (!LOAD_SAMPLE(gameover))
		goto destroy;
	if (!LOAD_SAMPLE(good))
		goto destroy;
	if (!LOAD_SAMPLE(great))
		goto destroy;
	if (!LOAD_SAMPLE(hurryup))
		goto destroy;
	if (!LOAD_SAMPLE(menu_change))
		goto destroy;
	if (!LOAD_SAMPLE(menu_choose))
		goto destroy;
	if (!LOAD_SAMPLE(ring))
		goto destroy;
	if (!LOAD_SAMPLE(splat))
		goto destroy;
	if (!LOAD_SAMPLE(splendid))
		goto destroy;
	if (!LOAD_SAMPLE(step))
		goto destroy;
	if (!LOAD_SAMPLE(super))
		goto destroy;
	if (!LOAD_SAMPLE(sweet))
		goto destroy;
	if (!LOAD_SAMPLE(tryagain))
		goto destroy;
	if (!LOAD_SAMPLE(unbelievable))
		goto destroy;
	if (!LOAD_SAMPLE(wow))
		goto destroy;

	if (!LOAD_SAMPLE_CHARACTER(harold, edge))
		goto destroy;
	if (!LOAD_SAMPLE_CHARACTER(harold, falling))
		goto destroy;
	if (!LOAD_SAMPLE_CHARACTER(harold, jump_hi))
		goto destroy;
	if (!LOAD_SAMPLE_CHARACTER(harold, jump_lo))
		goto destroy;
	if (!LOAD_SAMPLE_CHARACTER(harold, jump_mid))
		goto destroy;
	if (!LOAD_SAMPLE_CHARACTER(harold, wazup))
		goto destroy;
	if (!LOAD_SAMPLE_CHARACTER(harold, yo))
		goto destroy;

	if (!LOAD_SAMPLE_CHARACTER(disco_dave, ahey))
		goto destroy;
	if (!LOAD_SAMPLE_CHARACTER(disco_dave, cmonyo))
		goto destroy;
	if (!LOAD_SAMPLE_CHARACTER(disco_dave, diggin))
		goto destroy;
	if (!LOAD_SAMPLE_CHARACTER(disco_dave, goinon))
		goto destroy;
	if (!LOAD_SAMPLE_CHARACTER(disco_dave, ho))
		goto destroy;
	if (!LOAD_SAMPLE_CHARACTER(disco_dave, stayinalive))
		goto destroy;
	if (!LOAD_SAMPLE_CHARACTER(disco_dave, watchit))
		goto destroy;

	if (!sfx_init_bgm_streams())
		goto destroy;

	return true;
destroy:
	sfx_destroy_audio_streams_and_samples();
	return false;
}

void sfx_apply_music_volume(void)
{
	float g = volume_music / 10.0f;

	if (bg_menu_ctx.stream)
		al_set_audio_stream_gain(bg_menu_ctx.stream, g);
	if (bg_beat_ctx.stream)
		al_set_audio_stream_gain(bg_beat_ctx.stream, g);
	if (bg_dave_ctx.stream)
		al_set_audio_stream_gain(bg_dave_ctx.stream, g);
}

void sfx_apply_playback_speed(void)
{
	float s = options_playback_speed();

	if (bg_menu_ctx.sonic)
		sonicSetSpeed(bg_menu_ctx.sonic, s);
	if (bg_beat_ctx.sonic)
		sonicSetSpeed(bg_beat_ctx.sonic, s);
	if (bg_dave_ctx.sonic)
		sonicSetSpeed(bg_dave_ctx.sonic, s);
}

/*
 * Foreground samples follow gameplay BPM via Allegro resampling (pitch morphs).
 * Looping backgrounds: Sonic time-stretch in the audio stream fragment path.
 */
void sfx_play_sample(ALLEGRO_SAMPLE *spl, float gain,
		ALLEGRO_PLAYMODE mode)
{
	al_play_sample(spl, gain, 0.0f, options_playback_speed(), mode, NULL);
}

void sfx_bgm_stop_menu(void)
{
	sfx_bg_stream_stop(&bg_menu_ctx);
}

void sfx_bgm_play_menu(void)
{
	sfx_bg_stream_start(&bg_menu_ctx);
}

void sfx_bgm_stop_character(unsigned character_index)
{
	if (character_index == 1)
		sfx_bg_stream_stop(&bg_dave_ctx);
	else
		sfx_bg_stream_stop(&bg_beat_ctx);
}

void sfx_bgm_play_character(unsigned character_index)
{
	if (character_index == 1)
		sfx_bg_stream_start(&bg_dave_ctx);
	else
		sfx_bg_stream_start(&bg_beat_ctx);
}

void sfx_destroy_audio_streams_and_samples(void)
{
	sfx_destroy_bgm_streams();

	DESTROY_SAMPLE(aight);
	DESTROY_SAMPLE(amazing);
	DESTROY_SAMPLE(cheer);
	DESTROY_SAMPLE(extreme);
	DESTROY_SAMPLE(fantastic);
	DESTROY_SAMPLE(gameover);
	DESTROY_SAMPLE(good);
	DESTROY_SAMPLE(great);
	DESTROY_SAMPLE(hurryup);
	DESTROY_SAMPLE(menu_change);
	DESTROY_SAMPLE(menu_choose);
	DESTROY_SAMPLE(ring);
	DESTROY_SAMPLE(splat);
	DESTROY_SAMPLE(splendid);
	DESTROY_SAMPLE(step);
	DESTROY_SAMPLE(super);
	DESTROY_SAMPLE(sweet);
	DESTROY_SAMPLE(tryagain);
	DESTROY_SAMPLE(unbelievable);
	DESTROY_SAMPLE(wow);

	DESTROY_SAMPLE_CHARACTER(harold, edge);
	DESTROY_SAMPLE_CHARACTER(harold, falling);
	DESTROY_SAMPLE_CHARACTER(harold, jump_hi);
	DESTROY_SAMPLE_CHARACTER(harold, jump_lo);
	DESTROY_SAMPLE_CHARACTER(harold, jump_mid);
	DESTROY_SAMPLE_CHARACTER(harold, wazup);
	DESTROY_SAMPLE_CHARACTER(harold, yo);

	DESTROY_SAMPLE_CHARACTER(disco_dave, ahey);
	DESTROY_SAMPLE_CHARACTER(disco_dave, cmonyo);
	DESTROY_SAMPLE_CHARACTER(disco_dave, diggin);
	DESTROY_SAMPLE_CHARACTER(disco_dave, goinon);
	DESTROY_SAMPLE_CHARACTER(disco_dave, ho);
	DESTROY_SAMPLE_CHARACTER(disco_dave, stayinalive);
	DESTROY_SAMPLE_CHARACTER(disco_dave, watchit);
}
