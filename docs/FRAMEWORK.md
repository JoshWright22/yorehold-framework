# Framework APIs and verification

The framework is a C++20 library in namespace `yh`. The client supplies game screens, content, the rules that accept or reject multiplayer actions, and an editor. All runtime APIs run on the calling thread. GPU handles are renderer-specific.

## Runtime and rendering

Implement `Game::load`, `update`, `draw`, `handleEvent` and `unload`, then call `run`. `unload` runs while the window and renderer still exist. `Scenes` owns a stack of `Game` screens; push/pop/clear requested from callbacks are deferred until callbacks finish. Overlay flags independently allow drawing or updating lower screens.

`Renderer` batches textured quads and coloured shapes. Camera transforms, nested viewports, blend modes, offscreen targets and gradients share that pipeline. Pair each push with its matching pop. `textureRelease(id)` returns a weak lifetime-safe callback; invoke it once when an owned texture is no longer needed. Releases are drained at the next frame boundary, after previously queued commands have been submitted. Assets, fonts, overview textures, lights, particles and tokens use this ownership mechanism.

`HostSettings` controls window size, vsync, feedback and state directories. `--fixed-dt` drives deterministic simulation; `maxDeltaSeconds` limits measured pauses. Scripted events and screenshots are available without publishing anything. Coordinates delivered to games use drawable pixels, including high-density windows.

F3 shows frame times with a graph of the last 120 frames, the frame's draw calls, passes, vertices and texture memory (`Renderer::stats()`), and anything the game reports. `yh::debug::value("tokens", n)` shows a number; `YH_PROFILE("pathfinding")` times a scope, and scopes with the same name add up across the frame. Scripted `shot` screenshots include the overlay.

## Input, camera and movement

`InputMap` binds named actions to keys or mouse buttons. Persist `toJson()` and restore with `fromJson()`. `makeControlScheme(ControlPreset::Foundry)` and `makeControlScheme(ControlPreset::BG3)` provide presets. Feed SDL events into `Input`, read it, then call `endFrame()` once. Focus loss clears held buttons and keys; UI key repeats and text input remain separate from action presses.

Gamepads open automatically when connected. `Binding::gamepadButton` and `Binding::gamepadAxis(axis, ±1)` bind buttons and stick or trigger halves; bindings save with SDL's names (`"a"`, `"dpup"`, `"leftx"`). Buttons and axes combine across all pads, and the most-pushed pad wins an axis. Sticks have a 0.2 dead zone; an axis half counts as pressed past 0.5. `value(action)` is analog (0–1) for sticks and 1 for held keys, so camera panning follows stick tilt. Both presets pan with either stick or the d-pad, zoom with the shoulders and recentre with Back.

`CameraControls` handles eased pan/zoom, bounds, edge scroll, selection following and the Home key. `Grid` supports square, hex and gridless cell lookup, neighbours, distance and snapping. `findPath` provides bounded A* with no diagonal corner cutting. `Navigation` caches connected components within chunks and searches between those components before refining a route. Its coarse route favors bounded work; it does not promise a globally shortest cell path. Call `invalidate(cell)` after changing passability, or `clear()` after changing the grid/topology.

`TokenController` implements clicks, drags, box selection, path previews and group destinations. `link(follower, leader)` creates same-owner chains and rejects cycles; followers trail their parent outside combat. `settings.inCombat` and `settings.activeTurn` restrict movement to the active token. `setFloor` can transfer a chain; `viewedFloor` filters drawing and interaction. The client selects when stairs or transitions trigger it. The passability callback describes the current floor. Links use token indices: call `clearLinks()` before erasing/reordering the public token vector. Set `contextActions` to client labels and consume `contextChoice` after update.

Set `Token::image` to a virtual file path and call `controller.useAssets(assets)` once; the assets must outlive the controller. Portraits crop the image's centre to an antialiased circle with a ring of the token's colour. `TokenImageStyle::Cutout` draws the complete image, preserves transparency and aspect ratio, and uses `imageScale` to adjust its size relative to the token's footprint. Selection, movement, hit testing and collision still use the token's radius. Empty image paths keep the coloured disc and initial.

