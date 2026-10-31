#include "graphics/object.h"

#include "graphics/draw_face.h"
#include "psx/libetc.h"
#include "psx/libgte.h"
#include "system/maths.h"
#include "system/mpe_heap.h"

#include <stdlib.h>
#include <string.h>

/* Port packets start with a 16-byte native link instead of the 4-byte tag, so they are bigger
 * than the sizes the game asks for (taken from PSX data). Doubling covers the worst case,
 * POLY_F3: 20 bytes on the PSX, 32 here. */

/* Free-list node at the start of each block of buffer 0, in 8-byte block units. */
typedef struct {
    uint32_t next;
    uint32_t size;
} PrimBlock;

uint8_t *prim_buffer[2];
int32_t prim_free_blocks;
static uint32_t prim_rover;      /* 0x80079AB4, free-list position */
static int32_t prim_buffer_size; /* 0x80079954 */

int32_t screen_centre_x, screen_centre_y;
int32_t projection_distance;
int32_t zclip_near, zclip_far;
const void *gtexture_def;
int16_t view_norm_x[3], view_norm_y[3];

const MATRIX *light_matrix;
MATRIX world_matrix = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {0, 0, 0}};
MATRIX object_matrix;

static void set_identity(MATRIX *m)
{
    static const int16_t identity[3][3] = {{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}};
    memcpy(m->m, identity, sizeof(identity));
}

void Set_World_Position(const int32_t pos[3])
{
    memcpy(world_matrix.t, pos, sizeof(world_matrix.t));
}

void Set_World_Matrix(const int16_t m[3][3])
{
    memcpy(world_matrix.m, m, sizeof(world_matrix.m));
}

/* The camera from (pitch, yaw, roll); also the rotations the sprite kinds draw with. */
void Set_World_View(const int16_t angles[3])
{
    set_identity(&world_matrix);
    RotMatrixY(angles[1], &world_matrix);
    RotMatrixX(angles[0], &world_matrix);
    RotMatrixZ(angles[2], &world_matrix);
    set_identity(&tilt_matrix);
    RotMatrixX(angles[0], &tilt_matrix);
    RotMatrixZ(angles[2], &tilt_matrix);
    set_identity(&sprite_matrix);
    RotMatrixZ(angles[2], &sprite_matrix);
}

/* Puts the GTE in the object's view space: its position relative to the camera, rotated into
 * the camera's frame, and (with a rotation) world_matrix times that rotation. */
void Set_Object_View(const MATRIX *rotation, const int32_t pos[3])
{
    VECTOR rel = {pos[0] - world_matrix.t[0], pos[1] - world_matrix.t[1], pos[2] - world_matrix.t[2], 0};
    VECTOR t;
    ApplyMatrixLV(&world_matrix, &rel, &t);
    object_matrix.t[0] = t.vx;
    object_matrix.t[1] = t.vy;
    object_matrix.t[2] = t.vz;
    if (rotation) {
        MulMatrix0(&world_matrix, rotation, &object_matrix);
        gte_SetRotMatrix(&object_matrix);
    }
    gte_SetTransMatrix(&object_matrix);
}

void Calc_Object_MatrixYXZ(MATRIX *m, const int16_t angles[3])
{
    set_identity(m);
    RotMatrixZ(angles[2], m);
    RotMatrixX(angles[0], m);
    RotMatrixY(angles[1], m);
}

/* Pitch, yaw and roll of a rotation matrix (the inverse of Calc_Object_MatrixYXZ). */
void Calc_Object_Angles(const MATRIX *m, int16_t angles[3])
{
    int32_t h = SquareRoot0(m->m[1][0] * m->m[1][0] + m->m[1][1] * m->m[1][1]);
    angles[0] = (int16_t)Calc_Angle(-m->m[1][2], h);
    if ((angles[0] & (HALF_TURN - 1)) == QUARTER_TURN) {
        angles[1] = (int16_t)Calc_Angle(-m->m[2][0], m->m[0][0]);
        angles[2] = 0;
    } else {
        angles[1] = (int16_t)Calc_Angle(m->m[0][2], m->m[2][2]);
        angles[2] = (int16_t)Calc_Angle(m->m[1][0], m->m[1][1]);
    }
}

/* Turns the camera to look at `target` from its position, rolled by `roll`; also the sprite and tilt
 * matrices. The offset is halved until it fits the 16-bit range the squares are taken in. */
