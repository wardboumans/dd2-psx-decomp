#include "game/track.h"

#include "game/camera.h"
#include "game/car.h"
#include "game/frontend.h"
#include "game/main_init.h"
#include "game/play_game.h"
#include "game/sound_fx.h"
#include "graphics/object.h"
#include "psx/libgte.h"
#include "sound/sound.h"
#include "system/maths.h"

#include <stdlib.h>
#include <string.h>

#define BOWL_GRID 32         /* vertices per grid row */
#define BOWL_CELLS 31        /* quads per grid row */
#define BOWL_CELL 1000       /* grid spacing */
#define BOWL_ORIGIN 16000    /* the grid starts at -16000, -16000 */
#define LANE_START 0x80
#define QUAD_EDGES 0xf /* a bit per edge of a quad */

uint8_t *strip_data;
TrackVertex *strip_vertices;
int32_t num_strips;

/* Per strip type: where the two vertex rows start relative to the strip's first vertex (the second row
 * also skips the first row's width + 1 vertices). */
static const int32_t track_rows[12][2] = {
    {0, 0}, {0, 0}, {-1, -1}, {0, -1}, {-1, -2}, {0, -1}, {0, 0}, {0, -1}, {0, 0}, {0, 0}, {-1, -1}, {0, 0},
};

/* Per strip type: the quad edges that lead off the road, on the first and on the last quad of the strip
 * (bit 0 row-A edge, 1 forward edge, 2 row-B edge, 3 backward edge). */
static const uint32_t track_edges[12][2] = {
    {0x0, 0x0}, {0x7, 0xd}, {0x6, 0xd}, {0x7, 0xc}, {0x6, 0xc}, {0x3, 0xd},
    {0x7, 0x9}, {0x3, 0x9}, {0x7, 0xd}, {0x7, 0xd}, {0x6, 0x9}, {0x3, 0xc},
};

/* Per strip type: lane_base change before and after the strip. */
static const int32_t lane_shift_before[12] = {0, 0, -1, 0, -1, 0, 0, 0, 0, 0, -1, 0};
static const int32_t lane_shift_after[12] = {0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0};

static int32_t row_a(const Strip *s)
{
    return s->vertex + track_rows[s->type][0];
}

static int32_t row_b(const Strip *s)
{
    return s->vertex + track_rows[s->type][1] + s->width + 1;
}

static const TrackVertex *vtx(int32_t index)
{
    return &strip_vertices[index];
}

/* Which side of the edge a -> b the point is on, in the ground plane (> 0: right of it). The products
 * wrap in 32 bits as on the PSX. */
static int32_t side(int32_t px, int32_t pz, const TrackVertex *a, const TrackVertex *b)
{
    uint32_t l = (uint32_t)(px - a->x) * (uint32_t)(b->z - a->z);
    uint32_t r = (uint32_t)(pz - a->z) * (uint32_t)(b->x - a->x);
    return (int32_t)(l - r);
}

static int32_t fd_side(const TrackFd *fd, const TrackVertex *a, const TrackVertex *b)
{
    return side(fd->x, fd->z, a, b);
}

/* The edges of quad `index` of the strip that lead off the road. */
static uint32_t quad_edges(const Strip *s, int index)
{
    uint32_t edges = QUAD_EDGES;
    if (index == 0)
        edges = track_edges[s->type][0] & QUAD_EDGES;
    if (index == s->width - 1)
        edges &= track_edges[s->type][1];
    return edges;
}

static void set_normal(int16_t dst[3], const VECTOR *n)
{
    dst[0] = (int16_t)n->vx;
    dst[1] = (int16_t)n->vy;
    dst[2] = (int16_t)n->vz;
}

/* Unit normal of the triangle (p, q, r), from (r - q) x (p - q); straight up when it comes out flat. */
static void triangle_normal(const TrackVertex *p, const TrackVertex *q, const TrackVertex *r, int16_t out[3])
{
    VECTOR u = {p->x - q->x, p->y - q->y, p->z - q->z, 0};
    VECTOR v = {r->x - q->x, r->y - q->y, r->z - q->z, 0};
    VECTOR n;
    OuterProduct12(&v, &u, &n);
    VectorNormal(&n, &n);
    if (n.vy == 0) {
        n.vx = 0;
        n.vy = ONE;
        n.vz = 0;
    }
    set_normal(out, &n);
}