```cpp
tokens.useAssets(assets);
tokens.tokens[0].image = "tokens/hero.jpg"; // circular portrait
tokens.tokens[1].image = "tokens/archer.webp";
tokens.tokens[1].imageStyle = yh::TokenImageStyle::Cutout;
tokens.tokens[1].imageScale = 1.2f;
```

`AreaTemplate` covers circles, cones, lines and squares in world units. `AreaTemplate::aimed(shape, origin, target, size)` aims cones and lines. `cells(grid)` returns every cell whose centre lies inside (the Foundry/5e rule), on square, hex and gridless grids; `draw` fills and outlines it. `measure(grid, waypoints)` returns per-leg and total distances in cells. The 5-10-5 diagonal count carries across waypoints. `drawRuler` labels each waypoint in client units (`5, "ft"`).

## Maps, objects and streamed worlds

`TileMap(width, height, tileSize)` stores 32×32 chunks only where cells differ from a layer's uniform default. `addLayer(name, floor, defaultTile)` makes a huge uniform map without filling every cell. `setLayerImage` supports painted floors behind tiles. Far views use 1024×1024 overview pages. Tile/image data survives JSON saves; texture handles do not, so bind the tileset and image textures after loading. Ownership of supplied atlas/image textures stays with the caller.

Call `trackChanges()` after loading authored terrain. `changesJson()` stores final edited cells, including removals; `applyChanges()` validates the entire delta before applying it. Full map snapshots preserve the tracking state. Layer structure and images are authored data; cell deltas require the same dimensions, tile size and layer indices.

Use `assets.image(path)` and `buildTileset(renderer, images, tilePixels)` to turn separate PNG/JPEG/WebP or other supported pictures into a tileset. Non-square pictures are centre-cropped and resampled; each image becomes one tile id, starting at 1. Repeated edge padding prevents filtering seams. The resulting tileset shares ownership of its GPU texture and computes colours and alpha for distant overview rendering. `sliceImage(sheet, cellWidth, cellHeight, margin, spacing)` splits existing sheets, skipping incomplete cells and the border on every side. Rebuild an atlas after its source files change.

```cpp
auto stone = assets.image("tiles/stone.jpg");
auto grass = assets.image("tiles/grass.png");
if (stone && grass)
{
    const std::array<yh::Image, 2> images{*stone, *grass};
    if (auto tileset = yh::buildTileset(renderer, images, 64))
        map.setTileset(std::move(*tileset)); // stone = tile 1, grass = tile 2
}
// A painted map uses the same decoder, without splitting it into cells.
map.setLayerImage(ground, "maps/tavern.webp", assets.texture("maps/tavern.webp", true), map.worldBounds());
```

`Kit` is a JSON object prototype. `Objects::place` creates independent instances with unique ids. Tags control movement/sight blocking, containers, destructibility, levers and throwing; optional components carry door state, health and lights. Inventory keys use `key:*`; levers and doors share `link:*`. `walls()` and `lights()` feed visibility and lighting. `throwTo` animates ground position plus a visual arc; `update` reports landings after iteration, so callbacks can safely remove objects. Flight state survives saves. The client decides throw range, collision/hit rules and landing damage.

`Regions` registers authored loaders. `enter(player, id)` loads before leaving the current region. Every occupied region ticks; leaving its last player saves and unloads it. Returning restores object/map/variable/clock state. Empty regions never advance. Membership changes must happen outside update callbacks. `WorldPosition` retains doubles until subtracting the camera origin, preserving local motion far from the origin.

```cpp
yh::Regions world;
world.add("village", loadAuthoredRegion);
world.enter(0, "village");
world.update(dt, [](yh::Region& region, double seconds) {
    region.objects.update(seconds);
});
std::string save = world.changesJson();
world.leave(0);
world.restoreChanges(save); // loaders supply authored maps; saved edits are applied
world.enter(0, "village");
```

`toJson/restore` provide complete sparse snapshots. `changesJson/restoreChanges` save terrain edits over authored maps and complete object/variable/clock state. All players must leave before restoring. Restore validates all entries before committing, and regions absent from the save reset to authored state. Player membership is client/session state, so explicitly re-enter players after restore.

## Assets, themes, effects and audio

