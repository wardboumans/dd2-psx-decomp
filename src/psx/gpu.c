#include "psx/gpu.h"
#include "psx/libgpu.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Behaviour follows the PSX GPU (see the psx-spx "GPU" chapter): integer vertices with a
 * top-left fill rule, quads drawn as triangles (0,1,2) and (1,2,3), affine texturing,
 * 128 = 1.0 colour modulation, the four semi-transparency modes and the 4x4 dither. */

typedef struct {
    int x, y;
    int r, g, b;
    int u, v;
} Vertex;

static struct {
    int clip_x1, clip_y1, clip_x2, clip_y2; /* inclusive */
    int ofs_x, ofs_y;
    u_int texpage;     /* E1 bits 0-8: page x/y, semi mode, depth */
    bool dither;
    bool draw_to_display;
    int tw_mask_x, tw_mask_y, tw_ofs_x, tw_ofs_y;
    bool set_mask, check_mask;
} st;

static const int dither_table[4][4] = {
    {-4, 0, -3, 1},
    {2, -2, 3, -1},
    {-3, 1, -4, 0},
    {3, -1, 2, -2},
};

static u_short *vram;

void gpu_reset(void)
{
    vram = gpu_vram();
    memset(&st, 0, sizeof(st));
    st.clip_x2 = VRAM_WIDTH - 1;
    st.clip_y2 = VRAM_HEIGHT - 1;
}

static int sign11(u_int v)
{
    return (int)((v & 0x7ff) ^ 0x400) - 0x400;
}

/* ---- pixel pipeline ---- */

