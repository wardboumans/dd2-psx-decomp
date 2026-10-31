/* The movie executables: a .STR stream played from the CD, each frame decoded by the MDEC in 16-pixel
 * columns into one of two VRAM areas while the other is shown, with the XA sound played by the drive.
 * Start or Cross (the NeGcon: Start, the A button or a push of the I button) ends it.
 * Addresses are in NTSCINTR.EXE; the other three executables differ only in their numbers. */
#include "game/movie.h"

#include "psx/libcd.h"
#include "psx/libetc.h"
#include "psx/libgpu.h"
#include "psx/libpad.h"
#include "psx/libpress.h"

#include <stdlib.h>

#define FRAME_TIMEOUT 0x800000 /* polls of the ring before a frame counts as lost */
#define VLC_BUFFER 0x28000
#define PAD_NEGCON 0x2300      /* the receive buffer's first halfword: status 0, NeGcon */
#define BUTTON_START 0x0008
#define BUTTON_NEGCON_A 0x2000
#define BUTTON_CROSS 0x4000

/* The decoding state (0x8012167C). */
typedef struct {
    u_long *vlcbuf[2]; /* run-level codes of the frame being decoded and of the next */
    int vlcid;
    u_long *imgbuf;    /* one column of macroblocks */
    RECT rect[2];      /* the two frame areas */
    int rectid;        /* the one being drawn into */
    RECT slice;        /* the column being loaded */
    int isdone;
} DecEnv;

static DecEnv dec;
static u_char pad_buf[8 + 8]; /* 0x8012165C */
static int32_t frame_no;     /* of the last frame taken from the ring */
static int32_t prev_frame_no;
static int timed_out;
static int movie_end;        /* the stream wrapped round or reached its last frames */
static int first_slice = 1;
static int frame_width, frame_height;
static int16_t depth;        /* VRAM words per 2 pixels: 2 for 15-bit, 3 for 24-bit */
static int16_t end_frame;
static int16_t negcon_i;     /* the NeGcon's I button last frame */

/* Words of MDEC output for one column. */
static int slice_words(void)
{
    return ((dec.slice.w - 1) / (depth * 8) + 1) * 8 * depth * 16 * ((dec.slice.h - 1) / 16 + 1) >> 1;
}

/* The MDEC has finished a column: into VRAM with it, then the next column, or the frame is done and
 * the areas swap. The original first services the stream library's interrupt in 24-bit mode; the
 * port's stream does not need it. */
static void Movie_Out_Callback(void) /* 0x80100E9C */
{
    LoadImage(&dec.slice, dec.imgbuf);
    int rest = 0;
    if (first_slice)
        rest = dec.rect[dec.rectid].w % (depth * 8);
    if (rest != 0) {
        first_slice = 0;
        dec.slice.x = (short)(dec.slice.x + rest * depth / 2);
    } else {
        dec.slice.x = (short)(dec.slice.x + depth * 8);
    }
    if (dec.slice.x < dec.rect[dec.rectid].x + dec.rect[dec.rectid].w) {
        DecDCTout(dec.imgbuf, slice_words());
    } else {
        dec.rectid = dec.rectid == 0;
        dec.isdone = 1;
        dec.slice.x = dec.rect[dec.rectid].x;
        first_slice = 1;
        dec.slice.y = dec.rect[dec.rectid].y;
    }
}

/* The next whole frame from the ring, checked and measured; NULL when none comes in time or its
 * header is wrong. A new frame size clears the frame areas. */
static u_long *Movie_Get_Frame(void) /* 0x801010DC */
{
    u_long *addr, *header;
    for (int timeout = FRAME_TIMEOUT;;) {
        u_long busy = StGetNext(&addr, &header);
        timeout--;
        if (busy == 0)
            break;
        if (timeout == 0)
            return NULL;
    }
    if (addr[0] != header[5] || addr[1] != header[6]) {
        StFreeRing(addr);
        return NULL;
    }
    prev_frame_no = frame_no;
    if ((int32_t)header[2] < frame_no)
        movie_end = 1;
    if ((u_long)(end_frame - 2) <= header[2])
        movie_end = 1;
    frame_no = (int32_t)header[2];
    if (frame_width != (int)(header[4] & 0xffff) || frame_height != (int)(header[4] >> 16)) {
        RECT all = {0, 0, 480, 512};
        ClearImage(&all, 0, 0, 0);
        frame_width = (int)(header[4] & 0xffff);
        frame_height = (int)(header[4] >> 16);
    }
    short w = (short)(depth * frame_width / 2);
    dec.rect[0].h = dec.rect[1].h = dec.slice.h = (short)frame_height;
    dec.rect[0].w = dec.rect[1].w = w;
    return addr;
}

/* The next frame's bitstream into the other run-level buffer. */
static int Movie_Next_Frame(void) /* 0x8010126C */
{
    u_long *bs = Movie_Get_Frame();
    if (bs == NULL) {
        timed_out = 1;
        return -1;
    }
    dec.vlcid = dec.vlcid == 0;
    DecDCTvlc(bs, dec.vlcbuf[dec.vlcid]);
    StFreeRing(bs);
    return 0;
}

/* Waits for the frame's last column. The port's MDEC finishes inside DecDCTout, so the time-out the
 * original has here (swapping the areas anyway) never comes. */
static void Movie_Sync(void) /* 0x801012FC */
{
    dec.isdone = 0;
}

