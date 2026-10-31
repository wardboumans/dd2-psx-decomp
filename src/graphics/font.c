#include "graphics/font.h"

#include "graphics/object.h"
#include "graphics/sprite.h"
#include "psx/libgte.h"
#include "system/mpe_heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bytes of primitive buffer per character in the original: one POLY_FT4. */
#define GLYPH_PRIM_SIZE 0x28

Glyph *font_glyphs[MAX_FONTS];
Font *fonts;
char *print_chars;
PrintBank *print_banks;
int32_t print_bank_count;
int32_t print_char_count;
int32_t font_count;
POLY_FT4 *print_prims[2];
int32_t print_values[256];

static int32_t current_bank;  /* 0x800798AC */
static int32_t bank_length;   /* 0x800798B0 */
static int32_t chars_used;    /* 0x800798B4 */

void Reset_Print_Banks(void)
{
    for (int i = 0; i < print_bank_count; i++) {
        PrintBank *b = &print_banks[i];
        b->font = 0;
        b->flags = 0;
        b->first_char = 0;
        b->length = 0;
        b->trans = 0;
        b->move_frames = 0;
        b->justify = 0;
    }
    current_bank = 0;
    chars_used = 0;
    bank_length = 0;
}

void Allocate_Font_Buffers(int banks, int chars, int count)
{
    print_bank_count = banks;
    print_char_count = chars;
    font_count = count;
    for (int i = 0; i < count; i++)
        font_glyphs[i] = MPE_malloc(FONT_GLYPHS * sizeof(Glyph));
    fonts = MPE_malloc((size_t)count * sizeof(Font));
    print_chars = MPE_malloc((size_t)chars);
    print_banks = MPE_malloc((size_t)banks * sizeof(PrintBank));
    int32_t offset = Alloc_Prim_Buffer(chars * GLYPH_PRIM_SIZE);
    print_prims[0] = (POLY_FT4 *)(prim_buffer[0] + offset);
    print_prims[1] = (POLY_FT4 *)(prim_buffer[1] + offset);
}

void Setup_Font(const char *sprite, int font, int spacing)
{
    Sprite s;
    memset(&s, 0, sizeof(s));
    Setup_Sprite_Quad(NULL, sprite, &s);
    Font *f = &fonts[font];
    f->tpage = s.tpage;
    f->clut = s.clut;
    f->u = s.u;
    f->v = s.v;
    f->spacing = (int16_t)spacing;
}

/* Another colour of an existing font: same glyphs, the CLUT of `clut_sprite`. */
void Duplicate_Font(int font, int copy, const char *clut_sprite)
{
    Sprite s;
    memset(&s, 0, sizeof(s));
    Setup_Sprite_Quad(NULL, clut_sprite, &s);
    const Font *src = &fonts[font];
    Font *dst = &fonts[copy];
    dst->tpage = src->tpage;
    dst->clut = s.clut;
    dst->u = src->u;
    dst->v = src->v;
    dst->spacing = src->spacing;
    memcpy(font_glyphs[copy], font_glyphs[font], FONT_GLYPHS * sizeof(Glyph));
}

static const Glyph *glyph(int font, char c)
{
    return &font_glyphs[font][(uint8_t)(c - 0x20)];
}

/* x << 16 / z: a text coordinate scaled by its z (256 = 1:1) in 24.8 screen units. */
static int32_t scale_coord(int32_t v, int32_t z)
{
    return (int32_t)((uint32_t)v << 16) / z;
}

void Print_Locate(int32_t x, int32_t y, int32_t z)
{
    Print_Close_Bank();
    PrintBank *b = &print_banks[current_bank];
    b->pos[0] = b->dest[0] = x;
    b->pos[1] = b->dest[1] = y;
    b->pos[2] = b->dest[2] = z;
}

void Print_Font(int font)
{
    Print_Close_Bank();
    print_banks[current_bank].font = (int8_t)font;
}

void Print_Ink(const uint8_t *rgb)
{
    Print_Close_Bank();
    memcpy(print_banks[current_bank].ink, rgb, 3);
}

