/* Particles: smoke, steam, fire and sparks, as camera-facing sprites. */
#ifndef GAME_PARTICLES_H
#define GAME_PARTICLES_H

#include "psx/libgpu.h"
#include "psx/libgte.h"

#include <stdint.h>

#define MAX_PARTICLES 100

/* A particle (0xB0 bytes in the original). Colours are 20.12. */
typedef struct Particle {
    POLY_FT4 packet[2];
    int32_t x, y, z;              /* +0x50 */
    int32_t pad;                  /* +0x5C: never accessed */
    int32_t life;                 /* +0x60: frames left, 0 when free */
    int (*update)(struct Particle *p); /* +0x64: 0 when it dies */
    int32_t vx, vy, vz;           /* +0x68 */
    int32_t max_size, size, grow; /* +0x74 */
    const uint16_t *frames;       /* +0x80: sprite per frame; PARTICLE_END / _LOOP / _HOLD */
    int32_t frame;                /* +0x84 */
    int32_t r, g, b;              /* +0x88 */
    int32_t dr, dg, db;           /* +0x94 */
    int32_t end_r, end_g, end_b;  /* +0xA0: the colour stops changing when r reaches end_r */
    uint8_t blend;                /* +0xAC: semi-transparency mode */
} Particle;

/* What an emitter is given (the original passes it by value after the position). */
typedef struct {
    int32_t max_size, size, grow;
    int32_t unused;
    int32_t r, g, b;
    int32_t end_r, end_g, end_b;
    int32_t dr, dg, db;
    int32_t blend;
} ParticleParams;

extern Particle particles[MAX_PARTICLES]; /* 0x8009741C */

void InitialiseParticleSystem(void);                                            /* 0x8002CE90 */
void Smoke(const VECTOR *pos, Particle *p, const ParticleParams *pp);           /* 0x8002D0E8 */
void Fire(const VECTOR *pos, Particle *p, const ParticleParams *pp);            /* 0x8002D854 */
void Sparks(const VECTOR *pos, const int32_t vel[3], Particle *p, const ParticleParams *pp); /* 0x8002D9D0 */
void Steam(const VECTOR *pos, Particle *p, const ParticleParams *pp);           /* 0x8002DE84 */
int Pop_Particle_Slot(void);                                                    /* 0x8002E32C */
void Update_Particles(void);                                                    /* 0x8002E3EC */

#endif