static u_short texel(int u, int v, u_int texpage, u_int clut)
{
    u = (u & ~(st.tw_mask_x * 8)) | ((st.tw_ofs_x & st.tw_mask_x) * 8);
    v = (v & ~(st.tw_mask_y * 8)) | ((st.tw_ofs_y & st.tw_mask_y) * 8);
    u &= 0xff;
    v &= 0xff;
    int page_x = (int)(texpage & 0xf) * 64;
    int page_y = (int)((texpage >> 4) & 1) * 256;
    int clut_x = (int)(clut & 0x3f) * 16;
    int clut_y = (int)((clut >> 6) & 0x1ff);
    const u_short *row = vram + ((page_y + v) & (VRAM_HEIGHT - 1)) * VRAM_WIDTH;
    switch ((texpage >> 7) & 3) {
    case 0: {
        u_short w = row[(page_x + u / 4) & (VRAM_WIDTH - 1)];
        int idx = (w >> ((u & 3) * 4)) & 0xf;
        return vram[clut_y * VRAM_WIDTH + ((clut_x + idx) & (VRAM_WIDTH - 1))];
    }
    case 1: {
        u_short w = row[(page_x + u / 2) & (VRAM_WIDTH - 1)];
        int idx = (w >> ((u & 1) * 8)) & 0xff;
        return vram[clut_y * VRAM_WIDTH + ((clut_x + idx) & (VRAM_WIDTH - 1))];
    }
    default:
        return row[(page_x + u) & (VRAM_WIDTH - 1)];
    }
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Writes one pixel. `r, g, b` are 8-bit; with a texture they modulate it (128 = 1.0) unless raw. */
static void plot(int x, int y, int r, int g, int b, bool textured, bool raw, bool semi, bool dither,
                 u_int texpage, u_int clut, int u, int v)
{
    if (x < st.clip_x1 || x > st.clip_x2 || y < st.clip_y1 || y > st.clip_y2)
        return;
    u_short *dst = &vram[y * VRAM_WIDTH + x];
    if (st.check_mask && (*dst & 0x8000))
        return;

    int fr, fg, fb;
    bool mask_bit = false;
    if (textured) {
        u_short t = texel(u, v, texpage, clut);
        if (t == 0)
            return;
        mask_bit = (t & 0x8000) != 0;
        semi = semi && mask_bit;
        int tr = t & 31, tg = (t >> 5) & 31, tb = (t >> 10) & 31;
        if (raw) {
            fr = tr;
            fg = tg;
            fb = tb;
        } else {
            int d = dither ? dither_table[y & 3][x & 3] : 0;
            fr = clamp(((tr * r) >> 4) + d, 0, 255) >> 3;
            fg = clamp(((tg * g) >> 4) + d, 0, 255) >> 3;
            fb = clamp(((tb * b) >> 4) + d, 0, 255) >> 3;
        }
    } else {
        int d = dither ? dither_table[y & 3][x & 3] : 0;
        fr = clamp(r + d, 0, 255) >> 3;
        fg = clamp(g + d, 0, 255) >> 3;
        fb = clamp(b + d, 0, 255) >> 3;
    }

    if (semi) {
        int br = *dst & 31, bg = (*dst >> 5) & 31, bb = (*dst >> 10) & 31;
        switch ((texpage >> 5) & 3) {
        case 0: fr = (br + fr) >> 1; fg = (bg + fg) >> 1; fb = (bb + fb) >> 1; break;
        case 1: fr = br + fr; fg = bg + fg; fb = bb + fb; break;
        case 2: fr = br - fr; fg = bg - fg; fb = bb - fb; break;
        default: fr = br + fr / 4; fg = bg + fg / 4; fb = bb + fb / 4; break;
        }
        fr = clamp(fr, 0, 31);
        fg = clamp(fg, 0, 31);
        fb = clamp(fb, 0, 31);
    }
    *dst = (u_short)(fr | fg << 5 | fb << 10 | ((mask_bit || st.set_mask) ? 0x8000 : 0));
}

/* ---- triangles ---- */

static int64_t edge(const Vertex *a, const Vertex *b, int px, int py)
{
    return (int64_t)(b->x - a->x) * (py - a->y) - (int64_t)(b->y - a->y) * (px - a->x);
}

/* Top-left rule: pixels exactly on an edge are drawn only for top or left edges. With the
 * winding triangle() normalises to (positive area, y down), a top edge runs towards +x and a
 * left edge runs upwards. */
static bool top_left(const Vertex *a, const Vertex *b)
{
    return (a->y == b->y && b->x > a->x) || b->y < a->y;
}

static void triangle(Vertex v0, Vertex v1, Vertex v2, bool gouraud, bool textured, bool raw, bool semi,
                     u_int texpage, u_int clut)
{
    /* The GPU skips polygons with an edge longer than 1023 x 511. */
    const Vertex *vs[3] = {&v0, &v1, &v2};
    for (int i = 0; i < 3; i++) {
        const Vertex *a = vs[i], *b = vs[(i + 1) % 3];
        if (a->x - b->x > 1023 || b->x - a->x > 1023 || a->y - b->y > 511 || b->y - a->y > 511)
            return;
    }
    int64_t area = edge(&v0, &v1, v2.x, v2.y);
    if (area == 0)
        return;
    if (area < 0) {
        Vertex t = v1;
        v1 = v2;
        v2 = t;
        area = -area;
    }
    int min_x = clamp(v0.x < v1.x ? (v0.x < v2.x ? v0.x : v2.x) : (v1.x < v2.x ? v1.x : v2.x), st.clip_x1, st.clip_x2);
    int max_x = clamp(v0.x > v1.x ? (v0.x > v2.x ? v0.x : v2.x) : (v1.x > v2.x ? v1.x : v2.x), st.clip_x1, st.clip_x2);
    int min_y = clamp(v0.y < v1.y ? (v0.y < v2.y ? v0.y : v2.y) : (v1.y < v2.y ? v1.y : v2.y), st.clip_y1, st.clip_y2);
    int max_y = clamp(v0.y > v1.y ? (v0.y > v2.y ? v0.y : v2.y) : (v1.y > v2.y ? v1.y : v2.y), st.clip_y1, st.clip_y2);
    bool tl0 = top_left(&v1, &v2), tl1 = top_left(&v2, &v0), tl2 = top_left(&v0, &v1);
    bool dither = st.dither && (gouraud || (textured && !raw));

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int64_t w0 = edge(&v1, &v2, x, y), w1 = edge(&v2, &v0, x, y), w2 = edge(&v0, &v1, x, y);
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            if ((w0 == 0 && !tl0) || (w1 == 0 && !tl1) || (w2 == 0 && !tl2))
                continue;
            int r = v0.r, g = v0.g, b = v0.b, u = 0, v = 0;
            if (gouraud) {
                r = (int)((w0 * v0.r + w1 * v1.r + w2 * v2.r) / area);
                g = (int)((w0 * v0.g + w1 * v1.g + w2 * v2.g) / area);
                b = (int)((w0 * v0.b + w1 * v1.b + w2 * v2.b) / area);
            }
            if (textured) {
                u = (int)((w0 * v0.u + w1 * v1.u + w2 * v2.u) / area);
                v = (int)((w0 * v0.v + w1 * v1.v + w2 * v2.v) / area);
            }
            plot(x, y, r, g, b, textured, raw, semi, dither, texpage, clut, u, v);
        }
    }
}

