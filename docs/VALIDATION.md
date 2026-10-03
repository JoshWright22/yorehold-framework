# Verified 2026-10-02

Windows 10 x64, Visual Studio 2026/MSVC 14.51, RelWithDebInfo, SDL 3.4.16 and Dawn v20260930.214659. GPU: NVIDIA GeForce GTX 1080, D3D12 driver 32.0.15.8180.

- Framework library, game library, game executable, client browser, framework browser and regression executable all build from a clean build directory with no warnings at `/W4`.
- CPU regression: **2,246 checks, zero failures**, 20 groups; CTest `framework-unit` passes in the isolated client/framework build.
- The image update builds the framework, game and both browsers without compiler warnings. F8 Images ran its 50-frame script and saved close, overview and restored screenshots. PNG/JPEG tiles, circular portraits, transparent cutouts, distinct filtering cache entries, atlas padding and average alpha were checked. A screenshot pixel check confirms that distant ground stops at the small map's bounds. Existing M3 Tile map, M4 Tokens and F6 World scenes also pass 40-frame GPU smoke tests without validation errors.
- The readability refactor passes the same 2,246 checks and all four GPU scenes above. Its close and overview PNG screenshots are byte-for-byte identical to the images saved before refactoring. Image checks now live in `tests/unit/ImageTests.cpp`; the editing guide is [IMAGES.md](IMAGES.md).
- The previous full sweep ran the original **14 framework scenes** for 40 fixed-time frames in hidden windows and saved screenshots without GPU validation errors. The client Game scene and the game executable also ran successfully.
- The World script exercised lever links, snapshot save/restore, throwing, Unicode text input, travel away/back, two simultaneously occupied regions, scrolling and playback through SDL's dummy audio driver. Screenshots confirmed remembered object changes, two active regions and resetting a region absent from an older save.
- The Lighting script exercised movement, remembered exploration, colour/shadow rendering, fog/light toggles and exploration reset.
- The Tools script painted a brush stroke and undid/redid it as a single step, measured a two-leg 5-10-5 ruler (20 ft, then 65 ft), aimed a line template, triggered a sprite clip's "hit" event, Shift-selected text, switched to Portuguese plurals and captured the F3 overlay.
- Networking was tested over the in-process loopback and over real TCP on 127.0.0.1: a 300 KB message, connection refusal and peer departure.

CPU coverage includes delayed/zero-length tweens and callback reentrancy, seeded continuous/burst particles and saturated pools, frame partitioning, wall visibility, fog teams/floors and saves, sparse terrain/default cells/deltas, path topology invalidation, tagged interactions, resumable throws, region freezing and failed/old restores, party-chain cycles/owners/turns/floors, focus loss, orphan clicks, folder/zip overlays, theme/binding/ruleset/sheet parsing, excessive dice rejection, offline audio pan/mute/end-of-clip and scene lifecycle. It also covers save migrations, refusals, atomic writes and backup fallback; undo merges, groups, limits and branching; session joins, rejections, version and admit refusal, late-join snapshots, checksum desync, kicks, leaves and timeouts; UTF-8 selection, word movement, paste cleaning and byte limits; IME composition; gamepad dead zones, multiple pads, unplugging and binding JSON; language fallback and plural rules; template coverage on square and hex grids and rulers; packing density, padding extrusion and atlas UVs; sprite clip ranges, events, chaining and validation; rich-text tags, escaping and wrapping; and the profiler.

Image coverage includes PNG/BMP/TGA RGBA round trips, lossless WebP, JPEG with all eight EXIF orientations in both byte orders, GIF's first frame, PNM, malformed/truncated input, alpha-correct resizing, aspect-preserving fitting, centred crops, antialiased circles, sheet margins/spacing, invalid pixel buffers and oversized tile sizes, and padded tile UVs. Test fixture art in `assets/images` was drawn locally for this scene.

Repeat the scripted GPU tests from the workspace root (the directory containing both repos). Create `.dev` if it is absent. The scripts intentionally save local screenshots there.

```powershell
$env:SDL_AUDIODRIVER = 'dummy'
./yorehold/out/bin/RelWithDebInfo/yorehold-framework-tests.exe --scene 'F6 World + UI + audio' --hidden --no-vsync --frames 190 --fixed-dt 0.0166666667 --input yorehold-framework/tests/visual/scripts/world.txt
./yorehold/out/bin/RelWithDebInfo/yorehold-framework-tests.exe --scene 'F5 Lighting + fog' --hidden --no-vsync --frames 110 --fixed-dt 0.0166666667 --input yorehold-framework/tests/visual/scripts/lighting.txt
./yorehold/out/bin/RelWithDebInfo/yorehold-framework-tests.exe --scene 'F7 Tools + text' --hidden --no-vsync --frames 110 --fixed-dt 0.0166666667 --input yorehold-framework/tests/visual/scripts/tools.txt
./yorehold/out/bin/RelWithDebInfo/yorehold-framework-tests.exe --scene 'F8 Images' --hidden --no-vsync --frames 50 --fixed-dt 0.0166666667 --input yorehold-framework/tests/visual/scripts/images.txt
```

Short GPU sweeps include startup and screenshot readback stalls; their frame timings are smoke-test results, not steady-state performance benchmarks. Real audio-device output, physical gamepads, a real input-method editor (IME), the clipboard, multiplayer between two separate machines, long-duration resource/performance tests, and all non-Windows SDK builds remain unverified. See [FRAMEWORK.md](FRAMEWORK.md) for platform prerequisites and API limits.
