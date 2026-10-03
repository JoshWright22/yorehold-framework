#pragma once

#include "yorehold/framework/graphics/Renderer.h"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

class Font;

// Text with inline markup for logs, dialogue, tooltips and chat:
//   "[color=#ff8040]Fire[/color] deals [color=bad]12[/color] damage [icon=fire]"
//   "[wave]Welcome[/wave]  [shake]Danger![/shake]  [rainbow]Critical![/rainbow]"
// [[ is a literal [. Unknown or badly nested tags show as typed. Tags must close in reverse
// order. Escape anything a player typed with escape() so they can't inject tags.
class RichText
{
public:
    // Named colours for [color=name], e.g. a UiTheme's accent/good/bad.
    using Palette = std::map<std::string, Color, std::less<>>;
    // Draws an [icon=name] into `dest` (a line-height square), e.g. from an Atlas.
    using IconDrawer = std::function<void(Renderer&, std::string_view name, const Rect& dest)>;

    enum Effect : uint8_t { Wave = 1, Shake = 2, Rainbow = 4 };

    static RichText parse(std::string_view markup, const Palette* palette = nullptr);
    static std::string escape(std::string_view text);

    // The text without tags (icons excluded), for copying, search and screen readers.
    const std::string& plain() const { return plain_; }

    // Wraps at spaces to `maxWidth` (0 = only at line breaks). `font` null uses the debug font at
    // `debugScale`. Lay out again when the font or width changes.
    void layout(Font* font, float maxWidth = 0, float debugScale = 2);
    Vec2 size() const { return size_; }
    size_t lines() const { return lineCount_; }

    // `time` animates effects (seconds; pass a running clock). Colourless text uses `base`.
    void draw(Renderer& renderer, Vec2 position, Color base, double time = 0, const IconDrawer& icons = {}) const;

private:
    struct Style
    {
        Color color;
        bool colored = false;
        uint8_t effects = 0;
    };
    struct Piece
    {
        std::string text; // one code point, or an icon name
        bool icon = false;
        Style style;
    };
    struct Placed
    {
        size_t piece;
        Vec2 position;
        float width;
    };

    std::vector<Piece> pieces_;
    std::vector<Placed> placed_;
    std::string plain_;
    Font* font_ = nullptr;
    float scale_ = 2;
    float lineHeight_ = 0;
    Vec2 size_;
    size_t lineCount_ = 0;
};

}
