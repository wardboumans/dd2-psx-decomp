#include "graphics/sprite.h"

#include "graphics/draw_face.h"
#include "graphics/object.h"
#include "psx/libgpu.h"
#include "system/mpe_heap.h"

#include <stdio.h>
#include <string.h>

const SpriteInfo *sprite_table;
int32_t sprite_count;

/* LEVEL.SPR: a count, then the SpriteInfo records sorted by name. */
void Load_Sprite_Info(const uint8_t *blob)
{
    sprite_table = (const SpriteInfo *)(blob + 4);
    memcpy(&sprite_count, blob, sizeof(sprite_count));
}

/* The original's binary search, step for step: when the name is greater the window halves
 * towards `lo` from the current probe rather than from `hi`. */
const SpriteInfo *Search_For_Sprite(const char *name)
{
    int lo = 0, hi = sprite_count - 1;
    int mid = hi;
    for (;;) {
        mid >>= 1;
        int c;
        do {
            const SpriteInfo *s = &sprite_table[mid];
            c = strcmp(s->name, name);
            if (c == 0)
                return s;
            if (hi <= lo)
                return NULL;
            if (c > 0) {
                hi = mid - 1;
                mid = (mid + lo) / 2;
            }
        } while (c >= 0);
        lo = mid + 1;
        mid = hi + lo + 1;
    }
}

void Setup_Sprite_Quad(POLY_FT4 *quad, const char *name, Sprite *sprite)
{
    const SpriteInfo *info = Search_For_Sprite(name);
    if (quad)
        setPolyFT4(quad);
    if (!info) {
        printf("Error: texture \"%s\" does not exist\n", name);
        return;
    }
    int depth = info->depth;
    uint16_t tpage;
    if (GetGraphType() == 1 || GetGraphType() == 2) {
        tpage = (uint16_t)(((depth / 4 - 1) & 3) << 9 | (sprite->blend & 3) << 7 | (info->vram_y & 0x100) >> 3 |
                           (info->vram_x & 0x3c0) >> 6);
    } else {
        tpage = (uint16_t)(((depth / 4 - 1) & 3) << 7 | (sprite->blend & 3) << 5 | (info->vram_y & 0x100) >> 4 |
                           (info->vram_x & 0x3c0) >> 6);
    }
    uint16_t clut = (uint16_t)(info->clut_y << 6 | ((info->clut_x >> 4) & 0x3f));
    uint8_t u = (uint8_t)(((info->vram_x << 4) / depth) & (0x400 / depth - 1));
    uint8_t v = (uint8_t)info->vram_y;
    if (quad) {
        uint8_t u_end = (uint8_t)(u + info->width - 1), v_end = (uint8_t)(v + info->height - 1);
        setUV4(quad, u, v, u_end, v, u, v_end, u_end, v_end);
        int16_t x_end = (int16_t)(sprite->x - 1 + info->width), y_end = (int16_t)(sprite->y - 1 + info->height);
        setXY4(quad, sprite->x, sprite->y, x_end, sprite->y, sprite->x, y_end, x_end, y_end);
        quad->tpage = tpage;
        quad->clut = clut;
        setSemiTrans(quad, sprite->semi_trans);
        setShadeTex(quad, sprite->raw);
        setRGB0(quad, sprite->r, sprite->g, sprite->b);
    }
    sprite->depth = (uint8_t)depth;
    sprite->tpage = tpage;
    sprite->clut = clut;
    sprite->u = u;
    sprite->v = v;
    sprite->w = (uint8_t)info->width;
    sprite->h = (uint8_t)info->height;
    sprite->clut_x = info->clut_x;
    sprite->clut_y = info->clut_y;
}

/* Shows the w x h part at (du, dv) of the sprite's texture, sized 1:1 from the quad's corner 0. */
void Modify_Sprite(POLY_FT4 *quad, const Sprite *sprite, int du, int dv, int w, int h)
{
    quad->u0 = (u_char)(sprite->u + du);
    quad->v0 = (u_char)(sprite->v + dv);
    quad->u1 = (u_char)(sprite->u + du + w);
    quad->v1 = (u_char)(sprite->v + dv);
    quad->u2 = (u_char)(sprite->u + du);
    quad->v2 = (u_char)(sprite->v + dv + h);
    quad->u3 = (u_char)(sprite->u + du + w);
    quad->v3 = (u_char)(sprite->v + dv + h);
    quad->x1 = quad->x3 = (short)(quad->x0 + w);
    quad->y2 = quad->y3 = (short)(quad->y0 + h);
}

