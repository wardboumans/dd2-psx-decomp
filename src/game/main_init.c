#include "game/main_init.h"
#include "game/movie.h"
#include "game/debris.h"
#include "game/denting.h"
#include "game/flag.h"
#include "game/lens_flare.h"
#include "game/overlays.h"
#include "game/camera.h"
#include "game/car_motion.h"
#include "game/pause.h"
#include "game/scene.h"
#include "game/track.h"
#include "game/frontend_init.h"
#include "game/car_draw.h"
#include "game/particles.h"
#include "game/play_game.h"
#include "game/replay.h"
#include "game/triggers.h"
#include "graphics/draw_face.h"
#include "system/mpe_heap.h"

#include "game/sound_fx.h"
#include "psx/libapi.h"
#include "psx/libetc.h"
#include "psx/libgpu.h"
#include "psx/libgte.h"
#include "psx/libcard.h"
#include "psx/libpad.h"
#include "psx/libspu.h"
#include "sound/cd.h"
#include "sound/sound.h"
#include "system/card.h"
#include "system/controller.h"
#include "system/debug.h"
#include "system/file.h"

#include "graphics/object.h"

#include <stdio.h>

int32_t music_volume = VOLUME_MAX;
int32_t sound_volume = VOLUME_MAX;
int32_t current_level;
FileRecord level_texture_files[MAX_LEVEL_TEXTURE_FILES];
FileRecord level_cluts_file;
FileRecord fi_levdat;
uint8_t *level_data;

/* Queues the level's texture table (TX0), as many texture data files as exist, and its CLUTs. */
void Set_Load_Textures(void)
{
    char name[24];
    sprintf(name, "LEV%X\\LEVEL.TX0", current_level);
    Add_Buffer_Load(name, &level_texture_files[0], FILE_TEXTURE_TABLE);
    for (int i = 1;; i++) {
        sprintf(name, "LEV%X\\LEVEL.TX%d", current_level, i);
        if (!directory_has_file(name))
            break;
        Add_Buffer_Load(name, &level_texture_files[i], FILE_TEXTURE_DATA);
    }
    sprintf(name, "LEV%X\\LEVEL.TXC", current_level);
    Add_Buffer_Load(name, &level_cluts_file, FILE_CLUTS);
}

void Init_Graphics(void)
{
    Setup_View_Norm(level_data_section(4) + 4);
}

void Queue_Level_Data(void)
{
    char name[24];
    sprintf(name, "LEV%X\\LEVEL.DAT", current_level);
    Add_Buffer_Load(name, &fi_levdat, FILE_PLAIN);
}

/* The original also adds the blob address to the 29 section offsets here (see level_data_section). */
void Relocate_Level_Data(void)
{
    level_data = fi_levdat.data;
}

void Init_Main(void)
{
    EnterCriticalSection();
    ResetCallback();
    pad_config[0] = 1;
    pad_config[1] = 2;
    Init_Pads();
    InitGeom();
    ExitCriticalSection();
    Profile_Init();
    Sound_Init();
    SpuSetReverb(SPU_OFF);
    CD_Open();
    VSync(0);
    DrawSync(0);
    SetDispMask(0);
    ResetGraph(0);
    SetGraphDebug(0);
    VSyncCallback(Sound_Timer_VSync_Callback);
    set_master_volume(sound_volume);
    Set_CD_Volume((short)music_volume);
    InitCardSystem();
    Read_Directory("\\DIRINFO.;1");
    Read_CD_Toc();
    Load_Game_Vags();
}

/* Init_Main again after a movie, which left the hardware as its own executable wanted it; the data
 * the game loaded is still there. */
void Init_Main_Without_Data(void)
{
    EnterCriticalSection();
    ResetCallback();
    pad_config[0] = 1;
    pad_config[1] = 2;
    Init_Pads();
    InitGeom();
    ExitCriticalSection();
    Profile_Init();
    Sound_Init();
    SpuSetReverb(SPU_OFF);
    CD_Open();
    VSync(0);
    DrawSync(0);
    SetDispMask(0);
    ResetGraph(0);
    SetGraphDebug(0);
    VSyncCallback(Sound_Timer_VSync_Callback);
    set_master_volume(sound_volume);
    Set_CD_Volume((short)music_volume);
    InitCardSystem();
}

/* The original loads the movie's executable (NTSCINTR.EXE or PALINTRO.EXE) to 0x800FF800, shuts the
 * hardware down and runs it with Exec; here the movie player is part of the program. */
static void play_movie(int movie)
{
    CD_Open();
    SpuQuit();
    SetDispMask(0);
    StopCallback();
    StopPAD();
    ResetGraph(3);
    StopCARD();
    Movie_Main(movie, pal_mode != 0);
    Init_Main_Without_Data();
    Load_Game_Vags();
}

void Play_Intro(void)
{
    play_movie(MOVIE_INTRO);
}

void Play_Xtro(void)
{
    play_movie(MOVIE_OUTRO);
}

void Sound_Timer_VSync_Callback(void)
{
    Sound_Timer();
}

uint8_t race_buffer[RACE_BUFFER_SIZE];

/* The race's level: display set-up, the level files loaded into race_buffer, then every race system
 * initialised. The original has Set_Load_Textures' and Queue_Level_Data's code inline here. */
void Init_Game(void)
{
    DrawSync(0);
    InitHeap((u_long *)heap_base, HEAP_SIZE);
    reset_sound_channels();
    Init_Primitive_Buffer(0x40000);
    if (current_level < FIRST_BOWL_LEVEL) {
        Allocate_OT(0x2200);
        clip_flag_mask = GTE_CLIP_FLAGS;
    } else {
        Allocate_OT(0x3200);
        clip_flag_mask = GTE_CLIP_FLAGS & ~GTE_FLAG_IR3; /* the bowls keep faces with IR3 saturated */
    }
    set_screen_centre(screen_width / 2, screen_height / 2);
    set_projection_distance(0xa0);
    SetDispMask(1);
    Set_Zclip(0, 0x8000);
    Set_Load_Textures();
    Queue_Level_Data();
    CD_Stop();
    Buffer_Load(race_buffer, Load_Completion_Status);
    Init_Race_Records();
    Relocate_Level_Data();
    Init_Graphics();
    Init_Overlays();
    Init_Track_Strips();
    Init_Track_Strip_Numbers();
    Init_Sky();
    InitialiseParticleSystem();
    Place_Cars_On_Grid();
    Init_Scene();
    Init_Scene_Objects();
    Reset_Car_Camera(camera_car);
    Init_Debris();
    InitialiseDenting();
    Init_Race_Sounds();
    Init_Damage_Indicator();
    Init_End_Race();
    Init_Replay_Script();
    Init_LensFlare();
    Init_Flying_Objects();
    Init_Pit_State();
    Init_Flag();
}
