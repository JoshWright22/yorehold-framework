#pragma once

#include <vector>

namespace yh::debugfont
{

// The atlas holds ASCII 32-127 as a 16 x 6 grid of 8x8 glyphs, plus a solid white block below
// them. Plain shapes sample the white block, so shapes and text share one texture and one draw call.
constexpr int glyphSize = 8;
constexpr int columns = 16;
constexpr int firstChar = 32;
constexpr int lastChar = 127;
constexpr int atlasWidth = columns * glyphSize;
constexpr int atlasHeight = 7 * glyphSize;
constexpr int whiteX = 0;
constexpr int whiteY = 6 * glyphSize;

// White glyphs on transparent, RGBA8. Made from SDL's built-in debug font.
std::vector<unsigned char> buildAtlas();

}