void Print_InkRGB(int r, int g, int b)
{
    Print_Close_Bank();
    PrintBank *bank = &print_banks[current_bank];
    bank->ink[0] = (uint8_t)r;
    bank->ink[1] = (uint8_t)g;
    bank->ink[2] = (uint8_t)b;
}

/* An optional '-' and up to ten characters of digits; the character that ends them is
 * consumed too, so it can be any separator. Returns the index after it. */
static int Print_Parse_Int(int32_t *value, const char *text, int i) /* 0x8004D6A0 */
{
    char digits[16];
    int n = 0;
    if (text[i] == '-') {
        digits[n++] = '-';
        i++;
    }
    while (n < 10) {
        char c = text[i++];
        if (c < '0' || c > '9')
            break;
        digits[n++] = c;
    }
    digits[n] = '\0';
    *value = atoi(digits);
    return i;
}

static int Print_Parse_Rgb(uint8_t rgb[3], const char *text, int i) /* 0x8004D5B8 */
{
    for (int k = 0; k < 3; k++) {
        int32_t v;
        i = Print_Parse_Int(&v, text, i);
        rgb[k] = (uint8_t)v;
    }
    return i;
}

static int Print_Parse_Int3(int32_t v[3], const char *text, int i) /* 0x8004D62C */
{
    for (int k = 0; k < 3; k++)
        i = Print_Parse_Int(&v[k], text, i);
    return i;
}

static void put_char(char c)
{
    print_chars[chars_used++] = c;
    bank_length++;
}

static void put_value(const char *format, int32_t value)
{
    char text[16];
    snprintf(text, sizeof(text), format, value);
    for (const char *p = text; *p; p++)
        put_char(*p);
}

/* Adds text to the print banks and builds its glyph quads in both buffers. Codes (each starts
 * a new bank where it changes the bank):
 *   %%           a '%'
 *   %Px,y,z      position        %Dx,y,z  destination      %Ax,y,z  both
 *   %Cr,g,b      ink             %Fn      font n
 *   %JL %JR %JC  justify left, right, centre
 *   %T+ %T- %T/ %T=  transparency add, subtract, average, none
 *   %SSn         fade over n frames     %SPn  move to the destination over n frames
 *   %ZS %ZP      cancel the fade / move (followed by one ignored character)
 *   %R           lay out through the GTE
 *   %Hc %Vc      print_values[c] in hex / decimal
 * Returns the first character index in the high half and the next free one in the low half. */