void Point_Camera(const int32_t target[3], int roll)
{
    int c = rcos(roll);
    int s = rsin(roll);
    int32_t dx = target[0] - world_matrix.t[0];
    int32_t dy = target[1] - world_matrix.t[1];
    int32_t dz = target[2] - world_matrix.t[2];
    while (abs(dx) > 0x4000 || abs(dy) > 0x4000 || abs(dz) > 0x4000) {
        dx /= 2;
        dy /= 2;
        dz /= 2;
    }
    int16_t (*w)[3] = world_matrix.m;
    int32_t len = SquareRoot0(dx * dx + dy * dy + dz * dz);
    if (len < 1) {
        set_identity(&world_matrix);
    } else {
        int32_t nx = (dx << 12) / len;
        int32_t ny = (dy << 12) / len;
        int32_t nz = (dz << 12) / len;
        int32_t h = SquareRoot0(nx * nx + nz * nz);
        if (h < 1) {
            /* straight up or down */
            w[0][0] = ONE;
            w[0][2] = 0;
            w[1][0] = 0;
            w[1][1] = 0;
            w[1][2] = -ONE;
            w[2][0] = 0;
            w[2][1] = ONE;
            w[2][2] = 0;
        } else {
            w[1][1] = (int16_t)h;
            w[2][0] = (int16_t)nx;
            w[2][1] = (int16_t)ny;
            w[2][2] = (int16_t)nz;
            set_identity(&tilt_matrix);
            tilt_matrix.m[1][1] = (int16_t)h;
            tilt_matrix.m[1][2] = (int16_t)-ny;
            tilt_matrix.m[2][1] = (int16_t)ny;
            tilt_matrix.m[2][2] = (int16_t)h;
            w[0][0] = (int16_t)((nz << 12) / h);
            w[0][2] = (int16_t)((nx * -ONE) / h);
            w[1][0] = (int16_t)(-(nx * ny) / h);
            w[1][2] = (int16_t)(-(ny * nz) / h);
        }
    }
    w[0][1] = 0;
    MATRIX r = {{{(int16_t)c, (int16_t)-s, 0}, {(int16_t)s, (int16_t)c, 0}, {0, 0, ONE}}, {0, 0, 0}};
    MulMatrix2(&r, &world_matrix);
    MulMatrix2(&r, &tilt_matrix);
    sprite_matrix = r;
}

/* A rotation looking along `dir`, rolled by `roll`. Built as the original does: a frame `a`
 * from the direction, the roll `b`, and `t` (a's transpose, except that t[2][0] has the
 * opposite sign of a[0][2]); m = (a * b) * t. Products are 32-bit as on the PSX. */
void Calc_Direction_Matrix(MATRIX *m, const int32_t dir[3], int roll)
{
    int32_t c = rcos(roll), s = rsin(roll);
    int32_t x = dir[0], y = dir[1], z = dir[2];
    int32_t r_yz = SquareRoot0((int32_t)((int64_t)y * y + (int64_t)z * z));
    int32_t r = SquareRoot0((int32_t)((int64_t)x * x + (int64_t)y * y + (int64_t)z * z));
    int32_t d = (int32_t)((int64_t)r_yz * r) / 4096;
    MATRIX a, b, t;
    a.m[0][0] = (int16_t)((r_yz << 12) / r);
    a.m[0][1] = 0;
    a.m[0][2] = (int16_t)(x * -ONE / r);
    a.m[1][0] = (int16_t)((int32_t)(-(int64_t)x * y) / d);
    a.m[1][1] = (int16_t)((z << 12) / r_yz);
    a.m[1][2] = (int16_t)((y << 12) / r);
    a.m[2][0] = (int16_t)((int32_t)(-(int64_t)x * z) / d);
    a.m[2][1] = (int16_t)(y * -ONE / r_yz);
    a.m[2][2] = (int16_t)((z << 12) / r);
    b.m[0][0] = (int16_t)c;
    b.m[0][1] = (int16_t)s;
    b.m[0][2] = 0;
    b.m[1][0] = (int16_t)-s;
    b.m[1][1] = (int16_t)c;
    b.m[1][2] = 0;
    b.m[2][0] = 0;
    b.m[2][1] = 0;
    b.m[2][2] = ONE;
    t.m[0][0] = a.m[0][0];
    t.m[0][1] = a.m[1][0];
    t.m[0][2] = a.m[2][0];
    t.m[1][0] = 0;
    t.m[1][1] = a.m[1][1];
    t.m[1][2] = a.m[2][1];
    t.m[2][0] = (int16_t)((x << 12) / r);
    t.m[2][1] = a.m[1][2];
    t.m[2][2] = a.m[2][2];
    MulMatrix2(&a, &b);
    MulMatrix0(&b, &t, m);
}

