// Hn screen ! — l'album des captures d'écran de la 3DS, façon menu HOME.
#include "config.h"
#include "img.h"
#include "qrcodegen.h"
#include "share.h"
#include "shots.h"
#include "sound.h"
#include "update.h"

#ifndef APP_BUILD
#define APP_BUILD 0
#endif
#include "titles.h"
#include "ui.h"
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef enum { M_ALBUM, M_VIEW, M_SHARE, M_SORT, M_UPDATE, M_CONFIRM } Mode;

#define MAX_CATS 64
#define GRID_TOP 58
#define GRID_PAD 12 // espace entre les onglets et la grille
#define TH_W 70
#define TH_H 42
#define TH_GAP 10
#define ROW_H (TH_H + TH_GAP)
#define HEAD_H 18

typedef struct {
    int shot;      // index dans la liste, -1 pour un titre de date
    float x, y;    // position dans la grille (y relatif au haut du contenu)
    int row, col;
    char label[32];
} Cell;

static ShotList L;
static char s_cats[MAX_CATS][64];
static int s_catCount[MAX_CATS];
static int s_ncats = 0, s_cat = 0;

static int *s_f = NULL;   // captures de la catégorie
static int s_nf = 0, s_sel = 0;
static Cell *s_cells = NULL;
static int s_ncells = 0, s_rows = 0;
static float s_contentH = 0, s_scroll = 0, s_tabScroll = 0;

static Mode s_mode = M_ALBUM;
static GpuImage s_prevTop, s_prevBot;
static int s_prevFor = -1, s_selStable = 0;
static bool s_rescan = false;
static u32 s_frame = 0;

static Title *s_titles = NULL;
static int s_ntitles = -1;
static int s_sortSel = 0;
static float s_sortScroll = 0;

static C2D_SpriteSheet s_sheet = NULL;
static int s_toast = 0;            // images restantes pour le message « Sons activés / coupés »
static ShareState s_lastShare = SHARE_IDLE;
static int s_doneFrames = 0;

// ---------------------------------------------------------------------------
// Catégories, filtrage et grille
static void build_cats(void)
{
    char keep[64];
    snprintf(keep, sizeof(keep), "%s", s_ncats ? s_cats[s_cat] : "Tout");
    s_ncats = 0;
    snprintf(s_cats[s_ncats], 64, "Tout");
    s_catCount[s_ncats++] = L.count;
    snprintf(s_cats[s_ncats], 64, "À trier");
    s_catCount[s_ncats] = 0;
    for (int i = 0; i < L.count; i++)
        if (!L.items[i].game[0]) s_catCount[s_ncats]++;
    s_ncats++;
    for (int i = 0; i < L.count && s_ncats < MAX_CATS; i++) {
        const char *g = L.items[i].game;
        if (!g[0]) continue;
        int k;
        for (k = 2; k < s_ncats; k++)
            if (!strcmp(s_cats[k], g)) break;
        if (k == s_ncats) {
            snprintf(s_cats[s_ncats], 64, "%s", g);
            s_catCount[s_ncats++] = 0;
        }
        s_catCount[k]++;
    }
    s_cat = 0;
    for (int k = 0; k < s_ncats; k++)
        if (!strcmp(s_cats[k], keep)) s_cat = k;
}

static bool in_cat(int i)
{
    if (s_cat == 0) return true;
    if (s_cat == 1) return !L.items[i].game[0];
    return !strcmp(L.items[i].game, s_cats[s_cat]);
}

static void build_grid(void)
{
    free(s_f);
    free(s_cells);
    s_f = malloc(sizeof(int) * (L.count + 1));
    s_cells = malloc(sizeof(Cell) * (L.count * 2 + 1));
    s_nf = s_ncells = s_rows = 0;
    if (!s_f || !s_cells) return;
    float y = 0;
    long day = -999999;
    int col = 0;
    bool inRow = false;
    for (int i = 0; i < L.count; i++) {
        if (!in_cat(i)) continue;
        s_f[s_nf++] = i;
        long d = shots_day(L.items[i].t);
        if (d != day) {
            day = d;
            if (inRow) y += ROW_H;
            inRow = false;
            col = 0;
            Cell *h = &s_cells[s_ncells++];
            memset(h, 0, sizeof(*h));
            h->shot = -1;
            h->y = y;
            shots_day_label(L.items[i].t, h->label, sizeof(h->label));
            y += HEAD_H;
        }
        if (!inRow) {
            inRow = true;
            s_rows++;
        }
        Cell *c = &s_cells[s_ncells++];
        memset(c, 0, sizeof(*c));
        c->shot = i;
        c->col = col;
        c->row = s_rows - 1;
        c->x = 10 + col * (TH_W + TH_GAP);
        c->y = y;
        if (++col == 4) {
            col = 0;
            y += ROW_H;
            inRow = false;
        }
    }
    s_contentH = y + (inRow ? ROW_H : 0);
    if (s_sel >= s_nf) s_sel = s_nf - 1;
    if (s_sel < 0) s_sel = 0;
}

static int cell_of(int fpos)
{
    if (fpos < 0 || fpos >= s_nf) return -1;
    int shot = s_f[fpos];
    for (int k = 0; k < s_ncells; k++)
        if (s_cells[k].shot == shot) return k;
    return -1;
}

static int fpos_of_shot(int shot)
{
    for (int k = 0; k < s_nf; k++)
        if (s_f[k] == shot) return k;
    return -1;
}