uint32_t Print(int ot_slot, const char *text)
{
    Print_Close_Bank();
    int first_bank = current_bank;
    print_banks[current_bank].ot_slot = (int16_t)ot_slot;
    int32_t start = chars_used;
    int32_t v[3];
    uint8_t rgb[3];
    PrintBank *b;

    for (int i = 0; text[i] != '\0';) {
        if (text[i] != '%') {
            put_char(text[i++]);
            continue;
        }
        char code = text[i + 1];
        i += 2;
        switch (code) {
        case '%':
            put_char('%');
            break;
        case 'A':
            Print_Close_Bank();
            i = Print_Parse_Int3(v, text, i);
            b = &print_banks[current_bank];
            b->flags |= PRINT_POS | PRINT_DEST;
            memcpy(b->pos, v, sizeof(v));
            memcpy(b->dest, v, sizeof(v));
            break;
        case 'C':
            Print_Close_Bank();
            i = Print_Parse_Rgb(rgb, text, i);
            b = &print_banks[current_bank];
            b->flags |= PRINT_COLOUR;
            memcpy(b->ink, rgb, 3);
            break;
        case 'D':
            Print_Close_Bank();
            i = Print_Parse_Int3(v, text, i);
            b = &print_banks[current_bank];
            b->flags |= PRINT_DEST;
            memcpy(b->dest, v, sizeof(v));
            break;
        case 'F':
            Print_Close_Bank();
            print_banks[current_bank].font = (int8_t)(text[i++] - '0');
            break;
        case 'H':
            put_value("%X", print_values[(uint8_t)text[i++]]);
            break;
        case 'V':
            put_value("%d", print_values[(uint8_t)text[i++]]);
            break;
        case 'J':
            Print_Close_Bank();
            switch (text[i++]) {
            case 'L': print_banks[current_bank].justify = 0; break;
            case 'R': print_banks[current_bank].justify = 1; break;
            case 'C': print_banks[current_bank].justify = 2; break;
            default: break;
            }
            break;
        case 'P':
            Print_Close_Bank();
            i = Print_Parse_Int3(v, text, i);
            b = &print_banks[current_bank];
            b->flags |= PRINT_POS;
            memcpy(b->pos, v, sizeof(v));
            break;
        case 'R':
            Print_Close_Bank();
            print_banks[current_bank].flags |= PRINT_GTE;
            break;
        case 'S': {
            Print_Close_Bank();
            char what = text[i++];
            int32_t frames;
            if (what == 'P') {
                i = Print_Parse_Int(&frames, text, i);
                b = &print_banks[current_bank];
                b->move_frames = (int16_t)frames;
                b->flags |= PRINT_MOVE_TIME;
            } else if (what == 'S') {
                i = Print_Parse_Int(&frames, text, i);
                b = &print_banks[current_bank];
                b->fade_frames = (int16_t)frames;
                b->flags |= PRINT_FADE_TIME;
            }
            break;
        }
        case 'T':
            Print_Close_Bank();
            b = &print_banks[current_bank];
            switch (text[i++]) {
            case '-': b->trans = -1; b->flags |= PRINT_TRANS; break;
            case '+': b->trans = 1; b->flags |= PRINT_TRANS; break;
            case '/': b->trans = 2; break;
            case '=': b->trans = 0; break;
            default: break;
            }
            break;
        case 'Z': {
            Print_Close_Bank();
            char what = text[i++];
            b = &print_banks[current_bank];
            if (what == 'P') {
                b->move_frames = 0;
                b->flags &= ~(uint32_t)PRINT_MOVE_TIME;
                i++;
            } else if (what == 'S') {
                b->fade_frames = 0;
                b->flags &= ~(uint32_t)PRINT_FADE_TIME;
                i++;
            }
            break;
        }
        default:
            break;
        }
    }
    print_banks[current_bank].length = (int16_t)bank_length;

    for (int n = first_bank; n <= current_bank; n++) {
        b = &print_banks[n];
        const Font *font = &fonts[b->font];
        int32_t width = 0;
        if (b->justify == 1 || b->justify == 2) {
            for (int c = b->first_char; c < b->first_char + b->length; c++)
                width += glyph(b->font, print_chars[c])->width + font->spacing - 1;
            if (b->justify == 2)
                width /= 2;
        }
        b->pos[0] -= width;
        b->dest[0] -= width;

        for (int buf = 0; buf < 2; buf++) {
            int32_t z = b->pos[2] != 0 ? b->pos[2] : 1;
            int32_t x = (screen_width / 2) * 0x100 + scale_coord(b->pos[0], z);
            int32_t y = (screen_height / 2) * 0x100 + scale_coord(b->pos[1], z);
            for (int c = b->first_char; c < b->first_char + b->length; c++) {
                const Glyph *g = glyph(b->font, print_chars[c]);
                POLY_FT4 *q = &print_prims[buf][c];
                u_char u0 = (u_char)(font->u + g->u), v0 = (u_char)(font->v + g->v);
                u_char u1 = (u_char)(u0 + g->width - 1), v2 = (u_char)(v0 + g->height);
                q->u0 = u0; q->v0 = v0;
                q->u1 = u1; q->v1 = v0;
                q->u2 = u0; q->v2 = v2;
                q->u3 = u1; q->v3 = v2;
                int32_t w = scale_coord(g->width - 1, z);
                int32_t y2 = y + scale_coord(g->height, z);
                q->x0 = q->x2 = (short)(x >> 8);
                q->x1 = q->x3 = (short)((x + w) >> 8);
                q->y0 = q->y1 = (short)(y >> 8);
                q->y2 = q->y3 = (short)(y2 >> 8);
                Set_Glyph_Quad_Attrs(q, font->tpage, font->clut, b->ink, b->trans);
                x += w + scale_coord(font->spacing, z);
            }
        }
    }
    return (uint32_t)start << 16 | (uint32_t)chars_used;
}