/* The two triangle planes of every quad. The end quads of some strip types are triangles in disguise
 * (a row has a repeated vertex there), so they take both planes from the one real triangle. */
void Generate_Surface_Normals(Strip *s)
{
    int32_t a = s->vertex + track_rows[s->type][0];
    int32_t b = row_b(s);
    for (int i = 0; i < s->width; i++, a++, b++) {
        for (int k = 0; k < 2; k++) {
            /* corners A0 A1 on row A, B0 B1 on row B */
            int32_t p = a + 1, q = a, r = b;   /* first triangle: A1, A0, B0 */
            if (k == 1) {
                p = b;                         /* second: B0, B1, A1 */
                q = b + 1;
                r = a + 1;
            }
            if (i == 0) {
                switch (s->type) {
                case 2:
                case 4:
                    p = b;
                    q = b + 1;
                    r = a + 1;
                    break;
                case 5:
                case 7:
                    p = a;
                    q = b + 1;
                    r = a + 1;
                    break;
                default:
                    break;
                }
            } else if (i == s->width - 1) {
                switch (s->type) {
                case 3:
                case 4:
                    p = b;
                    q = b + 1;
                    r = a;
                    break;
                case 6:
                case 7:
                    p = a;
                    q = b;
                    r = a + 1;
                    break;
                default:
                    break;
                }
            }
            triangle_normal(vtx(p), vtx(q), vtx(r), strip_quad(s, i)->normal[k]);
        }
    }
}

/* Numbers the strip and sets its lane base, then its normals. */
static void init_strip(Strip *s, int32_t *lane, int number)
{
    s->lane_base = (uint8_t)(*lane + lane_shift_before[s->type]);
    s->number = (uint16_t)((s->number & 0x3f) | number << 6);
    *lane += lane_shift_before[s->type] + lane_shift_after[s->type];
    Generate_Surface_Normals(s);
}

static void init_bowl_grid(void)
{
    StripQuad *grid = (StripQuad *)strip_data;
    for (int col = 0; col < BOWL_CELLS; col++) {
        for (int row = 0; row < BOWL_CELLS; row++) {
            int i = row * BOWL_GRID + col;
            const TrackVertex *v = vtx(i);
            triangle_normal(&v[1], &v[0], &v[BOWL_GRID], grid[i].normal[0]);
            triangle_normal(&v[BOWL_GRID], &v[BOWL_GRID + 1], &v[1], grid[i].normal[1]);
        }
        StripQuad *edge = &grid[BOWL_CELLS * BOWL_GRID + col];
        edge->normal[0][0] = 0;
        edge->normal[0][1] = ONE;
        edge->normal[0][2] = 0;
        edge->normal[1][0] = 0;
        edge->normal[1][1] = ONE;
        edge->normal[1][2] = 0;
    }
    for (int row = 0; row < BOWL_GRID; row++) {
        StripQuad *edge = &grid[row * BOWL_GRID + BOWL_CELLS];
        edge->normal[0][0] = 0;
        edge->normal[0][1] = ONE;
        edge->normal[0][2] = 0;
        edge->normal[1][0] = 0;
        edge->normal[1][1] = ONE;
        edge->normal[1][2] = 0;
    }
}

/* The main chain first, then each fork's branch, which continues the numbering; the walk resumes at the
 * join strip. */