static Shot *cur(void)
{
    return s_nf ? &L.items[s_f[s_sel]] : NULL;
}

static void rescan(void)
{
    char keep[64] = "";
    if (cur()) snprintf(keep, sizeof(keep), "%s", cur()->base);
    gpu_free(&s_prevTop);
    gpu_free(&s_prevBot);
    s_prevFor = -1;
    shots_scan(&L);
    build_cats();
    build_grid();
    for (int k = 0; k < s_nf; k++)
        if (!strcmp(L.items[s_f[k]].base, keep)) s_sel = k;
}

static void move_vertical(int dir)
{
    int c = cell_of(s_sel);
    if (c < 0) return;
    int row = s_cells[c].row + dir, col = s_cells[c].col;
    int best = -1;
    for (int k = 0; k < s_ncells; k++) {
        if (s_cells[k].shot < 0 || s_cells[k].row != row) continue;
        if (best < 0 || abs(s_cells[k].col - col) < abs(s_cells[best].col - col)) best = k;
    }
    if (best >= 0) s_sel = fpos_of_shot(s_cells[best].shot);
}

static void set_cat(int k)
{
    if (k < 0 || k >= s_ncats || k == s_cat) return;
    sound_play(SND_TAB);
    s_cat = k;
    s_sel = 0;
    s_scroll = 0;
    build_grid();
}

// ---------------------------------------------------------------------------
// Images en grand
static void load_preview(bool withBottom)
{
    Shot *s = cur();
    if (!s) return;
    int id = s_f[s_sel];
    if (s_prevFor != id) {
        gpu_free(&s_prevTop);
        gpu_free(&s_prevBot);
        char p[160];
        RgbImage img;
        if (s->hasTop) {
            shots_path(s, true, p, sizeof(p));
            if (img_load_bmp(p, &img)) {
                gpu_from_rgb(&s_prevTop, &img);
                img_free(&img);
            }
        }
        s_prevFor = id;
    }
    if (withBottom && !s_prevBot.ok && s->hasBot) {
        char p[160];
        RgbImage img;
        shots_path(s, false, p, sizeof(p));
        if (img_load_bmp(p, &img)) {
            gpu_from_rgb(&s_prevBot, &img);
            img_free(&img);
        }
    }
}

static void load_visible_thumbs(void)
{
    u64 t0 = osGetTime();
    // d'abord la capture sélectionnée et ses voisines, puis le reste de l'écran
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k < s_ncells; k++) {
            Cell *c = &s_cells[k];
            if (c->shot < 0) continue;
            float y = GRID_TOP + c->y - s_scroll;
            if (y + TH_H < GRID_TOP - ROW_H || y > SCR_H + ROW_H) continue;
            Shot *s = &L.items[c->shot];
            if (s->thumbState) continue;
            if (pass == 0 && abs(fpos_of_shot(c->shot) - s_sel) > 4) continue;
            shots_load_thumb(s);
            if (osGetTime() - t0 > 10) return;
        }
    }
}

// ---------------------------------------------------------------------------
// Dessin : écran du haut
static void draw_status_bar(const char *hints)
{
    ui_grey_bar(0, 212, TOP_W, 28);
    ui_camera_badge(8, 216, 20);
    ui_text(34, 215, 0.55f, C_WHITE, APP_NAME);
    float w = ui_text_width(0.55f, APP_NAME);
    ui_text(38 + w, 217, 0.48f, COL(0xE6, 0xE6, 0xE6), "/ Album");
    ui_text_right(392, 218, 0.45f, COL(0xF0, 0xF0, 0xF0), hints);
}

static void draw_empty_top(void)
{
    ui_stripes(TOP_W, SCR_H);
    if (s_sheet) {
        C2D_Image logo = C2D_SpriteSheetGetImage(s_sheet, 0);
        float s = 150.0f / logo.subtex->width;
        ui_rrect(TOP_W / 2 - 100, 20, 200, 120, 14, C_WHITE);
        C2D_DrawImageAt(logo, TOP_W / 2 - 75, 80 - logo.subtex->height * s / 2, 0.5f, NULL, s, s);
    }
    ui_text_center(TOP_W / 2, 150, 0.6f, C_TXT, "Aucune capture pour l'instant");
    ui_text_center(TOP_W / 2, 176, 0.45f, C_TXT2, "En jeu : L + ↓ + Select, puis « Prendre une capture »");
    draw_status_bar("SELECT : sons   START : quitter");
}

// Version et état des mises à jour, en petit en haut de l'écran du haut.
static void draw_version_line(void)
{
    char buf[200];
    const char *st = update_status_text();
    snprintf(buf, sizeof(buf), "v%s · build %d%s%s", APP_VERSION, APP_BUILD, st[0] ? " · " : "", st);
    ui_text_fit(8, 6, 0.36f, C_TXT2, TOP_W - 16, buf);
}

