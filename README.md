# yorehold-framework

A C++20 2D game framework written with [Yorehold](https://github.com/JoshWright22/yorehold) in mind.

## Objectives

The framework holds everything the game needs that isn't Yorehold-specific: windowing, input, rendering, animation, particles, text, and the skin system. The game client stays focused on gameplay.

- One codebase for Windows, Linux, macOS, Android, iOS and the browser.
- Big maps stay smooth: tile maps are cut into chunks and only what's on screen gets drawn.
- Everything visual can be reskinned from data (folders or `.yoreskin` zips), so players can customize without code.

## Status

The framework implements the runtime and generic RPG systems in [docs/DESIGN.md](docs/DESIGN.md):

- SDL3 host and scene stack; batched WebGPU shapes, sprites, atlases, clipping, render targets and screenshots.
- Rebindable and saved controls, eased cameras, square/hex/gridless maps, path previews, token selection, party chains, floors and combat turn movement.
- Sparse tile layers, uniform ground, painted images, overview pages, hierarchical navigation, streamed regions, frozen empty regions and authored-map change saves.
- PNG/JPEG/WebP and other raster art for image tiles, painted maps, circular token portraits and transparent cutout tokens.
- Layered folder/zip assets, hot reload, TTF text, skin manifests, themes, editable text, sliders, checkboxes and scrolling.
- Tweens, seeded pooled particles, shadowed coloured lighting and remembered fog per team/floor.
- Tagged objects and JSON kits, doors/keys/levers, containers, destruction, resumable throws, WAV audio and a stereo mixer.
- Seeded dice, stats/modifiers, JSON rulesets and character sheets, inventory, conditions and turn-based encounters.

The visual browser has 15 scenes, F3 frame times and F12 local screenshot feedback. F8 Images accepts dropped files as tile and token art. The regression executable tests image decoding, simulation, parsing, saves, navigation, input, audio and lifecycle behavior without a GPU. See [docs/FRAMEWORK.md](docs/FRAMEWORK.md) for API examples, build targets and verification status; [docs/STRUCTURE.md](docs/STRUCTURE.md) describes the layout.

Windows/D3D12 is built and tested. Linux, Apple and Android surface implementations and an Emscripten/WebGPU build path are included; those targets require their SDKs and have not been compiled or run in this Windows workspace. Mobile app packaging and platform-specific deployment belong to the consuming client.

## Requirements

- Visual Studio 2026 (or Build Tools) with C++; the prebuilt Dawn needs its compiler
- CMake 4.2+ (bundled with VS 2026)
- Git (CMake downloads SDL3 and Dawn on the first configure)

## Building

```shell
git clone https://github.com/JoshWright22/yorehold-framework
cd yorehold-framework
cmake -S . -B out -G "Visual Studio 18 2026" -A x64
cmake --build out --config RelWithDebInfo
```

This builds the static library, visual browser and regression executable. When building through the neighboring client, executables land in `yorehold/out/bin/RelWithDebInfo`.

```shell
ctest --test-dir out -C RelWithDebInfo --output-on-failure
out/RelWithDebInfo/yorehold-framework-tests.exe --list
out/RelWithDebInfo/yorehold-framework-tests.exe --scene "F4 Animation + particles"
```

## Using it in a project

```cmake
add_subdirectory(../yorehold-framework yorehold-framework)
target_link_libraries(my-game PRIVATE yorehold::framework)
```

```cpp
#include <yorehold/framework/Host.h>

class MyGame : public yh::Game { /* load, update, draw, unload */ };

int main(int, char**)
{
    MyGame game;
    return yh::run(game);
}
```

## Contributing

Pull requests are welcome. Open an issue first for anything big. Keep it generic: if code only makes sense for Yorehold, it belongs in the client repo.

Code style: C++20, `yh` namespace, PascalCase types, camelCase functions, 4-space indents, braces on their own line. The build uses warning level 4 (`/W4`, or `-Wall -Wextra`), so keep it warning-free.

## Licence

Not picked yet. Until it is, the code is all rights reserved.