void Init_Track_Strips(void)
{
    strip_data = level_data_section(1);
    strip_vertices = (TrackVertex *)level_data_section(2);
    if (current_level >= FIRST_BOWL_LEVEL) {
        init_bowl_grid();
        return;
    }
    int32_t lane = LANE_START;
    int number = 0;
    int32_t offset = 0;
    do {
        Strip *s = strip_at(offset);
        init_strip(s, &lane, number++);
        offset = s->next;
    } while (offset != 0);
    num_strips = number;

    offset = 0;
    do {
        Strip *s = strip_at(offset);
        offset = s->next;
        if (s->type != STRIP_FORK)
            continue;
        Strip *b = strip_at(s->branch);
        s->fork_length = (uint8_t)(s->width - b->width);
        lane = s->lane_base + s->fork_length;
        const Strip *last;
        do {
            init_strip(b, &lane, number++);
            last = b;
            offset = b->next;
            b = strip_at(offset);
        } while (b->type != STRIP_JOIN);
        b->fork_length = (uint8_t)(b->width - last->width);
    } while (offset != 0);
}

/* The strip a fd record moves to over the backward or forward end of its strip: a fork's or join's
 * branch-side quads lead into the branch. */
static int32_t strip_behind(const Strip *s, int segment)
{
    return s->type == STRIP_JOIN && s->fork_length <= segment ? s->branch : s->prev;
}

static int32_t strip_ahead(const Strip *s, int segment)
{
    return s->type == STRIP_FORK && s->fork_length <= segment ? s->branch : s->next;
}

static int lane_change(const Strip *to, const Strip *from)
{
    return (int)to->lane_base - (int)from->lane_base;
}

/* Keeps the record's strip and quad up to date as its point moves: tests the point against the open
 * edges of its quad and steps to the neighbouring quad or strip it crossed into. The cases are the
 * combinations of crossed edges (bit 0 row A, 1 forward, 2 row B, 3 backward). */
