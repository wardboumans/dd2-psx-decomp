#include "game/scene.h"

#include "game/anim.h"
#include "game/main_init.h"
#include "game/car_draw.h"
#include "game/other_objects.h"
#include "game/particles.h"
#include "game/play_game.h"
#include "game/track.h"
#include "graphics/draw_face.h"
#include "graphics/sprite.h"
#include "psx/libgpu.h"
#include "system/mpe_heap.h"

#include <string.h>

#define SKY_OBJECTS 8
#define SKY_FIRST_SECTION 7       /* level sections 7..14 are the sky meshes */
#define WARM_UP_STRIPS 0xc0       /* Init_Scene_Objects replays this many strips up to the camera */
#define DECRUNCH_CHUNK 0x400      /* bytes of a streamed block unpacked per frame */
#define FREE_DELAY 2              /* frames an out-of-view object keeps its packets */

/* Where a placed object is: a coarse position (32 K grid, centred) with the GTE translation, and the
 * remainder as a short vector from it. */
#define COARSE_MASK 0xffff8000u
#define COARSE_CENTRE 0x4000
#define FINE_MASK 0x7fff
#define FINE_BIAS 0xc000
#define MESH_EXACT 0x80

uint8_t *scene_block_data[SCENE_BLOCKS];
int32_t scene_block_id[SCENE_BLOCKS];
int32_t scene_object_count[SCENE_BLOCKS];
Object scene_objects[SCENE_BLOCKS][BLOCK_OBJECTS];
static int32_t scene_object_pos[SCENE_BLOCKS][BLOCK_OBJECTS][4];  /* 0x800A722C */
static SVECTOR scene_object_offset[SCENE_BLOCKS][BLOCK_OBJECTS]; /* 0x800A5910 */

static DecompressState block_decrunch; /* 0x800942B8 */
static int32_t decrunch_active;        /* 0x80079B80 */
static int32_t decrunch_slot;          /* 0x80079AD0 */
static int32_t decrunch_block;         /* 0x80079C28 */

static Object sky_objects[SKY_OBJECTS]; /* 0x8007FE9C */

/* Per level: ambient colour, depth-cue colour, the light direction and the light colour. */
static const uint8_t level_ambient[12][4] = {
    {0x00, 0x00, 0x00, 0}, {0x60, 0x60, 0x60, 0}, {0x60, 0x60, 0x60, 0}, {0x40, 0x40, 0x40, 0},
    {0x40, 0x40, 0x40, 0}, {0x60, 0x60, 0x60, 0}, {0x60, 0x60, 0x60, 0}, {0x60, 0x60, 0x60, 0},
    {0x60, 0x60, 0x60, 0}, {0x60, 0x60, 0x60, 0}, {0x60, 0x60, 0x60, 0}, {0x60, 0x60, 0x60, 0},
};
static const uint8_t level_fog[12][4] = {
    {0x60, 0x60, 0x60, 0}, {0x80, 0x80, 0x80, 0}, {0x80, 0x80, 0x80, 0}, {0x50, 0x50, 0x50, 0},
    {0x20, 0x20, 0x20, 0}, {0x80, 0x80, 0x80, 0}, {0x00, 0x00, 0x00, 0}, {0x80, 0x80, 0x80, 0},
    {0x80, 0x80, 0x80, 0}, {0x80, 0x80, 0x80, 0}, {0x80, 0x80, 0x80, 0}, {0x80, 0x80, 0x80, 0},
};
const int16_t light_directions[12][4] = { /* by level - 1 */
    {2096, 2096, 0, 0}, {2096, 2096, 0, 0}, {2096, 2096, 0, 0}, {2096, 2096, 0, 0}, {2096, 2096, 0, 0},
    {2096, 2096, 0, 0}, {2096, 2096, 0, 0}, {2096, 2096, 0, 0}, {2096, 2096, 0, 0}, {2096, 2096, 0, 0},
    {2096, 2096, 0, 0}, {4096, 4096, 4096, 0},
};
const int16_t light_colours[12][4] = { /* by level - 1 */
    {4096, 4096, 4096, 0}, {4096, 4096, 4096, 0}, {4096, 4096, 4096, 0}, {4096, 4096, 4096, 0},
    {4096, 4096, 4096, 0}, {0, 0, 0, 0}, {4096, 4096, 4096, 0}, {4096, 4096, 4096, 0},
    {4096, 4096, 4096, 0}, {4096, 4096, 4096, 0}, {4096, 4096, 4096, 0}, {2, 0, 1, 0},
};
#define FOG_NEAR 0x1900
#define FOG_FAR 32000