/* The subdivided mesh's new vertices go after the old ones: a triangle adds its three edge midpoints,
 * a quad its four edge midpoints and its centre. */
static void midpoint(SVECTOR *out, const SVECTOR *a, const SVECTOR *b)
{
    out->vx = (int16_t)((a->vx + b->vx) / 2);
    out->vy = (int16_t)((a->vy + b->vy) / 2);
    out->vz = (int16_t)((a->vz + b->vz) / 2);
}

static void centre(SVECTOR *out, const SVECTOR *a, const SVECTOR *b, const SVECTOR *c, const SVECTOR *d)
{
    out->vx = (int16_t)((a->vx + b->vx + c->vx + d->vx) / 4);
    out->vy = (int16_t)((a->vy + b->vy + c->vy + d->vy) / 4);
    out->vz = (int16_t)((a->vz + b->vz + c->vz + d->vz) / 4);
}

/* Fills the three-point face's vertices: (v0, m01, m02), (m01, v1, m12), (m12, m02, m01), (m02, m12, v2). */
static void split_tri(int16_t *v[4], const int16_t old[3], int16_t m)
{
    int16_t idx[4][3] = {
        {old[0], m, (int16_t)(m + 1)},
        {m, old[1], (int16_t)(m + 2)},
        {(int16_t)(m + 2), (int16_t)(m + 1), m},
        {(int16_t)(m + 1), (int16_t)(m + 2), old[2]},
    };
    for (int k = 0; k < 4; k++)
        memcpy(v[k], idx[k], sizeof(idx[k]));
}

/* (v0, m01, m02, c), (m01, v1, c, m13), (m02, c, v2, m23), (c, m13, m23, v3) */
static void split_quad(int16_t *v[4], const int16_t old[4], int16_t m)
{
    int16_t idx[4][4] = {
        {old[0], m, (int16_t)(m + 1), (int16_t)(m + 2)},
        {m, old[1], (int16_t)(m + 2), (int16_t)(m + 3)},
        {(int16_t)(m + 1), (int16_t)(m + 2), old[2], (int16_t)(m + 4)},
        {(int16_t)(m + 2), (int16_t)(m + 3), (int16_t)(m + 4), old[3]},
    };
    for (int k = 0; k < 4; k++)
        memcpy(v[k], idx[k], sizeof(idx[k]));
}

#define COPY_RGBC(p, o) ((p)->r0 = (o)->r0, (p)->g0 = (o)->g0, (p)->b0 = (o)->b0, (p)->code = (o)->code)
#define MID(a, b) (uint8_t)(((unsigned)(a) + (unsigned)(b)) >> 1)

/* Splits every face of the object's current mesh into four, as a new mesh at the next level with its
 * packets built (in both buffers). Only flat and textured faces (kinds 0..15) are handled. Returns -1
 * when there is no memory for it. */