static void draw_top_album(void)
{
    Shot *s = cur();
    if (!L.count) {
        draw_empty_top();
        draw_version_line();
        return;
    }
    ui_stripes(TOP_W, SCR_H);
    draw_version_line();
    const float px = 14, py = 34, pw = 240, ph = 144;
    ui_rrect(px - 5, py - 5, pw + 10, ph + 10, 7, C_BORDER);
    ui_rrect(px - 4, py - 4, pw + 8, ph + 8, 6, C_WHITE);
    if (s && s_prevTop.ok && s_prevFor == s_f[s_sel]) {
        C2D_DrawImageAt(s_prevTop.img, px, py, 0.5f, NULL, pw / 400.0f, ph / 240.0f);
    } else {
        ui_rect(px, py, pw, ph, C_PLACE);
        if (s && !s->hasTop) ui_text_center(px + pw / 2, py + ph / 2 - 9, 0.5f, C_TXT2, "Écran du bas seulement");
        else ui_spinner(px + pw / 2, py + ph / 2, 12, s_frame);
    }

    const float ix = 270, iw = 122;
    if (s_mode == M_SORT || s_mode == M_CONFIRM) {
        ui_text_fit(ix, 34, 0.45f, s && s->game[0] ? C_TXT2 : C_CYAN_DK, iw, s && s->game[0] ? s->game : "À trier");
        ui_text_wrap(ix, 56, 0.6f, C_TXT, iw, s_mode == M_CONFIRM ? "Supprimer ?" : "Que faire de cette capture ?");
        draw_status_bar(G_A " Choisir   " G_B " Annuler");
        return;
    }
    if (!s) {
        ui_text_wrap(ix, 60, 0.5f, C_TXT2, iw, "Aucune capture dans cette catégorie");
        draw_status_bar(G_L " " G_R " Catégories");
        return;
    }
    ui_text_fit(ix, 34, 0.45f, s->game[0] ? C_TXT2 : C_CYAN_DK, iw, s->game[0] ? s->game : "À trier");
    char day[32], line[64];
    shots_day_label(s->t, day, sizeof(day));
    ui_text_fit(ix, 52, 0.72f, C_TXT, iw, day);
    struct tm tm;
    gmtime_r(&s->t, &tm);
    snprintf(line, sizeof(line), "%02d:%02d · %d sur %d", tm.tm_hour, tm.tm_min, s_sel + 1, s_nf);
    ui_text_fit(ix, 82, 0.46f, C_TXT2, iw, line);
    ui_text_fit(ix, 100, 0.42f, C_TXT2, iw, s->src == SRC_SHORTCUT ? "Raccourci L + R + ↓" : "Capture Rosalina");
    ui_button(ix, 128, 118, 28, G_Y " Partager", true);
    ui_text_wrap(ix, 162, 0.38f, C_TXT2, iw, "QR code à scanner avec ton téléphone");
    draw_status_bar(G_A " Agrandir  " G_X " Ranger / supprimer");
}

static void draw_top_view(void)
{
    ui_rect(0, 0, TOP_W, SCR_H, COL(0x18, 0x18, 0x18));
    if (s_prevTop.ok) C2D_DrawImageAt(s_prevTop.img, 0, 0, 0.5f, NULL, 1, 1);
    else ui_text_center(TOP_W / 2, 110, 0.5f, COL(0xC8, 0xC8, 0xC8), "Pas d'écran du haut");
}

// ---------------------------------------------------------------------------
// Dessin : écran du bas
static void draw_tabs(void)
{
    ui_rect(0, 26, BOT_W, 30, COL(0xF2, 0xF2, 0xF2));
    ui_rect(0, 55, BOT_W, 1, C_BORDER);
    float x = 8 - s_tabScroll;
    for (int k = 0; k < s_ncats; k++) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%s  %d", s_cats[k], s_catCount[k]);
        float w = ui_text_width(0.45f, buf) + 18;
        if (w > 150) w = 150;
        if (x + w > 0 && x < BOT_W) {
            bool on = k == s_cat;
            if (on) {
                ui_rrect(x - 1, 30, w + 2, 22, 6, C_CYAN_LINE);
                ui_rrect_grad(x, 31, w, 20, 5, C_CYAN_HI, C_CYAN_DK);
            } else {
                ui_rrect(x - 1, 30, w + 2, 22, 6, C_BORDER2);
                ui_rrect_grad(x, 31, w, 20, 5, C_WHITE, C_SILVER);
            }
            ui_text_fit(x + 9, 33, 0.45f, on ? C_WHITE : C_TXT, w - 14, buf);
        }
        x += w + 6;
    }
}

static int tab_at(float tx)
{
    float x = 8 - s_tabScroll;
    for (int k = 0; k < s_ncats; k++) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%s  %d", s_cats[k], s_catCount[k]);
        float w = ui_text_width(0.45f, buf) + 18;
        if (w > 150) w = 150;
        if (tx >= x && tx < x + w) return k;
        x += w + 6;
    }
    return -1;
}

static void keep_tab_visible(void)
{
    float x = 8, sx = 0, sw = 0;
    for (int k = 0; k < s_ncats; k++) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%s  %d", s_cats[k], s_catCount[k]);
        float w = ui_text_width(0.45f, buf) + 18;
        if (w > 150) w = 150;
        if (k == s_cat) {
            sx = x;
            sw = w;
        }
        x += w + 6;
    }
    float target = s_tabScroll;
    if (sx - target < 8) target = sx - 8;
    if (sx + sw - target > BOT_W - 8) target = sx + sw - (BOT_W - 8);
    if (target < 0) target = 0;
    s_tabScroll += (target - s_tabScroll) * 0.35f;
}

