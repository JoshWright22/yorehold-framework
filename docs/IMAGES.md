# Editing image tiles and tokens

Start with `tests/visual/TestSceneImages.h`. It is a small working example of loading files, building a map tileset, and drawing both token styles. `loadExampleArt` sets up the examples, `loadDroppedImage` applies user art, and `draw` renders the scene. The fixture images are in `assets/images`.

## Where to make a change

| Change | File and function |
|---|---|
| Supported file formats or JPEG orientation | `src/graphics/Image.cpp`: `decodeImage`, `decodeWebp`, `decodeStbImage`, `jpegOrientation` |
| How resized art looks | `src/graphics/Image.cpp`: `makeFilterWeights`, `resampleHorizontally`, `resampleVertically` |
| Which part of a photo becomes a tile or portrait | `src/graphics/Image.cpp`: `squareImage`, `circleImage` |
| Tile-sheet margins and gaps | `src/graphics/Image.cpp`: `sliceImage` |
| Tile spacing in an atlas or filtering seams | `src/map/TileMap.cpp`: `tilePadding`, `copyPaddedTile`, `Tileset::uv` |
| Colours and transparency at distant zoom | `src/map/TileMap.cpp`: `averageColor`, `blendOverviewPixel`, `TileMap::rebuildPage` |
| Portrait border width or cutout fitting | `src/map/Tokens.cpp`: `portraitInnerFraction`, `TokenController::drawImage` |
| Texture caching and file reloads | `src/assets/Assets.cpp`: `load`, `loadTexture`, `reloadTextures` |

The public image functions in `Image.h` work on CPU pixels. `Renderer::loadTexture` uploads those pixels; `Assets` adds virtual file paths and caching. Tokens and tiles use those same pieces, so a decoder fix applies to both.

## Details to preserve

- `Image::rgba` stores four bytes per pixel: red, green, blue, alpha. Rows are packed without extra bytes. Check `valid()` before reading a caller-supplied image.
- Resize colours with alpha applied, then remove that multiplication after both filter passes. Otherwise invisible black pixels can leave dark edges around transparent art.
- Atlas padding repeats edge pixels. If you change its size, keep the copy positions and UV calculation in agreement. Average colours use the actual tile pixels, excluding padding.
- Cache keys include the file path, filtering mode, and circle size. Reload every variant of a changed file; different uses of the same picture must not overwrite one another.
- `Assets` owns its textures. A built `Tileset` shares ownership between copies. The token controller borrows `Assets`, which must outlive it. Keep these lifetimes when adding new loading paths.

## Checking a change

`tests/unit/ImageTests.cpp` groups the image checks into file formats, JPEG orientation, transformations, and tilesets. `Checks.h` supplies checks that stay active in release builds and report the source file and line on failure. Add image regressions to the relevant group; other features can use their own test source, listed in the unit-test CMake target and registered in `tests/unit/main.cpp`.

Build the neighboring client and run CTest using the commands in [FRAMEWORK.md](FRAMEWORK.md#builds-and-verification). Open the framework browser's **F8 Images** scene for visual checks, or run its script from the workspace root:

```powershell
./yorehold/out/bin/RelWithDebInfo/yorehold-framework-tests.exe --scene 'F8 Images' --hidden --no-vsync --frames 50 --fixed-dt 0.0166666667 --input yorehold-framework/tests/visual/scripts/images.txt
```

The screenshots in `.dev/images-close.png`, `.dev/images-overview.png`, and `.dev/images-restored.png` show normal drawing, distant map rendering, and the return to normal zoom. Dropping an image onto the visible scene replaces its tiles and token art together.