void Track_Follow(TrackFd *fd)
{
    if (current_level >= FIRST_BOWL_LEVEL)
        return;
    int seg = fd->segment;
    const Strip *s = strip_at(fd->strip);
    fd->prev_strip = fd->strip;
    int a = row_a(s);
    int b = row_b(s);
    uint32_t edges = quad_edges(s, seg);
    unsigned crossed = 0;
    if ((edges & 1) && fd_side(fd, vtx(a + seg), vtx(a + seg + 1)) > 0)
        crossed = 1;
    if ((edges & 2) && fd_side(fd, vtx(a + seg + 1), vtx(b + seg + 1)) > 0)
        crossed |= 2;
    if ((edges & 4) && fd_side(fd, vtx(b + seg + 1), vtx(b + seg)) > 0)
        crossed |= 4;
    if ((edges & 8) && fd_side(fd, vtx(b + seg), vtx(a + seg)) > 0)
        crossed |= 8;

    int32_t to = 0;
    int8_t step = 0;
    const Strip *n;
    int na, nb, i;
    uint32_t e;
    switch (crossed) {
    case 0:
        fd->step = 0;
        goto done;
    case 1:
        to = strip_behind(s, seg);
        n = strip_at(to);
        i = seg - lane_change(n, s);
        na = row_a(n);
        nb = row_b(n);
        e = quad_edges(n, i);
        if ((e & 2) && fd_side(fd, vtx(na + i + 1), vtx(nb + i + 1)) > 0)
            fd->segment = (int8_t)(i + 1);
        else if ((e & 8) && fd_side(fd, vtx(nb + i), vtx(na + i)) > 0)
            fd->segment = (int8_t)(i - 1);
        else
            fd->segment = (int8_t)i;
        step = -1;
        break;
    case 2:
        e = QUAD_EDGES;
        if (fd->segment == s->width - 2)
            e = track_edges[s->type][1] & QUAD_EDGES;
        if ((e & 4) && fd_side(fd, vtx(b + seg + 2), vtx(b + seg + 1)) > 0) {
            to = strip_ahead(s, seg);
            step = 1;
        } else if ((e & 1) && fd_side(fd, vtx(a + seg + 1), vtx(a + seg + 2)) > 0) {
            to = strip_behind(s, seg);
            step = -1;
        } else {
            fd->step = 0;
            fd->segment = (int8_t)(seg + 1);
            goto done;
        }
        fd->strip = to;
        fd->step = step;
        fd->segment = (int8_t)(seg - lane_change(strip_at(to), s) + 1);
        goto done;
    case 3:
        to = strip_behind(s, seg);
        n = strip_at(to);
        i = seg - lane_change(n, s);
        nb = row_b(n);
        e = QUAD_EDGES;
        if (i == -1)
            e = track_edges[n->type][0] & QUAD_EDGES;
        if (i + 1 == n->width - 1)
            e &= track_edges[n->type][1];
        if ((e & 4) && fd_side(fd, vtx(nb + i + 2), vtx(nb + i + 1)) > 0) {
            fd->step = 0;
            fd->segment++;
            goto done;
        }
        if ((e & 8) && fd_side(fd, vtx(nb + i + 1), vtx(row_a(n) + i + 1)) > 0)
            fd->segment = (int8_t)i;
        else
            fd->segment = (int8_t)(i + 1);
        step = -1;
        break;
    case 4:
        to = strip_ahead(s, seg);
        n = strip_at(to);
        i = seg - lane_change(n, s);
        na = row_a(n);
        nb = row_b(n);
        e = quad_edges(n, i);
        if ((e & 2) && fd_side(fd, vtx(na + i + 1), vtx(nb + i + 1)) > 0)
            fd->segment = (int8_t)(i + 1);
        else if ((e & 8) && fd_side(fd, vtx(nb + i), vtx(na + i)) > 0)
            fd->segment = (int8_t)(i - 1);
        else
            fd->segment = (int8_t)i;
        step = 1;
        break;
    case 6:
        to = strip_ahead(s, seg);
        n = strip_at(to);
        i = seg - lane_change(n, s);
        na = row_a(n);
        e = QUAD_EDGES;
        if (i == -1)
            e = track_edges[n->type][0] & QUAD_EDGES;
        if (i + 1 == n->width - 1)
            e &= track_edges[n->type][1];
        if ((e & 1) && fd_side(fd, vtx(na + i + 1), vtx(na + i + 2)) > 0) {
            fd->step = 0;
            fd->segment++;
            goto done;
        }
        if ((e & 8) && fd_side(fd, vtx(row_b(n) + i + 1), vtx(na + i + 1)) > 0)
            fd->segment = (int8_t)i;
        else
            fd->segment = (int8_t)(i + 1);
        step = 1;
        break;
    case 8:
        e = QUAD_EDGES;
        if (fd->segment == 1)
            e = track_edges[s->type][0] & QUAD_EDGES;
        if ((e & 4) && fd_side(fd, vtx(b + seg), vtx(b + seg - 1)) > 0) {
            to = strip_ahead(s, seg);
            step = 1;
        } else if ((e & 1) && fd_side(fd, vtx(a + seg - 1), vtx(a + seg)) > 0) {
            to = strip_behind(s, seg);
            step = -1;
        } else {
            fd->step = 0;
            fd->segment = (int8_t)(seg - 1);
            goto done;
        }
        fd->strip = to;
        fd->step = step;
        fd->segment = (int8_t)(seg - lane_change(strip_at(to), s) - 1);
        goto done;
    case 9:
        to = strip_behind(s, seg);
        n = strip_at(to);
        i = seg - lane_change(n, s);
        nb = row_b(n);
        e = QUAD_EDGES;
        if (i == 1)
            e = track_edges[n->type][0] & QUAD_EDGES;
        if (i - 1 == n->width - 1)
            e &= track_edges[n->type][1];
        if ((e & 4) && fd_side(fd, vtx(nb + i), vtx(nb + i - 1)) > 0) {
            fd->step = 0;
            fd->segment--;
            goto done;
        }
        if ((e & 2) && fd_side(fd, vtx(row_a(n) + i), vtx(nb + i)) > 0)
            fd->segment = (int8_t)i;
        else
            fd->segment = (int8_t)(i - 1);
        step = -1;
        break;
    case 12:
        to = strip_ahead(s, seg);
        n = strip_at(to);
        i = seg - lane_change(n, s);
        na = row_a(n);
        e = QUAD_EDGES;
        if (i == 1)
            e = track_edges[n->type][0] & QUAD_EDGES;
        if (i - 1 == n->width - 1)
            e &= track_edges[n->type][1];
        if ((e & 1) && fd_side(fd, vtx(na + i - 1), vtx(na + i)) > 0) {
            fd->step = 0;
            fd->segment--;
            goto done;
        }
        if ((e & 2) && fd_side(fd, vtx(na + i), vtx(row_b(n) + i)) > 0)
            fd->segment = (int8_t)i;
        else
            fd->segment = (int8_t)(i - 1);
        step = 1;
        break;
    default:
        goto done;
    }
    fd->strip = to;
    fd->step = step;
done:
    s = strip_at(fd->strip);
    fd->strip_number = (int16_t)(s->number >> 6);
    fd->track_position = s->track_position;
    if (s->width <= fd->segment)
        fd->segment = (int8_t)(s->width - 1);
    if (fd->segment < 0)
        fd->segment = 0;
}