static void draw_bottom_album(void)
{
    ui_stripes(BOT_W, SCR_H);
    if (!L.count) {
        ui_white_bar(0, 0, BOT_W, 26);
        ui_text(10, 4, 0.55f, C_TXT, "Album");
        ui_rrect(15, 46, 290, 150, 10, C_BORDER);
        ui_rrect(16, 47, 288, 148, 9, C_WHITE);
        ui_text(30, 58, 0.55f, C_TXT, "Pour faire une capture");
        ui_text_wrap(30, 84, 0.45f, C_TXT2, 262,
                     "1. En jeu, appuie sur L + ↓ + Select.\n2. Choisis « Prendre une capture d'écran ».\n3. Reviens ici : elle apparaît dans l'album.");
        return;
    }
    // Tout ce qui passe sous la barre d'onglets est coupé net : rien ne déborde dessus.
    const float clip = GRID_TOP + 2;
    for (int k = 0; k < s_ncells; k++) {
        Cell *c = &s_cells[k];
        float y = GRID_TOP + GRID_PAD + c->y - s_scroll;
        if (y > SCR_H || y + TH_H <= clip) continue;
        if (c->shot < 0) {
            if (y >= clip) ui_text(12, y, 0.42f, C_TXT2, c->label);
            continue;
        }
        Shot *s = &L.items[c->shot];
        bool on = c->shot == s_f[s_sel];
        s->thumb.img.tex = &s->thumb.tex;
        s->thumb.img.subtex = &s->thumb.sub;
        if (y - 6 >= clip) {
            if (on) ui_glow(c->x, y, TH_W, TH_H, 3);
            else {
                ui_rrect(c->x - 3, y - 3, TH_W + 6, TH_H + 6, 5, C_BORDER);
                ui_rrect(c->x - 2, y - 2, TH_W + 4, TH_H + 4, 4, C_WHITE);
            }
        }
        float cut = y < clip ? clip - y : 0; // pixels cachés en haut
        float sx = TH_W / (float)THUMB_W, sy = TH_H / (float)THUMB_H;
        if (s->thumbState == 1) {
            Tex3DS_SubTexture sub = s->thumb.sub;
            float srcCut = cut / sy;
            sub.height = (u16)(THUMB_H - srcCut);
            sub.top = 1.0f - srcCut / 64.0f;
            C2D_Image img = {s->thumb.img.tex, &sub};
            if (sub.height > 0) C2D_DrawImageAt(img, c->x, y + cut, 0.5f, NULL, sx, sy);
        } else {
            ui_rect(c->x, y + cut, TH_W, TH_H - cut, C_PLACE);
        }
    }
    if (!s_nf) ui_text_center(BOT_W / 2, 130, 0.5f, C_TXT2, "Aucune capture ici");
    draw_tabs();
    ui_white_bar(0, 0, BOT_W, 26);
    ui_text(10, 4, 0.55f, C_TXT, "Album");
    char buf[32];
    snprintf(buf, sizeof(buf), "%d capture%s", s_nf, s_nf > 1 ? "s" : "");
    ui_text_right(310, 6, 0.45f, C_TXT2, buf);
}

static void draw_bottom_view(void)
{
    ui_rect(0, 0, BOT_W, SCR_H, COL(0x18, 0x18, 0x18));
    if (s_prevBot.ok) C2D_DrawImageAt(s_prevBot.img, 0, 0, 0.5f, NULL, 1, 1);
    else ui_text_center(BOT_W / 2, 110, 0.5f, COL(0xC8, 0xC8, 0xC8), "Pas d'écran du bas");
}

// QR code mis en cache pour l'adresse courante.
static uint8_t s_qr[qrcodegen_BUFFER_LEN_MAX];
static char s_qrFor[160] = "";
static bool s_qrOk = false;

static void draw_qr(const char *text, float x, float y, float size)
{
    if (strcmp(text, s_qrFor)) {
        static uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
        snprintf(s_qrFor, sizeof(s_qrFor), "%s", text);
        s_qrOk = qrcodegen_encodeText(text, tmp, s_qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                      qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true);
    }
    ui_rect(x, y, size, size, C_WHITE);
    if (!s_qrOk) return;
    int n = qrcodegen_getSize(s_qr);
    int m = (int)(size / (n + 6));
    if (m < 1) m = 1;
    float ox = x + (size - m * n) / 2, oy = y + (size - m * n) / 2;
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            if (qrcodegen_getModule(s_qr, c, r)) C2D_DrawRectSolid(ox + c * m, oy + r * m, 0.5f, m, m, COL(0x10, 0x10, 0x10));
}

static void draw_bottom_share(void)
{
    ui_stripes(BOT_W, SCR_H);
    ui_rrect(7, 7, 306, 198, 10, C_BORDER);
    ui_rrect(8, 8, 304, 196, 9, C_WHITE);
    ui_text(18, 14, 0.55f, C_TXT, "Partager");
    ShareState st = share_state();
    const float tx = 176, tw = 128;
    switch (st) {
    case SHARE_SENDING:
        ui_spinner(160, 100, 16, s_frame);
        ui_text_center(160, 128, 0.45f, C_TXT2, "Envoi sur heivoli-network.fr…");
        break;
    case SHARE_ONLINE:
    case SHARE_LOCAL:
        draw_qr(share_url(), 18, 42, 150);
        if (st == SHARE_ONLINE) {
            ui_text_wrap(tx, 44, 0.42f, C_TXT, tw, "Scanne le QR code avec ton téléphone ou ton PC.");
            ui_text(tx, 100, 0.38f, C_TXT2, "Ou ouvre :");
            ui_text_wrap(tx, 114, 0.38f, C_CYAN_DK, tw, share_label());
            ui_text_wrap(tx, 160, 0.36f, C_TXT2, tw, "Lien valable 24 h.");
        } else {
            ui_text_wrap(tx, 44, 0.42f, C_TXT, tw, "Même Wi-Fi que la console. Garde l'appli ouverte.");
            ui_text(tx, 100, 0.38f, C_TXT2, "Ou ouvre :");
            ui_text_wrap(tx, 114, 0.38f, C_CYAN_DK, tw, share_label());
            if (share_error()[0]) ui_text_wrap(tx, 150, 0.34f, C_TXT2, tw, "Le site ne répond pas : partage en Wi-Fi local.");
        }
        break;
    case SHARE_NO_WIFI:
        ui_text_wrap(24, 60, 0.5f, C_TXT, 270, "Pas de connexion Wi-Fi.");
        ui_text_wrap(24, 86, 0.42f, C_TXT2, 270, "Active le Wi-Fi de la console, puis réessaie.");
        break;
    default:
        ui_text_wrap(24, 60, 0.5f, C_TXT, 270, "Le partage n'a pas marché.");
        ui_text_wrap(24, 86, 0.42f, C_TXT2, 270, share_error());
        break;
    }
    ui_button(8, 212, 96, 24, G_B " Fermer", false);
    if (st == SHARE_ONLINE) ui_button(112, 212, 200, 24, G_X " Partager en Wi-Fi local", false);
    else if (st != SHARE_SENDING) ui_button(112, 212, 200, 24, G_X " Réessayer avec le site", false);
}

