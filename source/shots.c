#include "shots.h"
#include "config.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static Shot *find_or_add(ShotList *l, const char *base, const char *dir, ShotSource src)
{
    for (int i = 0; i < l->count; i++)
        if (!strcmp(l->items[i].base, base) && !strcmp(l->items[i].dir, dir)) return &l->items[i];
    if (l->count == l->cap) {
        int nc = l->cap ? l->cap * 2 : 64;
        Shot *n = realloc(l->items, sizeof(Shot) * nc);
        if (!n) return NULL;
        l->items = n;
        l->cap = nc;
    }
    Shot *s = &l->items[l->count++];
    memset(s, 0, sizeof(*s));
    snprintf(s->base, sizeof(s->base), "%s", base);
    snprintf(s->dir, sizeof(s->dir), "%s", dir);
    s->src = src;
    int Y, M, D, h, m, sec;
    if (sscanf(base, "%4d-%2d-%2d_%2d-%2d-%2d", &Y, &M, &D, &h, &m, &sec) == 6) {
        struct tm tm = {0};
        tm.tm_year = Y - 1900;
        tm.tm_mon = M - 1;
        tm.tm_mday = D;
        tm.tm_hour = h;
        tm.tm_min = m;
        tm.tm_sec = sec;
        s->t = mktime(&tm);
    }
    return s;
}

static bool ends_with(const char *s, const char *suf, size_t *baseLen)
{
    size_t a = strlen(s), b = strlen(suf);
    if (a <= b || strcasecmp(s + a - b, suf)) return false;
    *baseLen = a - b;
    return true;
}

static void scan_dir(ShotList *l, const char *dir, ShotSource src, const char *game)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        size_t n;
        bool top = false, bot = false;
        if (ends_with(de->d_name, "_top.bmp", &n)) top = true;
        else if (ends_with(de->d_name, "_bot.bmp", &n)) bot = true;
        else continue;
        if (n >= 64) continue;
        char base[64];
        memcpy(base, de->d_name, n);
        base[n] = 0;
        Shot *s = find_or_add(l, base, dir, src);
        if (!s) break;
        if (top) s->hasTop = true;
        if (bot) s->hasBot = true;
        if (game && !s->game[0]) snprintf(s->game, sizeof(s->game), "%s", game);
    }
    closedir(d);
}

// Le futur plugin range ses captures dans PLUGIN_DIR/<nom du jeu>/.
static void scan_plugin(ShotList *l)
{
    DIR *d = opendir(PLUGIN_DIR);
    if (!d) return;
    struct dirent *de;
    char p[300];
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' || de->d_type != DT_DIR) continue;
        snprintf(p, sizeof(p), "%s/%s", PLUGIN_DIR, de->d_name);
        if (strlen(p) < 48) scan_dir(l, p, SRC_SHORTCUT, de->d_name);
    }
    closedir(d);
}

// jeux.txt : une ligne par capture rangée à la main, « <base>\t<jeu> » (la dernière l'emporte).
static void load_tags(ShotList *l)
{
    FILE *f = fopen(TAGS_PATH, "r");
    if (!f) return;
    char line[200];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        for (int i = 0; i < l->count; i++)
            if (l->items[i].src == SRC_ROSALINA && !strcmp(l->items[i].base, line))
                snprintf(l->items[i].game, sizeof(l->items[i].game), "%s", tab + 1);
    }
    fclose(f);
}

static int cmp_shot(const void *a, const void *b)
{
    const Shot *x = a, *y = b;
    if (x->t != y->t) return x->t < y->t ? 1 : -1;
    return strcmp(y->base, x->base);
}

void shots_scan(ShotList *l)
{
    shots_free(l);
    scan_dir(l, ROSALINA_DIR, SRC_ROSALINA, NULL);
    scan_plugin(l);
    // une capture sans écran du haut n'a pas de vignette : on la garde quand même
    load_tags(l);
    if (l->count) qsort(l->items, l->count, sizeof(Shot), cmp_shot);
}

void shots_free(ShotList *l)
{
    for (int i = 0; i < l->count; i++) gpu_free(&l->items[i].thumb);
    free(l->items);
    memset(l, 0, sizeof(*l));
}