/* Fades, moves and lays out every bank's glyphs and adds them to the current ordering table. */
void Print_Draw(void)
{
    int first = 0;
    for (int n = 0; n < print_bank_count; n++) {
        int count = print_banks[n].length;
        int ot_slot = print_banks[n].ot_slot;
        Print_Bank_Fade(n, count, first);
        Print_Bank_Perspective(n, count, first);
        Print_Bank_Layout_Gte(n, count, first);
        for (int k = 0; k < count; k++, first++)
            Draw_Font_Poly(&print_prims[buffer_num][first], ot_slot);
    }
}

/* Ends the current bank if it holds characters: the next bank starts as a copy of it, placed
 * after its text. */
void Print_Close_Bank(void)
{
    if (bank_length == 0)
        return;
    PrintBank *b = &print_banks[current_bank];
    b[1] = b[0];
    b->length = (int16_t)bank_length;
    int32_t width = 0;
    for (int c = 0; c < bank_length; c++)
        width += glyph(b->font, print_chars[b->first_char + c])->width + fonts[b->font].spacing - 1;
    PrintBank *next = &b[1];
    next->first_char = chars_used;
    bank_length = 0;
    next->flags = 0;
    next->length = 0;
    current_bank++;
    next->pos[0] += width;
}

/* Transparent text fades: added text from black up to its ink, subtracted text from its ink
 * down to nothing, over fade_frames. */
void Print_Bank_Fade(int bank, int count, int first)
{
    if (count == 0)
        return;
    PrintBank *b = &print_banks[bank];
    if (b->flags & (PRINT_COLOUR | PRINT_TRANS | PRINT_FADE_TIME)) {
        b->flags &= ~(uint32_t)(PRINT_COLOUR | PRINT_TRANS | PRINT_FADE_TIME);
        if (b->trans == 0)
            return;
        /* The original leaves the targets in uninitialised registers for trans 2 (average);
         * the port does not fade then. */
        int32_t target[3] = {0, 0, 0};
        if (b->trans == -1) {
            for (int k = 0; k < 3; k++) {
                target[k] = -b->ink[k];
                b->fade_rgb[k] = b->ink[k] << 12;
            }
        } else if (b->trans == 1) {
            for (int k = 0; k < 3; k++) {
                target[k] = b->ink[k];
                b->fade_rgb[k] = 0;
            }
        }
        b->flags |= PRINT_FADING;
        if (b->fade_frames == 0) {
            b->flags &= ~(uint32_t)PRINT_FADING;
            return;
        }
        for (int k = 0; k < 3; k++)
            b->fade_step[k] = target[k] * 0x1000 / b->fade_frames;
    }
    if (b->flags & PRINT_FADING) {
        for (int k = 0; k < 3; k++)
            b->fade_rgb[k] += b->fade_step[k];
        for (int i = 0; i < count; i++)
            setRGB0(&print_prims[buffer_num][first + i], (u_char)(b->fade_rgb[0] >> 12),
                    (u_char)(b->fade_rgb[1] >> 12), (u_char)(b->fade_rgb[2] >> 12));
        if (--b->fade_frames == 0)
            b->flags &= ~(uint32_t)PRINT_FADING;
    }
}

/* Moves text from its position to its destination over move_frames, rebuilding the glyph
 * corners of the current buffer each frame; when it arrives, the other buffer is brought up to
 * date the next frame. */
