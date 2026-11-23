// Outils de dessin citro2d dans le style du menu HOME de la 3DS.
// Toutes les formes sont opaques (les coins arrondis sont faits de cercles
// qui se chevauchent : de la transparence ferait des artefacts).
#include "ui.h"
#include <math.h>
#include <string.h>

static C2D_TextBuf s_buf;

void ui_init(void) { s_buf = C2D_TextBufNew(8192); }
void ui_exit(void) { C2D_TextBufDelete(s_buf); }
void ui_frame_begin(void) { C2D_TextBufClear(s_buf); }

u32 ui_mix(u32 a, u32 b, float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    u32 out = 0;
    for (int i = 0; i < 4; i++) {
        float ca = (a >> (i * 8)) & 0xFF, cb = (b >> (i * 8)) & 0xFF;
        out |= ((u32)(ca + (cb - ca) * t + 0.5f) & 0xFF) << (i * 8);
    }
    return out;
}

static bool parse(C2D_Text *t, const char *str)
{
    if (!str || !*str) return false;
    if (!C2D_TextParse(t, s_buf, str)) return false;
    C2D_TextOptimize(t);
    return true;
}

void ui_text(float x, float y, float scale, u32 color, const char *str)
{
    C2D_Text t;
    if (parse(&t, str)) C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

float ui_text_width(float scale, const char *str)
{
    C2D_Text t;
    float w = 0;
    if (parse(&t, str)) C2D_TextGetDimensions(&t, scale, scale, &w, NULL);
    return w;
}

void ui_text_right(float xr, float y, float scale, u32 color, const char *str)
{
    ui_text(xr - ui_text_width(scale, str), y, scale, color, str);
}

void ui_text_center(float cx, float y, float scale, u32 color, const char *str)
{
    ui_text(cx - ui_text_width(scale, str) / 2, y, scale, color, str);
}

void ui_text_wrap(float x, float y, float scale, u32 color, float width, const char *str)
{
    C2D_Text t;
    if (parse(&t, str)) C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, x, y, 0.5f, scale, scale, color, width);
}

void ui_text_fit(float x, float y, float scale, u32 color, float width, const char *str)
{
    if (!str) return;
    if (ui_text_width(scale, str) <= width) {
        ui_text(x, y, scale, color, str);
        return;
    }
    char buf[160];
    size_t n = strlen(str);
    if (n > sizeof(buf) - 4) n = sizeof(buf) - 4;
    while (n > 0) {
        n--;
        while (n > 0 && ((unsigned char)str[n] & 0xC0) == 0x80) n--; // ne pas couper un caractère UTF-8
        memcpy(buf, str, n);
        memcpy(buf + n, "…", 4);
        if (ui_text_width(scale, buf) <= width) break;
    }
    ui_text(x, y, scale, color, buf);
}

void ui_rect(float x, float y, float w, float h, u32 c)
{
    C2D_DrawRectSolid(x, y, 0.5f, w, h, c);
}

void ui_rrect(float x, float y, float w, float h, float r, u32 c)
{
    ui_rrect_grad(x, y, w, h, r, c, c);
}

void ui_rrect_grad(float x, float y, float w, float h, float r, u32 top, u32 bot)
{
    if (w <= 0 || h <= 0) return;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r < 0) r = 0;
    const float z = 0.5f;
    float k = r / h;
    u32 mt = ui_mix(top, bot, k), mb = ui_mix(top, bot, 1 - k);
    C2D_DrawRectangle(x + r, y, z, w - 2 * r, r, top, top, mt, mt);
    C2D_DrawRectangle(x, y + r, z, w, h - 2 * r, mt, mt, mb, mb);
    C2D_DrawRectangle(x + r, y + h - r, z, w - 2 * r, r, mb, mb, bot, bot);
    if (r > 0.5f) {
        u32 ct = ui_mix(top, mt, 0.5f), cb = ui_mix(mb, bot, 0.5f);
        C2D_DrawCircleSolid(x + r, y + r, z, r, ct);
        C2D_DrawCircleSolid(x + w - r, y + r, z, r, ct);
        C2D_DrawCircleSolid(x + r, y + h - r, z, r, cb);
        C2D_DrawCircleSolid(x + w - r, y + h - r, z, r, cb);
    }
}

