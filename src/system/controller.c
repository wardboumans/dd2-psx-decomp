#include "system/controller.h"

#include "psx/libapi.h"
#include "psx/libpad.h"

#include <string.h>

PadState pad;
uint8_t pad_buffer[2][PAD_BUFFER_SIZE];
uint16_t pad_config[2];
uint16_t pad_layout[16] = {
    PAD_SELECT, 0, 0, PAD_START, PAD_UP, PAD_RIGHT, PAD_DOWN, PAD_LEFT,
    PAD_L2, PAD_R2, PAD_L1, PAD_R1, PAD_TRIANGLE, PAD_CIRCLE, PAD_CROSS, PAD_SQUARE,
};
uint16_t button_map[16];

/* 0x800799F0/0x800799F4: the configs as ints, read by Poll_Pad_Buttons */
static int32_t pad_config_int[2];

/* 0x8007991C: called by Poll_Pad_Buttons when the pad type changes */
static void (*pad_change_callback)(int type);

/* The first word of the receive buffer (status byte, type nibble) for each pad the game knows. */
#define PAD_ID_MASK 0xf0ff
#define PAD_ID_DIGITAL 0x4000 /* 0x800798F8 */

int16_t pad_id_negcon = PAD_ID_NEGCON;
int16_t pad_id_analog = PAD_ID_ANALOG;

/* Twist dead zone (by pad_config[0]) and full-lock range (by pad_config[1]) of the analog pads. */
static const uint16_t negcon_dead_zone[4] = {0, 5, 7, 10};    /* 0x800798FC */
static const uint16_t negcon_range[4] = {25, 40, 70, 100};    /* 0x80079904 */
static const uint16_t analog_dead_zone[4] = {0, 2, 5, 8};     /* 0x8007990C */
static const uint16_t analog_range[4] = {15, 45, 90, 120};    /* 0x80079914 */

static uint16_t last_held;   /* 0x800799E8 */
static uint16_t last_mapped; /* 0x800799EC */
static int16_t last_type;    /* 0x800799E4 */

void Set_Pad_Change_Callback(void (*callback)(int type))
{
    pad_change_callback = callback;
}

void Init_Pads(void)
{
    InitPAD(pad_buffer[0], PAD_BUFFER_SIZE, pad_buffer[1], PAD_BUFFER_SIZE);
    StartPAD();
    ChangeClearPAD(0);
    Init_Pad_Config();
}

void Init_Pad_Config(void)
{
    EnterCriticalSection();
    pad_config_int[0] = pad_config[0];
    pad_config_int[1] = pad_config[1];
    memcpy(button_map, pad_layout, sizeof(button_map));
    ExitCriticalSection();
}

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint16_t map_buttons(uint16_t buttons)
{
    uint16_t mapped = 0;
    for (int i = 0; i < 16; i++)
        if (buttons & (1 << i))
            mapped |= button_map[i];
    return mapped;
}

/* NeGcon-style pads: the twist steers, and the analog I, II and L buttons count as Cross, Square
 * and L1 once pressed past half way. */
static void poll_twist_pad(uint8_t type, int dead_zone, int range)
{
    const uint8_t *buf = pad_buffer[0];
    uint16_t buttons = (uint16_t)~read_u16(buf + 2);
    int twist = buf[4];
    if (buf[7] > 0x7f)
        buttons |= PAD_L1;
    if (buf[5] > 0x7f)
        buttons |= PAD_CROSS;
    if (buf[6] > 0x7f)
        buttons |= PAD_SQUARE;
    if (twist < 0x80 - dead_zone) {
        int d = 0x80 - twist - dead_zone;
        if (d > range)
            d = range;
        pad.steer = (uint8_t)(0x80 - d * 0x80 / range);
    } else if (twist > 0x80 + dead_zone) {
        int d = twist - 0x80 - dead_zone;
        if (d > range - 1)
            d = range - 1;
        pad.steer = (uint8_t)(0x80 + d * 0x80 / range);
    } else {
        pad.steer = 0x80;
    }
    pad.type = type;
    pad.held = buttons;
    pad.pressed = buttons & (uint16_t)~last_held;
    pad.mapped = map_buttons(buttons);
}

/* Reads pad 1 into `pad`: buttons held and newly pressed, the game functions they map to, the
 * steering of analog pads; tells the pad change callback when another kind of pad appears. */
void Poll_Pad_Buttons(void)
{
    int16_t id = (int16_t)(read_u16(pad_buffer[0]) & PAD_ID_MASK);
    if (id == pad_id_negcon) {
        poll_twist_pad(PAD_NEGCON, negcon_dead_zone[pad_config_int[0]], negcon_range[pad_config_int[1]]);
    } else if (id == pad_id_analog) {
        poll_twist_pad(PAD_ANALOG, analog_dead_zone[pad_config_int[0]], analog_range[pad_config_int[1]]);
    } else {
        if (id == PAD_ID_DIGITAL) {
            pad.type = PAD_DIGITAL;
            uint16_t buttons = (uint16_t)~read_u16(pad_buffer[0] + 2);
            pad.pressed = buttons & (uint16_t)~last_held;
            last_held = buttons;
            pad.mapped = map_buttons(buttons);
            pad.mapped_pressed = pad.mapped & (uint16_t)~last_mapped;
            last_mapped = pad.mapped;
            pad.held = last_held;
        } else {
            pad.type = PAD_NONE;
            pad.held = 0;
            pad.pressed = 0;
            pad.mapped = 0;
            pad.mapped_pressed = 0;
        }
        pad.steer = 0;
    }
    if (id == pad_id_negcon || id == pad_id_analog) {
        pad.mapped_pressed = pad.mapped & (uint16_t)~last_mapped;
        last_held = pad.held;
        last_mapped = pad.mapped;
    }
    if (pad.type != last_type && pad.type != PAD_NONE) {
        last_type = pad.type;
        if (pad_change_callback)
            pad_change_callback(pad.type);
    }
}