int32_t Subdivide_Object(Object *obj)
{
    int level = obj->level;
    Mesh *src = level == 1 ? obj->mesh : obj->mesh2;
    int32_t src_prim = level == 1 ? obj->prim_offset : obj->prim_offset2;
    int nv = src->vertex_count;
    int new_vertices = nv + src->tri_count * 3 + src->quad_count * 5;
    int new_normals = src->normal_count + src->tri_count * 3 + src->quad_count * 5;
    int face_bytes = (src->face_words - src->run_count * 3) * 4;
    Mesh *mesh = MPE_malloc((size_t)(face_bytes + new_vertices * 8 + (int)sizeof(Mesh) + new_normals * 8));
    if (mesh == NULL)
        return -1;
    int32_t prim = Alloc_Prim_Buffer(src->prim_size * 4);
    if (prim == -1) {
        MPE_free(mesh);
        return -1;
    }
    uint8_t *packet = prim_buffer[0] + prim;
    uint8_t *other = prim_buffer[1] + prim;
    const uint8_t *old_packet = prim_buffer[0] + src_prim;
    const FaceRun *run = (const FaceRun *)mesh_section(src, src->faces);
    const SVECTOR *ov = (const SVECTOR *)mesh_section(src, src->vertices);
    obj->mesh2 = mesh;
    obj->prim_offset2 = prim;

    memset(mesh, 0, sizeof(Mesh));
    uint32_t vertices = (uint32_t)(sizeof(Mesh) + face_bytes);
    SVECTOR *verts = (SVECTOR *)((uint8_t *)mesh + vertices);
    memcpy(verts, ov, (size_t)nv * 8);
    memcpy((uint8_t *)verts + new_vertices * 8, mesh_section(src, src->normals), (size_t)src->normal_count * 8);
    mesh->vertex_count = (uint16_t)new_vertices;
    mesh->normal_count = (uint16_t)new_normals;
    mesh->tri_count = (uint16_t)(src->tri_count * 4);
    mesh->tri_count_copy = mesh->tri_count;
    mesh->quad_count = (uint16_t)(src->quad_count * 4);
    mesh->quad_count_copy = mesh->quad_count;
    mesh->prim_size = (uint16_t)(src->prim_size * 4);
    mesh->face_words = (uint16_t)face_bytes;
    mesh->run_count = src->run_count;
    mesh->vertices = vertices;
    mesh->normals = vertices + (uint32_t)new_vertices * 8;
    mesh->faces = sizeof(Mesh);
    mesh->face_count = (uint16_t)(src->face_count << 2);
    obj->level = (uint8_t)(level + 1);

    FaceRun *out = (FaceRun *)((uint8_t *)mesh + sizeof(Mesh));
    while (run->record_size) {
        int count = run->count;
        out->count = (int16_t)(count * 4);
        out->record_size = run->record_size;
        out->kind = run->kind;
        const uint8_t *face = (const uint8_t *)(run + 1);
        uint8_t *rec = (uint8_t *)(out + 1);
        switch (run->kind & 0x1f) {
        case 0:
        case 1:
        case 2:
        case 3:
            for (int i = 0; i < count; i++, face += sizeof(FaceFlat), rec += 4 * sizeof(FaceFlat), nv += 3) {
                const FaceFlat *f = (const FaceFlat *)face;
                FaceFlat *r = (FaceFlat *)rec;
                const SVECTOR *a = &ov[f->v[0]], *b = &ov[f->v[1]], *c = &ov[f->v[2]];
                midpoint(&verts[nv], a, b);
                midpoint(&verts[nv + 1], a, c);
                midpoint(&verts[nv + 2], b, c);
                int16_t *v[4];
                for (int k = 0; k < 4; k++) {
                    r[k] = *f;
                    v[k] = r[k].v;
                }
                split_tri(v, f->v, (int16_t)nv);
                const POLY_F3 *o = (const POLY_F3 *)old_packet;
                POLY_F3 *p = (POLY_F3 *)packet;
                for (int k = 0; k < 4; k++) {
                    setPolyF3(&p[k]);
                    COPY_RGBC(&p[k], o);
                    ((POLY_F3 *)other)[k] = p[k];
                }
                old_packet += sizeof(POLY_F3);
                packet += 4 * sizeof(POLY_F3);
                other += 4 * sizeof(POLY_F3);
            }
            break;
        case 4:
        case 5:
        case 6:
        case 7:
            for (int i = 0; i < count; i++, face += sizeof(FaceFlat), rec += 4 * sizeof(FaceFlat), nv += 5) {
                const FaceFlat *f = (const FaceFlat *)face;
                FaceFlat *r = (FaceFlat *)rec;
                const SVECTOR *a = &ov[f->v[0]], *b = &ov[f->v[1]], *c = &ov[f->v[2]], *d = &ov[f->v[3]];
                midpoint(&verts[nv], a, b);
                midpoint(&verts[nv + 1], a, c);
                centre(&verts[nv + 2], a, b, c, d);
                midpoint(&verts[nv + 3], b, d);
                midpoint(&verts[nv + 4], c, d);
                int16_t *v[4];
                for (int k = 0; k < 4; k++) {
                    r[k] = *f;
                    v[k] = r[k].v;
                }
                split_quad(v, f->v, (int16_t)nv);
                const POLY_F4 *o = (const POLY_F4 *)old_packet;
                POLY_F4 *p = (POLY_F4 *)packet;
                for (int k = 0; k < 4; k++) {
                    setPolyF4(&p[k]);
                    COPY_RGBC(&p[k], o);
                    ((POLY_F4 *)other)[k] = p[k];
                }
                old_packet += sizeof(POLY_F4);
                packet += 4 * sizeof(POLY_F4);
                other += 4 * sizeof(POLY_F4);
            }
            break;
        case 8:
        case 9:
        case 10:
        case 11:
            for (int i = 0; i < count; i++, face += sizeof(FaceText), rec += 4 * sizeof(FaceText), nv += 3) {
                const FaceText *f = (const FaceText *)face;
                FaceText *r = (FaceText *)rec;
                const SVECTOR *a = &ov[f->v[0]], *b = &ov[f->v[1]], *c = &ov[f->v[2]];
                midpoint(&verts[nv], a, b);
                midpoint(&verts[nv + 1], a, c);
                midpoint(&verts[nv + 2], b, c);
                int16_t *v[4];
                for (int k = 0; k < 4; k++) {
                    /* the original copies only the first two words and the vertices; the texture
                     * fields are not used once the packets exist */
                    r[k] = *f;
                    v[k] = r[k].v;
                }
                split_tri(v, f->v, (int16_t)nv);
                const POLY_FT3 *o = (const POLY_FT3 *)old_packet;
                POLY_FT3 *p = (POLY_FT3 *)packet;
                uint8_t m01u = MID(o->u0, o->u1), m01v = MID(o->v0, o->v1);
                uint8_t m02u = MID(o->u0, o->u2), m02v = MID(o->v0, o->v2);
                uint8_t m12u = MID(o->u1, o->u2), m12v = MID(o->v1, o->v2);
                /* the original gives the second triangle's middle corner v2 rather than v1 */
                setUV3(&p[0], o->u0, o->v0, m01u, m01v, m02u, m02v);
                setUV3(&p[1], m01u, m01v, o->u1, o->v2, m12u, m12v);
                setUV3(&p[2], m12u, m12v, m02u, m02v, m01u, m01v);
                setUV3(&p[3], m02u, m02v, m12u, m12v, o->u2, o->v2);
                for (int k = 0; k < 4; k++) {
                    setPolyFT3(&p[k]);
                    p[k].tpage = o->tpage;
                    p[k].clut = o->clut;
                    COPY_RGBC(&p[k], o);
                    ((POLY_FT3 *)other)[k] = p[k];
                }
                old_packet += sizeof(POLY_FT3);
                packet += 4 * sizeof(POLY_FT3);
                other += 4 * sizeof(POLY_FT3);
            }
            break;
        case 12:
        case 13:
        case 14:
        case 15:
            for (int i = 0; i < count; i++, face += sizeof(FaceText), rec += 4 * sizeof(FaceText), nv += 5) {
                const FaceText *f = (const FaceText *)face;
                FaceText *r = (FaceText *)rec;
                const SVECTOR *a = &ov[f->v[0]], *b = &ov[f->v[1]], *c = &ov[f->v[2]], *d = &ov[f->v[3]];
                midpoint(&verts[nv], a, b);
                midpoint(&verts[nv + 1], a, c);
                centre(&verts[nv + 2], a, b, c, d);
                midpoint(&verts[nv + 3], b, d);
                midpoint(&verts[nv + 4], c, d);
                int16_t *v[4];
                for (int k = 0; k < 4; k++) {
                    r[k] = *f;
                    v[k] = r[k].v;
                }
                split_quad(v, f->v, (int16_t)nv);
                const POLY_FT4 *o = (const POLY_FT4 *)old_packet;
                POLY_FT4 *p = (POLY_FT4 *)packet;
                uint8_t m01u = MID(o->u0, o->u1), m01v = MID(o->v0, o->v1);
                uint8_t m02u = MID(o->u0, o->u2), m02v = MID(o->v0, o->v2);
                uint8_t m13u = MID(o->u1, o->u3), m13v = MID(o->v1, o->v3);
                uint8_t m23u = MID(o->u2, o->u3), m23v = MID(o->v2, o->v3);
                uint8_t cu = (uint8_t)(((unsigned)o->u0 + o->u1 + o->u2 + o->u3) >> 2);
                uint8_t cv = (uint8_t)(((unsigned)o->v0 + o->v1 + o->v2 + o->v3) >> 2);
                setUV4(&p[0], o->u0, o->v0, m01u, m01v, m02u, m02v, cu, cv);
                setUV4(&p[1], m01u, m01v, o->u1, o->v1, cu, cv, m13u, m13v);
                setUV4(&p[2], m02u, m02v, cu, cv, o->u2, o->v2, m23u, m23v);
                setUV4(&p[3], cu, cv, m13u, m13v, m23u, m23v, o->u3, o->v3);
                for (int k = 0; k < 4; k++) {
                    setPolyFT4(&p[k]);
                    p[k].tpage = o->tpage;
                    p[k].clut = o->clut;
                    COPY_RGBC(&p[k], o);
                    ((POLY_FT4 *)other)[k] = p[k];
                }
                old_packet += sizeof(POLY_FT4);
                packet += 4 * sizeof(POLY_FT4);
                other += 4 * sizeof(POLY_FT4);
            }
            break;
        default:
            /* the original has no case for the other kinds and carries on from the first record */
            break;
        }
        run = (const FaceRun *)face;
        out = (FaceRun *)rec;
    }
    out->count = 0;
    out->kind = 0;
    out->record_size = 0;
    return 0;
}
