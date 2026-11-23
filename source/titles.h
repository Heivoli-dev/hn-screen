#pragma once
#include "img.h"

typedef struct {
    char name[64];
    u64 tid;
    bool hasIcon;
    GpuImage icon; // 48 x 48
} Title;

// Jeux et applis installés (carte SD + cartouche), triés par nom.
int titles_load(Title **out);
void titles_free(Title *t, int n);
