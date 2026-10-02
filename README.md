# yorehold-framework

A C++20 2D game framework written with [Yorehold](https://github.com/JoshWright22/yorehold) in mind.

## Objectives

The framework holds everything the game needs that isn't Yorehold-specific: windowing, input, rendering, animation, particles, text, and the skin system. The game client stays focused on gameplay.

- One codebase for Windows, Linux, macOS, Android, iOS and the browser.
- Big maps stay smooth: tile maps are cut into chunks and only what's on screen gets drawn.
- Everything visual can be reskinned from data (folders or `.yoreskin` zips), so players can customize without code.

## Status

Very early. Today it opens an SDL3 window and runs a game loop (`yh::run` and `yh::Game` in `include/yorehold/framework/Host.h`). Drawing still uses SDL_Renderer.

Next up:

1. WebGPU renderer (Dawn on native, Emscripten in the browser)
2. Sprite batching and texture atlases
3. Chunked tile maps
4. Particles
5. Browser and Android builds

## Requirements

- [CMake](https://cmake.org/download/) 3.24+
- A C++20 compiler (Visual Studio 2022, recent Clang or GCC)
- Git (CMake downloads SDL3 on the first configure)

## Building

```shell
git clone https://github.com/JoshWright22/yorehold-framework
cd yorehold-framework
cmake -S . -B build
cmake --build build
```

That builds the framework as a static library. To run something, build the [game client](https://github.com/JoshWright22/yorehold) with this repo cloned next to it.

## Using it in a project

```cmake
add_subdirectory(../yorehold-framework yorehold-framework)
target_link_libraries(my-game PRIVATE yorehold::framework)
```

```cpp
#include <yorehold/framework/Host.h>

class MyGame : public yh::Game { /* load, update, draw */ };

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
