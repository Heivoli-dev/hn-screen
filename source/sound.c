// Petits sons de l'interface, identiques à ceux du site (sons.js) :
// une note = sinus + harmoniques 2 (18 %) et 3.01 (6 %), attaque de 8 ms puis décroissance
// exponentielle ; un « glissé » = sinus dont la fréquence monte de façon exponentielle.
// Tout est calculé une fois au démarrage, puis joué avec ndsp.
#include "sound.h"
#include "config.h"
#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define RATE 32000
#define CHANNELS 4
#define VOLUME 0.5f
#define BOOST 2.6f // les haut-parleurs de la console sont plus discrets qu'un ordinateur
#define SETTINGS DATA_DIR "/sons.txt"

typedef struct {
    s16 *pcm;
    int len;
} Clip;

static Clip s_clips[SND_COUNT];
static float *s_mix = NULL;
static int s_mixLen = 0;
static ndspWaveBuf s_wave[CHANNELS];
static int s_next = 0;
static bool s_ready = false, s_enabled = true;

static float osc(int type, float phase)
{
    if (type == 1) { // triangle
        float p = phase - floorf(phase);
        return p < 0.25f ? 4 * p : (p < 0.75f ? 2 - 4 * p : 4 * p - 4);
    }
    return sinf(phase * 6.2831853f);
}

// Enveloppe « exponentialRamp » de Web Audio : 0.0001 -> gain en `att` s, puis -> 0.0001 à `len` s.
static float env(float t, float att, float len, float gain)
{
    const float lo = 0.0001f;
    if (t < 0 || t > len) return 0;
    if (t < att) return lo * powf(gain / lo, t / att);
    return gain * powf(lo / gain, (t - att) / (len - att));
}

static void note(float freq, float start, float len, float gain, int type)
{
    static const float mult[3] = {1, 2, 3.01f}, level[3] = {1, 0.18f, 0.06f};
    int s0 = (int)(start * RATE), n = (int)((len + 0.05f) * RATE);
    for (int i = 0; i < n && s0 + i < s_mixLen; i++) {
        float t = (float)i / RATE, e = env(t, 0.008f, len, gain * VOLUME), v = 0;
        for (int h = 0; h < 3; h++) v += level[h] * osc(h == 0 ? type : 0, freq * mult[h] * t);
        s_mix[s0 + i] += v * e;
    }
}

static void glide(float from, float to, float start, float len, float gain)
{
    int s0 = (int)(start * RATE), n = (int)((len + 0.05f) * RATE);
    double phase = 0;
    for (int i = 0; i < n && s0 + i < s_mixLen; i++) {
        float t = (float)i / RATE;
        float f = t < len ? from * powf(to / from, t / len) : to;
        phase += f / RATE;
        s_mix[s0 + i] += sinf((float)(phase * 6.2831853)) * env(t, 0.01f, len, gain * VOLUME);
    }
}

static void render(SoundId id)
{
    s_mixLen = RATE * 6 / 10; // 0,6 s au plus
    memset(s_mix, 0, sizeof(float) * s_mixLen);
    switch (id) {
    case SND_TAP: note(1760, 0, 0.07f, 0.16f, 0); break;
    case SND_MOVE: note(1760, 0, 0.05f, 0.09f, 0); break; // « tap » plus doux pour la croix
    case SND_TAB: glide(880, 1320, 0, 0.09f, 0.14f); note(1975.5f, 0.05f, 0.16f, 0.12f, 0); break;
    case SND_OPEN: note(1046.5f, 0, 0.12f, 0.16f, 0); note(1568, 0.07f, 0.2f, 0.14f, 0); break;
    case SND_BACK: note(1568, 0, 0.1f, 0.13f, 0); note(1046.5f, 0.06f, 0.14f, 0.12f, 0); break;
    case SND_COPY: note(2093, 0, 0.06f, 0.14f, 0); note(2637, 0.07f, 0.1f, 0.12f, 0); break;
    case SND_SUCCESS: {
        const float f[4] = {1046.5f, 1318.5f, 1568, 2093};
        for (int i = 0; i < 4; i++) note(f[i], i * 0.07f, 0.32f, 0.15f, 0);
        break;
    }
    case SND_ERROR: note(392, 0, 0.16f, 0.2f, 1); note(311.1f, 0.13f, 0.24f, 0.2f, 1); break;
    default: break;
    }
    int last = 0;
    for (int i = 0; i < s_mixLen; i++)
        if (fabsf(s_mix[i]) > 0.0005f) last = i;
    int len = last + 1;
    s16 *pcm = linearAlloc(sizeof(s16) * len);
    if (!pcm) return;
    for (int i = 0; i < len; i++) {
        float v = s_mix[i] * BOOST;
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        pcm[i] = (s16)(v * 32767);
    }
    DSP_FlushDataCache(pcm, sizeof(s16) * len);
    s_clips[id].pcm = pcm;
    s_clips[id].len = len;
}

void sound_init(void)
{
    FILE *f = fopen(SETTINGS, "r");
    if (f) {
        s_enabled = fgetc(f) != '0';
        fclose(f);
    }
    if (R_FAILED(ndspInit())) return; // pas de dspfirm.cdc : l'appli reste muette
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    s_mix = malloc(sizeof(float) * RATE);
    if (!s_mix) {
        ndspExit();
        return;
    }
    for (int i = 0; i < SND_COUNT; i++) render((SoundId)i);
    free(s_mix);
    s_mix = NULL;
    for (int c = 0; c < CHANNELS; c++) {
        ndspChnReset(c);
        ndspChnSetInterp(c, NDSP_INTERP_LINEAR);
        ndspChnSetRate(c, RATE);
        ndspChnSetFormat(c, NDSP_FORMAT_MONO_PCM16);
        float mix[12] = {1, 1};
        ndspChnSetMix(c, mix);
        memset(&s_wave[c], 0, sizeof(ndspWaveBuf));
    }
    s_ready = true;
}

void sound_exit(void)
{
    if (!s_ready) return;
    for (int c = 0; c < CHANNELS; c++) ndspChnWaveBufClear(c);
    ndspExit();
    for (int i = 0; i < SND_COUNT; i++) linearFree(s_clips[i].pcm);
    memset(s_clips, 0, sizeof(s_clips));
    s_ready = false;
}

void sound_play(SoundId id)
{
    if (!s_ready || !s_enabled || id >= SND_COUNT || !s_clips[id].pcm) return;
    int c = s_next;
    s_next = (s_next + 1) % CHANNELS;
    ndspChnWaveBufClear(c);
    memset(&s_wave[c], 0, sizeof(ndspWaveBuf));
    s_wave[c].data_vaddr = s_clips[id].pcm;
    s_wave[c].nsamples = s_clips[id].len;
    ndspChnWaveBufAdd(c, &s_wave[c]);
}

void sound_set_enabled(bool on)
{
    s_enabled = on;
    mkdir(DATA_DIR, 0777);
    FILE *f = fopen(SETTINGS, "w");
    if (f) {
        fputc(on ? '1' : '0', f);
        fclose(f);
    }
}

bool sound_enabled(void) { return s_enabled; }
bool sound_available(void) { return s_ready; }
