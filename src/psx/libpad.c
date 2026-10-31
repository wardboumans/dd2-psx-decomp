#include "psx/libpad.h"

#include "platform/platform.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Receive buffers in the format the BIOS fills: status byte, type/size byte, then the buttons. */
static u_char *pad_buffer[2];
static long pad_buffer_len[2];

void InitPAD(u_char *buf1, long len1, u_char *buf2, long len2)
{
    pad_buffer[0] = buf1;
    pad_buffer[1] = buf2;
    pad_buffer_len[0] = len1;
    pad_buffer_len[1] = len2;
}

#define PAD_ID_DIGITAL 0x41 /* type 4, two data words */
#define PAD_ID_NEGCON 0x23  /* type 2, three data words */

static int pad_started;
static int vblanks_since_start;

#define SCRIPT_MAX 64
#define SCRIPT_HOLD 6 /* vblanks a scripted press lasts: the game polls every frame or two */

static struct {
    int at, hold;
    uint16_t buttons;
} script[SCRIPT_MAX];
static int script_count;

void pad_script(int at, int hold, unsigned buttons)
{
    if (script_count < SCRIPT_MAX) {
        script[script_count].at = at;
        script[script_count].hold = hold > 0 ? hold : SCRIPT_HOLD;
        script[script_count].buttons = (uint16_t)buttons;
        script_count++;
    }
}

static uint16_t scripted_buttons(void)
{
    uint16_t buttons = 0;
    for (int i = 0; i < script_count; i++)
        if (vblanks_since_start >= script[i].at && vblanks_since_start < script[i].at + script[i].hold)
            buttons |= script[i].buttons;
    return buttons;
}

/* Port 2 stays empty: 0xFF in the status byte means "no pad" to the game. */
void StartPAD(void)
{
    for (int i = 0; i < 2; i++)
        if (pad_buffer[i])
            memset(pad_buffer[i], 0xff, (size_t)pad_buffer_len[i]);
    pad_started = 1;
}

void StopPAD(void)
{
    pad_started = 0;
}

void ChangeClearPAD(long mode)
{
    (void)mode;
}

static bool negcon;

void pad_set_negcon(bool on)
{
    negcon = on;
}

/* PORT: the NeGcon: the twist from the stick, I, II and L as analog bytes (scripted Cross, Square and
 * L1 press them fully), A, B, R, Start and the D-pad as buttons. */
static void negcon_service(uint16_t buttons)
{
    PlatformAnalog a;
    platform_pad_analog(&a);
    if (buttons & 0x4000)
        a.i = 0xff;
    if (buttons & 0x8000)
        a.ii = 0xff;
    if (buttons & 0x0400)
        a.l = 0xff;
    uint16_t raw = (uint16_t)~(buttons & ~(0x4000 | 0x8000 | 0x0400));
    pad_buffer[0][1] = PAD_ID_NEGCON;
    pad_buffer[0][2] = (u_char)(raw & 0xff);
    pad_buffer[0][3] = (u_char)(raw >> 8);
    pad_buffer[0][4] = a.twist;
    pad_buffer[0][5] = a.i;
    pad_buffer[0][6] = a.ii;
    pad_buffer[0][7] = a.l;
}

/* Buttons are active low in the buffer, as the pad sends them. */
void pad_service(void)
{
    if (!pad_started || !pad_buffer[0] || pad_buffer_len[0] < 4)
        return;
    vblanks_since_start++;
    uint16_t buttons = platform_pad_buttons() | scripted_buttons();
    pad_buffer[0][0] = 0x00;
    if (negcon && pad_buffer_len[0] >= 8) {
        negcon_service(buttons);
        return;
    }
    uint16_t raw = (uint16_t)~buttons;
    pad_buffer[0][1] = PAD_ID_DIGITAL;
    pad_buffer[0][2] = (u_char)(raw & 0xff);
    pad_buffer[0][3] = (u_char)(raw >> 8);
}
