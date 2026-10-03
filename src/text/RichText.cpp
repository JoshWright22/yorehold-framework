#include "yorehold/framework/text/RichText.h"

#include "yorehold/framework/graphics/Font.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>

namespace yh
{

namespace
{

constexpr size_t longestTag = 64;

std::optional<Color> parseColor(std::string_view value, const RichText::Palette* palette)
{
    if (palette)
        if (const auto named = palette->find(value); named != palette->end()) return named->second;
    if (value.size() != 7 && value.size() != 9) return std::nullopt;
    if (value[0] != '#') return std::nullopt;
    uint8_t channels[4] = {0, 0, 0, 255};
    for (size_t c = 0; c * 2 + 1 < value.size(); ++c)
    {
        int byte = 0;
        for (size_t k = 1 + c * 2; k < 3 + c * 2; ++k)
        {
            const char h = static_cast<char>(std::tolower(static_cast<unsigned char>(value[k])));
            if (h >= '0' && h <= '9') byte = byte * 16 + (h - '0');
            else if (h >= 'a' && h <= 'f') byte = byte * 16 + (h - 'a' + 10);
            else return std::nullopt;
        }
        channels[c] = static_cast<uint8_t>(byte);
    }
    return Color{channels[0], channels[1], channels[2], channels[3]};
}

size_t codepointLength(unsigned char lead)
{
    return lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
}

Color hue(float h, uint8_t alpha)
{
    const float r = std::clamp(std::abs(h * 6 - 3) - 1, 0.0f, 1.0f);
    const float g = std::clamp(2 - std::abs(h * 6 - 2), 0.0f, 1.0f);
    const float b = std::clamp(2 - std::abs(h * 6 - 4), 0.0f, 1.0f);
    return {static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(b * 255), alpha};
}

// Cheap repeatable noise for shaking letters: same letter + same tick = same offset.
float jitter(size_t index, long long tick, int axis)
{
    uint32_t x = static_cast<uint32_t>(index * 73856093u) ^ static_cast<uint32_t>(tick * 19349663) ^ static_cast<uint32_t>(axis * 83492791);
    x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15;
    return static_cast<float>(x & 0xffff) / 32767.5f - 1;
}

}

RichText RichText::parse(std::string_view markup, const Palette* palette)
{
    RichText text;
    Style style;
    std::vector<std::pair<std::string, Style>> open; // tag kind and the style before it
    auto handleTag = [&](std::string_view tag) {
        if (!tag.empty() && tag[0] == '/')
        {
            if (open.empty() || open.back().first != tag.substr(1)) return false;
            style = open.back().second;
            open.pop_back();
            return true;
        }
        const size_t equals = tag.find('=');
        const std::string_view name = tag.substr(0, equals);
        const std::string_view value = equals == std::string_view::npos ? std::string_view() : tag.substr(equals + 1);
        if (name == "icon" && !value.empty())
        {
            text.pieces_.push_back({std::string(value), true, style});
            return true;
        }
        if (name == "color")
        {
            const auto color = parseColor(value, palette);
            if (!color) return false;
            open.emplace_back("color", style);
            style.color = *color;
            style.colored = true;
            return true;
        }
        const uint8_t effect = name == "wave" ? Wave : name == "shake" ? Shake : name == "rainbow" ? Rainbow : 0;
        if (!effect || !value.empty()) return false;
        open.emplace_back(std::string(name), style);
        style.effects |= effect;
        return true;
    };
    for (size_t i = 0; i < markup.size();)
    {
        if (markup[i] == '[')
        {
            if (i + 1 < markup.size() && markup[i + 1] == '[')
            {
                text.pieces_.push_back({"[", false, style});
                text.plain_ += '[';
                i += 2;
                continue;
            }
            const size_t close = markup.find(']', i);
            if (close != std::string_view::npos && close - i <= longestTag && handleTag(markup.substr(i + 1, close - i - 1)))
            {
                i = close + 1;
                continue;
            }
        }
        const size_t length = std::min(codepointLength(static_cast<unsigned char>(markup[i])), markup.size() - i);
        text.pieces_.push_back({std::string(markup.substr(i, length)), false, style});
        text.plain_ += markup.substr(i, length);
        i += length;
    }
    return text;
}

std::string RichText::escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
    {
        out += c;
        if (c == '[') out += '[';
    }
    return out;
}

void RichText::layout(Font* font, float maxWidth, float debugScale)
{
    font_ = font;
    scale_ = debugScale;
    lineHeight_ = font ? font->lineHeight() : Renderer::lineHeight(debugScale);
    auto measure = [&](std::string_view s) { return font ? font->measure(s) : Renderer::textWidth(s, debugScale); };
    auto isBreak = [&](size_t i) { return !pieces_[i].icon && (pieces_[i].text == " " || pieces_[i].text == "\n"); };

    placed_.clear();
    float x = 0, y = 0, widest = 0;
    size_t lines = pieces_.empty() ? 0 : 1;
    std::vector<float> widths;
    for (size_t i = 0; i < pieces_.size();)
    {
        if (!pieces_[i].icon && pieces_[i].text == "\n") { x = 0; y += lineHeight_; ++lines; ++i; continue; }
        if (isBreak(i)) { x += measure(" "); ++i; continue; }
        // A word runs to the next space or line break, across style changes.
        size_t end = i;
        widths.clear();
        float wordWidth = 0;
        for (; end < pieces_.size() && !isBreak(end); ++end)
        {
            const Piece& piece = pieces_[end];
            float w = lineHeight_ + 2;
            if (!piece.icon)
            {
                // Include kerning against the previous letter of the same word.
                const bool joined = end > i && !pieces_[end - 1].icon;
                w = joined ? measure(pieces_[end - 1].text + piece.text) - measure(pieces_[end - 1].text) : measure(piece.text);
            }
            widths.push_back(w);
            wordWidth += w;
        }
        if (maxWidth > 0 && x > 0 && x + wordWidth > maxWidth) { x = 0; y += lineHeight_; ++lines; }
        for (size_t k = i; k < end; ++k)
        {
            const float w = widths[k - i];
            if (maxWidth > 0 && x > 0 && x + w > maxWidth) { x = 0; y += lineHeight_; ++lines; } // longer than a whole line
            placed_.push_back({k, {x, y}, w});
            x += w;
            widest = std::max(widest, x);
        }
        i = end;
    }
    lineCount_ = lines;
    size_ = {widest, static_cast<float>(lines) * lineHeight_};
}

void RichText::draw(Renderer& renderer, Vec2 position, Color base, double time, const IconDrawer& icons) const
{
    const long long tick = static_cast<long long>(time * 20);
    for (const Placed& p : placed_)
    {
        const Piece& piece = pieces_[p.piece];
        Vec2 at = position + p.position;
        Color color = piece.style.colored ? piece.style.color : base;
        const float index = static_cast<float>(p.piece);
        if (piece.style.effects & Wave) at.y += std::sin(static_cast<float>(time) * 6 - index * 0.45f) * lineHeight_ * 0.12f;
        if (piece.style.effects & Shake) at = at + Vec2{jitter(p.piece, tick, 0), jitter(p.piece, tick, 1)} * (lineHeight_ * 0.06f);
        if (piece.style.effects & Rainbow) color = hue(static_cast<float>(std::fmod(time * 0.5 + index * 0.06, 1.0)), color.a);
        if (piece.icon)
        {
            if (icons) icons(renderer, piece.text, {at.x + 1, at.y, lineHeight_, lineHeight_});
        }
        else if (font_) font_->draw(renderer, at, piece.text, color);
        else renderer.drawText(at, piece.text, color, scale_);
    }
}

}
