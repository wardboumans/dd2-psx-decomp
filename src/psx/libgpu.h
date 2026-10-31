/* libgpu replacement: 1024x512 16-bit VRAM, a software GPU, display and transfer functions. */
#ifndef PSX_LIBGPU_H
#define PSX_LIBGPU_H

#include "psx/types.h"

#define VRAM_WIDTH 1024
#define VRAM_HEIGHT 512

typedef struct {
    short x, y;
    short w, h;
} RECT;

typedef struct {
    RECT disp;   /* VRAM area shown on screen */
    RECT screen; /* position on the TV; ignored */
    u_char isinter;
    u_char isrgb24;
    u_char pad0, pad1;
} DISPENV;

typedef struct {
    RECT clip;
    short ofs[2];
    RECT tw;
    u_short tpage;
    u_char dtd;
    u_char dfe;
    u_char isbg;
    u_char r0, g0, b0;
} DRAWENV;

/* Ordering-table link. Psy-Q packs a 24-bit address and the length into one 32-bit tag; a
 * native pointer does not fit, so every OT entry and every primitive starts with this instead.
 * The GPU command words follow it exactly as on the PSX (r0, g0, b0, code, x0, y0, ...). */
typedef struct OT_TAG {
    struct OT_TAG *next;
    u_int len; /* command words after the tag; 0 for OT entries */
    u_int pad;
} OT_TAG;

/* Primitives. Field order and sizes after the tag are the GPU's command layout. */
typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0, x1, y1, x2, y2;
} POLY_F3;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0, x1, y1, x2, y2, x3, y3;
} POLY_F4;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0;
    u_char u0, v0;
    u_short clut;
    short x1, y1;
    u_char u1, v1;
    u_short tpage;
    short x2, y2;
    u_char u2, v2;
    u_short pad1;
} POLY_FT3;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0;
    u_char u0, v0;
    u_short clut;
    short x1, y1;
    u_char u1, v1;
    u_short tpage;
    short x2, y2;
    u_char u2, v2;
    u_short pad1;
    short x3, y3;
    u_char u3, v3;
    u_short pad2;
} POLY_FT4;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0;
    u_char r1, g1, b1, pad1;
    short x1, y1;
    u_char r2, g2, b2, pad2;
    short x2, y2;
} POLY_G3;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0;
    u_char r1, g1, b1, pad1;
    short x1, y1;
    u_char r2, g2, b2, pad2;
    short x2, y2;
    u_char r3, g3, b3, pad3;
    short x3, y3;
} POLY_G4;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0;
    u_char u0, v0;
    u_short clut;
    u_char r1, g1, b1, p1;
    short x1, y1;
    u_char u1, v1;
    u_short tpage;
    u_char r2, g2, b2, p2;
    short x2, y2;
    u_char u2, v2;
    u_short pad2;
} POLY_GT3;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0;
    u_char u0, v0;
    u_short clut;
    u_char r1, g1, b1, p1;
    short x1, y1;
    u_char u1, v1;
    u_short tpage;
    u_char r2, g2, b2, p2;
    short x2, y2;
    u_char u2, v2;
    u_short pad2;
    u_char r3, g3, b3, p3;
    short x3, y3;
    u_char u3, v3;
    u_short pad3;
} POLY_GT4;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0, x1, y1;
} LINE_F2;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0;
    u_char r1, g1, b1, p1;
    short x1, y1;
} LINE_G2;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0, w, h;
} TILE;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0;
} TILE_1, TILE_8, TILE_16;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0;
    u_char u0, v0;
    u_short clut;
    short w, h;
} SPRT;

typedef struct {
    OT_TAG tag;
    u_char r0, g0, b0, code;
    short x0, y0;
    u_char u0, v0;
    u_short clut;
} SPRT_8, SPRT_16;

typedef struct {
    OT_TAG tag;
    u_long code[2];
} DR_MODE;

typedef struct {
    OT_TAG tag;
    u_long code[1];
} DR_TPAGE;

typedef struct {
    OT_TAG tag;
    u_long code[2];
} DR_AREA, DR_OFFSET, DR_TWIN;

#define setlen(p, n) (((OT_TAG *)(p))->len = (n))
#define setcode(p, c) ((p)->code = (c))
#define getcode(p) ((p)->code)

/* Codes of textured polygons, for a colour whose cd byte the GTE passes on as the packet's code. */
#define CODE_POLY_FT3 0x24
#define CODE_POLY_FT4 0x2c
#define CODE_SEMI_TRANS 0x02 /* the code bit setSemiTrans sets */
#define TPAGE_ABR 0x60 /* a texture page's semi-transparency mode (bits 5-6, as GetTPage's abr) */

#define setPolyF3(p) (setlen(p, 4), setcode(p, 0x20))
#define setPolyFT3(p) (setlen(p, 7), setcode(p, 0x24))
#define setPolyG3(p) (setlen(p, 6), setcode(p, 0x30))
#define setPolyGT3(p) (setlen(p, 9), setcode(p, 0x34))
#define setPolyF4(p) (setlen(p, 5), setcode(p, 0x28))
#define setPolyFT4(p) (setlen(p, 9), setcode(p, 0x2c))
#define setPolyG4(p) (setlen(p, 8), setcode(p, 0x38))
#define setPolyGT4(p) (setlen(p, 12), setcode(p, 0x3c))
#define setLineF2(p) (setlen(p, 3), setcode(p, 0x40))
#define setLineG2(p) (setlen(p, 4), setcode(p, 0x50))
#define setTile(p) (setlen(p, 3), setcode(p, 0x60))
#define setTile1(p) (setlen(p, 2), setcode(p, 0x68))
#define setTile8(p) (setlen(p, 2), setcode(p, 0x70))
#define setTile16(p) (setlen(p, 2), setcode(p, 0x78))
#define setSprt(p) (setlen(p, 4), setcode(p, 0x64))
#define setSprt8(p) (setlen(p, 3), setcode(p, 0x74))
#define setSprt16(p) (setlen(p, 3), setcode(p, 0x7c))

