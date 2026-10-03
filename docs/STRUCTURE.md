# Code structure

How the Yorehold repos are laid out and the rules that keep the code easy to read and fast. Based on the "Pitchfork" layout most CMake projects use (SDL, fmt, spdlog, EnTT) plus what osu-framework does for testing.

## Folders

```
yorehold-framework/
  include/yorehold/framework/   public headers: what the game is allowed to use
    Host.h
    Scenes.h   deferred screen/overlay stack
    animation/ Tweens and easing, SpriteSheet + SpriteAnimator
    assets/    FileSystem, Assets, Skin
    audio/     Sound, Audio
    debug/     Profiler (F3 values and scope timers)
    editor/    History (undo/redo)
    graphics/  Renderer, Camera, Font, Image (decoding/resizing/cropping/slicing), Atlas (+ RectPacker), Particles, Lighting, Types
    input/     Input (actions + bindings, gamepads), ControlScheme (Foundry/BG3 presets)
    map/       Grid, TileMap, Pathfinding, Navigation, Tokens, CameraControls, Objects, Regions, FogOfWar, LightLevels, Templates (+ ruler)
    net/       Transport (loopback, TCP), Session (host-authoritative intents -> commands)
    rpg/       Random (seeded PCG32), Dice, Stats, Ruleset, Character (+ JSON), Combat
    save/      SaveFile (versioned envelopes, migrations, atomic writes)
    text/      Strings (languages, plurals), RichText (markup, effects, icons)
    ui/        Ui (themed widgets, text boxes with selection/clipboard/IME, sliders, scroll areas, log), TextEdit
    testing/   TestScene, TestBrowser
  src/                          .cpp files + private headers, same folders as include/
    Host.cpp  graphics/DebugFont.h (private)  graphics/Renderer.cpp  map/...  testing/TestBrowser.cpp
  tests/
    visual/                     test browser scenes (yorehold-framework-tests.exe)
    unit/                       release-enabled checks; framework-unit in CTest
  docs/
yorehold/
  src/                          game code; main.cpp stays tiny
  tests/visual/                 client scenes (yorehold-tests.exe)
```

- Public headers live under `include/yorehold/framework/` so includes read as `#include <yorehold/framework/Host.h>` and can't clash with other libraries.
- A header that only the framework needs (like `Screenshot.h`) stays in `src/`. The smaller the public API, the faster everything compiles and the less the game can break.
- Folder = module = sub-namespace when it grows big enough (`yh::gfx`, `yh::input`). One main class per file, file named after it.
- Third-party code comes in through CMake `FetchContent` with a pinned tag, never copied into the repo.

## Readability rules

- C++20, `#pragma once`, 4-space indents, braces on their own line, PascalCase types, camelCase functions, `member_` with trailing underscore.
- Headers include only what they need; forward-declare SDL types (`struct SDL_Renderer;`) instead of including SDL in public headers.
- Comments say *why*, not what. Short functions, early returns.
- Plain structs + free functions for data, classes when there's an invariant to protect. Virtual functions at the edges (Game, TestScene), never in per-sprite loops.
- Every visual feature gets a test scene before it's used in the game.

## Performance rules

- Data-oriented hot paths: things that exist in the thousands (sprites, particles, tiles) are stored as arrays of plain data, ideally structure-of-arrays (`x[]`, `y[]`, `vx[]`), and updated in tight loops. See `TestSceneSpriteStress`.
- Batch draws: draw calls change with texture, clip, blend or render target. Sparse tile chunks are culled before quads are batched; far views reuse overview textures.
- Reserve hot-path arrays and reuse scratch storage. Particles have a fixed capacity and drop overflow. Loading, path queries, text layout and tooling may allocate; profile their actual workloads.
- Measure before optimizing: `--frames N --no-vsync` prints avg/p99/worst frame time; compare before and after a change.
- Build types: Debug for stepping through code, RelWithDebInfo for day-to-day testing and profiling, Release + LTO for shipping.
- Keep compile times down as the code grows: forward declarations, private headers, and later a precompiled header for SDL/STL.

## Testing and feedback

| What | How |
|---|---|
| Auto build + reopen on save | `dev.ps1` next to the repos (`-App framework`, `tests` or `game`), or the `Yorehold Dev - *.lnk` shortcuts. R rebuilds, Q quits. Window position and last scene are remembered in `../.dev/` |
| Browse visual tests | run `yorehold-framework-tests` or `yorehold-tests`; Tab / click to switch, F5 restart, F1 hide list |
| Frame times, draw calls, game counters | F3 in any build; report your own with `yh::debug::value` / `YH_PROFILE` |
| Give feedback | F12: screenshot + type a note, Enter. Saved to `../feedback/` (next to the repos) with a line in `log.md` |
| Automated screenshot | `--scene "Sprite stress" --hidden --screenshot out.png` (add `--frames N` to wait longer) |
| Perf run | `--scene "Sprite stress" --hidden --no-vsync --frames 600` |
| List scenes | `--list` |
| Unit regression | `ctest --test-dir out -C RelWithDebInfo --output-on-failure` |
| Scripted input | `--fixed-dt 0.0166666667 --input script.txt`: `10 move 400 300`, `11 down left`, `14 up left`, `20 key F6`, `21 keyup F6`, `30 wheel 2`, `35 text Café`, `40 shot out.png` (frame number first) |