Mount default assets in `FileSystem`, then a skin folder or `.yoreskin` zip. Later mounts override earlier ones. Paths are normalized and parent/absolute paths are rejected. `Assets` caches textures and TTF fonts; `hotReload()` reloads changed folder assets. Clear it after changing mounts. Cached Font pointers must be reacquired after clearing/reloading. Archived assets are remounted to pick up archive changes.

`decodeImage` identifies files from their contents and reads PNG, JPEG, static WebP, GIF's first frame, BMP, TGA, flattened PSD and PNM. JPEG EXIF orientation is applied before cropping. `Assets::texture`, `circleTexture`, `image`, and `Renderer::loadTexture` share that decoder. Resampling filters premultiplied alpha to preserve transparent edges; texture uploads fit oversized images within an 8192-pixel side while keeping their proportions. Plain, smooth and circle textures have separate cache entries and all reload when their file changes. SVG, TIFF, HEIC and AVIF require conversion to a supported raster format.

[Editing image tiles and tokens](IMAGES.md) lists the functions to change for cropping, filtering, portraits, atlas padding and hot reload, with the relevant tests and ownership rules.

`Skin::load(files)` reads metadata, theme colours, body font, animation settings and particle effects from `skin.json`. `resolveTheme(assets)` refreshes its Font pointer. The shipped manifest demonstrates the schema. Reload the manifest itself when changing theme data; asset hot reload refreshes images/fonts independently. `Ui` provides themed panels, buttons, toggles, checkboxes, bars, sliders, UTF-8 text boxes, logs and nested scrolling. Text boxes support clicking to place the caret, drag or Shift selection, double-click (word) and triple-click (all), Ctrl word jumps and word deletes, and Ctrl+A/C/X/V through the system clipboard. Input-method composition shows inline, underlined at the caret, and the OS candidate list opens next to it. Pasted line breaks become spaces, and text over the byte limit is cut at a character boundary. The editing logic is `TextEdit`, which editors can reuse without rendering.

`Atlas` packs many small images into a few large pages (skyline packing, tallest first) so sprites draw in one batch. Padding repeats each image's edge pixels so filtering never bleeds. `add` queues images (`decodeImage` reads the supported raster formats without the GPU), `upload` packs and creates or updates pages, and `find`/`draw` use regions by name. Atlases own their pages and release them when destroyed. `RectPacker` is available on its own.

`SpriteSheet::fromJson` cuts frames from a grid (`frameWidth`, `frameHeight`, optional `margin`/`spacing`/`count`) or an explicit `frames` list, inside a whole texture or an atlas region. Clips list frames as arrays or ranges (`"0-3"`, `"7-5"` plays backwards) with fps or per-frame `durations`, `loop`, `next` (attack → idle) and frame `events` ("hit"). `SpriteAnimator` is plain per-entity state: events fire for every frame entered, even when one long update skips several, and `speed` scales playback. Draw with `sheet.draw(renderer, animator.frame(sheet), dest, tint, flipX)`.

`Tweens::to` replaces a tween on the same float, samples its start after a delay and delivers completion callbacks after removal. Targets must remain alive until completion/cancellation. Zero-duration tweens finish on update, including `update(0)`. Nine easing curves are available.

`ParticleEffect::fromJson` validates ranges, colours and emitter settings. `ParticleSystem` reserves parallel arrays to its capacity, uses independent seeded emitter random streams and shares style data. Bursts and continuous emission support gravity, drag, scaling, fading, spin, custom textures and blending. Overflow is discarded. Closed-form integration and birth-time offsets keep normal unsaturated simulations consistent across frame lengths; pool saturation deliberately drops emissions.

`Visibility` casts rays to walls; `Lighting` combines ambient colour and additive radial shadowed lights, then multiplies the world. `FogOfWar` stores sparse explored/visible bits separately for every team and floor. Exploration survives saves; visibility is recalculated from current observers. Fog is cell-based; light shadow polygons are continuous.

`Sound::fromWav` converts decoded audio to 48 kHz stereo floats. `Audio` mixes up to 64 simultaneous voices with looping, pitch, pan, master gain and Effects/Music/Ui buses. `init` failure is recoverable when no device exists; `mix` works offline for tests. Call `update` regularly to fill the device's 50 ms queue. Samples shared with voices must not be modified during playback.

## RPG data

