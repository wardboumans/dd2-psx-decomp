/* GTE emulator: the PSX geometry coprocessor's registers and instructions (see the psx-spx
 * "Geometry Transformation Engine" chapter). libgte.c builds the Psy-Q API on top. */
#ifndef PSX_GTE_H
#define PSX_GTE_H

#include <stdint.h>

typedef struct {
    /* data registers */
    int16_t vx[3], vy[3], vz[3]; /* V0..V2 */
    uint8_t rgbc[4];             /* RGBC: r, g, b, code */
    uint16_t otz;
    int32_t ir[4];               /* IR0..IR3 (16-bit values, sign-extended) */
    int16_t sx[3], sy[3];        /* SXY0..SXY2 FIFO */
    uint16_t sz[4];              /* SZ0..SZ3 FIFO */
    uint8_t rgb_fifo[3][4];      /* RGB0..RGB2 */
    int32_t mac[4];              /* MAC0..MAC3 */
    uint32_t lzcs;
    /* control registers */
    int16_t rt[3][3];            /* rotation */
    int32_t tr[3];               /* translation */
    int16_t llm[3][3];           /* light directions */
    int32_t bk[3];               /* background colour */
    int16_t lcm[3][3];           /* light colours */
    int32_t fc[3];               /* far colour */
    int32_t ofx, ofy;            /* screen offset, 16.16 */
    uint16_t h;                  /* projection plane distance */
    int16_t dqa;                 /* depth cue */
    int32_t dqb;
    int16_t zsf3, zsf4;          /* average Z scale factors */
    uint32_t flag;
} GteState;

extern GteState gte;

/* FLAG bits */
#define GTE_FLAG_ERROR (1u << 31)
#define GTE_FLAG_IR1 (1u << 24)
#define GTE_FLAG_IR2 (1u << 23)
#define GTE_FLAG_IR3 (1u << 22)
#define GTE_FLAG_SZ_OTZ (1u << 18)
#define GTE_FLAG_DIVIDE (1u << 17)
#define GTE_FLAG_SX (1u << 14)
#define GTE_FLAG_SY (1u << 13)

/* The saturations the game takes as a projection gone wrong (0x1C66000): it drops such a face. */
#define GTE_CLIP_FLAGS (GTE_FLAG_IR1 | GTE_FLAG_IR2 | GTE_FLAG_IR3 | GTE_FLAG_SZ_OTZ | GTE_FLAG_DIVIDE | GTE_FLAG_SX | \
                        GTE_FLAG_SY)

/* Instructions. `sf` shifts results by 12, `lm` clamps IR to 0 instead of -0x8000. */
void gte_op_rtps(int sf);
void gte_op_rtpt(int sf);
void gte_op_nclip(void);
void gte_op_avsz3(void);
void gte_op_avsz4(void);
void gte_op_op(int sf, int lm);
void gte_op_sqr(int sf);
void gte_op_dpcs(int sf);
void gte_op_dpct(int sf);
void gte_op_intpl(int sf);
void gte_op_dcpl(int sf);
void gte_op_ncs(int sf);
void gte_op_nct(int sf);
void gte_op_nccs(int sf);
void gte_op_ncct(int sf);
void gte_op_ncds(int sf);
void gte_op_ncdt(int sf);
void gte_op_cc(int sf);
void gte_op_cdp(int sf);
void gte_op_gpf(int sf);
void gte_op_gpl(int sf);
/* MVMVA: matrix 0 rotation, 1 light, 2 colour; vector 0-2 V0-V2, 3 IR; translation 0 TR, 1 BK, 2 FC, 3 none */
void gte_op_mvmva(int sf, int lm, int matrix, int vector, int translation);

/* SXYP/IRGB writes, which have side effects on the FIFOs and IR. */
void gte_push_sxy(int16_t x, int16_t y);
void gte_set_irgb(uint32_t v);
uint32_t gte_get_orgb(void);

#endif
