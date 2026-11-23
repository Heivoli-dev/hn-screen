#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

// Image RGB 8 bits par canal, lignes de haut en bas.
typedef struct {
    int w, h;
    u8 *px; // w * h * 3
} RgbImage;

bool img_load_bmp(const char *path, RgbImage *out);
void img_free(RgbImage *img);

// Texture GPU (RGB565) prête pour citro2d.
typedef struct {
    bool ok;
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
    C2D_Image img;
} GpuImage;

bool gpu_from_rgb(GpuImage *g, const RgbImage *src);
// Vignette 100 x 60 (RGB565 linéaire) : création, cache sur la carte SD, texture.
#define THUMB_W 100
#define THUMB_H 60
bool thumb_make(const RgbImage *top, u16 *out565);
bool gpu_from_565(GpuImage *g, const u16 *px, int w, int h);
void gpu_free(GpuImage *g);

// PNG non compressé (deflate « stocké ») : rapide sur la console, recompressé par le navigateur.
u8 *png_encode_rgb(const u8 *rgb, int w, int h, size_t *outLen);

// Assemble haut (400x240) + bas (320x240, centré) en une image 400x480.
bool img_compose_both(const RgbImage *top, const RgbImage *bot, RgbImage *out);