MATRIX scene_colour_matrix = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {0, 0, 0}}; /* 0x8006F7A4 */
MATRIX scene_light_matrix = {{{0x830, 0x830, 0}, {0, 0, 0}, {0, 0, 0}}, {0, 0, 0}};     /* 0x8006F7C4 */
static const MATRIX sky_matrix = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {0, 0, 0}};      /* 0x80010618 */

/* Block `id` of the level's packed scenery (the table at the start of section 0). */
static const uint8_t *packed_block(int id)
{
    const uint8_t *table = level_data_section(0);
    int32_t offset;
    memcpy(&offset, table + 4 * id, sizeof(offset));
    return table + offset;
}

void Init_Scene(void)
{
    for (int i = 0; i < SCENE_BLOCKS; i++)
        scene_block_data[i] = MPE_malloc(SCENE_BLOCK_SIZE);
    for (int i = SCENE_BLOCKS - 1; i >= 0; i--)
        scene_block_id[i] = BLOCK_FREE;
    switch (current_level) {
    case 1:
        Init_Wild_Bill();
        Init_Texture_Animation();
        break;
    case 4:
        Init_Rollercoaster();
        break;
    case 5:
    case 11:
        Init_Texture_Animation();
        break;
    case 6:
        Init_CLUT_Animation();
        Init_Texture_Animation();
        break;
    default:
        break;
    }
    Set_Ambient_Light(level_ambient[current_level]);
    Set_Depth_Cue(FOG_NEAR, FOG_FAR, level_fog[current_level]);
    const int16_t *colour = light_colours[current_level - 1];
    const int16_t *light = light_directions[current_level - 1];
    memset(&scene_colour_matrix.m, 0, sizeof(scene_colour_matrix.m));
    scene_colour_matrix.m[0][0] = colour[0];
    scene_colour_matrix.m[1][0] = colour[1];
    scene_colour_matrix.m[2][0] = colour[2];
    memset(&scene_light_matrix.m, 0, sizeof(scene_light_matrix.m));
    scene_light_matrix.m[0][0] = light[0];
    scene_light_matrix.m[0][1] = light[1];
    scene_light_matrix.m[0][2] = light[2];
    decrunch_active = 0;
}

/* Is the object (at the current object view) inside the view frustum? */
static int in_view(int32_t r, int32_t x, int32_t y, int32_t z)
{
    return zclip_near <= r + z && z - r <= zclip_far &&
           (view_norm_x[0] * x + view_norm_x[2] * z) >> 12 <= r &&
           (-x * view_norm_x[0] + view_norm_x[2] * z) >> 12 <= r &&
           (view_norm_y[1] * y + view_norm_y[2] * z) >> 12 <= r &&
           (-y * view_norm_y[1] + view_norm_y[2] * z) >> 12 <= r;
}

/* Draws the object at level 1 if its bounding sphere is in view; the edges are exclusive here. */
int VVDraw_Object(Object *obj)
{
    int32_t r = obj->mesh->radius;
    int32_t x = object_matrix.t[0], y = object_matrix.t[1], z = object_matrix.t[2];
    if (zclip_near < r + z && z - r < zclip_far &&
        (view_norm_x[0] * x + view_norm_x[2] * z) >> 12 < r &&
        (-x * view_norm_x[0] + view_norm_x[2] * z) >> 12 < r &&
        (view_norm_y[1] * y + view_norm_y[2] * z) >> 12 < r &&
        (-y * view_norm_y[1] + view_norm_y[2] * z) >> 12 < r) {
        Draw_Object_Level(obj, 1);
        return 1;
    }
    return 0;
}

/* A scenery object: packets are built when it comes into view and freed a couple of frames after it
 * leaves; close up, objects that allow it switch to a subdivided mesh. */
