#include "img.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 rd32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static u16 rd16(const u8 *p) { return p[0] | (p[1] << 8); }

bool img_load_bmp(const char *path, RgbImage *out)
{
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 54 || size > 4 * 1024 * 1024) {
        fclose(f);
        return false;
    }
    u8 *data = malloc(size);
    if (!data) {
        fclose(f);
        return false;
    }
    size_t got = fread(data, 1, size, f);
    fclose(f);
    bool ok = false;
    if (got == (size_t)size && data[0] == 'B' && data[1] == 'M') {
        u32 off = rd32(data + 10);
        int w = (int)rd32(data + 18);
        int h = (int)rd32(data + 22);
        int bpp = rd16(data + 28);
        bool topDown = h < 0;
        if (topDown) h = -h;
        int bytes = bpp / 8;
        if (w > 0 && h > 0 && w <= 1024 && h <= 1024 && (bpp == 24 || bpp == 32)) {
            size_t stride = ((size_t)w * bytes + 3) & ~3u;
            if (off + stride * h <= (size_t)size) {
                out->px = malloc((size_t)w * h * 3);
                if (out->px) {
                    out->w = w;
                    out->h = h;
                    for (int y = 0; y < h; y++) {
                        const u8 *row = data + off + stride * (topDown ? y : h - 1 - y);
                        u8 *dst = out->px + (size_t)y * w * 3;
                        for (int x = 0; x < w; x++) {
                            dst[x * 3 + 0] = row[x * bytes + 2];
                            dst[x * 3 + 1] = row[x * bytes + 1];
                            dst[x * 3 + 2] = row[x * bytes + 0];
                        }
                    }
                    ok = true;
                }
            }
        }
    }
    free(data);
    return ok;
}

void img_free(RgbImage *img)
{
    free(img->px);
    memset(img, 0, sizeof(*img));
}

static inline u32 morton(u32 x, u32 y)
{
    static const u8 tbl[8] = {0, 1, 4, 5, 16, 17, 20, 21};
    return tbl[x & 7] | (tbl[y & 7] << 1);
}

static inline u16 to565(const u8 *p)
{
    return ((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3);
}

static int pow2(int v)
{
    int p = 8;
    while (p < v) p <<= 1;
    return p;
}

static bool gpu_alloc(GpuImage *g, int w, int h, int *tw, int *th)
{
    gpu_free(g);
    *tw = pow2(w);
    *th = pow2(h);
    if (!C3D_TexInit(&g->tex, *tw, *th, GPU_RGB565)) return false;
    memset(g->tex.data, 0, g->tex.size);
    C3D_TexSetFilter(&g->tex, GPU_LINEAR, GPU_LINEAR);
    g->sub.width = w;
    g->sub.height = h;
    g->sub.left = 0.0f;
    g->sub.top = 1.0f;
    g->sub.right = (float)w / *tw;
    g->sub.bottom = 1.0f - (float)h / *th;
    g->img.tex = &g->tex;
    g->img.subtex = &g->sub;
    g->ok = true;
    return true;
}

bool gpu_from_rgb(GpuImage *g, const RgbImage *src)
{
    int tw, th;
    if (!src->px || !gpu_alloc(g, src->w, src->h, &tw, &th)) return false;
    u16 *dst = (u16 *)g->tex.data;
    for (int y = 0; y < src->h; y++)
        for (int x = 0; x < src->w; x++)
            dst[((y >> 3) * (tw >> 3) + (x >> 3)) * 64 + morton(x, y)] = to565(src->px + ((size_t)y * src->w + x) * 3);
    C3D_TexFlush(&g->tex);
    return true;
}

bool gpu_from_565(GpuImage *g, const u16 *px, int w, int h)
{
    int tw, th;
    if (!gpu_alloc(g, w, h, &tw, &th)) return false;
    u16 *dst = (u16 *)g->tex.data;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            dst[((y >> 3) * (tw >> 3) + (x >> 3)) * 64 + morton(x, y)] = px[y * w + x];
    C3D_TexFlush(&g->tex);
    return true;
}

void gpu_free(GpuImage *g)
{
    if (g->ok) C3D_TexDelete(&g->tex);
    memset(g, 0, sizeof(*g));
}

bool thumb_make(const RgbImage *top, u16 *out)
{
    if (!top->px || top->w < THUMB_W || top->h < THUMB_H) return false;
    float sx = (float)top->w / THUMB_W, sy = (float)top->h / THUMB_H;
    for (int y = 0; y < THUMB_H; y++) {
        for (int x = 0; x < THUMB_W; x++) {
            int x0 = (int)(x * sx), x1 = (int)((x + 1) * sx), y0 = (int)(y * sy), y1 = (int)((y + 1) * sy);
            u32 r = 0, gg = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    const u8 *p = top->px + ((size_t)yy * top->w + xx) * 3;
                    r += p[0];
                    gg += p[1];
                    b += p[2];
                    n++;
                }
            if (!n) n = 1;
            u8 c[3] = {(u8)(r / n), (u8)(gg / n), (u8)(b / n)};
            out[y * THUMB_W + x] = to565(c);
        }
    }
    return true;
}