/* ---- commands ---- */

static void set_texpage_bits(u_int bits)
{
    st.texpage = bits & 0x1ff;
    st.dither = (bits >> 9) & 1;
    st.draw_to_display = (bits >> 10) & 1;
}

static int polygon(u_int cmd, const u_int *w, int count)
{
    bool gouraud = (cmd >> 28) & 1, quad = (cmd >> 27) & 1, textured = (cmd >> 26) & 1;
    bool semi = (cmd >> 25) & 1, raw = (cmd >> 24) & 1;
    int n = quad ? 4 : 3;
    int size = 1 + n + (textured ? n : 0) + (gouraud ? n - 1 : 0);
    if (count < size)
        return count;
    Vertex v[4];
    u_int clut = 0, texpage = st.texpage;
    int i = 1;
    for (int k = 0; k < n; k++) {
        u_int color = (k == 0 || !gouraud) ? cmd : w[i++];
        v[k].r = (int)(color & 0xff);
        v[k].g = (int)((color >> 8) & 0xff);
        v[k].b = (int)((color >> 16) & 0xff);
        v[k].x = sign11(w[i]) + st.ofs_x;
        v[k].y = sign11(w[i] >> 16) + st.ofs_y;
        i++;
        if (textured) {
            v[k].u = (int)(w[i] & 0xff);
            v[k].v = (int)((w[i] >> 8) & 0xff);
            if (k == 0)
                clut = w[i] >> 16;
            if (k == 1) {
                texpage = (w[i] >> 16) & 0x1ff;
                st.texpage = texpage;
            }
            i++;
        }
    }
    if (!textured)
        raw = false;
    triangle(v[0], v[1], v[2], gouraud, textured, raw, semi, texpage, clut);
    if (quad)
        triangle(v[1], v[2], v[3], gouraud, textured, raw, semi, texpage, clut);
    return size;
}

static void line(Vertex a, Vertex b, bool gouraud, bool semi)
{
    int dx = b.x - a.x, dy = b.y - a.y;
    if (dx > 1023 || -dx > 1023 || dy > 511 || -dy > 511)
        return;
    int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    bool dither = st.dither && gouraud;
    for (int s = 0; s <= steps; s++) {
        int x = steps ? a.x + (dx * s + (dx >= 0 ? steps / 2 : -steps / 2)) / steps : a.x;
        int y = steps ? a.y + (dy * s + (dy >= 0 ? steps / 2 : -steps / 2)) / steps : a.y;
        int r = a.r, g = a.g, bl = a.b;
        if (gouraud && steps) {
            r = a.r + (b.r - a.r) * s / steps;
            g = a.g + (b.g - a.g) * s / steps;
            bl = a.b + (b.b - a.b) * s / steps;
        }
        plot(x, y, r, g, bl, false, false, semi, dither, st.texpage, 0, 0, 0);
    }
}

static Vertex line_vertex(u_int color, u_int xy)
{
    Vertex v = {sign11(xy) + st.ofs_x, sign11(xy >> 16) + st.ofs_y,
                (int)(color & 0xff), (int)((color >> 8) & 0xff), (int)((color >> 16) & 0xff), 0, 0};
    return v;
}

/* cmd, xy0, [color1,] xy1 for one line; a polyline continues until a 0x5xxx5xxx terminator
 * word, which can stand where the next color or vertex would be. */
static int line_cmd(u_int cmd, const u_int *w, int count)
{
    bool gouraud = (cmd >> 28) & 1, poly = (cmd >> 27) & 1, semi = (cmd >> 25) & 1;
    if (count < 2)
        return count;
    int i = 1;
    Vertex prev = line_vertex(cmd, w[i++]);
    for (int n = 0; poly || n < 1; n++) {
        if (i >= count)
            break;
        if (poly && (w[i] & 0xf000f000) == 0x50005000) {
            i++;
            break;
        }
        u_int color = gouraud ? w[i++] : cmd;
        if (i >= count)
            break;
        Vertex cur = line_vertex(color, w[i++]);
        line(prev, cur, gouraud, semi);
        prev = cur;
    }
    return i;
}

