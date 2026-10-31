#ifndef GRAPHICS_FONT_H
#define GRAPHICS_FONT_H

#include "psx/libgpu.h"

#include <stdint.h>

#define FONT_GLYPHS 0x60 /* characters 0x20..0x7F */
#define MAX_FONTS 10

/* Where a font's glyph sheet is: its texture page, CLUT and top-left texel. */
typedef struct {
    uint8_t u, v;
    uint16_t tpage;
    uint16_t clut;
    int16_t spacing; /* extra pixels between characters (plus one) */
} Font;

/* One character of a font (FONT.BNK): its texels relative to the font's u, v. */
typedef struct {
    uint8_t u, v;
    uint8_t width, height;
} Glyph;

/* PrintBank flags. The low ones are requests set by Print's % codes; the per-frame functions
 * turn them into the running states. */
#define PRINT_COLOUR      0x001 /* %C: ink changed, restart the fade */
#define PRINT_TRANS       0x002 /* %T: transparency changed, restart the fade */
#define PRINT_FADE_TIME   0x004 /* %SS: fade length changed */
#define PRINT_FADING      0x008
#define PRINT_POS         0x010 /* %P */
#define PRINT_DEST        0x020 /* %D */
#define PRINT_MOVE_TIME   0x040 /* %SP: move length changed */
#define PRINT_MOVING      0x080
#define PRINT_MOVED       0x100 /* the move just ended: copy the positions to the other buffer */
#define PRINT_GTE         0x200 /* %R: project through the current GTE matrix */

/* A run of text with one font, colour and position; Print starts a new bank at every % code. */
typedef struct {
    uint32_t flags;          /* +0x00 PRINT_* */
    int32_t first_char;      /* +0x04, index into print_chars and print_prims */
    int32_t pos[3];          /* +0x08 x, y, z: z is the scale, 256 for 1:1 */
    int32_t dest[3];         /* +0x14 where %SP moves the text to */
    int32_t fade_step[3];    /* +0x20, 20.12 per frame */
    int32_t fade_rgb[3];     /* +0x2C, 20.12 */
    int32_t move_step[3];    /* +0x38, 24.8 per frame */
    int32_t move_pos[3];     /* +0x44, 24.8 */
    int8_t font;             /* +0x50 */
    int8_t trans;            /* +0x51: 0 opaque, 1 add (fades in), -1 subtract (fades out), 2 average */
    int16_t length;          /* +0x52 */
    int16_t fade_frames;     /* +0x54 */
    int16_t move_frames;     /* +0x56 */
    int16_t justify;         /* +0x58: 0 left, 1 right, 2 centre */
    int16_t ot_slot;         /* +0x5A */
    uint8_t ink[3];          /* +0x5C */
    uint8_t pad5f;
} PrintBank;

_Static_assert(sizeof(PrintBank) == 0x60, "PrintBank keeps the original offsets");

extern Glyph *font_glyphs[MAX_FONTS];      /* 0x80088D28, FONT_GLYPHS each */
extern Font *fonts;                        /* 0x80079B58 */
extern char *print_chars;                  /* 0x800799C0 */
extern PrintBank *print_banks;             /* 0x800799C4 */
extern int32_t print_bank_count;           /* 0x800799C8 */
extern int32_t print_char_count;           /* 0x800799CC */
extern int32_t font_count;                 /* 0x800799D0 */
extern POLY_FT4 *print_prims[2];           /* 0x800799B8, one glyph quad per character */
extern int32_t print_values[256];          /* 0x8007FB70, printed by %H and %V; never set */

void Reset_Print_Banks(void);                                  /* 0x8004D53C */
void Allocate_Font_Buffers(int banks, int chars, int count);   /* 0x8004C1D0 */
void Setup_Font(const char *sprite, int font, int spacing);    /* 0x8004C2B0 */
void Duplicate_Font(int font, int copy, const char *clut_sprite); /* 0x8004C340 */
void Print_Locate(int32_t x, int32_t y, int32_t z);            /* 0x8004C464 */
void Print_Font(int font);                                     /* 0x8004C4D8 */
void Print_Ink(const uint8_t *rgb);                            /* 0x8004C520 */
void Print_InkRGB(int r, int g, int b);                        /* 0x8004C5A8 */
uint32_t Print(int ot_slot, const char *text);                 /* 0x8004C648 */
void Print_Draw(void);                                         /* 0x8004D42C */
void Print_Close_Bank(void);                                   /* 0x8004D760 */
void Print_Bank_Fade(int bank, int count, int first);          /* 0x8004D8A8 */
void Print_Bank_Perspective(int bank, int count, int first);   /* 0x8004DB94 */
void Print_Bank_Layout_Gte(int bank, int count, int first);    /* 0x8004E128 */

#endif