void Calc_Object_MatrixYZX(MATRIX *m, const int16_t angles[3])
{
    set_identity(m);
    RotMatrixX(angles[0], m);
    RotMatrixZ(angles[2], m);
    RotMatrixY(angles[1], m);
}

static PrimBlock *prim_block(uint32_t index)
{
    return (PrimBlock *)prim_buffer[0] + index;
}

/* Hands every run of the mesh's face stream to its handler; leaves gpoly at the end marker. */
static void run_faces(const FaceHandler *table, const Mesh *mesh)
{
    const FaceRun *run = (const FaceRun *)mesh_section(mesh, mesh->faces);
    while (run->record_size) {
        gpoly = (const uint8_t *)(run + 1);
        table[run->kind](run->count);
        run = (const FaceRun *)gpoly;
    }
    gpoly = (const uint8_t *)run;
}

/* The original turns the section offsets into pointers here; see mesh_section(). */
static void relocate_mesh(Mesh *mesh)
{
    if (mesh->relocated == 0)
        mesh->relocated = 1;
}

int32_t Build_Object_Packets(Object *obj)
{
    int32_t offset = Alloc_Prim_Buffer(obj->mesh->prim_size);
    if (offset != -1) {
        gtexture = obj->textures;
        gprim_other = prim_buffer[1] + offset;
        gprim = prim_buffer[0] + offset;
        run_faces(face_table_build, obj->mesh);
    }
    return offset;
}

void Create_Object(Object *obj, Mesh *mesh)
{
    obj->mesh = mesh;
    obj->f1b = 1;
    obj->textures = gtexture_def;
    relocate_mesh(mesh);
    int32_t offset = Alloc_Prim_Buffer(mesh->prim_size);
    if (offset != -1) {
        gtexture = obj->textures;
        gprim_other = prim_buffer[1] + offset;
        gprim = prim_buffer[0] + offset;
        run_faces(face_table_build, mesh);
        obj->prim_offset = offset;
        obj->level = 1;
    }
}

void Set_Object(Object *obj, Mesh *mesh)
{
    obj->mesh = mesh;
    obj->level = 0;
    obj->level1_timer = 0;
    obj->level2_timer = 0;
    obj->f1b = 1;
    obj->textures = gtexture_def;
    relocate_mesh(mesh);
}

/* Level 1 frees the packets; level 2 the subdivided mesh's packets and the mesh itself. */
void Remove_Object(Object *obj, int level)
{
    if (level == 1) {
        if (obj->level != 0)
            Free_Prim_Buffer(obj->prim_offset);
    } else if (level == 2 && obj->level > 1) {
        Free_Prim_Buffer(obj->prim_offset2);
        MPE_free(obj->mesh2);
    }
}

static void draw_mesh(const Object *obj, const Mesh *mesh, int32_t prim_offset, const FaceHandler *table)
{
    gtexture = obj->textures;
    got = &cdb->ot[obj->mesh->ot_bias];
    gvertices = (const SVECTOR *)mesh_section(mesh, mesh->vertices);
    gnormals = (const SVECTOR *)mesh_section(mesh, mesh->normals);
    gprim = prim_buffer[buffer_num] + prim_offset;
    run_faces(table, mesh);
}

void Draw_Subdiv_Object(Object *obj)
{
    draw_mesh(obj, obj->mesh2, obj->prim_offset2, face_table_subdiv);
}

void Draw_Object_Level(Object *obj, int level)
{
    if (level == 1)
        draw_mesh(obj, obj->mesh, obj->prim_offset, face_table_object);
    else
        draw_mesh(obj, obj->mesh2, obj->prim_offset2, face_table_object);
}

/* Draws the object at its current level, with the light matrix when the mesh is lit. */
void Update_Object(Object *obj)
{
    if (obj->mesh->flags & 1)
        memcpy(gte.llm, light_matrix->m, sizeof(gte.llm));
    Draw_Object_Level(obj, obj->level);
}

/* The two side planes of the view frustum, from the screen size and projection distance. */
void Setup_View_Norm(const void *textures)
{
    gtexture_def = textures;
    int32_t d = SquareRoot0(screen_width * screen_width / 4 + projection_distance * projection_distance);
    view_norm_x[1] = 0;
    view_norm_x[0] = (int16_t)((projection_distance << 12) / d);
    view_norm_x[2] = (int16_t)((screen_width / 2) * -ONE / d);
    d = SquareRoot0(screen_height * screen_height / 4 + projection_distance * projection_distance);
    view_norm_y[0] = 0;
    view_norm_y[1] = (int16_t)((projection_distance << 12) / d);
    view_norm_y[2] = (int16_t)((screen_height / 2) * -ONE / d);
}