// Menu de la touche X : supprimer, ranger dans un jeu, retirer du jeu.
enum { IT_DELETE, IT_KEYBOARD, IT_REMOVE, IT_TITLE };

static int menu_fixed(int *kinds)
{
    Shot *s = cur();
    int n = 0;
    if (!s) return 0;
    if (s->src == SRC_ROSALINA) {
        if (s->game[0]) kinds[n++] = IT_REMOVE;
        kinds[n++] = IT_KEYBOARD;
    }
    kinds[n++] = IT_DELETE;
    return n;
}

static int sort_items(void)
{
    int kinds[4];
    int n = menu_fixed(kinds);
    if (cur() && cur()->src == SRC_ROSALINA && s_ntitles > 0) n += s_ntitles;
    return n;
}

static void draw_bottom_sort(void)
{
    ui_stripes(BOT_W, SCR_H);
    if (s_ntitles < 0) {
        ui_spinner(160, 110, 16, s_frame);
        ui_text_center(160, 140, 0.45f, C_TXT2, "Chargement des jeux…");
        return;
    }
    int kinds[4];
    int nf = menu_fixed(kinds);
    int n = sort_items();
    char buf[96];
    for (int k = 0; k < n; k++) {
        float y = 32 + k * 38 - s_sortScroll;
        if (y < -40 || y > SCR_H) continue;
        bool on = k == s_sortSel;
        if (on) ui_glow(10, y, 300, 32, 6);
        else ui_rrect(9, y - 1, 302, 34, 7, C_BORDER);
        ui_rrect_grad(10, y, 300, 32, 6, C_WHITE, on ? COL(0xEA, 0xF8, 0xFE) : C_SILVER);
        const char *label;
        u32 color = C_TXT;
        float tx = 18;
        if (k < nf) {
            switch (kinds[k]) {
            case IT_DELETE:
                label = "Supprimer cette capture";
                color = COL(0xB4, 0x23, 0x23);
                break;
            case IT_REMOVE:
                snprintf(buf, sizeof(buf), "Retirer de « %s »", cur()->game);
                label = buf;
                break;
            default:
                label = "Autre jeu… (clavier)";
                break;
            }
        } else {
            Title *t = &s_titles[k - nf];
            if (t->hasIcon) C2D_DrawImageAt(t->icon.img, 14, y + 2, 0.5f, NULL, 28 / 48.0f, 28 / 48.0f);
            label = t->name;
            tx = 50;
        }
        ui_text_fit(tx, y + 8, 0.48f, color, 300 - tx, label);
    }
    ui_white_bar(0, 0, BOT_W, 26);
    ui_text(10, 4, 0.52f, C_TXT, cur() && cur()->src == SRC_ROSALINA ? "Ranger dans quel jeu ?" : "Capture");
}

static void draw_bottom_confirm(void)
{
    ui_stripes(BOT_W, SCR_H);
    ui_rrect(19, 49, 282, 142, 12, C_BORDER);
    ui_rrect(20, 50, 280, 140, 11, C_WHITE);
    ui_text(36, 64, 0.55f, C_TXT, "Supprimer cette capture ?");
    ui_text_wrap(36, 92, 0.42f, C_TXT2, 250, "L'écran du haut et l'écran du bas seront effacés de la carte SD. Impossible de revenir en arrière.");
    ui_button(36, 148, 116, 28, G_A " Supprimer", true);
    ui_button(168, 148, 116, 28, G_B " Annuler", false);
}