void Draw_Scene_Object(Object *obj, const SVECTOR *offset)
{
    VECTOR c;
    gte_ldv0(offset);
    gte_rt();
    gte_stlvnl(&c);
    const Mesh *mesh = obj->mesh;
    int32_t r = mesh->radius;
    if (in_view(r, c.vx, c.vy, c.vz)) {
        int level = 1;
        obj->level1_timer = 0;
        int subdivide = mesh->subdivide;
        int32_t near_z = mesh->subdivide_z;
        switch (obj->level) {
        case 0: {
            int32_t prim = Build_Object_Packets(obj);
            if (prim == -1)
                goto timers;
            obj->level = 1;
            obj->prim_offset = prim;
            if (subdivide != 0 && c.vz < near_z) {
                obj->level2_timer = 0;
                if (Subdivide_Object(obj) != -1)
                    level = 2;
            }
            break;
        }
        case 1:
            if (subdivide != 0 && c.vz < near_z) {
                obj->level2_timer = 0;
                if (Subdivide_Object(obj) != -1)
                    level = 2;
            }
            break;
        case 2:
            if (c.vz < near_z) {
                obj->level2_timer = 0;
                if (subdivide == 2 && c.vz < r) {
                    Draw_Subdiv_Object(obj);
                    goto timers;
                }
                level = 2;
            } else if (obj->level2_timer == 0) {
                obj->level2_timer = FREE_DELAY;
            }
            break;
        default:
            goto timers;
        }
        Draw_Object_Level(obj, level);
    } else if (obj->level != 0 && obj->level1_timer == 0) {
        obj->level1_timer = FREE_DELAY;
        obj->level2_timer = FREE_DELAY;
    }
timers:
    if (obj->level2_timer != 0 && --obj->level2_timer == 0) {
        Remove_Object(obj, 2);
        obj->level = 1;
    }
    if (obj->level1_timer != 0 && --obj->level1_timer == 0) {
        Remove_Object(obj, 1);
        obj->level = 0;
    }
}

void Draw_Scene_Blocks(void)
{
    SetLightMatrix(&scene_light_matrix);
    SetColorMatrix(&scene_colour_matrix);
    for (int b = 0; b < SCENE_BLOCKS; b++) {
        if (scene_block_id[b] < 0)
            continue;
        for (int i = 0; i < scene_object_count[b]; i++) {
            Set_Object_View(NULL, scene_object_pos[b][i]);
            Draw_Scene_Object(&scene_objects[b][i], &scene_object_offset[b][i]);
        }
    }
    Draw_Other_Objects();
}

/* An unpacked block: the object count, then per object its mesh (offset in the block) and position. */
void Setup_Object_Block(const uint8_t *block, int slot)
{
    int32_t count;
    memcpy(&count, block, sizeof(count));
    scene_object_count[slot] = count;
    const uint8_t *entry = block + 4;
    for (int i = 0; i < count; i++, entry += 16) {
        int32_t rec[4];
        memcpy(rec, entry, sizeof(rec));
        Mesh *mesh = (Mesh *)(block + rec[0]);
        Set_Object(&scene_objects[slot][i], mesh);
        int32_t *pos = scene_object_pos[slot][i];
        SVECTOR *off = &scene_object_offset[slot][i];
        if (mesh->flags & MESH_EXACT) {
            pos[0] = rec[1];
            pos[1] = rec[2];
            pos[2] = rec[3];
            off->vx = off->vy = off->vz = 0;
        } else {
            pos[0] = (int32_t)(((uint32_t)rec[1] & COARSE_MASK) + COARSE_CENTRE);
            pos[1] = (int32_t)(((uint32_t)rec[2] & COARSE_MASK) + COARSE_CENTRE);
            pos[2] = (int32_t)(((uint32_t)rec[3] & COARSE_MASK) + COARSE_CENTRE);
            off->vx = (int16_t)((rec[1] & FINE_MASK) + FINE_BIAS);
            off->vy = (int16_t)((rec[2] & FINE_MASK) + FINE_BIAS);
            off->vz = (int16_t)((rec[3] & FINE_MASK) + FINE_BIAS);
        }
    }
}