void set_screen_centre(int32_t x, int32_t y)
{
    SetGeomOffset(x, y);
    screen_centre_x = x;
    screen_centre_y = y;
}

void set_projection_distance(int32_t d)
{
    SetGeomScreen(d);
    projection_distance = d;
}

void Set_Zclip(int32_t near_z, int32_t far_z)
{
    zclip_near = near_z;
    zclip_far = far_z;
}

void Set_Ambient_Light(const uint8_t *rgb)
{
    SetBackColor(rgb[0], rgb[1], rgb[2]);
}

void Set_Depth_Cue(int32_t near_z, int32_t far_z, const uint8_t *rgb)
{
    SetFarColor(rgb[0], rgb[1], rgb[2]);
    SetFogNearFar(near_z, far_z, projection_distance);
}

void Init_Primitive_Buffer(int32_t size)
{
    size *= PRIM_SIZE_SCALE;
    prim_buffer[0] = MPE_malloc((size_t)size);
    prim_buffer[1] = MPE_malloc((size_t)size);
    prim_block(0)->next = 0;
    prim_free_blocks = size >> 3;
    prim_rover = 0;
    prim_buffer_size = size;
    prim_block(0)->size = (uint32_t)prim_free_blocks;
}

/* First fit on a circular free list (K&R malloc); returns a byte offset or -1. */
int32_t Alloc_Prim_Buffer(int32_t size)
{
    uint32_t need = ((uint32_t)(size * PRIM_SIZE_SCALE) + 7) / 8 + 1;
    uint32_t prev = prim_rover;
    uint32_t p = prim_block(prev)->next;
    for (;;) {
        PrimBlock *b = prim_block(p);
        if (need <= b->size) {
            if (b->size == need) {
                prim_block(prev)->next = b->next;
            } else {
                b->size -= need;
                p += b->size;
                prim_block(p)->size = need;
            }
            prim_free_blocks -= (int32_t)need;
            prim_rover = prev;
            return (int32_t)(p + 1) * 8;
        }
        if (p == prim_rover)
            return -1;
        prev = p;
        p = b->next;
    }
}

/* Puts the block back in the address-ordered free list, merging it with free neighbours. */
void Free_Prim_Buffer(int32_t offset)
{
    uint32_t p = (uint32_t)offset / 8 - 1;
    prim_free_blocks += (int32_t)prim_block(p)->size;
    uint32_t q;
    for (;;) {
        q = prim_rover;
        uint32_t next = prim_block(q)->next;
        if (q < p && p < next)
            break;
        prim_rover = next;
        if (!(q < next || (p <= q && next <= p)))
            break;
    }
    PrimBlock *pb = prim_block(p), *qb = prim_block(q);
    if (p + pb->size == qb->next) {
        pb->size += prim_block(qb->next)->size;
        pb->next = prim_block(qb->next)->next;
    } else {
        pb->next = qb->next;
    }
    if (q + qb->size == p) {
        qb->size += pb->size;
        qb->next = pb->next;
    } else {
        qb->next = p;
    }
    prim_rover = q;
}

/* 0x800717A0: PAL then NTSC. Set_Draw_Mode indexes PAL at [mode] and NTSC at [mode + 1],
 * so only mode 0 is meaningful. Centre_Screen_Page changes the screen positions. */
VideoMode video_modes[2] = {
    {0, 0, 0, 256, 320, 256, 3, 21},
    {0, 8, 0, 264, 320, 240, 0, 5},
};

DrawBuffer db[2];
DrawBuffer *cdb;
int32_t buffer_num;
int32_t otsize;
int32_t pal_mode;
int32_t frame_count;
int32_t screen_width = 320;
int32_t screen_height = 256;
int32_t frames_per_sec = 50;
int32_t draw_height = 0x138;
int32_t last_vsync;

/* A glyph quad's packet fields: 4-bit texels from the font's page, the transparency mode of
 * Print's %T codes (an unknown mode keeps the old page), modulated by `rgb`. */
void Set_Glyph_Quad_Attrs(POLY_FT4 *quad, int tpage, int clut, const uint8_t *rgb, int trans)
{
    setPolyFT4(quad);
    quad->clut = (u_short)clut;
    int x = (tpage & 0xf) << 6, y = (tpage & 0x10) << 4;
    int abr = -1;
    switch (trans) {
    case 0:
        quad->tpage = GetTPage(0, 1, x, y);
        setSemiTrans(quad, 0);
        break;
    case -1: abr = 2; break;
    case 1: abr = 1; break;
    case 2: abr = 0; break;
    default: break;
    }
    if (abr >= 0) {
        quad->tpage = GetTPage(0, abr, x, y);
        setSemiTrans(quad, 1);
    }
    setShadeTex(quad, 0);
    setRGB0(quad, rgb[0], rgb[1], rgb[2]);
}