/* Plays `name` up to frame `last`; `bytes` 3 for 24-bit, 2 for 15-bit; the frame's top left at x, y
 * within an area `screen_h` lines high. */
static void Play_Movie(char *name, int16_t last, int16_t bytes, int16_t x, int16_t y, int16_t screen_h,
                       int video_mode) /* 0x801013C0 */
{
    DISPENV disp = {0};
    DRAWENV draw = {0};
    CdlFILE file = {0};
    u_short prev_buttons = 0; /* the original starts from whatever its register held */
    first_slice = 1;
    movie_end = 0;
    depth = bytes;
    end_frame = last;
    /* The original reads the CD's directory itself; libcd finds the file the same. */
    CdSearchFile(&file, name);
    dec.vlcbuf[0] = malloc(VLC_BUFFER);
    dec.vlcbuf[1] = malloc(VLC_BUFFER);
    dec.vlcid = 0;
    dec.imgbuf = malloc((size_t)depth << 13);
    dec.rectid = 0;
    dec.isdone = 0;
    dec.rect[0] = (RECT){(short)(x * depth / 2), y, (short)(depth * 160), screen_h};
    dec.rect[1] = (RECT){dec.rect[0].x, (short)(y + screen_h), (short)(depth * 160), screen_h};
    dec.slice = (RECT){dec.rect[0].x, y, (short)(depth * 8), screen_h};
    DecDCTReset(0);
    DecDCToutCallback(Movie_Out_Callback);
    u_long *ring = malloc(0x4000);
    StSetRing(ring, 32);
    StSetStream(depth == 3, 1, 0xfffffff, NULL, NULL);
    ResetGraph(0);
    SetGraphDebug(0);
    SetDispMask(0);
    SetVideoMode(video_mode);
    if (depth == 3) {
        disp.isrgb24 = 1;
        disp.disp.w = (short)(disp.disp.w * 2 / 3);
    }
    PutDispEnv(&disp);
    PutDrawEnv(&draw);
    while (!CdControl(CdlSeekL, (u_char *)&file.pos, NULL))
        ;
    while (!CdRead2(CdlModeStream | CdlModeSpeed | CdlModeRT))
        ;
    Movie_Next_Frame();
    if (frame_no < end_frame) {
        for (;;) {
            u_short buttons = (u_short)~(pad_buf[2] | pad_buf[3] << 8);
            if (pad_buf[0] == 0xff)
                buttons = 0;
            u_short pressed = buttons & (buttons ^ prev_buttons);
            DecDCTin(dec.vlcbuf[dec.vlcid], depth);
            DecDCTout(dec.imgbuf, slice_words());
            Movie_Next_Frame();
            if (timed_out == 1)
                goto done;
            Movie_Sync();
            VSync(0);
            int shown = dec.rectid == 0;
            const RECT *r = &dec.rect[shown];
            SetDefDispEnv(&disp, r->x - x * depth / 2, r->y - y, r->w, r->h);
            SetDefDrawEnv(&draw, r->x, r->y, r->w, r->h);
            if (depth == 3) {
                disp.isrgb24 = 1;
                disp.disp.w = (short)(disp.disp.w * 2 / 3);
            }
            PutDispEnv(&disp);
            PutDrawEnv(&draw);
            SetDispMask(1);
            if (movie_end == 1)
                break;
            if ((pad_buf[0] | pad_buf[1] << 8) == PAD_NEGCON) {
                if ((pressed & (BUTTON_NEGCON_A | BUTTON_START)) || (pad_buf[5] > 100 && negcon_i < 101))
                    goto done;
                negcon_i = pad_buf[5];
            } else if (pressed & (BUTTON_START | BUTTON_CROSS)) {
                goto done;
            }
            prev_buttons = buttons;
            if (end_frame <= frame_no)
                goto done;
        }
        CdControl(CdlStop, NULL, NULL);
    }
done:
    StClearRing();
    StUnSetRing();
    DecDCToutCallback(NULL);
    while (!CdControl(CdlStop, NULL, NULL))
        ;
    DrawSync(0);
    free(ring);
    free(dec.vlcbuf[0]);
    free(dec.vlcbuf[1]);
    free(dec.imgbuf);
}

void Movie_Main(int movie, int pal) /* 0x80101A2C */
{
    /* The frames each executable plays to, and where on the screen. */
    static const struct {
        char *name;
        int16_t last_frame;
    } movies[2][2] = {
        {{"\\INTRO.STR;1", 0x808}, {"\\XTRO.STR;1", 0x870}}, /* NTSCINTR.EXE, NTSCXTRO.EXE */
        {{"\\INTRO.STR;1", 0x6b5}, {"\\XTRO.STR;1", 0x708}}, /* PALINTRO.EXE, PALXTRO.EXE */
    };
    /* Each run is a freshly loaded executable. */
    frame_no = prev_frame_no = 0;
    timed_out = 0;
    frame_width = frame_height = 0;
    negcon_i = 0;
    ResetCallback();
    InitPAD(pad_buf, 8, pad_buf + 8, 0);
    StartPAD();
    ChangeClearPAD(0);
    CdInit();
    Play_Movie(movies[pal][movie].name, movies[pal][movie].last_frame, 3, 0, pal ? 0x28 : 0x20, pal ? 0x100 : 0xf0,
               pal ? MODE_PAL : MODE_NTSC);
    StopCallback();
    StopPAD();
    ResetGraph(3);
    CdInit();
}