static void free_block_objects(int slot)
{
    DrawSync(0);
    for (int i = 0; i < scene_object_count[slot]; i++) {
        Remove_Object(&scene_objects[slot][i], 1);
        Remove_Object(&scene_objects[slot][i], 2);
    }
    scene_block_id[slot] = BLOCK_FREE;
}

static int find_block(const int32_t *ids, int id)
{
    int i;
    for (i = 0; i < SCENE_BLOCKS; i++)
        if (ids[i] == id)
            break;
    return i;
}

/* Unpacks the rest of the block being streamed in, and makes its objects. */
static void finish_decrunch(void)
{
    block_decrunch.chunk = SCENE_BLOCK_SIZE;
    Decompress(&block_decrunch);
    Setup_Object_Block(scene_block_data[decrunch_slot], decrunch_slot);
    scene_block_id[decrunch_slot] = decrunch_block;
}

static void start_decrunch(int slot, int id, uint16_t chunk)
{
    if (decrunch_active)
        finish_decrunch();
    decrunch_active = 1;
    block_decrunch.src = packed_block(id);
    block_decrunch.dst = scene_block_data[slot];
    block_decrunch.chunk = chunk;
    block_decrunch.state = 0;
    decrunch_slot = slot;
    decrunch_block = id;
    scene_block_id[slot] = BLOCK_LOADING;
}

/* Streams the scenery: when the camera's strip record moved onto another strip, the block it left
 * behind goes and the one ahead starts unpacking, a piece each frame. */
void Finish_Object_Block_Decrunch(void)
{
    const TrackFd *fd = camera_fd_pt;
    if (current_level >= FIRST_BOWL_LEVEL)
        return;
    for (int b = 0; b < SCENE_BLOCKS; b++)
        if (scene_block_id[b] == BLOCK_REMOVE)
            free_block_objects(b);
    const Strip *now = strip_at(fd->strip);
    const Strip *was = strip_at(fd->prev_strip);
    if (fd->step != 0) {
        int id = fd->step == 1 ? was->block_behind : was->block_ahead;
        if (id >= 0) {
            int slot = find_block(scene_block_id, id);
            if (slot != SCENE_BLOCKS)
                scene_block_id[slot] = BLOCK_REMOVE;
        }
        id = fd->step == 1 ? now->block_ahead : now->block_behind;
        if (id >= 0 && find_block(scene_block_id, id) == SCENE_BLOCKS) {
            int slot = find_block(scene_block_id, BLOCK_FREE);
            if (slot != SCENE_BLOCKS)
                start_decrunch(slot, id, DECRUNCH_CHUNK);
        }
    }
    if (decrunch_active) {
        Decompress(&block_decrunch);
        if (block_decrunch.state == -1) {
            finish_decrunch();
            decrunch_active = 0;
        }
    }
}

/* Which blocks are loaded when a race starts: replays the load and unload events of the strips
 * leading up to the camera, then unpacks the resulting blocks at once. */
static void Object_Decompression(void)
{
    static int32_t ring[WARM_UP_STRIPS]; /* 0x80080154 */
    int32_t wanted[SCENE_BLOCKS];
    for (int i = SCENE_BLOCKS - 1; i >= 0; i--)
        scene_block_id[i] = BLOCK_FREE;
    for (int i = SCENE_BLOCKS - 1; i >= 0; i--)
        wanted[i] = BLOCK_FREE;
    int32_t offset = camera_fd_pt->strip;
    for (int i = 0; i < WARM_UP_STRIPS; i++) {
        ring[i] = offset;
        offset = strip_at(offset)->prev;
    }
    const Strip *s = strip_at(offset);
    for (int i = 0; i < WARM_UP_STRIPS; i++) {
        int id = s->block_behind;
        if (id >= 0) {
            int slot = find_block(wanted, id);
            if (slot != SCENE_BLOCKS)
                wanted[slot] = BLOCK_FREE;
        }
        s = strip_at(ring[WARM_UP_STRIPS - 1 - i]);
        id = s->block_ahead;
        if (id >= 0 && find_block(wanted, id) == SCENE_BLOCKS) {
            int slot = find_block(wanted, BLOCK_FREE);
            if (slot != SCENE_BLOCKS)
                wanted[slot] = id;
        }
    }
    for (int i = 0; i < SCENE_BLOCKS; i++) {
        if (wanted[i] == BLOCK_FREE)
            continue;
        start_decrunch(i, wanted[i], SCENE_BLOCK_SIZE);
        finish_decrunch();
        decrunch_active = 0;
    }
}