static void draw_bottom_update(void)
{
    ui_stripes(BOT_W, SCR_H);
    ui_rrect(19, 39, 282, 162, 12, C_BORDER);
    ui_rrect(20, 40, 280, 160, 11, C_WHITE);
    ui_camera_badge(34, 52, 26);
    ui_text(70, 54, 0.55f, C_TXT, "Mise à jour");
    char buf[96];
    switch (update_state()) {
    case UPD_INSTALLING: {
        ui_text(34, 96, 0.45f, C_TXT2, "Téléchargement et installation…");
        float k = update_progress();
        ui_rrect(34, 124, 252, 14, 7, COL(0xDC, 0xDC, 0xDC));
        if (k > 0.03f) ui_rrect_grad(34, 124, 252 * k, 14, 7, C_CYAN_HI, C_CYAN_DK);
        snprintf(buf, sizeof(buf), "%d %%", (int)(k * 100));
        ui_text_center(160, 146, 0.45f, C_TXT2, buf);
        ui_text_wrap(34, 168, 0.38f, C_TXT2, 252, "N'éteins pas la console.");
        break;
    }
    case UPD_DONE:
        ui_text_wrap(34, 96, 0.5f, C_TXT, 252, "C'est installé !");
        ui_text_wrap(34, 122, 0.42f, C_TXT2, 252, "Hn screen ! redémarre avec la nouvelle version…");
        break;
    case UPD_ERROR:
        ui_text_wrap(34, 92, 0.48f, C_TXT, 252, "La mise à jour n'a pas marché.");
        ui_text_wrap(34, 116, 0.38f, C_TXT2, 252, update_error());
        ui_button(34, 160, 120, 28, G_A " Réessayer", true);
        ui_button(166, 160, 120, 28, G_B " Plus tard", false);
        break;
    default:
        snprintf(buf, sizeof(buf), "Une nouvelle version de Hn screen ! est prête (build %d).", update_remote_build());
        ui_text_wrap(34, 92, 0.45f, C_TXT, 252, buf);
        ui_text_wrap(34, 128, 0.38f, C_TXT2, 252, "Elle s'installe toute seule en quelques secondes.");
        ui_button(34, 160, 120, 28, G_A " Installer", true);
        ui_button(166, 160, 120, 28, G_B " Plus tard", false);
        break;
    }
}

static void draw_toast(void)
{
    if (s_toast <= 0) return;
    const char *msg = !sound_available() ? "Son indisponible (dspfirm.cdc absent)" : (sound_enabled() ? "Sons activés" : "Sons coupés");
    float w = ui_text_width(0.45f, msg) + 28;
    ui_rrect(BOT_W / 2 - w / 2 - 1, 199, w + 2, 30, 9, C_BORDER);
    ui_rrect_grad(BOT_W / 2 - w / 2, 200, w, 28, 8, C_WHITE, C_SILVER);
    ui_text_center(BOT_W / 2, 206, 0.45f, C_TXT, msg);
    s_toast--;
}

// ---------------------------------------------------------------------------
// Actions
static void open_share(bool localOnly)
{
    Shot *s = cur();
    if (!s) return;
    char top[160], bot[160];
    shots_path(s, true, top, sizeof(top));
    shots_path(s, false, bot, sizeof(bot));
    s_mode = M_SHARE;
    sound_play(SND_OPEN);
    s_lastShare = SHARE_SENDING;
    // dessine une image « Envoi… » avant le travail (encodage PNG ~ 1/2 s)
    share_start(s->hasTop ? top : NULL, s->hasBot ? bot : NULL, localOnly);
}

static void reselect(const char *keep, int fallback)
{
    build_cats();
    build_grid();
    int p = -1;
    for (int k = 0; k < s_nf; k++)
        if (!strcmp(L.items[s_f[k]].base, keep)) p = k;
    s_sel = p >= 0 ? p : (fallback < s_nf ? fallback : (s_nf ? s_nf - 1 : 0));
    if (s_sel < 0) s_sel = 0;
    s_prevFor = -1;
}

static void apply_sort(const char *game)
{
    Shot *s = cur();
    if (!s) return;
    char keep[64];
    snprintf(keep, sizeof(keep), "%s", s->base);
    int idx = s_f[s_sel];
    shots_set_game(&L, &idx, 1, game);
    reselect(keep, s_sel);
}

static void delete_current(void)
{
    Shot *s = cur();
    if (!s) return;
    int keepPos = s_sel;
    gpu_free(&s_prevTop);
    gpu_free(&s_prevBot);
    shots_delete(&L, s_f[s_sel]);
    reselect("", keepPos);
}

static void sort_confirm(void)
{
    int kinds[4];
    int nf = menu_fixed(kinds);
    if (s_sortSel < nf) {
        switch (kinds[s_sortSel]) {
        case IT_DELETE:
            sound_play(SND_OPEN);
            s_mode = M_CONFIRM;
            return;
        case IT_REMOVE:
            apply_sort("");
            sound_play(SND_SUCCESS);
            s_mode = M_ALBUM;
            return;
        default: {
            SwkbdState kb;
            char out[64] = "";
            swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 60);
            swkbdSetHintText(&kb, "Nom du jeu");
            if (cur() && cur()->game[0]) swkbdSetInitialText(&kb, cur()->game);
            swkbdSetValidation(&kb, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);
            if (swkbdInputText(&kb, out, sizeof(out)) == SWKBD_BUTTON_CONFIRM && out[0]) {
                for (char *c = out; *c; c++)
                    if (*c == '\t' || *c == '\n') *c = ' ';
                apply_sort(out);
                sound_play(SND_SUCCESS);
                s_mode = M_ALBUM;
            }
            return;
        }
        }
    }
    apply_sort(s_titles[s_sortSel - nf].name);
    sound_play(SND_SUCCESS);
    s_mode = M_ALBUM;
}

static void open_sort(void)
{
    if (!cur()) return;
    s_sortSel = 0;
    s_sortScroll = 0;
    sound_play(SND_OPEN);
    s_mode = M_SORT;
}

static void apt_hook(APT_HookType hook, void *param)
{
    (void)param;
    if (hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP) s_rescan = true;
}

