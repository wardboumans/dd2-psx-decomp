# Findings, quirks and open checks

What was learnt about the PSX game while porting it, where the port deliberately differs, and what has not
been checked against the real game yet. Differences in the code are marked `PORT:`; list new ones here.

## Architecture

- `src/psx/` replaces Sony's Psy-Q SDK: `libgte` software GTE, `libgpu` VRAM 1024×512 with OT and a primitive
  rasterizer, `libspu` voices/ADPCM, `libcd` CdSearchFile/CdRead on `assets/` plus CD-DA, `libpad` pad from SDL
  gamepad/keys, `libcard` memory card files, `libetc` VSync/callbacks, `libapi` events/heap, `libpress` MDEC.
- Overlays (FRONTEND, GENERAL, RESULTS) are normal source files linked into the exe; overlay loads are no-ops.
  Loads to fixed addresses (`DRIVERS.BIN` → `0x8018C000`, `InitHeap(.., 0xE0000)`, scratchpad `0x1F800000`)
  go to buffers.
- The intro and outro movies are separate executables on the PSX (NTSCINTR.EXE, NTSCXTRO.EXE and the PAL ones,
  Sony's streaming movie sample); their code is `game/movie.c`, run in-process by `Play_Intro`/`Play_Xtro`.

## Timing

- Races run one game frame per two vblanks (`Draw_All(2)`: 29.97 a second on NTSC), measured over 600 demo
  frames. A `VSync(-1)` that slept a vblank on every repeated read once made `Draw_All(2)` take 3 vblanks
  (races at two-thirds speed); `VSync(-1)` must only read the counter.
- `Play_Game` does exactly one game step per drawn frame, never a number derived from the measured frame time.
- Replay slow motion (hold Cross) waits two vblanks before the frame's work; on the PSX that work takes over a
  vblank, so a frame takes 3 vblanks (two-thirds speed). The port's work takes no time, so it waits a third
  vblank in its place (`PORT:` in `play_game.c`). Assumes a replay frame needs one to two vblanks of CPU, as a
  20-car race does.
- Movies stream at double speed: 150 sectors a second, on the audio clock.

## Sound

- SPU pitch `0x1000` = 44.1 kHz (sample rate × pitch / 4096); the voices use the hardware's Gaussian
  interpolation, ADSR and loop flags (`tests/spu_test.c`).
- `Update_Race_Sounds` gives engines to the player and the 7 nearest cars: pitch by speed and gear, Doppler,
  distance, pan.
- The game sets CdMix to 255 (about 2×): CD music clips like on hardware (≈1.5% of samples on the menu track),
  and so does the movies' XA sound.
- Movie XA sound goes to 44.1 kHz by linear interpolation, not the hardware's 7-phase filter.
- The game has not been seen turning on volume sweeps, reverb, noise or pitch modulation, so `libspu` does not
  emulate them; add them if it does.
  `Sound_Reverb_On`/`Off`, `Sound_Set_Reverb` and `Sound_Set_Reverb_Depth` (0x8004E7A0..) have no callers and
  were not ported.
- `Play_Sound` returns 0/1 instead of a channel when the sample rate is too low for one frame (original bug,
  kept).
- `Allocate_Sound_Effect` drops an effect when no channel is free; the original writes before the table
  (`PORT:`).

## Original quirks kept

- Car-to-car collisions turn the second car's roll instead of its heading; the head-on side table is always
  indexed 0; `Knock_Car_Pitch` takes the edge from the first table for both directions.
- The flying-object ring is stepped with `& 0x17`, so only records 16..23 are used (eight parts at once).
- Dents of the computer cars go through the near model's shared mesh, whose vertices point at the car drawn
  last (`Dent_Car_Zone`, `Boot_Lost_Geometry` for car 0), as the PSX code reads.
- The configuration save puts `champ_info` 4 bytes into the last season's statistics: a load gives those bytes
  back as the first name's letters.
- The statistics pages number the seasons from `seasons_played + 1` once more than five were played (the oldest
  kept is `seasons_played - 3`).
- `rand` is the BIOS generator and the game never seeds it.

## Port deviations (`PORT:`)

- `Print_Bank_Fade` with transparency mode 2 reads uninitialised registers in the original; the port does not
  fade.
- `Place_Cars_On_Grid` seeds the fd position.
- The bowl AI refuses a target pick of 20 (one past the cars) and picks the player when its car list is used up.
- The load/save screen reads the block before a card's table when the chosen card is unformatted or gone
  (block -1); the port takes it as an empty block (`loadsave.c`).
- `OpenEvent` returns the open event for the same class and kind (`libapi.c`): the game opens the card events
  again after every movie.
- Movie decoding is not bit-exact: the IDCT's intermediate rounding is ours (VLC, quantisation and IDCT tables
  are the PSX ones).