`Random` supplies seeded PCG32 rolls. Dice expressions support multiple terms, constants and keep-high/low rolls. `StatBlock` combines base values, additions, multiplication and overrides by source. `Ruleset` supplies ability/skill/condition definitions, score conversion, AC, proficiency and XP progression; built-in classic/modern presets can be serialized and customized as JSON.

`Character` includes inventory, equipment, resources, conditions, HP/temp HP, checks and saves. `Encounter` handles initiative, turns, action/movement budgets, attacks, damage and condition durations. Empty encounters and negative movement requests are rejected. Which actions are legal, and the UI flow around them, remain client responsibilities.

## Text and languages

`Strings` loads `lang/<locale>.json` from the `FileSystem`, so skins and chapters can add or override languages. Nested objects become dotted keys (`menu.start`). `load(files, "pt-BR")` layers the fallback (`en`), then `pt`, then `pt-BR`. Missing keys fall back the same way and finally show the key itself; `missing()` lists them for translators. `get(key, {{"name", "Ana"}})` fills `{name}` placeholders, and `{{`/`}}` are literal braces. `plural(key, count)` picks `zero/one/two/few/many/other` with simplified CLDR integer rules: English-style by default, French/Portuguese treat 0 as one, plus Slavic few/many, Czech/Slovak, Arabic, and no plurals for CJK/Thai/Vietnamese/Indonesian. `preferredLocales()` reads the OS languages.

