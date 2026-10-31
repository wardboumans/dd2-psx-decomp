# DD2 PSX decomp → portable C11 / SDL3

This file says **how to work**. `docs/NOTES.md` has what was learnt about the game: timing and sound findings,
original quirks kept, port deviations and what is still to be checked against the real game.

## Goal in one paragraph

Turn the PSX version of Destruction Derby 2 (`SCUS_943.50` + overlays) into readable, portable C11 on SDL3 that
plays exactly like the PSX game. It is **not** a matching decomp. The acceptance tests: every car, AI cars
included, has its engine sound; sound effects play at the right pitch; the game runs at the PSX's speed. Every
decision about sound, timing or speed follows the PSX code.

## Sources of truth, in order

1. **The PSX binary in Ghidra** (`ghidra/dd2-psx.gpr`, reached through the Ghidra MCP). It decides behaviour.
2. **The real game** in an emulator such as DuckStation, with the disc in `game/`, when behaviour is in doubt.
3. **Game data.** The port reads `assets/` (CD files plus `music/trackNN.wav`, made by `tools/make_assets.py`
   from the disc image in `game/`); `src/psx/libcd.c` serves it to the game as a virtual disc. `game/extracted/`
   and `game/dirinfo/` (unpacked `DIRINFO`) are for browsing. Never modify any of these.

## Repo layout

| Path | What |
|---|---|
| `CMakeLists.txt`, `src/`, `tests/` | The port. `src/game`, `src/graphics`, `src/system`, `src/sound` (DD2 code), `src/psx` (our Psy-Q replacement), `src/platform` (the only SDL3 code) |
| `docs/STRUCTURE.md` | PSX module map: address ranges → source file |
| `docs/CONTROLS.md` | Keyboard, gamepad and in-game button mapping |
| `tools/` | Python tools for the game data. Run Python with `-I`. Throwaway scripts go in the session scratchpad, not here. |
| `docs/NOTES.md` | Findings, quirks, port deviations, open checks. Keep it up to date. |
| `docs/archive/` | The finished decomp stage (function mapping data and its scripts). Kept for reference; not maintained. |

Functions and globals carry their PSX address in a comment (`/* 0x80050BA0 */`): grep for an address to find
its C, and look it up in Ghidra to check it.

## Code rules

- C11 only, no compiler extensions; must build on MSVC, GCC and Clang, 64-bit. `stdint.h` types for anything
  whose size matters.
- **File data keeps its exact on-disk layout**: offsets stay `uint32_t`, resolved through a helper at use;
  `_Static_assert(sizeof(T) == N)` on every file struct. Runtime-only data uses native pointers.
- **Primitives**: every GPU packet starts with a native ordering-table link, not the PSX 24-bit `P_TAG`. Step
  primitive buffers with `sizeof`, never with byte counts copied from the assembly (`+= 0x28` is a bug).
- **Fixed PSX addresses** (overlay load addresses, `0x8018C000`, the heap, scratchpad `0x1F800000`) become globals or
  buffers. Overlay loads become no-ops; overlay code is ordinary source.
- **Game code calls Psy-Q names** (`DrawOTag`, `RotTransPers`, `SpuSetKey`, `CdRead` ...). Implement those in
  `src/psx/` with Psy-Q semantics. Only `src/platform/` includes SDL headers.
- **Timing**: logic runs on the emulated 59.94 Hz vblank (`VSync`, `src/psx/libetc.c`). Never tie game speed to
  the monitor, the frame time or the audio callback.
- **Sound**: emulate the SPU (ADPCM decode, pitch `0x1000` = 44.1 kHz, ADSR, voice allocation) faithfully. Do not
  "simplify" the voice logic: engine sounds for every car depend on it.
- Match the surrounding code's style; comment *why*, not what. No dead code, no `#if 0` blocks.
- Mark deliberate differences from the PSX behaviour with `PORT:` in the code and list them in `docs/NOTES.md`.

## Build

```
cmake --preset msvc                    # only when CMake files changed or build-msvc/ is missing
cmake --build build-msvc --config Debug 2>&1 | grep -E "warning|error" | grep -v _deps
ctest --test-dir build-msvc -C Debug --output-on-failure
```

Zero warnings in our code on MSVC `/W4`. Everything except `src/main.c` builds into the `dd2_core` library, which the game and the tests link. Add a test
under `tests/` (and an `add_test` line) for library behaviour with exact rules, as `gpu_test.c` does
for the GPU.

The exe is `build-msvc/Debug/dd2.exe`; run it from the repo root (it reads `assets/`, or the folder
given as first argument). It opens a window; run it with `timeout` when testing so it can't hang the session.
`--screenshot file.bmp` saves the displayed picture after two seconds and exits: use it to check output.
SDL3 3.4.18 is fetched and built by CMake (pinned hash); don't add other dependencies without asking.

## Ghidra (MCP) notes

- Program `SCUS_943.50`. Blocks: exe `.text` `0x800116EC-0x8006DC43`; FRONTEND.BIN `0x801C4000`; GENERAL.BIN
  `0x801E0000`; RESULTS.BIN is the overlay block `ov1` at `0x801C4000` (addresses look like `ov1::801c5058`).
  FRONTEND and RESULTS share addresses, so calls from the exe into `0x801C....` resolve to FRONTEND only.
- Library functions were identified by the PSX loader plugin (`CdRead`, `SpuSetKey`, `RotTransPers` ...).
- `list_functions` output is too big to read inline; it gets saved to a file, parse that with Python.
- `run_script_inline` compiles Java: methods that call Ghidra APIs need `throws Exception`. Scripts land in
  Ghidra's user script folder; delete throwaway ones (and `*.java_failed`) when done.
- Save the program after a batch of renames.
- **Scripts that change the program (create/delete functions, disassemble, clear) are dangerous** and Ghidra's
  undo history does not reliably show MCP script transactions. A careless gap-filling script once turned the
  overlays' data into 5,500 bogus functions. Rules:
  1. Run every such script first with a dry-run flag that only prints what it would change, and read that output.
  2. Work on an explicit list of addresses, never "everything that looks like a gap".
  3. Never disassemble or create functions in areas that may hold data (strings, jump tables, the overlays' data
     areas), and never at unaligned addresses. Never `clearListing` defined data.
  4. Afterwards, compare the function count and per-function instruction counts with before, and report any
     difference.

## Keeping the user informed

The user wants steady progress, not a flood. After each piece of work: what was done, how it was checked,
what is still uncertain, what comes next. Don't claim behaviour is correct unless it was compared with the PSX code
or the real game. Keep `docs/NOTES.md` up to date.

Stop and ask the user, rather than guess, when a design decision isn't covered here or in `docs/NOTES.md`, or the PSX code can
be read more than one way in a way that changes gameplay.
