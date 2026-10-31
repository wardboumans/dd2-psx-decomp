#include "game/play_game.h"

#include "game/camera.h"
#include "game/ai.h"
#include "game/car.h"
#include "game/car_collision.h"
#include "game/collision.h"
#include "game/car_motion.h"
#include "game/debris.h"
#include "game/flag.h"
#include "game/frontend.h"
#include "game/frontend_init.h"
#include "game/lens_flare.h"
#include "game/main_init.h"
#include "game/car_draw.h"
#include "game/overlays.h"
#include "game/particles.h"
#include "game/pause.h"
#include "game/replay.h"
#include "game/scene.h"
#include "game/sound_fx.h"
#include "game/triggers.h"
#include "graphics/object.h"
#include "psx/libapi.h"
#include "psx/libetc.h"
#include "sound/cd.h"
#include "sound/sound.h"
#include "system/controller.h"
#include "system/debug.h"

#define DEMO_CAMERA_CAR 4
#define DEMO_FRAMES 0x2ee        /* how long the attract mode races */
#define OVERLAY_START 100
#define QUIT_HOLD_FRAMES 60      /* Start + Select held this long abandons the race */
#define FINISHED_TO_END 14       /* the race ends once more cars than this have finished */
#define SCRATCHPAD_STACK 0x1f800400

int32_t camera_car;
int32_t camera_mode = 1; /* the race starts on the chase camera, which flies in over the grid */
TrackFd *camera_fd_pt;
TrackFd car_fd[24];
int32_t overlay_state;
int32_t overlay_hold;
int32_t camera_tilt;
int32_t quit_flag;
int32_t yes_quit;
int32_t replay_playback;
int32_t pit_in;
int32_t current_player_car;
int32_t race_timers_on = 1;
int32_t cars_finished;

static int32_t demo_frames;  /* 0x80079998 */
static int32_t quit_hold;    /* 0x80079124 */
static long saved_stack;     /* 0x80079994 */

/* One race: the level loaded, then a game frame every two vblanks (30 a second on NTSC) until the race
 * ends, the player quits or the attract mode runs out. Exactly one game step per drawn frame, never a number
 * taken from the measured frame time. Returns 1 when abandoned. */
int Play_Game(void)
{
    if (demo_mode == 0) {
        camera_car = 0;
    } else {
        camera_car = DEMO_CAMERA_CAR;
        camera_mode = 1;
    }
    camera_fd_pt = &car_fd[camera_car];
    Init_Game();
    CD_Open();
    Set_CD_Track(current_level, 1);
    Set_CD_Volume((short)music_volume);
    set_master_volume(sound_volume);
    Initialise_Pause_Mode();
    overlay_state = OVERLAY_START;
    overlay_hold = demo_mode == 0 && replay_playback == 0 ? OVERLAY_HOLD_START : 0;
    camera_tilt = 0;
    frame_count = 0;
    demo_frames = DEMO_FRAMES;
    quit_flag = 0;
    yes_quit = 0;
    saved_stack = SetSp(SCRATCHPAD_STACK);
    for (;;) {
        Debug_Stub();
        Swap_Draw_Buffers();
        Poll_Pad_Buttons();
        demo_frames--;
        if (demo_mode != 0) {
            if (pad.pressed != 0) {
                quit_flag = 1;
                yes_quit = 1;
            }
            if (demo_frames < 0)
                quit_flag = 1;
        }
        if (replay_playback == 0 && demo_mode == 0 &&
            (((pad.pressed & PAD_START) && !(pad.held & PAD_SELECT)) || pad.type != current_pad_type) &&
            Pause_Mode() == PAUSE_END_RACE)
            quit_flag = 1;
        if (replay_playback != 0) {
            /* Holding Cross slows a replay down. */
            if (pad.held & PAD_CROSS) {
                DrawSync(0);
                VSync(0);
                VSync(0);
                /* PORT: on the PSX the frame's work (over one vblank) runs after these waits, so Draw_All(2)
                 * finds three vblanks gone: 3 a frame, two-thirds speed. Here the work takes no time and the
                 * waits would only replace Draw_All's, so this vblank stands in for it. */
                VSync(0);
            }
            if (pad.pressed & PAD_TRIANGLE)
                quit_flag = 1;
        }
        if (overlay_state < 1) {
            AI_Update_Strip_Info();
            if (demo_mode == 0) {
                if (pit_in == 0)
                    Control_Player_Car_From_Pad(current_player_car, &pad);
                else
                    Drive_Car_Into_Pit(current_player_car);
                for (int car = 1; car < num_cars; car++)
                    AI_Com_Server(car);
            } else {
                for (int car = 0; car < num_cars; car++)
                    AI_Com_Server(car);
            }
        }
        for (int car = 0; car < num_cars; car++)
            Car_Movement(car);
        Do_Car_Collisions();
        Get_Race_Positions();
        Car_Camera(camera_car);
        Strip_Trigger_Handler();
        Calc_Track_Positions();
        Get_Race_Positions();
        Recover_Stuck_Cars();
        if (quit_flag != 0)
            break;
        for (int car = 0; car < current_player_car; car++)
            Draw_Car(car);
        if (camera_mode != 0)
            Draw_Player_Car_Near(current_player_car);
        for (int car = current_player_car + 1; car < num_cars; car++)
            Draw_Car(car);
        Draw_Scene_Blocks();
        Draw_Sky();
        if (race_timers_on != 0)
            Update_Race_Timers(camera_car);
        DrawLensFlare();
        Display_Position_Pointers();
        if (cars_finished > 0)
            DrawFlagObject();
        Update_Particles();
        Update_Debris();
        CheckPointScoring();
        Update_Race_Commentary();
        Update_Race_Sounds();
        if (demo_mode == 0)
            Camera_Pad_Control(camera_car);
        Update_Damage_Indicator(camera_car);
        Update_Flying_Objects();
        for (int car = 0; car < num_cars; car++)
            if (car_blocks[car].part_loss_timer > 0)
                car_blocks[car].part_loss_timer--;
        Finish_Object_Block_Decrunch();
        Draw_All(2);
        if (demo_mode == 0 && (pad.held & (PAD_START | PAD_SELECT)) == (PAD_START | PAD_SELECT)) {
            if (++quit_hold > QUIT_HOLD_FRAMES) {
                yes_quit = 1;
                break;
            }
        } else {
            quit_hold = 0;
        }
        if (cars_finished > FINISHED_TO_END)
            quit_flag = 1;
    }
    if (replay_playback == 0) {
        if (overlay_hold == 0 && overlay_state < 1)
            Terminate_Replay(&pad);
        else
            Terminate_Replay_Bodge(&pad);
    }
    Sound_Stop();
    SetSp(saved_stack);
    return yes_quit;
}