void shots_path(const Shot *s, bool top, char *out, size_t cap)
{
    snprintf(out, cap, "%s/%s_%s.bmp", s->dir, s->base, top ? "top" : "bot");
}

void shots_set_game(ShotList *l, const int *idx, int n, const char *game)
{
    mkdir(DATA_DIR, 0777);
    FILE *f = fopen(TAGS_PATH, "a");
    for (int k = 0; k < n; k++) {
        Shot *s = &l->items[idx[k]];
        if (s->src != SRC_ROSALINA) continue; // celles du raccourci sont rangées par le plugin
        snprintf(s->game, sizeof(s->game), "%s", game ? game : "");
        if (f) fprintf(f, "%s\t%s\n", s->base, s->game);
    }
    if (f) fclose(f);
}

int shots_session(const ShotList *l, int i, int *out, int max)
{
    // Liste triée de la plus récente à la plus ancienne : on s'étend des deux côtés.
    const char *g = l->items[i].game;
    int lo = i, hi = i;
    while (lo > 0 && l->items[lo - 1].t - l->items[lo].t <= 20 * 60 && !strcmp(l->items[lo - 1].game, g)) lo--;
    while (hi + 1 < l->count && l->items[hi].t - l->items[hi + 1].t <= 20 * 60 && !strcmp(l->items[hi + 1].game, g)) hi++;
    int n = 0;
    for (int k = lo; k <= hi && n < max; k++)
        if (l->items[k].src == SRC_ROSALINA) out[n++] = k;
    return n;
}

void shots_load_thumb(Shot *s)
{
    if (s->thumbState) return;
    s->thumbState = 2;
    static u16 px[THUMB_W * THUMB_H];
    char cache[160];
    snprintf(cache, sizeof(cache), "%s/%s.th", CACHE_DIR, s->base);
    FILE *f = fopen(cache, "rb");
    bool ok = false;
    if (f) {
        ok = fread(px, 1, sizeof(px), f) == sizeof(px);
        fclose(f);
    }
    if (!ok) {
        char p[160];
        shots_path(s, s->hasTop, p, sizeof(p));
        RgbImage img;
        if (!img_load_bmp(p, &img)) return;
        ok = thumb_make(&img, px);
        img_free(&img);
        if (!ok) return;
        mkdir(DATA_DIR, 0777);
        mkdir(CACHE_DIR, 0777);
        f = fopen(cache, "wb");
        if (f) {
            fwrite(px, 1, sizeof(px), f);
            fclose(f);
        }
    }
    if (gpu_from_565(&s->thumb, px, THUMB_W, THUMB_H)) s->thumbState = 1;
}

long shots_day(time_t t)
{
    return (long)(t / 86400);
}

void shots_day_label(time_t t, char *out, size_t cap)
{
    static const char *months[12] = {"janv.", "févr.", "mars", "avr.", "mai", "juin",
                                     "juil.", "août", "sept.", "oct.", "nov.", "déc."};
    time_t now = time(NULL);
    long d = shots_day(now) - shots_day(t);
    if (t == 0) {
        snprintf(out, cap, "Date inconnue");
        return;
    }
    if (d == 0) {
        snprintf(out, cap, "Aujourd'hui");
        return;
    }
    if (d == 1) {
        snprintf(out, cap, "Hier");
        return;
    }
    struct tm a, b;
    gmtime_r(&t, &a);
    gmtime_r(&now, &b);
    if (a.tm_year == b.tm_year) snprintf(out, cap, "%d %s", a.tm_mday, months[a.tm_mon]);
    else snprintf(out, cap, "%d %s %d", a.tm_mday, months[a.tm_mon], a.tm_year + 1900);
}

void shots_delete(ShotList *l, int i)
{
    if (i < 0 || i >= l->count) return;
    Shot *s = &l->items[i];
    char p[200];
    shots_path(s, true, p, sizeof(p));
    remove(p);
    shots_path(s, false, p, sizeof(p));
    remove(p);
    snprintf(p, sizeof(p), "%s/%s_top_right.bmp", s->dir, s->base);
    remove(p);
    snprintf(p, sizeof(p), "%s/%s.th", CACHE_DIR, s->base);
    remove(p);
    gpu_free(&s->thumb);
    memmove(&l->items[i], &l->items[i + 1], sizeof(Shot) * (l->count - i - 1));
    l->count--;
}
