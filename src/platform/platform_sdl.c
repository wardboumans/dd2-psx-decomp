#include "platform/platform.h"

#include <SDL3/SDL.h>
#include <stdio.h>

#define WINDOW_SCALE 3

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *screen; /* as large as VRAM, so any display mode fits */
static SDL_Texture *screen24; /* the 24-bit display mode, made when first needed */
static SDL_Gamepad *gamepad;
static SDL_AudioStream *audio;
static bool test_run;
static FILE *wav;
static uint32_t wav_bytes;

/* More than this much queued sound (about 6 vblanks) means the game ran ahead; drop it. */
#define AUDIO_MAX_QUEUED (6 * 736 * 4)

/* PSX digital pad bits. */
enum {
    PSX_SELECT = 1 << 0, PSX_L3 = 1 << 1, PSX_R3 = 1 << 2, PSX_START = 1 << 3,
    PSX_UP = 1 << 4, PSX_RIGHT = 1 << 5, PSX_DOWN = 1 << 6, PSX_LEFT = 1 << 7,
    PSX_L2 = 1 << 8, PSX_R2 = 1 << 9, PSX_L1 = 1 << 10, PSX_R1 = 1 << 11,
    PSX_TRIANGLE = 1 << 12, PSX_CIRCLE = 1 << 13, PSX_CROSS = 1 << 14, PSX_SQUARE = 1 << 15,
};

static const struct {
    SDL_Scancode key;
    uint16_t bit;
} key_map[] = {
    {SDL_SCANCODE_UP, PSX_UP}, {SDL_SCANCODE_DOWN, PSX_DOWN},
    {SDL_SCANCODE_LEFT, PSX_LEFT}, {SDL_SCANCODE_RIGHT, PSX_RIGHT},
    {SDL_SCANCODE_RETURN, PSX_START}, {SDL_SCANCODE_BACKSPACE, PSX_SELECT},
    {SDL_SCANCODE_X, PSX_CROSS}, {SDL_SCANCODE_SPACE, PSX_CROSS},
    {SDL_SCANCODE_C, PSX_CIRCLE}, {SDL_SCANCODE_Z, PSX_SQUARE}, {SDL_SCANCODE_S, PSX_TRIANGLE},
    {SDL_SCANCODE_Q, PSX_L1}, {SDL_SCANCODE_E, PSX_R1},
    {SDL_SCANCODE_1, PSX_L2}, {SDL_SCANCODE_3, PSX_R2},
};

static const struct {
    SDL_GamepadButton button;
    uint16_t bit;
} gamepad_map[] = {
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PSX_UP}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PSX_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PSX_LEFT}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PSX_RIGHT},
    {SDL_GAMEPAD_BUTTON_START, PSX_START}, {SDL_GAMEPAD_BUTTON_BACK, PSX_SELECT},
    {SDL_GAMEPAD_BUTTON_SOUTH, PSX_CROSS}, {SDL_GAMEPAD_BUTTON_EAST, PSX_CIRCLE},
    {SDL_GAMEPAD_BUTTON_WEST, PSX_SQUARE}, {SDL_GAMEPAD_BUTTON_NORTH, PSX_TRIANGLE},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PSX_L1}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PSX_R1},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, PSX_L3}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, PSX_R3},
};

