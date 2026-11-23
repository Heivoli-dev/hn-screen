#pragma once
#include <stdbool.h>

// Les mêmes petits sons que sur heivoli-network.fr (sons.js), recalculés sur la console.
typedef enum { SND_TAP, SND_MOVE, SND_TAB, SND_OPEN, SND_BACK, SND_COPY, SND_SUCCESS, SND_ERROR, SND_COUNT } SoundId;

void sound_init(void);   // silencieux si le son de la console est indisponible (dspfirm.cdc absent)
void sound_exit(void);
void sound_play(SoundId id);
void sound_set_enabled(bool on);
bool sound_enabled(void);
bool sound_available(void);
