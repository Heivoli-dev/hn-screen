#pragma once
#include "img.h"
#include <stdbool.h>
#include <time.h>

typedef enum { SRC_ROSALINA, SRC_SHORTCUT } ShotSource;

typedef struct {
    char base[64];     // « 2026-11-21_12-05-03.123 »
    char dir[48];      // dossier des fichiers BMP
    ShotSource src;
    bool hasTop, hasBot;
    time_t t;          // date de la capture (heure locale de la console)
    char game[64];     // vide = à trier
    int thumbState;    // 0 = pas chargée, 1 = prête, 2 = échec
    GpuImage thumb;
} Shot;

typedef struct {
    Shot *items;
    int count, cap;
} ShotList;

void shots_scan(ShotList *l);         // remplit et trie (plus récente d'abord)
void shots_free(ShotList *l);
void shots_path(const Shot *s, bool top, char *out, size_t cap);

// Range des captures dans un jeu (nom vide = retour dans « À trier »).
void shots_set_game(ShotList *l, const int *idx, int n, const char *game);
// Efface la capture de la carte SD (haut, bas, vignette) et de la liste.
void shots_delete(ShotList *l, int i);
// Captures de la même séance que `i` (écart de moins de 20 min) à ranger avec elle.
int shots_session(const ShotList *l, int i, int *out, int max);

// Charge la vignette de `s` (depuis le cache, sinon depuis le BMP).
void shots_load_thumb(Shot *s);

// « Aujourd'hui », « Hier », « 18 nov. », « 3 janv. 2025 »
void shots_day_label(time_t t, char *out, size_t cap);
long shots_day(time_t t);