/* Height of the plane with unit normal n through v, at the record's x and z. */
static int32_t plane_height(const TrackFd *fd, const TrackVertex *v, const int16_t n[3])
{
    return ((v->x - fd->x) * n[0] + (v->z - fd->z) * n[2]) / n[1] + v->y;
}

/* Puts the record's y on the ground: the plane of the quad's triangle it is over. */
void Map_Height(TrackFd *fd)
{
    if (current_level < FIRST_BOWL_LEVEL) {
        int i = fd->segment;
        Strip *s = strip_at(fd->strip);
        int type = s->type;
        fd->strip_type = s->type;
        const StripQuad *q = strip_quad(s, i);
        fd->plane = q;
        int32_t va = row_a(s) + i;
        int32_t vb = row_b(s) + i;
        fd->previous_y = (int16_t)fd->y;
        fd->left_vertex = (int16_t)va;
        fd->right_vertex = (int16_t)vb;
        const TrackVertex *a = vtx(va);
        const TrackVertex *b = vtx(vb);
        /* the first and last quads of some types have only one real triangle */
        int tri = fd_side(fd, &a[1], b) >= 0;
        if (i == 0) {
            if (type == 2 || type == 4)
                tri = 1;
            else if (type == 5 || type == 7)
                tri = 0;
        } else if (i == s->width - 1) {
            if (type == 3 || type == 4)
                tri = 1;
            else if (type == 6 || type == 7)
                tri = 0;
        }
        fd->y = tri ? plane_height(fd, &b[1], q->normal[1]) : plane_height(fd, a, q->normal[0]);
        return;
    }
    int cx = (fd->x + BOWL_ORIGIN) / BOWL_CELL;
    int cz = (fd->z + BOWL_ORIGIN) / BOWL_CELL;
    if (cx < 1)
        cx = 1;
    if (cx > BOWL_CELLS - 1)
        cx = BOWL_CELLS - 1;
    int row = (cz < 1 ? 1 : cz) * BOWL_GRID;
    if (cz > BOWL_CELLS - 1)
        row = (BOWL_CELLS - 1) * BOWL_GRID;
    int idx = cx + row;
    const StripQuad *q = (const StripQuad *)strip_data + idx;
    const TrackVertex *v = vtx(idx);
    fd->plane = q;
    fd->previous_y = (int16_t)fd->y;
    if (fd_side(fd, &v[1], &v[BOWL_GRID]) < 1)
        fd->y = plane_height(fd, v, q->normal[0]);
    else
        fd->y = plane_height(fd, &v[BOWL_GRID + 1], q->normal[1]);
}

/* Centre of the record's quad, and the track direction as a y angle. */
void Track_Quad_Centre(const TrackFd *fd, int32_t centre[3], int16_t angles[3])
{
    const Strip *s = strip_at(fd->strip);
    const TrackVertex *a = vtx(row_a(s) + fd->segment);
    const TrackVertex *b = vtx(row_b(s) + fd->segment);
    centre[0] = (a[0].x + a[1].x + b[0].x + b[1].x) / 4;
    centre[1] = (a[0].y + a[1].y + b[0].y + b[1].y) / 4;
    centre[2] = (a[0].z + a[1].z + b[0].z + b[1].z) / 4;
    angles[1] = (int16_t)(((0xc0 - s->heading) & 0xff) << 4);
}

