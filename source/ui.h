#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240

#define COL(r, g, b) C2D_Color32((r), (g), (b), 0xFF)

// Palette du menu HOME de la 3DS : gris clair rayé, argent, sélection cyan.
#define C_TXT       COL(0x3C, 0x3C, 0x3C)
#define C_TXT2      COL(0x6E, 0x6E, 0x6E)
#define C_WHITE     COL(0xFF, 0xFF, 0xFF)
#define C_BORDER    COL(0xB8, 0xB8, 0xB8)
#define C_BORDER2   COL(0xA8, 0xA8, 0xA8)
#define C_SILVER    COL(0xE0, 0xE0, 0xE0)
#define C_CYAN_HI   COL(0x5F, 0xD2, 0xFF)
#define C_CYAN      COL(0x12, 0xA8, 0xE8)
#define C_CYAN_DK   COL(0x00, 0x90, 0xD4)
#define C_CYAN_LINE COL(0x00, 0x79, 0xB5)
#define C_CYAN_GLOW COL(0xA6, 0xE2, 0xFA)
#define C_PLACE     COL(0xD4, 0xD4, 0xD4)

// Glyphes des boutons dans la police système.
#define G_A ""
#define G_B ""
#define G_X ""
#define G_Y ""
#define G_L ""
#define G_R ""

void ui_init(void);
void ui_exit(void);
void ui_frame_begin(void);

u32 ui_mix(u32 a, u32 b, float t);

void ui_text(float x, float y, float scale, u32 color, const char *str);
void ui_text_right(float xr, float y, float scale, u32 color, const char *str);
void ui_text_center(float cx, float y, float scale, u32 color, const char *str);
void ui_text_wrap(float x, float y, float scale, u32 color, float width, const char *str);
float ui_text_width(float scale, const char *str);
// Coupe le texte avec « … » pour qu'il tienne dans `width`.
void ui_text_fit(float x, float y, float scale, u32 color, float width, const char *str);

void ui_rect(float x, float y, float w, float h, u32 c);
void ui_rrect(float x, float y, float w, float h, float r, u32 c);
void ui_rrect_grad(float x, float y, float w, float h, float r, u32 top, u32 bot);

// Fond rayé du menu HOME.
void ui_stripes(float w, float h);
// Halo cyan de sélection autour d'un rectangle.
void ui_glow(float x, float y, float w, float h, float r);
// Bouton : argent (normal) ou cyan (principal / sélectionné).
void ui_button(float x, float y, float w, float h, const char *label, bool primary);
// Barre grise façon 3DS.
void ui_grey_bar(float x, float y, float w, float h);
// Barre blanche à dégradé (en-tête de l'écran du bas).
void ui_white_bar(float x, float y, float w, float h);
// Petite icône d'appareil photo (pastille cyan).
void ui_camera_badge(float x, float y, float size);
// Indicateur d'attente qui tourne.
void ui_spinner(float cx, float cy, float r, u32 frame);