static int rectangle(u_int cmd, const u_int *w, int count)
{
    int size_code = (cmd >> 27) & 3;
    bool textured = (cmd >> 26) & 1, semi = (cmd >> 25) & 1, raw = (cmd >> 24) & 1;
    int size = 2 + (textured ? 1 : 0) + (size_code == 0 ? 1 : 0);
    if (count < size)
        return count;
    int x = sign11(w[1]) + st.ofs_x, y = sign11(w[1] >> 16) + st.ofs_y;
    int i = 2, u0 = 0, v0 = 0;
    u_int clut = 0;
    if (textured) {
        u0 = (int)(w[i] & 0xff);
        v0 = (int)((w[i] >> 8) & 0xff);
        clut = w[i] >> 16;
        i++;
    }
    int width, height;
    switch (size_code) {
    case 0: width = (int)(w[i] & 0x3ff); height = (int)((w[i] >> 16) & 0x1ff); break;
    case 1: width = height = 1; break;
    case 2: width = height = 8; break;
    default: width = height = 16; break;
    }
    int r = (int)(cmd & 0xff), g = (int)((cmd >> 8) & 0xff), b = (int)((cmd >> 16) & 0xff);
    if (!textured)
        raw = false;
    for (int dy = 0; dy < height; dy++)
        for (int dx = 0; dx < width; dx++)
            plot(x + dx, y + dy, r, g, b, textured, raw, semi, false, st.texpage, clut, u0 + dx, v0 + dy);
    return size;
}

/* Fill ignores the drawing area, offset and mask; x and width go in steps of 16. */
static void fill(u_int cmd, u_int xy, u_int wh)
{
    u_short pixel = (u_short)(((cmd >> 3) & 31) | (((cmd >> 11) & 31) << 5) | (((cmd >> 19) & 31) << 10));
    int x0 = (int)(xy & 0x3f0), y0 = (int)((xy >> 16) & 0x1ff);
    int w = (int)(((wh & 0x3ff) + 15) & ~15), h = (int)((wh >> 16) & 0x1ff);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            vram[((y0 + y) & (VRAM_HEIGHT - 1)) * VRAM_WIDTH + ((x0 + x) & (VRAM_WIDTH - 1))] = pixel;
}

void gpu_gp0(const u_int *w, int count)
{
    if (!vram)
        gpu_reset();
    while (count > 0) {
        u_int cmd = w[0];
        int used = 1;
        switch (cmd >> 29) {
        case 1:
            used = polygon(cmd, w, count);
            break;
        case 2:
            used = line_cmd(cmd, w, count);
            break;
        case 3:
            used = rectangle(cmd, w, count);
            break;
        case 0:
            if ((cmd >> 24) == 0x02 && count >= 3) {
                fill(cmd, w[1], w[2]);
                used = 3;
            }
            break;
        case 7:
            switch (cmd >> 24) {
            case 0xe1: set_texpage_bits(cmd); break;
            case 0xe2:
                st.tw_mask_x = (int)(cmd & 31);
                st.tw_mask_y = (int)((cmd >> 5) & 31);
                st.tw_ofs_x = (int)((cmd >> 10) & 31);
                st.tw_ofs_y = (int)((cmd >> 15) & 31);
                break;
            case 0xe3: st.clip_x1 = (int)(cmd & 0x3ff); st.clip_y1 = (int)((cmd >> 10) & 0x1ff); break;
            case 0xe4: st.clip_x2 = (int)(cmd & 0x3ff); st.clip_y2 = (int)((cmd >> 10) & 0x1ff); break;
            case 0xe5: st.ofs_x = sign11(cmd); st.ofs_y = sign11(cmd >> 11); break;
            case 0xe6: st.set_mask = cmd & 1; st.check_mask = (cmd >> 1) & 1; break;
            default: break;
            }
            break;
        default: {
            static bool reported;
            if (!reported) {
                reported = true;
                fprintf(stderr, "gpu: GP0 command 0x%02x not supported\n", (unsigned)(cmd >> 24));
            }
            break;
        }
        }
        w += used;
        count -= used;
    }
}