/* Moves the record `steps` strips back along the prev links, keeping its lateral position. */
void Move_Backward_Strip(TrackFd *fd, int steps)
{
    int32_t offset = fd->strip;
    const Strip *s = strip_at(offset);
    int lane = fd->segment + (int8_t)s->lane_base;
    for (int i = 0; i < steps; i++) {
        offset = s->prev;
        s = strip_at(offset);
    }
    fd->segment = (int8_t)(lane - (int8_t)s->lane_base);
    fd->strip = offset;
    fd->strip_number = (int16_t)((fd->strip_number + num_strips - steps) % num_strips);
}

/* Finds the strip with the record's strip number: the main chain first, then through the branches. */
int Search_For_Strip(TrackFd *fd)
{
    int32_t offset = 0;
    do {
        const Strip *s = strip_at(offset);
        if (s->number >> 6 == fd->strip_number) {
            fd->strip = offset;
            return 1;
        }
        offset = s->next;
    } while (offset != 0);
    offset = 0;
    for (;;) {
        const Strip *s = strip_at(offset);
        if (s->number >> 6 == fd->strip_number) {
            fd->strip = offset;
            return 1;
        }
        offset = s->type == STRIP_FORK ? s->branch : s->next;
        if (offset == 0)
            return 0;
    }
}

#define REVERSING (-0x32)        /* forward_speed below this counts as reversing */
#define LOOK_BACK_AFTER 0x32     /* frames of reversing before the camera turns round */
#define LOOK_FORWARD_AFTER 10
#define FLY_IN_LAST (-0x128e)

const int32_t left_edge[12][2] = {
    {0, 0}, {0, 0}, {1, 0}, {0, 0}, {1, 0}, {0, 1}, {0, 0}, {0, 1}, {0, 0}, {0, 0}, {1, 0}, {0, 0},
};
const int32_t right_edge[12][2] = {
    {0, 0}, {0, 0}, {0, 0}, {-1, 0}, {-1, 0}, {0, 0}, {0, -1}, {0, -1}, {0, 0}, {0, 0}, {0, -1}, {0, 0},
};

const int32_t bowl_radius[12] = {0, 2, 0, 1, 0, 2, 0, 2, 14990, 14600, 14400, 14400};

void Reset_Car_Camera(int car)
{
    if (replay_playback != 0) {
        Init_The_Floaty_Camera(0);
        camera_mode = 1;
    }
    chase_height = CHASE_HEIGHT;
    chase_distance = CHASE_DISTANCE;
    chase_far = 0;
    chase_angle_offset = 0;
    look_height = LOOK_HEIGHT;
    look_back_count = 0;
    look_back = 0;
    chase_angle = car_blocks[car].angle[1];
}

