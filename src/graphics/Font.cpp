#include "yorehold/framework/graphics/Font.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4505)
#endif
#include <stb_truetype.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>

namespace yh
{

namespace
{

constexpr int atlasSize = 1024;

// Decodes one UTF-8 character starting at `i`, advancing `i`. Bad bytes become U+FFFD.
uint32_t nextCodepoint(std::string_view text, size_t& i)
{
    const unsigned char c = static_cast<unsigned char>(text[i++]);
    if (c < 0x80)
        return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (extra < 0)
        return 0xFFFD;
    uint32_t cp = c & (0x3F >> extra);
    while (extra-- > 0)
    {
        if (i >= text.size() || (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
            return 0xFFFD;
        cp = (cp << 6) | (static_cast<unsigned char>(text[i++]) & 0x3F);
    }
    return cp;
}

}

struct Font::Data
{
    std::vector<unsigned char> file;
    stbtt_fontinfo info{};
    std::vector<unsigned char> pixels; // RGBA atlas, white with coverage in alpha
    TextureId texture = 0;
    bool dirty = false;
    int penX = 1;
    int penY = 1;
    int rowHeight = 0;
    std::function<void()> release;
    ~Data() { if (release) release(); }
};

Font::~Font() = default;

std::unique_ptr<Font> Font::load(Renderer& renderer, std::vector<unsigned char> fileBytes, float pixelHeight)
{
    if (fileBytes.size() < 12 || !std::isfinite(pixelHeight) || pixelHeight <= 0 || pixelHeight > 512)
        return nullptr;
    std::unique_ptr<Font> font(new Font());
    font->data_ = std::make_unique<Data>();
    Data& d = *font->data_;
    d.file = std::move(fileBytes);
    const int offset = stbtt_GetFontOffsetForIndex(d.file.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&d.info, d.file.data(), offset))
        return nullptr;

    font->pixelHeight_ = pixelHeight;
    font->scale_ = stbtt_ScaleForPixelHeight(&d.info, pixelHeight);
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&d.info, &ascent, &descent, &gap);
    font->ascent_ = std::round(ascent * font->scale_);
    font->lineHeight_ = std::round((ascent - descent + gap) * font->scale_);

    d.pixels.assign(static_cast<size_t>(atlasSize) * atlasSize * 4, 0);
    for (size_t i = 0; i < d.pixels.size(); i += 4)
        d.pixels[i] = d.pixels[i + 1] = d.pixels[i + 2] = 255;
    d.texture = renderer.createTexture(atlasSize, atlasSize, d.pixels.data(), true);
    d.release = renderer.textureRelease(d.texture);

    // Warm up printable ASCII so the first frame of text doesn't stall.
    for (uint32_t c = 32; c < 127; c++)
        font->glyph(renderer, c);
    font->upload(renderer);
    return font;
}

const Font::Glyph& Font::glyph(Renderer& renderer, uint32_t codepoint)
{
    if (auto it = glyphs_.find(codepoint); it != glyphs_.end())
        return it->second;

    Data& d = *data_;
    int glyphIndex = stbtt_FindGlyphIndex(&d.info, static_cast<int>(codepoint));
    if (glyphIndex == 0 && codepoint != '?')
        return glyphs_[codepoint] = glyph(renderer, '?'); // missing characters show as '?'

    int advance = 0, bearing = 0;
    stbtt_GetGlyphHMetrics(&d.info, glyphIndex, &advance, &bearing);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&d.info, glyphIndex, scale_, scale_, &x0, &y0, &x1, &y1);
    const int w = x1 - x0;
    const int h = y1 - y0;

