/* Animated scenery textures: frames copied over a sprite in VRAM (pictures or colour tables). */
#ifndef GAME_ANIM_H
#define GAME_ANIM_H

#include "graphics/sprite.h"

#include <stdint.h>

#define ANIM_SPRITES 17

/* [0] is the sprite the scenery shows; [1..] the frames copied over it in turn, up to a NULL. */
typedef struct {
    const SpriteInfo *sprite[ANIM_SPRITES];
    int32_t frame; /* the frame shown last; the original keeps these in separate globals */
} Animation;

extern Animation video_anim;      /* 0x8007FCA8: level 1, the big screen (VIDANI) */
extern Animation flag_anims[2];   /* 0x80089404, 0x8008941C: level 5 (DUMBB, DUMBA) */
extern Animation driver_anim;     /* 0x80090BC4: level 6 (ZADRV) */
extern Animation goose_anim;      /* 0x80090BD4 */
extern Animation flash_anims[3];  /* 0x80090F8C, 0x80090FB0, 0x80090FD4 */
extern Animation flame_anim;      /* 0x8008C7CC: level 11 */
/* Level 6's neon signs: colour tables. */
extern Animation dollar_cluts;    /* 0x800884DC */
extern Animation safe_cluts;      /* 0x800A40FC */
extern Animation casino_cluts;    /* 0x800884B0 */
extern Animation j_cluts;         /* 0x800848F8 */
extern Animation twist_cluts[2];  /* 0x80084908, 0x80084918 */

void Init_CLUT_Animation(void);    /* 0x8001DB90 */
void Init_Texture_Animation(void); /* 0x8001DD38 */

#endif
