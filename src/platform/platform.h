/* The only interface between the PSX library replacement and SDL3. */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

/* `test`: an automated run; no sound to the speakers and no real keyboard or gamepad input. */
bool platform_init(const char *title, bool test);
void platform_shutdown(void);

/* Show a w x h block of 15-bit PSX pixels (row stride `pitch` pixels); NULL shows black. */
void platform_present(const uint16_t *pixels, int w, int h, int pitch);

/* Save a w x h block of 15-bit PSX pixels as a BMP file. */
bool platform_save_bmp(const char *path, const uint16_t *pixels, int w, int h, int pitch);

/* The same for the 24-bit display mode: R, G, B bytes, row stride `pitch` bytes. */
void platform_present_rgb24(const uint8_t *pixels, int w, int h, int pitch);
bool platform_save_bmp_rgb24(const char *path, const uint8_t *pixels, int w, int h, int pitch);

/* Handle window events; returns true when the user closed the window. */
bool platform_pump_events(void);

/* The buttons held on the keyboard and the first gamepad, in PSX digital pad bit order
 * (bit 0 SELECT ... bit 15 SQUARE), 1 = held. */
uint16_t platform_pad_buttons(void);

/* The analog controls a NeGcon needs, 0x00..0xFF: the twist (0x80 centred) from the left stick, I and II
 * from the right and left triggers, L from the left shoulder. All idle in a test run. */
typedef struct {
    uint8_t twist, i, ii, l;
} PlatformAnalog;

void platform_pad_analog(PlatformAnalog *a);

/* Queue `frames` frames of 44.1 kHz stereo sound. */
void platform_audio_push(const int16_t *stereo, int frames);

/* Also write all sound to a WAV file (for testing); finished by platform_shutdown. */
bool platform_audio_record(const char *path);

/* Where memory card image `slot` (0, 1) is kept: the user's data folder. False in a test run, whose
 * cards live only in memory. */
bool platform_card_path(int slot, char *path, int size);

uint64_t platform_ticks_ns(void);
void platform_sleep_until_ns(uint64_t ticks_ns);

#endif
