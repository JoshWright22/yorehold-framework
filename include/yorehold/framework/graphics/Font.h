#pragma once

#include "yorehold/framework/graphics/Renderer.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yh
{

// A TrueType/OpenType font at one pixel size. Glyphs are rasterised the first time they're used
// into a shared atlas texture, so any text (accents, symbols) works without a fixed character set.
//
// Only load fonts we ship or that passed moderation: the rasteriser isn't hardened against
// deliberately broken font files.
class Font
{
public:
    static std::unique_ptr<Font> load(Renderer& renderer, std::vector<unsigned char> fileBytes, float pixelHeight);
    ~Font();

    // `position` is the top-left of the first line.
    void draw(Renderer& renderer, Vec2 position, std::string_view utf8, Color color);
    void drawCentered(Renderer& renderer, const Rect& area, std::string_view utf8, Color color);
    float measure(std::string_view utf8);
    float lineHeight() const { return lineHeight_; }
    float size() const { return pixelHeight_; }
    // Breaks text into lines no wider than `maxWidth`, at spaces where possible.
    std::vector<std::string> wrap(std::string_view utf8, float maxWidth);

private:
    struct Glyph
    {
        Rect uv;
        float x0, y0, x1, y1; // quad relative to the pen position on the baseline
        float advance;
        bool empty;
    };

    Font() = default;
    const Glyph& glyph(Renderer& renderer, uint32_t codepoint);
    void upload(Renderer& renderer);

    struct Data;
    std::unique_ptr<Data> data_;
    float pixelHeight_ = 16;
    float scale_ = 1;
    float ascent_ = 0;
    float lineHeight_ = 0;
    std::unordered_map<uint32_t, Glyph> glyphs_;
};

}