void Print_Bank_Perspective(int bank, int count, int first)
{
    if (count == 0)
        return;
    PrintBank *b = &print_banks[bank];
    int16_t spacing = fonts[b->font].spacing;
    if (b->flags & (PRINT_POS | PRINT_DEST | PRINT_MOVE_TIME)) {
        b->flags = (b->flags & ~(uint32_t)(PRINT_POS | PRINT_DEST | PRINT_MOVE_TIME)) | PRINT_MOVING;
        for (int k = 0; k < 3; k++)
            b->move_pos[k] = b->pos[k] * 0x100;
        if (b->move_frames == 0) {
            b->flags &= ~(uint32_t)PRINT_MOVING;
            return;
        }
        for (int k = 0; k < 3; k++)
            b->move_step[k] = (b->dest[k] - b->pos[k]) * 0x100 / b->move_frames;
    }
    if (b->flags & PRINT_MOVED) {
        b->flags &= ~(uint32_t)PRINT_MOVED;
        for (int i = 0; i < count; i++) {
            POLY_FT4 *q = &print_prims[buffer_num][first + i];
            const POLY_FT4 *o = &print_prims[buffer_num ^ 1][first + i];
            q->x0 = o->x0; q->x1 = o->x1; q->x2 = o->x2; q->x3 = o->x3;
            q->y0 = o->y0; q->y1 = o->y1; q->y2 = o->y2; q->y3 = o->y3;
        }
    }
    if (b->flags & PRINT_MOVING) {
        for (int k = 0; k < 3; k++)
            b->move_pos[k] += b->move_step[k];
        int32_t z = b->move_pos[2] >> 8;
        if (z == 0)
            z = 1;
        int32_t x = (screen_width / 2) * 0x100 + (int32_t)((uint32_t)b->move_pos[0] << 8) / z;
        int32_t y = (screen_height / 2) * 0x100 + (int32_t)((uint32_t)b->move_pos[1] << 8) / z;
        for (int i = 0; i < count; i++) {
            const Glyph *g = glyph(b->font, print_chars[first + i]);
            POLY_FT4 *q = &print_prims[buffer_num][first + i];
            int32_t w = scale_coord(g->width - 1, z);
            int32_t y2 = y + scale_coord(g->height, z);
            q->x0 = q->x2 = (short)(x >> 8);
            q->x1 = q->x3 = (short)((x + w) >> 8);
            q->y0 = q->y1 = (short)(y >> 8);
            q->y2 = q->y3 = (short)(y2 >> 8);
            x += w + scale_coord(spacing, z);
        }
        if (--b->move_frames == 0)
            b->flags = (b->flags & ~(uint32_t)PRINT_MOVING) | PRINT_MOVED;
    }
}

/* %R text: each glyph is a quad in 3D at the bank's position, projected with the current GTE
 * rotation and translation. */
void Print_Bank_Layout_Gte(int bank, int count, int first)
{
    if (count == 0)
        return;
    const PrintBank *b = &print_banks[bank];
    if (!(b->flags & PRINT_GTE))
        return;
    int16_t spacing = fonts[b->font].spacing;
    int32_t x = b->pos[0], y = b->pos[1], z = b->pos[2];
    for (int i = 0; i < count; i++) {
        char c = print_chars[first + i];
        const Glyph *g = glyph(b->font, c);
        int16_t x1 = (int16_t)(x + g->width - 1), y1 = (int16_t)(y + g->height);
        SVECTOR v[4] = {
            {(int16_t)x, (int16_t)y, (int16_t)z, 0},
            {x1, (int16_t)y, (int16_t)z, 0},
            {(int16_t)x, y1, (int16_t)z, 0},
            {x1, y1, (int16_t)z, 0},
        };
        POLY_FT4 *q = &print_prims[buffer_num][first + i];
        gte_ldv3(&v[0], &v[1], &v[2]);
        gte_rtpt();
        gte_stsxy3_xy(q);
        gte_ldv0(&v[3]);
        gte_rtps();
        gte_stsxy_xy3(q);
        x += g->width - 1 + spacing;
        if (c == '.')
            x++;
    }
}