## Port decisions

- Memory cards are 128 KB raw card images (the format emulators use) in the user's data folder
  (`SDL_GetPrefPath`): `memcard1.mcd` is made, formatted, when missing; slot 2 is only there when
  `memcard2.mcd` exists. Test runs use a card in memory only.
- Card operations finish at once (the PSX takes real time per access). So the main menu's "Searching For
  Config" notice (`Show_Searching_For_Config`, FRONTEND 0x801CAFC0) only flashes for its 2 drawn frames: on the
  PSX the last of them stays on screen while `Load_First_Config` reads the cards. Kept as is.
- Save files keep the PSX layout byte for byte, so real card images load. Replay saves store the end of the
  script as a PSX address (0x8009581C + offset); the port converts it and keeps a damaged one inside the script.
- `--negcon` makes pad 1 a NeGcon (twist from the left stick, I/II from the triggers, L from the left shoulder);
  the default is a digital pad. Test runs skip the intro unless `--intro` is given.

## Code and data findings

- Many PSX globals are initialised data, not BSS (`0x80010000..0x80079FFF` in the exe): read the exe image
  (`tools/exe_data.py`) before declaring one without an initialiser. Zeroing them broke the camera mode (no
  grid fly-in), the car shadow sprite (subtractive grey 0x40; zeroed it drew an opaque dark quad), the particle
  and wheel sprites and the 2:30.00 track records.
- Not ported because the PSX never runs them: `Show_Analog_Screen` and `Show_NeGcon_Screen` (FRONTEND
  0x801CC1E0, 0x801CE848; `Select_Control_Method` turns a NeGcon/analog type into "no pad" first),
  `Labelled_Button_Pressed` (0x801D09A0, no callers). Ghidra splits two pieces off `Centre_Screen_Page`
  (FRONTEND 0x801C72D0, 0x801C7304); they are not functions.
- `Decompress`, `Set_Glyph_Quad_Attrs` and `Draw_Font_Poly` sit with the draw-buffer code in the exe and are in
  `object.c`; that block may be an original module of its own.
- Names: struct gaps no PSX code touches are `unused_XX` (checked by displacement scans in Ghidra and the level
  files), vector fourth words `pad` or `[3]`. What is left in hex is tuning, geometry and table data.

## Checking changes

- Refactors are checked by running the same build twice: a demo race, a scripted player race and the menu give
  byte-identical screenshots and `--wav` recordings before and after.

## Open checks against the real game

- Time a lap in DuckStation to confirm the race speed.
- Compare a recording (`dd2 --wav out.wav --after N`) with an emulator recording of the same moment; engine
  sounds have only been checked with a channel trace, not by ear.
- Compare the music and XA clipping (CdMix 255) by ear.
- Whether a hit computer car or another car shows the dent (shared mesh, above).
- Debris and lost parts were seen in a run (a bonnet flies off) but not compared.
- The boot seed of `rand` in `psx/libc.c` (0x24040001) is unverified.
- Front-end screens written but only partly tried by hand: race mode/type, car and track select, CD player,
  controller and sound config, centre screen, info menu, fastest laps, credits, name entry and cheat names.

## Ideas not started

- Widescreen, a hi-res renderer behind `libgpu`, options.
