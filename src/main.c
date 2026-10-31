/* Entry point.
 *
 * Usage: dd2 [assets dir] [--screenshot file.bmp] [--vram file.bmp] [--wav file.wav] [--after seconds]
 *   assets dir    the game data made by tools/make_assets.py (default: assets)
 *   --screenshot  save the displayed picture and exit (for testing)
 *   --vram        save all of VRAM at the same moment (for testing)
 *   --wav         record the sound output until then (for testing)
 *   --after       when to save them and exit (default 2 seconds)
 * Any of these makes a test run: no sound to the speakers, real keyboard and gamepad ignored.
 *   --keys        scripted pad presses for tests: "seconds:button,...", buttons up down left right
 *                 cross circle square triangle start select l1 r1 l2 r2 (e.g. "8:cross,9.5:right");
 *                 "seconds+held:button" holds it that many seconds (e.g. "14+5:cross")
 *
 *   --negcon      the pad is a NeGcon: steering from the left stick, I and II from the triggers
 *   --intro       play the intro movie in a test run too (other runs always play it)
 *   --outro       play the outro movie first (for testing; the game plays it after a championship)
 *
 * Runs the original start-up: Init_Main, the intro movie (Play_Intro), Init_Front_End and the menus
 * (Front_End, which never returns). */
#include "game/frontend.h"
#include "game/frontend_init.h"
#include "game/main_init.h"
#include "platform/platform.h"
#include "psx/libcd.h"
#include "psx/libetc.h"
#include "psx/libgpu.h"
#include "psx/libpad.h"
#include "system/controller.h"

#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VBLANKS_PER_SECOND 60

static const char *screenshot, *vram_dump, *wav_path;
static bool scripted;

/* "8:cross,9.5:right" -> scripted presses at those many seconds. */
static void parse_keys(const char *keys)
{
    static const struct {
        const char *name;
        unsigned bit;
    } buttons[] = {
        {"select", PAD_SELECT}, {"start", PAD_START}, {"up", PAD_UP}, {"right", PAD_RIGHT}, {"down", PAD_DOWN},
        {"left", PAD_LEFT}, {"l2", PAD_L2}, {"r2", PAD_R2}, {"l1", PAD_L1}, {"r1", PAD_R1},
        {"triangle", PAD_TRIANGLE}, {"circle", PAD_CIRCLE}, {"cross", PAD_CROSS}, {"square", PAD_SQUARE},
    };
    char copy[1024];
    snprintf(copy, sizeof(copy), "%s", keys);
    for (char *item = strtok(copy, ","); item; item = strtok(NULL, ",")) {
        char *colon = strchr(item, ':');
        if (!colon)
            continue;
        *colon = '\0';
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
            if (strcmp(colon + 1, buttons[i].name) == 0)
                pad_script((int)(atof(item) * VBLANKS_PER_SECOND),
                           strchr(item, '+') ? (int)(atof(strchr(item, '+') + 1) * VBLANKS_PER_SECOND) : 0,
                           buttons[i].bit);
    }
}

static void save_and_exit(void)
{
    int ok = 1;
    if (screenshot)
        ok &= gpu_save_display(screenshot);
    if (vram_dump)
        ok &= gpu_save_vram(vram_dump);
    printf(ok ? "saved\n" : "could not save\n");
    platform_shutdown();
    exit(ok ? 0 : 1);
}

int main(int argc, char *argv[])
{
    const char *assets = "assets";
    double after = 2.0;
    bool intro = false, outro = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            screenshot = argv[++i];
        else if (strcmp(argv[i], "--vram") == 0 && i + 1 < argc)
            vram_dump = argv[++i];
        else if (strcmp(argv[i], "--wav") == 0 && i + 1 < argc)
            wav_path = argv[++i];
        else if (strcmp(argv[i], "--after") == 0 && i + 1 < argc)
            after = atof(argv[++i]);
        else if (strcmp(argv[i], "--negcon") == 0)
            pad_set_negcon(true);
        else if (strcmp(argv[i], "--intro") == 0)
            intro = true;
        else if (strcmp(argv[i], "--outro") == 0)
            outro = true;
        else if (strcmp(argv[i], "--keys") == 0 && i + 1 < argc) {
            parse_keys(argv[++i]);
            scripted = true;
        }
        else
            assets = argv[i];
    }

    if (!cd_open_assets(assets))
        return 1;
    if (!platform_init("Destruction Derby 2", screenshot || vram_dump || wav_path || scripted))
        return 1;
    if (wav_path && !platform_audio_record(wav_path))
        return 1;
    if (screenshot || vram_dump || wav_path)
        etc_after_vblanks((int)(after * VBLANKS_PER_SECOND), save_and_exit);

    bool test = screenshot || vram_dump || wav_path || scripted;
    Init_Main();
    if (outro)
        Play_Xtro();
    if (intro || !test)
        Play_Intro();
    Init_Front_End();
    Front_End();
    return 0;
}