bool platform_init(const char *title, bool test)
{
    test_run = test;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return false;
    }
    if (!SDL_CreateWindowAndRenderer(title, 320 * WINDOW_SCALE, 240 * WINDOW_SCALE,
                                     SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer: %s", SDL_GetError());
        return false;
    }
    /* The game paces itself on the emulated 59.94 Hz vblank, not on the monitor. */
    SDL_SetRenderVSync(renderer, 0);
    SDL_SetRenderLogicalPresentation(renderer, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XBGR1555, SDL_TEXTUREACCESS_STREAMING, 1024, 512);
    if (!screen) {
        SDL_Log("SDL_CreateTexture: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(screen, SDL_SCALEMODE_NEAREST);
    if (!test_run) {
        SDL_AudioSpec spec = {SDL_AUDIO_S16LE, 2, 44100};
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (audio)
            SDL_ResumeAudioStreamDevice(audio);
        else
            SDL_Log("no sound: %s", SDL_GetError());
    }
    return true;
}

void platform_shutdown(void)
{
    if (gamepad)
        SDL_CloseGamepad(gamepad);
    if (audio)
        SDL_DestroyAudioStream(audio);
    if (wav) {
        uint8_t sizes[4];
        uint32_t riff = wav_bytes + 36;
        for (int i = 0; i < 4; i++)
            sizes[i] = (uint8_t)(riff >> (i * 8));
        fseek(wav, 4, SEEK_SET);
        fwrite(sizes, 1, 4, wav);
        for (int i = 0; i < 4; i++)
            sizes[i] = (uint8_t)(wav_bytes >> (i * 8));
        fseek(wav, 40, SEEK_SET);
        fwrite(sizes, 1, 4, wav);
        fclose(wav);
        wav = NULL;
    }
    if (screen24)
        SDL_DestroyTexture(screen24);
    SDL_DestroyTexture(screen);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void platform_present(const uint16_t *pixels, int w, int h, int pitch)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (pixels) {
        SDL_Rect area = {0, 0, w, h};
        SDL_FRect src = {0, 0, (float)w, (float)h};
        SDL_UpdateTexture(screen, &area, pixels, pitch * (int)sizeof(uint16_t));
        SDL_RenderTexture(renderer, screen, &src, NULL);
    }
    SDL_RenderPresent(renderer);
}

bool platform_save_bmp(const char *path, const uint16_t *pixels, int w, int h, int pitch)
{
    SDL_Surface *surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XBGR1555, (void *)pixels,
                                                 pitch * (int)sizeof(uint16_t));
    if (!surface)
        return false;
    bool ok = SDL_SaveBMP(surface, path);
    SDL_DestroySurface(surface);
    return ok;
}

void platform_present_rgb24(const uint8_t *pixels, int w, int h, int pitch)
{
    if (!screen24) {
        screen24 = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 682, 512);
        if (!screen24) {
            platform_present(NULL, 0, 0, 0);
            return;
        }
        SDL_SetTextureScaleMode(screen24, SDL_SCALEMODE_NEAREST);
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_Rect area = {0, 0, w, h};
    SDL_FRect src = {0, 0, (float)w, (float)h};
    SDL_UpdateTexture(screen24, &area, pixels, pitch);
    SDL_RenderTexture(renderer, screen24, &src, NULL);
    SDL_RenderPresent(renderer);
}

bool platform_save_bmp_rgb24(const char *path, const uint8_t *pixels, int w, int h, int pitch)
{
    SDL_Surface *surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGB24, (void *)pixels, pitch);
    if (!surface)
        return false;
    bool ok = SDL_SaveBMP(surface, path);
    SDL_DestroySurface(surface);
    return ok;
}

bool platform_pump_events(void)
{
    SDL_Event event;
    bool quit = false;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT)
            quit = true;
        else if (event.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad)
            gamepad = SDL_OpenGamepad(event.gdevice.which);
        else if (event.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad &&
                 SDL_GetGamepadID(gamepad) == event.gdevice.which) {
            SDL_CloseGamepad(gamepad);
            gamepad = NULL;
        }
    }
    return quit;
}

uint16_t platform_pad_buttons(void)
{
    uint16_t buttons = 0;
    if (test_run)
        return 0;
    const bool *keys = SDL_GetKeyboardState(NULL);
    for (size_t i = 0; i < SDL_arraysize(key_map); i++)
        if (keys[key_map[i].key])
            buttons |= key_map[i].bit;
    if (gamepad) {
        for (size_t i = 0; i < SDL_arraysize(gamepad_map); i++)
            if (SDL_GetGamepadButton(gamepad, gamepad_map[i].button))
                buttons |= gamepad_map[i].bit;
        if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0x4000)
            buttons |= PSX_L2;
        if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0x4000)
            buttons |= PSX_R2;
    }
    return buttons;
}

void platform_pad_analog(PlatformAnalog *a)
{
    a->twist = 0x80;
    a->i = a->ii = a->l = 0;
    if (test_run || !gamepad)
        return;
    int x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    a->twist = (uint8_t)((x + 0x8000) >> 8);
    a->i = (uint8_t)(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >> 7);
    a->ii = (uint8_t)(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >> 7);
    a->l = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ? 0xff : 0;
}

/* 44.1 kHz, 16-bit stereo; the two sizes are filled in at shutdown. */
bool platform_audio_record(const char *path)
{
    static const uint8_t header[44] = {
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
        0x44, 0xac, 0, 0, 0x10, 0xb1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0,
    };
    wav = fopen(path, "wb");
    if (!wav)
        return false;
    fwrite(header, 1, sizeof(header), wav);
    wav_bytes = 0;
    return true;
}

void platform_audio_push(const int16_t *stereo, int frames)
{
    if (wav) {
        for (int i = 0; i < frames * 2; i++) {
            uint8_t le[2] = {(uint8_t)stereo[i], (uint8_t)((uint16_t)stereo[i] >> 8)};
            fwrite(le, 1, 2, wav);
        }
        wav_bytes += (uint32_t)frames * 4;
    }
    if (audio && SDL_GetAudioStreamQueued(audio) < AUDIO_MAX_QUEUED)
        SDL_PutAudioStreamData(audio, stereo, frames * 4);
}

uint64_t platform_ticks_ns(void)
{
    return SDL_GetTicksNS();
}

void platform_sleep_until_ns(uint64_t ticks_ns)
{
    uint64_t now = SDL_GetTicksNS();
    if (ticks_ns > now)
        SDL_DelayPrecise(ticks_ns - now);
}

bool platform_card_path(int slot, char *path, int size)
{
    if (test_run)
        return false;
    char *dir = SDL_GetPrefPath("dd2-port", "Destruction Derby 2");
    if (!dir)
        return false;
    SDL_snprintf(path, (size_t)size, "%smemcard%d.mcd", dir, slot + 1);
    SDL_free(dir);
    return true;
}