void Draw_Font_Poly(POLY_FT4 *quad, int ot_slot)
{
    addPrim(&cdb->ot[ot_slot], quad);
}

/* Flag bytes, low bit first: 1 copies a byte, 0 copies 3..18 bytes from up to 4 KB back
 * (12-bit distance, counted from 4 KB before the output). A flag byte's eight items are
 * always done, so a call can write a little past its chunk. */
void Decompress(DecompressState *d)
{
    const uint8_t *src = d->src;
    uint8_t *dst = d->dst;
    if (d->state == 0) {
        memcpy(&d->remaining, src, sizeof(d->remaining));
        src += 4;
        d->state = 1;
    }
    uint32_t done = 0;
    uint32_t limit = (uint32_t)d->remaining;
    if ((int32_t)d->chunk < d->remaining)
        limit = d->chunk;
    while (done < limit) {
        unsigned flags = *src++ | 0xff00u;
        do {
            if (flags & 1) {
                *dst++ = *src++;
                done++;
            } else {
                unsigned lo = src[0], hi = src[1];
                src += 2;
                const uint8_t *from = dst + (int)((lo | (hi & 0xf0) << 4)) - 0x1000;
                int n = (int)(hi & 0xf) + 3;
                for (int i = 0; i < n; i++)
                    *dst++ = from[i];
                done += (uint32_t)n;
            }
            flags >>= 1;
        } while (flags & 0x100);
    }
    d->src = src;
    d->dst = dst;
    d->remaining -= (int32_t)done;
    if (d->remaining == 0)
        d->state = -1;
}

void Allocate_OT(int slots)
{
    DrawSync(0);
    otsize = slots;
    db[0].ot = MPE_malloc((size_t)slots * sizeof(OT_TAG));
    db[1].ot = MPE_malloc((size_t)slots * sizeof(OT_TAG));
}

void Set_Draw_Mode(int mode)
{
    const VideoMode *vm;
    if (pal_mode == 1) {
        vm = &video_modes[mode];
        frames_per_sec = 50;
        draw_height = 0x12e;
        SetVideoMode(MODE_PAL);
    } else {
        vm = &video_modes[mode + 1];
        frames_per_sec = 60;
        draw_height = 0x106;
    }
    SetDefDrawEnv(&db[0].draw, vm->draw_x, vm->draw_y, vm->w, vm->h);
    SetDefDrawEnv(&db[1].draw, vm->disp_x, vm->disp_y, vm->w, vm->h);
    SetDefDispEnv(&db[0].disp, vm->disp_x, vm->disp_y, vm->w, vm->h);
    SetDefDispEnv(&db[1].disp, vm->draw_x, vm->draw_y, vm->w, vm->h);
    screen_width = vm->w;
    screen_height = vm->h;
    for (int i = 0; i < 2; i++) {
        db[i].disp.screen.x = vm->screen_x;
        db[i].disp.screen.y = vm->screen_y;
        db[i].disp.screen.h = vm->h;
    }
}

void Swap_Draw_Buffers(void)
{
    buffer_num ^= 1;
    cdb = &db[buffer_num];
    ClearOTagR(cdb->ot, otsize);
}

/* Wait until `vsyncs` vblanks have passed since the previous frame: this sets the frame rate. */
static void wait_frame(int vsyncs)
{
    frame_count++;
    DrawSync(0);
    if (vsyncs < 2) {
        if (vsyncs == 1)
            VSync(0);
    } else {
        while (VSync(-1) - last_vsync < vsyncs)
            ;
        last_vsync = VSync(-1);
    }
}

void Swap_Buffers(int vsyncs)
{
    wait_frame(vsyncs);
    PutDrawEnv(&cdb->draw);
    PutDispEnv(&cdb->disp);
}

void Draw_All(int vsyncs)
{
    wait_frame(vsyncs);
    PutDrawEnv(&cdb->draw);
    PutDispEnv(&cdb->disp);
    DrawOTag(&cdb->ot[otsize - 1]);
}

void Draw_Tile(TILE *tile, int ot_slot, int x, int y, int w, int h, const uint8_t *rgb)
{
    setTile(tile);
    setXY0(tile, (short)x, (short)y);
    setWH(tile, (short)w, (short)h);
    setRGB0(tile, rgb[0], rgb[1], rgb[2]);
    addPrim(&cdb->ot[ot_slot], tile);
}