// ---------------------------------------------------------------------------
int main(void)
{
    romfsInit();
    cfguInit();
    amInit();
    acInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 2);
    C2D_Prepare();
    // L'ordre de dessin décide de ce qui passe devant (onglets au-dessus de la grille, etc.).
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    ui_init();
    s_sheet = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");
    hidSetRepeatParameters(18, 5);
    aptHookCookie hookCookie;
    aptHook(&hookCookie, apt_hook, NULL);
    mkdir(DATA_DIR, 0777);
    sound_init();

    rescan();
    update_check_async();
    bool updateAsked = false;
    bool needTitles = false;

    while (aptMainLoop()) {
        if (s_rescan) {
            s_rescan = false;
            if (s_mode == M_ALBUM || s_mode == M_VIEW) rescan();
        }
        hidScanInput();
        u32 down = hidKeysDown(), rep = hidKeysDownRepeat();
        touchPosition tp;
        hidTouchRead(&tp);
        if ((down & KEY_START) && s_mode != M_SORT && !(s_mode == M_UPDATE && (update_state() == UPD_INSTALLING || update_state() == UPD_DONE))) break;

        int before = s_sel;
        switch (s_mode) {
        case M_ALBUM:
            if (rep & (KEY_RIGHT | KEY_CPAD_RIGHT)) s_sel = s_sel + 1 < s_nf ? s_sel + 1 : s_sel;
            if (rep & (KEY_LEFT | KEY_CPAD_LEFT)) s_sel = s_sel > 0 ? s_sel - 1 : 0;
            if (rep & (KEY_DOWN | KEY_CPAD_DOWN)) move_vertical(1);
            if (rep & (KEY_UP | KEY_CPAD_UP)) move_vertical(-1);
            if (down & KEY_R) set_cat(s_cat + 1 < s_ncats ? s_cat + 1 : s_cat);
            if (down & KEY_L) set_cat(s_cat - 1);
            if ((down & KEY_A) && cur()) {
                s_mode = M_VIEW;
                sound_play(SND_OPEN);
            }
            if ((down & KEY_Y) && cur()) open_share(false);
            if ((down & KEY_X) && cur()) {
                open_sort();
                if (s_ntitles < 0) needTitles = true;
            }
            if (down & KEY_SELECT) {
                sound_set_enabled(!sound_enabled());
                sound_play(SND_OPEN);
                s_toast = 100;
            }
            if (down & KEY_TOUCH) {
                if (tp.py >= 26 && tp.py < 56) {
                    int k = tab_at(tp.px);
                    if (k >= 0) set_cat(k);
                } else if (tp.py >= GRID_TOP) {
                    for (int k = 0; k < s_ncells; k++) {
                        Cell *c = &s_cells[k];
                        float y = GRID_TOP + GRID_PAD + c->y - s_scroll;
                        if (c->shot >= 0 && tp.px >= c->x && tp.px < c->x + TH_W && tp.py >= y && tp.py < y + TH_H) {
                            int p = fpos_of_shot(c->shot);
                            if (p == s_sel) {
                                s_mode = M_VIEW;
                                sound_play(SND_OPEN);
                            } else s_sel = p;
                        }
                    }
                }
            }
            break;
        case M_VIEW:
            if (down & (KEY_B | KEY_A | KEY_TOUCH)) {
                s_mode = M_ALBUM;
                sound_play(SND_BACK);
            }
            if (rep & (KEY_RIGHT | KEY_R)) s_sel = s_sel + 1 < s_nf ? s_sel + 1 : s_sel;
            if (rep & (KEY_LEFT | KEY_L)) s_sel = s_sel > 0 ? s_sel - 1 : 0;
            if (down & KEY_Y) open_share(false);
            break;
        case M_SHARE:
            share_poll_fallback();
            if (down & KEY_B || ((down & KEY_TOUCH) && tp.py >= 210 && tp.px < 106)) {
                share_stop();
                sound_play(SND_BACK);
                s_mode = M_ALBUM;
            } else if ((down & KEY_X) || ((down & KEY_TOUCH) && tp.py >= 210 && tp.px >= 110)) {
                sound_play(SND_TAP);
                if (share_state() == SHARE_ONLINE) share_go_local();
                else if (share_state() != SHARE_SENDING) open_share(false);
            }
            break;
        case M_UPDATE: {
            UpdateState us = update_state();
            if ((us == UPD_AVAILABLE || us == UPD_ERROR) && (down & KEY_A)) {
                sound_play(SND_OPEN);
                update_install_async();
            } else if ((us == UPD_AVAILABLE || us == UPD_ERROR) && (down & KEY_B)) {
                sound_play(SND_BACK);
                update_dismiss();
                s_mode = M_ALBUM;
            } else if ((us == UPD_AVAILABLE || us == UPD_ERROR) && (down & KEY_TOUCH) && tp.py >= 160 && tp.py < 190) {
                if (tp.px < 160) {
                    sound_play(SND_OPEN);
                    update_install_async();
                } else {
                    sound_play(SND_BACK);
                    update_dismiss();
                    s_mode = M_ALBUM;
                }
            }
            break;
        }
        case M_CONFIRM:
            if ((down & KEY_A) || ((down & KEY_TOUCH) && tp.py >= 148 && tp.py < 180 && tp.px < 160)) {
                delete_current();
                sound_play(SND_COPY);
                s_mode = M_ALBUM;
            } else if ((down & KEY_B) || ((down & KEY_TOUCH) && tp.py >= 148 && tp.py < 180)) {
                sound_play(SND_BACK);
                s_mode = M_SORT;
            }
            break;
        case M_SORT: {
            int n = sort_items();
            int beforeSort = s_sortSel;
            if (rep & (KEY_DOWN | KEY_CPAD_DOWN)) s_sortSel = s_sortSel + 1 < n ? s_sortSel + 1 : s_sortSel;
            if (rep & (KEY_UP | KEY_CPAD_UP)) s_sortSel = s_sortSel > 0 ? s_sortSel - 1 : 0;
            if (rep & KEY_RIGHT) s_sortSel = s_sortSel + 5 < n ? s_sortSel + 5 : n - 1;
            if (rep & KEY_LEFT) s_sortSel = s_sortSel > 5 ? s_sortSel - 5 : 0;
            if (s_sortSel != beforeSort) sound_play(SND_MOVE);
            if (down & KEY_B) {
                s_mode = M_ALBUM;
                sound_play(SND_BACK);
            } else if ((down & KEY_A) && s_ntitles >= 0) sort_confirm();
            else if ((down & KEY_TOUCH) && tp.py > 28 && s_ntitles >= 0) {
                int k = (int)((tp.py - 32 + s_sortScroll) / 38);
                if (k >= 0 && k < n) {
                    if (k == s_sortSel) sort_confirm();
                    else {
                        s_sortSel = k;
                        sound_play(SND_TAP);
                    }
                }
            }
            break;
        }
        }
        if (s_sel != before && (s_mode == M_ALBUM || s_mode == M_VIEW)) sound_play(SND_MOVE);

        // sons de fin de partage
        if (s_mode == M_SHARE) {
            ShareState st = share_state();
            if (st != s_lastShare) {
                if (st == SHARE_ONLINE || st == SHARE_LOCAL) sound_play(SND_SUCCESS);
                else if (st == SHARE_ERROR || st == SHARE_NO_WIFI) sound_play(SND_ERROR);
                s_lastShare = st;
            }
        }

        update_poll();
        // mise à jour : proposée une fois, quand on est dans l'album
        UpdateState us = update_state();
        if (us == UPD_AVAILABLE && !updateAsked && s_mode == M_ALBUM) {
            updateAsked = true;
            s_mode = M_UPDATE;
            sound_play(SND_OPEN);
        }
        static bool errSounded = false;
        if (s_mode == M_UPDATE && us == UPD_ERROR && !errSounded) sound_play(SND_ERROR);
        errSounded = us == UPD_ERROR;
        if (s_mode == M_UPDATE && us == UPD_DONE) {
            if (s_doneFrames == 0) sound_play(SND_SUCCESS);
            if (++s_doneFrames > 90) {
                update_relaunch();
                break;
            }
        }
        if (s_mode == M_UPDATE && us == UPD_IDLE) s_mode = M_ALBUM;

        if (s_sel != before) s_selStable = 0;
        else s_selStable++;

        // défilement de la grille pour garder la sélection visible
        int c = cell_of(s_sel);
        if (c >= 0) {
            float y = s_cells[c].y, target = s_scroll;
            float viewH = SCR_H - GRID_TOP - GRID_PAD - 6;
            if (y - target < HEAD_H && (c == 0 || s_cells[c - 1].shot < 0)) target = y - HEAD_H - 2;
            else if (y - target < 4) target = y - 4;
            if (y + TH_H + 8 - target > viewH) target = y + TH_H + 8 - viewH;
            if (target < 0) target = 0;
            s_scroll += (target - s_scroll) * 0.3f;
        }
        keep_tab_visible();
        if (s_mode == M_SORT) {
            float target = s_sortScroll, y = s_sortSel * 38;
            if (y - target < 0) target = y;
            if (y + 38 - target > SCR_H - 36) target = y + 38 - (SCR_H - 36);
            s_sortScroll += (target - s_sortScroll) * 0.35f;
        }

        // ----- dessin
        ui_frame_begin();
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, COL(0xF4, 0xF4, 0xF4));
        C2D_SceneBegin(top);
        if (s_mode == M_VIEW) draw_top_view();
        else draw_top_album();
        C2D_TargetClear(bot, COL(0xF4, 0xF4, 0xF4));
        C2D_SceneBegin(bot);
        if (s_mode == M_VIEW) draw_bottom_view();
        else if (s_mode == M_SHARE) draw_bottom_share();
        else if (s_mode == M_SORT) draw_bottom_sort();
        else if (s_mode == M_UPDATE) draw_bottom_update();
        else if (s_mode == M_CONFIRM) draw_bottom_confirm();
        else draw_bottom_album();
        draw_toast();
        C3D_FrameEnd(0);
        s_frame++;

        // ----- chargements, après l'image pour garder l'interface fluide
        if (needTitles) {
            needTitles = false;
            s_ntitles = titles_load(&s_titles);
        }
        if (s_mode == M_ALBUM) load_visible_thumbs();
        if (s_mode == M_VIEW) load_preview(true);
        else if (s_selStable >= 4) load_preview(false);
    }

    update_exit();
    sound_exit();
    share_exit();
    if (s_titles) titles_free(s_titles, s_ntitles);
    gpu_free(&s_prevTop);
    gpu_free(&s_prevBot);
    shots_free(&L);
    free(s_f);
    free(s_cells);
    if (s_sheet) C2D_SpriteSheetFree(s_sheet);
    aptUnhook(&hookCookie);
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    acExit();
    amExit();
    cfguExit();
    romfsExit();
    return 0;
}