bool img_compose_both(const RgbImage *top, const RgbImage *bot, RgbImage *out)
{
    out->w = 400;
    out->h = 480;
    out->px = malloc(400 * 480 * 3);
    if (!out->px) return false;
    memset(out->px, 0x18, 400 * 480 * 3);
    const RgbImage *parts[2] = {top, bot};
    for (int i = 0; i < 2; i++) {
        const RgbImage *p = parts[i];
        if (!p || !p->px) continue;
        int ox = (400 - p->w) / 2, oy = i * 240;
        int w = p->w > 400 ? 400 : p->w, h = p->h > 240 ? 240 : p->h;
        if (ox < 0) ox = 0;
        for (int y = 0; y < h; y++)
            memcpy(out->px + ((size_t)(oy + y) * 400 + ox) * 3, p->px + (size_t)y * p->w * 3, (size_t)w * 3);
    }
    return true;
}

// --- PNG -------------------------------------------------------------------
static u32 s_crc[256];
static bool s_crcReady = false;

static u32 crc_update(u32 c, const u8 *p, size_t n)
{
    if (!s_crcReady) {
        for (u32 i = 0; i < 256; i++) {
            u32 v = i;
            for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            s_crc[i] = v;
        }
        s_crcReady = true;
    }
    for (size_t i = 0; i < n; i++) c = s_crc[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static void put32(u8 *p, u32 v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

u8 *png_encode_rgb(const u8 *rgb, int w, int h, size_t *outLen)
{
    size_t raw = (size_t)(w * 3 + 1) * h;
    size_t blocks = (raw + 65534) / 65535;
    size_t zlen = 2 + raw + blocks * 5 + 4;
    size_t total = 8 + 25 + (12 + zlen) + 12;
    u8 *png = malloc(total);
    if (!png) return NULL;
    u8 *p = png;
    static const u8 sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    memcpy(p, sig, 8);
    p += 8;
    // IHDR
    put32(p, 13);
    memcpy(p + 4, "IHDR", 4);
    put32(p + 8, w);
    put32(p + 12, h);
    p[16] = 8;  // bits
    p[17] = 2;  // RGB
    p[18] = 0;
    p[19] = 0;
    p[20] = 0;
    put32(p + 21, crc_update(0xFFFFFFFF, p + 4, 17) ^ 0xFFFFFFFF);
    p += 25;
    // IDAT
    put32(p, (u32)zlen);
    memcpy(p + 4, "IDAT", 4);
    u8 *z = p + 8;
    z[0] = 0x78;
    z[1] = 0x01;
    u8 *d = z + 2;
    u32 a = 1, b = 0;
    size_t left = raw, row = 0, col = 0;
    const size_t rowLen = (size_t)w * 3 + 1;
    while (left > 0) {
        size_t n = left > 65535 ? 65535 : left;
        *d++ = left == n ? 1 : 0;
        *d++ = n & 0xFF;
        *d++ = n >> 8;
        *d++ = ~n & 0xFF;
        *d++ = (~n >> 8) & 0xFF;
        for (size_t i = 0; i < n; i++) {
            u8 v = col == 0 ? 0 : rgb[row * w * 3 + col - 1];
            *d++ = v;
            a = (a + v) % 65521;
            b = (b + a) % 65521;
            if (++col == rowLen) {
                col = 0;
                row++;
            }
        }
        left -= n;
    }
    put32(d, (b << 16) | a);
    d += 4;
    put32(d, crc_update(0xFFFFFFFF, p + 4, 4 + zlen) ^ 0xFFFFFFFF);
    p = d + 4;
    // IEND
    put32(p, 0);
    memcpy(p + 4, "IEND", 4);
    put32(p + 8, crc_update(0xFFFFFFFF, p + 4, 4) ^ 0xFFFFFFFF);
    p += 12;
    *outLen = p - png;
    return png;
}