/* Bowls have all their scenery resident: the blocks follow one another unpacked in section 0, and a
 * full block (32 objects) means another one follows. */
void Init_Scene_Objects(void)
{
    if (current_level < FIRST_BOWL_LEVEL) {
        Object_Decompression();
        return;
    }
    for (int i = SCENE_BLOCKS - 1; i >= 0; i--)
        scene_block_id[i] = BLOCK_FREE;
    int32_t count;
    int i = 0;
    do {
        const uint8_t *block = packed_block(i);
        Setup_Object_Block(block, i);
        scene_block_id[i] = i;
        memcpy(&count, block, sizeof(count));
        i++;
    } while (count == BLOCK_OBJECTS);
}

void Remove_Scene_Objects(void)
{
    for (int b = 0; b < SCENE_BLOCKS; b++)
        if (scene_block_id[b] != BLOCK_FREE)
            free_block_objects(b);
    Swap_Draw_Buffers();
    Swap_Draw_Buffers();
}

void Init_Sky(void)
{
    for (int i = 0; i < SKY_OBJECTS; i++)
        Create_Object(&sky_objects[i], (Mesh *)level_data_section(SKY_FIRST_SECTION + i));
}

/* One eighth of the sky dome, centred on the camera: its quads go to the back of the OT when the
 * eighth's bounding sphere at (x, y, z) is in view. */
static void Draw_Sky_Object(const Object *obj, int16_t x, int16_t y, int16_t z, int32_t radius)
{
    const Mesh *mesh = obj->mesh;
    SVECTOR centre = {x, y, z, 0};
    VECTOR c;
    gte_ldv0(&centre);
    gte_rt();
    gte_stlvnl(&c);
    if ((view_norm_x[0] * c.vx + view_norm_x[2] * c.vz) >> 12 >= radius ||
        (-c.vx * view_norm_x[0] + view_norm_x[2] * c.vz) >> 12 >= radius ||
        (view_norm_y[1] * c.vy + view_norm_y[2] * c.vz) >> 12 >= radius ||
        (-c.vy * view_norm_y[1] + view_norm_y[2] * c.vz) >> 12 >= radius)
        return;
    const SVECTOR *v = (const SVECTOR *)mesh_section(mesh, mesh->vertices);
    const FaceRun *run = (const FaceRun *)mesh_section(mesh, mesh->faces);
    const FaceText *face = (const FaceText *)(run + 1);
    POLY_FT4 *p = (POLY_FT4 *)(prim_buffer[buffer_num] + obj->prim_offset);
    OT_TAG *ot = &cdb->ot[mesh->ot_bias + otsize - 1];
    for (int i = 0; i < run->count; i++, face++, p++) {
        gte_ldv3(&v[face->v[0]], &v[face->v[1]], &v[face->v[2]]);
        gte_rtpt();
        int32_t flag;
        gte_stflg(&flag);
        if ((flag & GTE_CLIP_FLAGS) != 0)
            continue;
        gte_stsxy3_xy(p);
        gte_ldv0(&v[face->v[3]]);
        gte_rtps();
        p->x3 = gte.sx[2];
        p->y3 = gte.sy[2];
        addPrim(ot, p);
    }
}

void Draw_Sky(void)
{
    Set_Object_View(&sky_matrix, world_matrix.t);
    Draw_Sky_Object(&sky_objects[0], 1250, -478, -1250, 1832);
    Draw_Sky_Object(&sky_objects[1], -1250, -478, 1250, 1832);
    Draw_Sky_Object(&sky_objects[2], 1250, -478, 1250, 1832);
    Draw_Sky_Object(&sky_objects[3], -1250, -478, -1250, 1832);
    Draw_Sky_Object(&sky_objects[4], 1250, 1250, -1250, 2166);
    Draw_Sky_Object(&sky_objects[5], -1250, 1250, 1250, 2166);
    Draw_Sky_Object(&sky_objects[6], 1250, 1250, 1250, 2166);
    Draw_Sky_Object(&sky_objects[7], -1250, 1250, -1250, 2166);
}
