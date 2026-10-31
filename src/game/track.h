/* Track strips: the road as a chain of strips, each a row of quads between two rows of vertices,
 * and the "fd" records that keep track of where a point is on them. */
#ifndef GAME_TRACK_H
#define GAME_TRACK_H

#include "system/maths.h"

#include <stdint.h>

/* A quad's two ground planes (one per triangle), as unit normals. Track levels keep them in the strip
 * record; bowl levels in a 32 x 32 grid at the start of level section 1. */
typedef struct {
    uint8_t surface;         /* +0x00: bits 1-2 the ground (SURFACE_*, see quad_surface) */
    uint8_t heading;         /* +0x01: the road's direction across this quad, like Strip.heading */
    int16_t normal[2][3];
} StripQuad;

_Static_assert(sizeof(StripQuad) == 0xe, "StripQuad");

/* The ground of a quad; the levels use only these two. */
#define SURFACE_ROAD 0  /* tarmac: sparks, tyre smoke and skid sounds */
#define SURFACE_LOOSE 1 /* dirt: a quarter of the grip, and wheels throw up dust */

static inline int quad_surface(const StripQuad *q)
{
    return q->surface >> 1 & 3;
}

#define STRIP_FORK 8 /* the strip where a branch leaves the main chain (by `branch`) */
#define STRIP_JOIN 9 /* the strip where it comes back */

/* A strip record of level section 1. The links are offsets of other strips (see strip_at). */
typedef struct {
    uint8_t type;            /* +0x00: 0..11, picks the vertex rows and the open edges */
    uint8_t width;           /* +0x01: quads across */
    uint8_t trigger;         /* +0x02: Strip_Trigger_Handler event (3 pit lane entry, 4 pit lane left, 5 pit) */
    uint8_t lane_base;       /* +0x03: lateral numbering, so a quad index carries over to the next strip */
    uint8_t ai_speed;        /* +0x04: the computer drivers' speed here (Recommended_Acceleration) */
    uint8_t heading;         /* +0x05: track direction, 0xC0 - angle in 1/256 turns */
    uint8_t ai_lane;         /* +0x06: STRIP_LANE_*, the lane in bits 4-7 */
    uint8_t ai_flags;        /* +0x07: STRIP_AI_*, the lane or the corner's number in bits 4-7 */
    uint8_t route;           /* +0x08: bit 0 set where a route starts, its number in bits 1-7 */
    uint8_t unused_09[5];    /* +0x09: 0 in every level */
    int16_t track_position;  /* +0x0E: number of the strip along the lap (Init_Track_Strip_Numbers) */
    uint16_t vertex;         /* +0x10: first vertex of the strip */
    uint16_t number;         /* +0x12: strip id << 6 (Init_Track_Strips) */
    int32_t next;            /* +0x14 */
    int32_t prev;            /* +0x18 */
    int32_t branch;          /* +0x1C: on a fork, the branch's first strip */
    /* The file stores the two block numbers as 16 bits; the game reads only their low bytes and keeps
     * fork_length in block_ahead's high byte. */
    int8_t block_ahead;      /* +0x20: scene block to load when passing forward, -1 none */
    uint8_t fork_length;     /* +0x21: quads of a fork/join that belong to the branch side */
    int8_t block_behind;     /* +0x22: scene block to load when passing backward */
    uint8_t block_behind_high; /* +0x23: never read */
} Strip; /* followed by `width` StripQuads */

_Static_assert(sizeof(Strip) == 0x24, "Strip header");

/* Strip.ai_lane: where the computer drivers keep. */
#define STRIP_LANE_KEEP_LEFT 0x01  /* left of the lane in bits 4-7 */
#define STRIP_LANE_NO_SWERVE 0x06
#define STRIP_LANE_KEEP_RIGHT 0x08

/* Strip.ai_flags: how the computer drivers take the strip. */
#define STRIP_AI_BRAKE_LEFT 0x01   /* brake unless left of the lane in bits 4-7 */
#define STRIP_AI_RESET 0x02        /* the braking ends */
#define STRIP_AI_CORNER 0x04       /* a corner starts, its number in bits 4-7 */
#define STRIP_AI_BRAKE_RIGHT 0x08  /* brake unless right of the lane */

typedef struct {
    int32_t x, y, z;
} TrackVertex;

/* Where a point is on the track (0x2C bytes in the original). */
typedef struct {
    int32_t x, y, z;         /* +0x00: y is the ground height after Map_Height */
    int32_t w;               /* +0x0C: copied with the position */
    const StripQuad *plane;  /* +0x10: the quad under the point (Map_Height) */
    int32_t strip;           /* +0x14: offset of the strip the point is on */
    int32_t prev_strip;      /* +0x18: strip before the last Track_Follow */
    int32_t track_position;  /* +0x1C: the strip's track_position */
    int16_t previous_y;      /* +0x20: y before the last Map_Height */
    int16_t strip_number;    /* +0x22 */
    int16_t left_vertex;     /* +0x24: vertex index of the quad's two rows (Map_Height) */
    int16_t right_vertex;    /* +0x26 */
    uint8_t strip_type;      /* +0x28 */
    uint8_t unused_29;       /* +0x29: never accessed */
    int8_t segment;          /* +0x2A: quad index across the strip */
    int8_t step;             /* +0x2B: 1 / -1 when the last Track_Follow moved to the next / previous strip */
} TrackFd;

extern uint8_t *strip_data;          /* 0x80079A5C: level section 1 */
extern TrackVertex *strip_vertices;  /* 0x80079B64: level section 2 */
/* Per strip type: the vertex offsets of the barrier edge at the first and at the last quad across. */
extern const int32_t left_edge[12][2];   /* 0x8006FD40 */
extern const int32_t right_edge[12][2];  /* 0x8006FDA0 */
extern const int32_t bowl_radius[12];    /* 0x8006FD10: the bowls' wall */
extern int32_t num_strips;           /* 0x80079A74: strips in the main chain */

/* A strip's or quad's heading as an angle. */
static inline int32_t road_angle(int heading)
{
    return (heading * 0x10 + QUARTER_TURN) & ANGLE_MASK;
}

static inline Strip *strip_at(int32_t offset)
{
    return (Strip *)(strip_data + 4 + offset);
}

static inline StripQuad *strip_quad(Strip *strip, int index)
{
    return (StripQuad *)((uint8_t *)strip + sizeof(Strip)) + index;
}

void Generate_Surface_Normals(Strip *strip);      /* 0x80016074 */
void Init_Track_Strips(void);                      /* 0x8001648C */
void Track_Follow(TrackFd *fd);                    /* 0x800168E0 */
void Map_Height(TrackFd *fd);                      /* 0x80017A70 */
void Track_Quad_Centre(const TrackFd *fd, int32_t centre[3], int16_t angles[3]); /* 0x80018274 */
void Move_Backward_Strip(TrackFd *fd, int steps);  /* 0x800184B0 */
int Search_For_Strip(TrackFd *fd);                 /* 0x80018558 */

#endif