static int32_t mul32(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

/* Counts frames of reversing (and of going forward again) to turn the camera round. */
static void update_look_back(int car)
{
    int32_t speed = car_blocks[car].forward_speed;
    if (look_back == 0) {
        if (speed > REVERSING - 1)
            look_back_count = 0;
        else if (++look_back_count > LOOK_BACK_AFTER)
            look_back = 1;
    } else if (speed < REVERSING + 1) {
        look_back_count = 0;
    } else if (++look_back_count > LOOK_FORWARD_AFTER) {
        look_back = 0;
    }
}

/* The heading the camera turns to, as a signed angle step from where it is. */
static int heading_error(int car)
{
    int32_t d = look_back * 0x800000 + car_blocks[car].angle[1] - chase_angle;
    int step = signed_angle((d / 4096) & ANGLE_MASK);
    return step;
}

/* Behind the car at the chase distance and height, turned by chase_angle and the offset. */
static void place_behind(int car)
{
    for (int i = 0; i < 4; i++)
        camera_position[i] = car_states[car].position[i];
    int angle = -chase_angle / 4096 + chase_angle_offset;
    camera_position[0] += rsin(angle) * chase_distance / 4096;
    camera_position[2] += -chase_distance * rcos(angle) / 4096;
    camera_position[1] += chase_height;
}

/* Pulls the camera in to where the line from the car meets the road edge a -> b, and starts the tilt
 * that lifts it over the edge. The products wrap in 32 bits as on the PSX. */
static void clip_to_edge(int car, const TrackVertex *a, const TrackVertex *b)
{
    int32_t cx = car_states[car].position[0], cz = car_states[car].position[2];
    int32_t den = mul32(b->x - a->x, cz - camera_fd.z) - mul32(cx - camera_fd.x, b->z - a->z);
    int32_t num = mul32(mul32(b->x - a->x, cz - a->z) - mul32(cx - a->x, b->z - a->z), 0x100);
    int32_t t = num / den;
    int32_t nx = mul32(cx, 0x100) + mul32(t, camera_fd.x - cx);
    int32_t nz = mul32(cz, 0x100) + mul32(t, camera_fd.z - cz);
    camera_tilt = camera_tilt < QUARTER_TURN ? camera_tilt + 0x20 : camera_tilt * 2 - QUARTER_TURN;
    camera_fd.z = nz / 256;
    camera_fd.x = nx / 256;
    if (camera_tilt < QUARTER_TURN)
        camera_tilt += 0x20;
    else if (camera_tilt > QUARTER_TURN)
        camera_tilt = QUARTER_TURN;
}

/* Which side of the road edge a -> b the camera is on. */
static int32_t edge_side(const TrackVertex *a, const TrackVertex *b)
{
    VECTOR edge = {a->z - b->z, 0, b->x - a->x, 0};
    VECTOR n;
    VectorNormal(&edge, &n);
    return (camera_fd.x - a->x) * n.vx + (camera_fd.z - a->z) * n.vz;
}

/* The race start: the camera swoops in to the grid while overlay_hold runs down. Always on car 0. */
static void fly_in(int32_t eye[3])
{
    const int32_t *pos = car_states[0].position;
    if (overlay_hold == OVERLAY_HOLD_START && race_type != RACE_TIME_TRIAL)
        Play_Sound(COMMENTARY_CHANNEL, sample_bank, SAY_RACE_START, SOUND_VOLUME_MAX, VOICE_PITCH, 0);
    if ((overlay_hold & 0x2c88) == 0)
        Play_Sound(ANY_CHANNEL, sample_bank, SAMPLE_WHOOSH, 0xdac, VOICE_PITCH, 0);
    uint32_t rise = 0, turn = 0;
    if (overlay_hold >= 0) {
        rise = (uint32_t)(overlay_hold * overlay_hold) >> 20;
        turn = ((uint32_t)overlay_hold << 14) / 0xa000;
    }
    switch (current_level) {
    case 6:
        rise = (uint32_t)((int32_t)rise >> 2);
        /* fall through */
    case 1:
    case 5:
        eye[1] = (int32_t)rise + CHASE_HEIGHT + pos[1];
        eye[2] = pos[2];
        eye[0] = pos[0] - (rsin(QUARTER_TURN - (int)(turn >> 4)) * 4 - 0x4400);
        overlay_hold -= 0x17c;
        break;
    case 3:
        eye[1] = (int32_t)rise + CHASE_HEIGHT + pos[1];
        eye[2] = rsin(QUARTER_TURN - (int)(turn >> 4)) * 4 - 0x4400 + pos[2];
        overlay_hold -= 0x17c;
        eye[0] = pos[0];
        break;
    default: {
        int angle = (car_states[0].angle[1] + (int)rise * 2) & ANGLE_MASK;
        camera_fd = car_fd[0];
        eye[2] = -rcos(angle) / 4 + pos[2];
        camera_fd.x = -rsin(angle) / 4 + pos[0];
        camera_fd.z = eye[2];
        eye[1] = (int32_t)rise + CHASE_HEIGHT + pos[1];
        eye[0] = camera_fd.x;
        Map_Height(&camera_fd);
        if (eye[1] < camera_fd.y + CHASE_HEIGHT)
            eye[1] = camera_fd.y + CHASE_HEIGHT;
        overlay_hold -= 0xbe;
        break;
    }
    }
    if (overlay_hold < FLY_IN_LAST)
        overlay_hold = 0;
}

static void camera_fd_at_position(void)
{
    camera_fd.x = camera_position[0];
    camera_fd.y = camera_position[1];
    camera_fd.z = camera_position[2];
    camera_fd.w = camera_position[3];
    Track_Follow(&camera_fd);
    Map_Height(&camera_fd);
}

/* The chase camera: trails the car, turning after it; on tracks it is kept on the road, in bowls inside
 * the wall. It looks at a point above the car. */
void Update_Chase_Camera(int car)
{
    int32_t target[4];
    if (overlay_hold != 0) {
        int32_t eye[3] = {0, 0, 0};
        fly_in(eye);
        Set_World_Position(eye);
        memcpy(target, car_states[0].position, sizeof(target));
    } else {
        if (current_level < FIRST_BOWL_LEVEL) {
            update_look_back(car);
            chase_angle += heading_error(car) * 350;
            place_behind(car);
            camera_fd = car_fd[car];
            camera_fd_at_position();
            camera_ground = camera_fd.y;
            const Strip *s = strip_at(camera_fd.strip);
            int type = camera_fd.strip_type;
            if (camera_fd.segment < 1) {
                const TrackVertex *a = vtx(camera_fd.left_vertex + left_edge[type][0]);
                const TrackVertex *b = vtx(camera_fd.right_vertex + left_edge[type][1]);
                if (edge_side(a, b) > 0)
                    clip_to_edge(car, a, b);
            } else if (camera_fd.segment == s->width - 1) {
                const TrackVertex *a = vtx(camera_fd.left_vertex + right_edge[type][0] + 1);
                const TrackVertex *b = vtx(camera_fd.right_vertex + right_edge[type][1] + 1);
                if (edge_side(a, b) < 0)
                    clip_to_edge(car, a, b);
            }
            if (camera_tilt < 1)
                camera_tilt <<= 1;
            else
                camera_tilt -= 0x20;
            camera_fd.y += rsin(camera_tilt) * 0x180 / 4096;
            camera_position[0] = camera_fd.x;
            camera_position[2] = camera_fd.z;
            if (camera_position[1] < camera_fd.y + CHASE_HEIGHT)
                camera_position[1] = camera_fd.y + CHASE_HEIGHT;
        } else if (current_level != LEVEL_DEATH_BOWL || car_states[car].position[1] > -0x65) {
            int32_t limit = bowl_radius[current_level];
            place_behind(car);
            int32_t r2 = camera_position[0] * camera_position[0] + camera_position[2] * camera_position[2];
            update_look_back(car);
            if (chase_far == 0 && r2 <= (limit - 500) * (limit - 500)) {
                chase_angle += heading_error(car) * 500;
            } else {
                /* near the wall: swing round to whichever of the two headings that keep the camera
                 * inside is closer */
                int a = Calc_Arc_Sin(0x1f4000 / chase_distance);
                int b = Calc_Angle(car_states[car].position[0], car_states[car].position[2]);
                int ca = chase_angle / 4096;
                int u1 = signed_angle((b + (QUARTER_TURN - a) - ca) & ANGLE_MASK);
                int u2 = signed_angle((b - (QUARTER_TURN - a) - ca) & ANGLE_MASK);
                chase_angle += (abs(u2) < abs(u1) ? u2 : u1) * 700;
                chase_far = r2 >= (limit - 2000) * (limit - 2000);
            }
            camera_fd_at_position();
            if (camera_position[1] < camera_fd.y + CHASE_HEIGHT)
                camera_position[1] = camera_fd.y + CHASE_HEIGHT;
        }
        Set_World_Position(camera_position);
        memcpy(target, car_states[car].position, sizeof(target));
    }
    target[1] += look_height;
    Point_Camera(target, HALF_TURN);
}