#define setSemiTrans(p, abe) ((abe) ? ((p)->code |= 2) : ((p)->code &= ~2))
#define setShadeTex(p, tge) ((tge) ? ((p)->code |= 1) : ((p)->code &= ~1))

#define setRGB0(p, r, g, b) ((p)->r0 = (r), (p)->g0 = (g), (p)->b0 = (b))
#define setRGB1(p, r, g, b) ((p)->r1 = (r), (p)->g1 = (g), (p)->b1 = (b))
#define setRGB2(p, r, g, b) ((p)->r2 = (r), (p)->g2 = (g), (p)->b2 = (b))
#define setRGB3(p, r, g, b) ((p)->r3 = (r), (p)->g3 = (g), (p)->b3 = (b))
#define setXY0(p, _x0, _y0) ((p)->x0 = (_x0), (p)->y0 = (_y0))
#define setXY2(p, _x0, _y0, _x1, _y1) ((p)->x0 = (_x0), (p)->y0 = (_y0), (p)->x1 = (_x1), (p)->y1 = (_y1))
#define setXY3(p, _x0, _y0, _x1, _y1, _x2, _y2) \
    (setXY2(p, _x0, _y0, _x1, _y1), (p)->x2 = (_x2), (p)->y2 = (_y2))
#define setXY4(p, _x0, _y0, _x1, _y1, _x2, _y2, _x3, _y3) \
    (setXY3(p, _x0, _y0, _x1, _y1, _x2, _y2), (p)->x3 = (_x3), (p)->y3 = (_y3))
#define setXYWH(p, _x0, _y0, _w, _h) \
    setXY4(p, _x0, _y0, (_x0) + (_w), _y0, _x0, (_y0) + (_h), (_x0) + (_w), (_y0) + (_h))
#define setWH(p, _w, _h) ((p)->w = (_w), (p)->h = (_h))
#define setUV0(p, _u0, _v0) ((p)->u0 = (_u0), (p)->v0 = (_v0))
#define setUV3(p, _u0, _v0, _u1, _v1, _u2, _v2) \
    ((p)->u0 = (_u0), (p)->v0 = (_v0), (p)->u1 = (_u1), (p)->v1 = (_v1), (p)->u2 = (_u2), (p)->v2 = (_v2))
#define setUV4(p, _u0, _v0, _u1, _v1, _u2, _v2, _u3, _v3) \
    (setUV3(p, _u0, _v0, _u1, _v1, _u2, _v2), (p)->u3 = (_u3), (p)->v3 = (_v3))
#define setUVWH(p, _u0, _v0, _w, _h) \
    setUV4(p, _u0, _v0, (_u0) + (_w), _v0, _u0, (_v0) + (_h), (_u0) + (_w), (_v0) + (_h))

#define getTPage(tp, abr, x, y) \
    ((u_short)((((tp) & 3) << 7) | (((abr) & 3) << 5) | (((y) & 0x100) >> 4) | (((x) & 0x3ff) >> 6)))
#define getClut(x, y) ((u_short)(((y) << 6) | (((x) >> 4) & 0x3f)))

#define addPrim(ot, p) (((OT_TAG *)(p))->next = ((OT_TAG *)(ot))->next, ((OT_TAG *)(ot))->next = (OT_TAG *)(p))
#define termPrim(p) (((OT_TAG *)(p))->next = NULL)

int ResetGraph(int mode);
int SetGraphDebug(int level);
int GetGraphType(void);
void SetDispMask(int mask);
int DrawSync(int mode);

/* Reverse OT: ot[i] links to ot[i-1], ot[0] ends the list; draw from ot[n-1]. */
OT_TAG *ClearOTagR(OT_TAG *ot, int n);
void DrawOTag(OT_TAG *p);
void DrawPrim(void *p);

u_short GetTPage(int tp, int abr, int x, int y);
u_short GetClut(int x, int y);
void SetDrawMode(DR_MODE *p, int dfe, int dtd, int tpage, RECT *tw);
void SetDrawTPage(DR_TPAGE *p, int dfe, int dtd, int tpage);

int ClearImage(RECT *rect, u_char r, u_char g, u_char b);
int LoadImage(RECT *rect, u_long *p);
int StoreImage(RECT *rect, u_long *p);
int MoveImage(RECT *rect, int x, int y);

DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h);
DISPENV *PutDispEnv(DISPENV *env);
DISPENV *GetDispEnv(DISPENV *env);
DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h);
DRAWENV *PutDrawEnv(DRAWENV *env);
DRAWENV *GetDrawEnv(DRAWENV *env);

/* Port-only: VRAM access, showing the display area (called on every vblank) and saving it. */
u_short *gpu_vram(void);
void gpu_present(void);
int gpu_save_display(const char *path);
int gpu_save_vram(const char *path);

#endif
