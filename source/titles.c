#include "titles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    u16 shortDescription[0x40];
    u16 longDescription[0x80];
    u16 publisher[0x40];
} SmdhTitle;

typedef struct {
    u32 magic;
    u16 version;
    u16 reserved;
    SmdhTitle titles[16];
    u8 settings[0x30];
    u8 reserved2[0x8];
    u16 smallIcon[0x240];
    u16 bigIcon[0x900];
} Smdh;

static bool read_smdh(u64 tid, u8 media, Smdh *out)
{
    static const u32 filePath[5] = {0x0, 0x0, 0x2, 0x6E6F6369, 0x0}; // "icon"
    u32 archPath[4] = {(u32)(tid & 0xFFFFFFFF), (u32)(tid >> 32), media, 0x0};
    FS_Path a = {PATH_BINARY, sizeof(archPath), archPath};
    FS_Path f = {PATH_BINARY, sizeof(filePath), filePath};
    Handle h;
    if (R_FAILED(FSUSER_OpenFileDirectly(&h, ARCHIVE_SAVEDATA_AND_CONTENT, a, f, FS_OPEN_READ, 0))) return false;
    u32 read = 0;
    Result rc = FSFILE_Read(h, &read, 0, out, sizeof(Smdh));
    FSFILE_Close(h);
    return R_SUCCEEDED(rc) && read == sizeof(Smdh) && out->magic == 0x48444D53;
}

static void icon_tex(Title *t, const Smdh *s)
{
    // La grande icône SMDH est déjà en tuiles 8x8 : on la copie bloc par bloc dans une texture 64x64.
    GpuImage *g = &t->icon;
    if (!C3D_TexInit(&g->tex, 64, 64, GPU_RGB565)) return;
    memset(g->tex.data, 0, g->tex.size);
    const u16 *src = s->bigIcon;
    u16 *dst = (u16 *)g->tex.data;
    for (int row = 0; row < 48; row += 8) {
        memcpy(dst, src, 48 * 8 * sizeof(u16));
        src += 48 * 8;
        dst += 64 * 8;
    }
    C3D_TexFlush(&g->tex);
    C3D_TexSetFilter(&g->tex, GPU_LINEAR, GPU_LINEAR);
    g->sub.width = 48;
    g->sub.height = 48;
    g->sub.left = 0;
    g->sub.top = 1;
    g->sub.right = 48 / 64.0f;
    g->sub.bottom = 1 - 48 / 64.0f;
    g->img.tex = &g->tex;
    g->img.subtex = &g->sub;
    g->ok = true;
    t->hasIcon = true;
}

static int cmp_title(const void *a, const void *b)
{
    return strcasecmp(((const Title *)a)->name, ((const Title *)b)->name);
}

int titles_load(Title **out)
{
    *out = NULL;
    int n = 0, cap = 0;
    Title *list = NULL;
    Smdh *smdh = malloc(sizeof(Smdh));
    if (!smdh) return 0;
    u8 lang = CFG_LANGUAGE_FR;
    CFGU_GetSystemLanguage(&lang);
    static const FS_MediaType medias[2] = {MEDIATYPE_GAME_CARD, MEDIATYPE_SD};
    for (int m = 0; m < 2; m++) {
        u32 count = 0;
        if (R_FAILED(AM_GetTitleCount(medias[m], &count)) || !count) continue;
        u64 *ids = malloc(sizeof(u64) * count);
        if (!ids) continue;
        u32 got = 0;
        if (R_SUCCEEDED(AM_GetTitleList(&got, medias[m], count, ids))) {
            for (u32 i = 0; i < got; i++) {
                if ((ids[i] >> 32) != 0x00040000) continue; // applications seulement
                if (!read_smdh(ids[i], medias[m], smdh)) continue;
                if (n == cap) {
                    cap = cap ? cap * 2 : 64;
                    Title *nl = realloc(list, sizeof(Title) * cap);
                    if (!nl) break;
                    list = nl;
                }
                Title *t = &list[n++];
                memset(t, 0, sizeof(*t));
                t->tid = ids[i];
                const u16 *name = smdh->titles[lang < 16 ? lang : 1].shortDescription;
                if (!name[0]) name = smdh->titles[1].shortDescription;
                u8 tmp[0x40 * 3 + 1] = {0};
                ssize_t len = utf16_to_utf8(tmp, name, sizeof(tmp) - 1);
                tmp[len > 0 ? len : 0] = 0;
                // le nom peut contenir un retour à la ligne : on le remplace par une espace
                for (u8 *c = tmp; *c; c++)
                    if (*c == '\n' || *c == '\t') *c = ' ';
                snprintf(t->name, sizeof(t->name), "%s", (char *)tmp);
                if (!t->name[0]) snprintf(t->name, sizeof(t->name), "%016llX", (unsigned long long)ids[i]);
                icon_tex(t, smdh);
            }
        }
        free(ids);
    }
    free(smdh);
    if (n) qsort(list, n, sizeof(Title), cmp_title);
    // les entrées ont bougé (realloc, tri) : on refait pointer chaque image vers sa texture
    for (int i = 0; i < n; i++) {
        list[i].icon.img.tex = &list[i].icon.tex;
        list[i].icon.img.subtex = &list[i].icon.sub;
    }
    *out = list;
    return n;
}

void titles_free(Title *t, int n)
{
    for (int i = 0; i < n; i++) gpu_free(&t[i].icon);
    free(t);
}