    Glyph g{};
    g.advance = advance * scale_;
    g.empty = w <= 0 || h <= 0;
    if (!g.empty)
    {
        // Shelf packing with a 1px gap so filtering never bleeds between glyphs.
        if (d.penX + w + 1 > atlasSize)
        {
            d.penX = 1;
            d.penY += d.rowHeight + 1;
            d.rowHeight = 0;
        }
        if (d.penY + h + 1 > atlasSize)
            return glyphs_[codepoint] = g; // atlas full; draws nothing rather than corrupting

        std::vector<unsigned char> coverage(static_cast<size_t>(w) * h);
        stbtt_MakeGlyphBitmap(&d.info, coverage.data(), w, h, w, scale_, scale_, glyphIndex);
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
                d.pixels[(static_cast<size_t>(d.penY + y) * atlasSize + d.penX + x) * 4 + 3] = coverage[static_cast<size_t>(y) * w + x];
        }
        g.uv = {static_cast<float>(d.penX) / atlasSize, static_cast<float>(d.penY) / atlasSize, static_cast<float>(w) / atlasSize,
            static_cast<float>(h) / atlasSize};
        g.x0 = static_cast<float>(x0);
        g.y0 = static_cast<float>(y0);
        g.x1 = static_cast<float>(x1);
        g.y1 = static_cast<float>(y1);
        d.penX += w + 1;
        d.rowHeight = std::max(d.rowHeight, h);
        d.dirty = true;
    }
    (void)renderer;
    return glyphs_[codepoint] = g;
}

void Font::upload(Renderer& renderer)
{
    if (data_->dirty)
    {
        renderer.updateTexture(data_->texture, data_->pixels.data());
        data_->dirty = false;
    }
}

void Font::draw(Renderer& renderer, Vec2 position, std::string_view text, Color color)
{
    Vec2 pen{std::round(position.x), std::round(position.y) + ascent_};
    const float left = pen.x;
    uint32_t previous = 0;
    for (size_t i = 0; i < text.size();)
    {
        const uint32_t cp = nextCodepoint(text, i);
        if (cp == '\n')
        {
            pen = {left, pen.y + lineHeight_};
            previous = 0;
            continue;
        }
        if (previous)
            pen.x += stbtt_GetCodepointKernAdvance(&data_->info, static_cast<int>(previous), static_cast<int>(cp)) * scale_;
        const Glyph& g = glyph(renderer, cp);
        if (!g.empty)
            renderer.drawSpriteRegion(data_->texture, {std::round(pen.x) + g.x0, pen.y + g.y0, g.x1 - g.x0, g.y1 - g.y0}, g.uv, color);
        pen.x += g.advance;
        previous = cp;
    }
    // New glyphs were added this call: upload before the batch is drawn.
    upload(renderer);
}

void Font::drawCentered(Renderer& renderer, const Rect& area, std::string_view text, Color color)
{
    const float width = measure(text);
    draw(renderer, {area.x + (area.w - width) / 2, area.y + (area.h - lineHeight_) / 2}, text, color);
}

float Font::measure(std::string_view text)
{
    float width = 0;
    float line = 0;
    uint32_t previous = 0;
    for (size_t i = 0; i < text.size();)
    {
        const uint32_t cp = nextCodepoint(text, i);
        if (cp == '\n')
        {
            width = std::max(width, line);
            line = 0;
            previous = 0;
            continue;
        }
        if (previous)
            line += stbtt_GetCodepointKernAdvance(&data_->info, static_cast<int>(previous), static_cast<int>(cp)) * scale_;
        auto it = glyphs_.find(cp);
        if (it != glyphs_.end())
            line += it->second.advance;
        else
        {
            int advance = 0, bearing = 0;
            stbtt_GetCodepointHMetrics(&data_->info, static_cast<int>(cp), &advance, &bearing);
            line += advance * scale_;
        }
        previous = cp;
    }
    return std::max(width, line);
}

std::vector<std::string> Font::wrap(std::string_view text, float maxWidth)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t newline = text.find('\n', start);
        std::string_view paragraph = text.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
        std::string line;
        size_t pos = 0;
        while (pos < paragraph.size())
        {
            size_t end = paragraph.find(' ', pos);
            if (end == std::string_view::npos)
                end = paragraph.size();
            const std::string word(paragraph.substr(pos, end - pos));
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && measure(candidate) > maxWidth)
            {
                lines.push_back(line);
                line = word;
            }
            else
            {
                line = candidate;
            }
            pos = end + 1;
        }
        lines.push_back(line);
        if (newline == std::string_view::npos)
            break;
        start = newline + 1;
    }
    return lines;
}

}