`RichText::parse` understands `[color=#rrggbb]`/`[color=#rrggbbaa]`/`[color=name]` (from a palette such as the theme's accent/good/bad), `[wave]`, `[shake]`, `[rainbow]` and `[icon=name]`. `[[` is a literal bracket. Unknown or badly nested tags show as typed. Escape player-typed text with `RichText::escape` so chat and names can't inject tags. `layout(font, maxWidth)` wraps at spaces across style changes and keeps kerning inside words. `draw(renderer, position, baseColour, time, iconDrawer)` animates effects from the clock you pass, and icons draw through a callback (usually `Atlas::draw`). The debug font works when `font` is null.

## Saves and undo

`SaveFormat("yorehold.save", 3)` wraps client data in `{"format", "version", "framework", "data"}`. Register `migrate(from, step)` for every older version. Steps edit `nlohmann::json` in place and throw to reject a save. `read` refuses other formats and newer saves, runs framework migrations first and then client steps up to the current version, and changes nothing if any step fails. `frameworkSaveVersion` tracks the shape of the framework's own snapshots. `writeFile` writes through a temporary file and keeps the previous save as `.bak`; `readFile` falls back to the backup when the main file is missing or corrupt. `writeFileAtomically` does the same for any text.

`History` records undo/redo as pairs of callbacks that capture their old and new values. `perform` applies and records an edit; `record` stores one that is already applied. `beginGroup`/`endGroup` make one step out of many edits. A merge key joins consecutive edits (one brush stroke, one slider drag) until `breakMerge()`. Edits are refused while undoing or redoing. `dirty()`/`markSaved()` track unsaved changes, including after undoing past a save and then branching. The default limit is 200 steps.

## Multiplayer sessions

`Transport` moves reliable, ordered messages and reports Connected/Disconnected/Message events from `poll()`. Both sides get a Disconnected event, including the side that disconnected. `LoopbackHub` runs host and clients in one process (tests, local play). `TcpTransport::listen(port)` and `connect(host, port)` use non-blocking sockets with length-prefixed messages up to 16 MB. `listen(0, …, true)` binds an ephemeral port on this machine only (no firewall prompt). TCP isn't available in browser builds, which will need a WebSocket/WebRTC transport.

`SessionHost` and `SessionClient` are host-authoritative. Clients send *intents*. The host's `Rules::validate` accepts or rejects each one and may fill in host-side results such as dice. Accepted intents become numbered *commands*, which the host and every client apply in the same order, so all copies match. Joiners send their game name and version (mismatches are refused, and `admit` can refuse others), then receive `snapshot()` and every later command. An optional `checksum` after each command catches desyncs and disconnects the client. Heartbeats run every 2 s and silent peers drop after 15 s. `kick` and `leave` give reasons, and `disconnected` reports exactly one reason. The host applies its own intents through the same rules, and submitting from inside `apply` is refused so ordering can't diverge.

## Builds and verification

The Windows build passed **2,246 regression checks**. GPU verification covers the new image scene and existing map, token and world scenes; the previous full sweep covered the original 14 framework scenes and client integration. [VALIDATION.md](VALIDATION.md) records the environment and repeatable interaction scripts.

Use the neighboring client build to verify library integration:

```powershell
cmake -S ../yorehold -B ../yorehold/out -G "Visual Studio 18 2026" -A x64
cmake --build ../yorehold/out --config RelWithDebInfo --parallel 4
ctest --test-dir ../yorehold/out -C RelWithDebInfo --output-on-failure
../yorehold/out/bin/RelWithDebInfo/yorehold-framework-tests.exe --list
```

`YOREHOLD_FRAMEWORK_TESTS` controls the visual browser; `YOREHOLD_FRAMEWORK_UNIT_TESTS` controls the CPU regression target. CTest uses release-enabled checks, so optimized builds still validate behavior. Tests cover tween timing/reentrancy, seeded emission and capacity, wall visibility, fog persistence, sparse map saves/deltas, navigation changes, object interactions/throws, region freezing/co-op/restores, party links/turns/floors, input focus, layered disk/zip files, theme/ruleset/sheet parsing, audio mixing, screen lifecycles, save migrations/backups, undo groups/merges/dirty state, loopback and TCP sessions (joins, rejections, version refusal, desync, kicks, timeouts), text selection/clipboard rules/composition, gamepad axes/buttons/hot-unplug, languages/plurals, templates/rulers, atlas packing/padding, sprite clips/events, rich-text parsing/wrapping and the profiler.

The visual browser contains Shapes, Sprite stress, Input, M1 Camera, M2 Grid, M3 Tile map, M4 Tokens, M5 Sheet, M6 Combat, F2 Skins + text, F4 Animation + particles, F5 Lighting + fog, F6 World + UI + audio, F7 Tools + text, and F8 Images. The image scene draws JPEG tiles and PNG decorations with portrait/cutout tokens; drop a supported image onto it to use your own art. Z toggles distant overview rendering. Scripts can drive `move`, `down`, `up`, `key`, `keyup`, `wheel`, `text` and `shot`. Key names use underscores for spaces (`key Left_Ctrl`), and `--fixed-dt` supplies reproducible timing. `tests/visual/scripts/tools.txt` exercises painting and undo/redo, the ruler, line templates, the attack event, text selection, switching language and F3. `tests/visual/scripts/images.txt` captures image tokens and near/far map rendering.

| Platform | Implementation | Validation in this workspace |
|---|---|---|
| Windows | SDL HWND, pinned Dawn binary, D3D12 runtime DLLs | Compiler, CPU tests and GTX 1080 GPU scenes |
| Linux | SDL Xlib/Wayland surface, native Dawn package/source | SDK build and runtime unverified |
| macOS/iOS | SDL Metal view/layer, native Dawn, iOS bundle target | Apple SDK build and runtime unverified |
| Android | SDL native window, shared-library app targets, native Dawn | NDK build, APK packaging and runtime unverified |
| Browser | Emscripten `emdawnwebgpu`, Asyncify loop, preloaded framework assets | Emscripten build and browser runtime unverified |

For native source builds, set `YOREHOLD_DAWN_FROM_SOURCE=ON`; an installed `Dawn_DIR` takes precedence. Native Dawn source is pinned to the same commit as the Windows package. Mobile consumers must provide the SDL application wrapper/signing/package configuration appropriate to their SDK.

For web builds, configure with `emcmake cmake -S . -B out-web -DBUILD_TESTING=OFF -DYOREHOLD_FRAMEWORK_UNIT_TESTS=OFF`, then build and serve the generated `.html/.js/.wasm/.data` from localhost or HTTPS. `YOREHOLD_WEBGPU_PORT` accepts the port name or a pinned port file. The default port can change its C++ API, so use a matching port when compiling this revision. See the primary [Dawn port instructions](https://dawn.googlesource.com/dawn/+/refs/heads/main/src/emdawnwebgpu/pkg/README.md), [native CMake guide](https://dawn.googlesource.com/dawn/+/refs/heads/main/docs/quickstart-cmake.md), and [Emscripten runtime guide](https://emscripten.org/docs/porting/emscripten-runtime-environment.html).
