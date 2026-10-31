#include "game/anim.h"

#include "game/main_init.h"

Animation video_anim;
Animation flag_anims[2];
Animation driver_anim;
Animation goose_anim;
Animation flash_anims[3];
Animation flame_anim;
Animation dollar_cluts;
Animation safe_cluts;
Animation casino_cluts;
Animation j_cluts;
Animation twist_cluts[2];

static void find_sprites(Animation *a, const char *const *names, int count)
{
    for (int i = 0; i < count; i++)
        a->sprite[i] = Search_For_Sprite(names[i]);
    a->sprite[count] = NULL;
}

void Init_CLUT_Animation(void)
{
    static const char *const dollar[3] = {"DOLLAR", "DOLCLUT1", "DOLCLUT2"};                 /* 0x8006EF58 */
    static const char *const safe[4] = {"SAFENEON", "SAFCLUT1", "SAFCLUT2", "SAFCLUT3"};   /* 0x8006EF68 */
    static const char *const casino[10] = {"CASNEON", "CASCLUT1", "CASCLUT2", "CASCLUT3", "CASCLUT4",
                                           "CASCLUT5", "CASCLUT6", "CASCLUT7", "CASCLUT8", "CASCLUT9"}; /* 0x8006EF7C */
    static const char *const j[3] = {"J", "JCLUT1", "JCLUT2"};                              /* 0x8006EFA8 */
    static const char *const twist1[3] = {"TWIST1", "TWCLUT1", "TWCLUT2"};                  /* 0x8006EFB8 */
    static const char *const twist2[3] = {"TWIST2", "TWCLUT1", "TWCLUT2"};                  /* 0x8006EFC8 */
    if (current_level != LEVEL_LIBERTY_CITY)
        return;
    find_sprites(&dollar_cluts, dollar, 3);
    find_sprites(&safe_cluts, safe, 4);
    find_sprites(&casino_cluts, casino, 10);
    find_sprites(&j_cluts, j, 3);
    find_sprites(&twist_cluts[0], twist1, 3);
    find_sprites(&twist_cluts[1], twist2, 3);
}

void Init_Texture_Animation(void)
{
    static const char *const video[16] = {"VIDANI1", "VIDANI0", "VIDANI2", "VIDANI3", "VIDANI4", "VIDANI5",
                                          "VIDANI6", "VIDANI7", "VIDANI8", "VIDANI9", "VIDANI10", "VIDANI11",
                                          "VIDANI12", "VIDANI13", "VIDANI14", "VIDANI15"}; /* 0x8006EE28 */
    static const char *const flags_b[5] = {"DUMBB", "FLAG1", "FLAG2", "FLAG3", "FLAG4"}; /* 0x8006EEA4 */
    static const char *const flags_a[5] = {"DUMBA", "FLAG3", "FLAG4", "FLAG1", "FLAG2"}; /* 0x8006EEBC */
    static const char *const driver[3] = {"ZADRV", "ZDRV", "ZDRV2"};                     /* 0x8006EE6C */
    static const char *const goose[9] = {"GOSEDUMB", "GOOSE1", "GOOSE2", "GOOSE3", "GOOSE4",
                                         "GOOSE5", "GOOSE6", "GOOSE7", "GOOSE8"};        /* 0x8006EE7C */
    static const char *const flash[3][8] = {
        {"DUMB1", "FLASH6", "FLASH6", "FLASH5", "FLASH4", "FLASH3", "FLASH2", "FLASH1"}, /* 0x8006EED4 */
        {"DUMB2", "FLASH4", "FLASH3", "FLASH2", "FLASH1", "FLASH6", "FLASH6", "FLASH5"}, /* 0x8006EEF8 */
        {"DUMB3", "FLASH2", "FLASH1", "FLASH6", "FLASH6", "FLASH5", "FLASH4", "FLASH3"}, /* 0x8006EF1C */
    };
    static const char *const flame[5] = {"FLAMDUMB", "FLAME1", "FLAME2", "FLAME3", "FLAME4"}; /* 0x8006EF40 */
    switch (current_level) {
    case 1:
        find_sprites(&video_anim, video, 16);
        break;
    case 5:
        find_sprites(&flag_anims[0], flags_b, 5);
        find_sprites(&flag_anims[1], flags_a, 5);
        break;
    case 6:
        find_sprites(&driver_anim, driver, 3);
        find_sprites(&goose_anim, goose, 9);
        for (int i = 0; i < 3; i++)
            find_sprites(&flash_anims[i], flash[i], 8);
        break;
    case 11:
        find_sprites(&flame_anim, flame, 5);
        break;
    }
}
