# Destruction Derby 2 — portable C port

A native port of the PlayStation version of Destruction Derby 2 (USA, `SCUS-94350`), decompiled into readable,
portable C11 on SDL3. The goal is a game that plays exactly like the PSX original: the same race speed, an
engine sound for every car, and sound effects at the right pitch.

It is not a byte-matching decomp. The game code is rewritten from the PSX executable and its overlays; the parts
of Sony's PSX SDK the game calls (GPU, GTE, SPU, CD, pad, memory card, movie decoder) are reimplemented in
`src/psx/` on top of SDL3.

**No game data is included.** You need your own copy of the game as a disc image.

## Status

Every part of the game the PSX code reaches is ported: boot, intro and outro movies, the front end, practice and
championship races with AI, damage and pit stops, replays, results and league tables, memory card saves and CD
music. What is still to be compared against the real game is listed in [docs/NOTES.md](docs/NOTES.md).

## Building

Requirements: CMake 3.24 or newer and a C11 compiler. SDL3 is used when installed; otherwise CMake downloads and
builds the pinned release (3.4.18).

```
cmake --preset msvc          # Visual Studio 2026, x64
cmake --build build-msvc --config Debug
ctest --test-dir build-msvc -C Debug
```

There is also a `ninja` preset (`build-ninja/`) for GCC or Clang. MSVC is the compiler the port is tested with.

## Game data

Make the `assets/` folder from a `.cue`/`.bin` disc image of the USA version (needs Python 3, no extra packages):

```
python tools/make_assets.py "path/to/Destruction Derby 2 (USA).cue" assets
```

This copies the disc's files and writes the CD audio tracks to `assets/music/trackNN.wav`.

## Running

Run from the folder that holds `assets/` (or give the assets folder as the first argument):

```
build-msvc/Debug/dd2.exe
```

The keyboard and the first gamepad both work; see [docs/CONTROLS.md](docs/CONTROLS.md). Memory cards are saved
as `memcard1.mcd` (and `memcard2.mcd` when present) in SDL's per-user data folder, in the raw card format
emulators use.

Options:

| Option | What it does |
|---|---|
| `--negcon` | The pad is a NeGcon: steering from the left stick, I and II from the triggers |
| `--screenshot file.bmp` | Save the displayed picture and exit (test run) |
| `--vram file.bmp` | Save all of VRAM and exit (test run) |
| `--wav file.wav` | Record the sound output (test run) |
| `--after seconds` | When a test run saves and exits (default 2) |
| `--keys "8:cross,9.5:right"` | Scripted pad presses for a test run; `"14+5:cross"` holds for 5 seconds |
| `--intro` / `--outro` | Play the intro in a test run / play the outro first |

Test runs make no sound and ignore the real keyboard and gamepad.

## Layout

| Path | What |
|---|---|
| `src/game`, `src/graphics`, `src/system`, `src/sound` | The game code, one file per original module |
| `src/psx` | The PSX SDK replacement: software GPU and GTE, SPU, CD, pad, memory card, MDEC |
| `src/platform` | The only SDL3 code: window, audio, input, clock |
| `tests/` | Unit tests for the GPU, GTE, SPU, memory card and movie decoder |
| `tools/` | Python scripts to extract the disc and build `assets/` |
| `docs/` | Notes and quirks, controls, the PSX address → source file map, and an archive of the decomp stage |