void ui_stripes(float w, float h)
{
    const u32 a = COL(0xF4, 0xF4, 0xF4), b = COL(0xEA, 0xEA, 0xEA);
    C2D_DrawRectSolid(0, 0, 0.5f, w, h, a);
    for (float x = 0; x < w + h; x += 12) {
        // bande « / » de 6 px
        C2D_DrawTriangle(x, 0, b, x + 6, 0, b, x - h, h, b, 0.5f);
        C2D_DrawTriangle(x + 6, 0, b, x + 6 - h, h, b, x - h, h, b, 0.5f);
    }
}

void ui_glow(float x, float y, float w, float h, float r)
{
    ui_rrect(x - 6, y - 6, w + 12, h + 12, r + 6, C_CYAN_GLOW);
    ui_rrect(x - 4, y - 4, w + 8, h + 8, r + 4, C_CYAN);
    ui_rrect(x - 2, y - 2, w + 4, h + 4, r + 2, C_WHITE);
}

void ui_button(float x, float y, float w, float h, const char *label, bool primary)
{
    float r = h > 24 ? 8 : 6;
    if (primary) {
        ui_rrect(x - 1, y - 1, w + 2, h + 2, r + 1, C_CYAN_LINE);
        ui_rrect_grad(x, y, w, h, r, C_CYAN_HI, C_CYAN_DK);
    } else {
        ui_rrect(x - 1, y - 1, w + 2, h + 2, r + 1, C_BORDER2);
        ui_rrect_grad(x, y, w, h, r, C_WHITE, C_SILVER);
    }
    float s = h >= 26 ? 0.5f : 0.45f;
    ui_text_center(x + w / 2, y + (h - 30 * s) / 2, s, primary ? C_WHITE : C_TXT, label);
}

void ui_grey_bar(float x, float y, float w, float h)
{
    C2D_DrawRectangle(x, y, 0.5f, w, h, COL(0x8C, 0x8C, 0x8C), COL(0x8C, 0x8C, 0x8C), COL(0x6E, 0x6E, 0x6E), COL(0x6E, 0x6E, 0x6E));
    C2D_DrawRectSolid(x, y, 0.5f, w, 1, COL(0x5A, 0x5A, 0x5A));
}

void ui_white_bar(float x, float y, float w, float h)
{
    C2D_DrawRectangle(x, y, 0.5f, w, h, C_WHITE, C_WHITE, COL(0xE4, 0xE4, 0xE4), COL(0xE4, 0xE4, 0xE4));
    C2D_DrawRectSolid(x, y + h - 1, 0.5f, w, 1, C_BORDER);
}

void ui_camera_badge(float x, float y, float s)
{
    ui_rrect(x - 1, y - 1, s + 2, s + 2, s * 0.28f + 1, C_WHITE);
    ui_rrect_grad(x, y, s, s, s * 0.28f, C_CYAN_HI, C_CYAN_DK);
    float bx = x + s * 0.18f, by = y + s * 0.34f, bw = s * 0.64f, bh = s * 0.44f;
    ui_rrect(bx + bw * 0.28f, by - s * 0.1f, bw * 0.44f, s * 0.14f, s * 0.04f, C_WHITE);
    ui_rrect(bx, by, bw, bh, s * 0.08f, C_WHITE);
    C2D_DrawCircleSolid(bx + bw / 2, by + bh / 2, 0.5f, bh * 0.32f, C_CYAN);
    C2D_DrawCircleSolid(bx + bw / 2, by + bh / 2, 0.5f, bh * 0.16f, C_WHITE);
}

void ui_spinner(float cx, float cy, float r, u32 frame)
{
    for (int i = 0; i < 8; i++) {
        float a = (i / 8.0f) * 6.2831853f;
        int age = (int)((frame / 4 + 8 - i) % 8);
        u32 c = ui_mix(C_CYAN, COL(0xD8, 0xD8, 0xD8), age / 7.0f);
        C2D_DrawCircleSolid(cx + cosf(a) * r, cy + sinf(a) * r, 0.5f, r * 0.22f, c);
    }
}
