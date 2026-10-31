#ifndef SYSTEM_CONTROLLER_H
#define SYSTEM_CONTROLLER_H

#include <stdint.h>

#define PAD_BUFFER_SIZE 0x22

/* Pad buttons (PadState.held and .pressed). */
#define PAD_SELECT 0x0001
#define PAD_START 0x0008
#define PAD_UP 0x0010
#define PAD_RIGHT 0x0020
#define PAD_DOWN 0x0040
#define PAD_LEFT 0x0080
#define PAD_L2 0x0100
#define PAD_R2 0x0200
#define PAD_L1 0x0400
#define PAD_R1 0x0800
#define PAD_TRIANGLE 0x1000
#define PAD_CIRCLE 0x2000
#define PAD_CROSS 0x4000
#define PAD_SQUARE 0x8000

/* Pad types as Poll_Pad_Buttons reports them. */
enum {
    PAD_NONE = 0,
    PAD_DIGITAL = 1,
    PAD_NEGCON = 2,
    PAD_ANALOG = 3,
};

/* The pad as the game reads it, refreshed by Poll_Pad_Buttons. */
typedef struct {
    uint16_t held;           /* +0x00, pad buttons */
    uint16_t pressed;        /* +0x02, newly pressed this poll */
    uint16_t mapped;         /* +0x04, game functions (through button_map) */
    uint16_t mapped_pressed; /* +0x06 */
    uint8_t steer;           /* +0x08, analog steering, 0x80 when centred */
    uint8_t type;            /* +0x09, PAD_* */
} PadState;

extern PadState pad;                           /* 0x8008C480 */

/* The receive buffer's first word (status, type) that counts as a NeGcon and as the analog pad. Both
 * kinds of pad send the NeGcon's; Ask_Analog_Pad_Type swaps the two to make it the analog pad. */
#define PAD_ID_NEGCON 0x2000
#define PAD_ID_ANALOG 0x2001
extern int16_t pad_id_negcon; /* 0x800798F4 */
extern int16_t pad_id_analog; /* 0x800798F6 */
extern uint8_t pad_buffer[2][PAD_BUFFER_SIZE]; /* 0x800A1C94, filled by the BIOS each vblank */
extern uint16_t pad_config[2];                 /* 0x8007179C, control method per player */
extern uint16_t pad_layout[16];                /* 0x8007177C, the chosen layout; Setup_Pad rewrites it */
extern uint16_t button_map[16];                /* 0x80090FF8, game function bit per pad button */

void Init_Pads(void);       /* 0x800502C4 */
void Init_Pad_Config(void); /* 0x8005030C */
void Set_Pad_Change_Callback(void (*callback)(int type)); /* 0x80050464 */
void Poll_Pad_Buttons(void); /* 0x80050470 */

#endif
