#pragma once

#include "yorehold/framework/graphics/Font.h"
#include "yorehold/framework/graphics/Renderer.h"
#include "yorehold/framework/input/Input.h"
#include "yorehold/framework/ui/TextEdit.h"

#include <string>
#include <optional>
#include <string_view>
#include <vector>

namespace yh
{

// Colours and a font for every widget, loaded from skin data.
struct UiTheme
{
    Color panel{18, 20, 30, 235};
    Color panelBorder{70, 74, 96, 255};
    Color button{44, 48, 70, 255};
    Color buttonHover{66, 72, 104, 255};
    Color buttonPressed{90, 98, 140, 255};
    Color buttonDisabled{30, 32, 42, 255};
    Color text{230, 230, 240, 255};
    Color textDim{140, 142, 160, 255};
    Color accent{255, 196, 64, 255};
    Color good{110, 200, 110, 255};
    Color bad{220, 90, 80, 255};
    // Frame shape, in pixels: outline thickness, light/dark bevel inside it, and a drop shadow
    // offset down-right. 1/0/0 is a thin flat look; ~3/2/4 gives a chunky blocky one.
    float border = 1;
    float bevel = 0;
    float shadow = 0;
    Color shadowColor{0, 0, 0, 140};
    float textScale = 2.0f; // debug-font scale, used when there's no font
    Font* font = nullptr;   // body text; set this for real text
    static std::optional<UiTheme> fromJson(std::string_view json, std::string* error = nullptr);
};

// Immediate-mode UI: call widgets every frame while drawing; they return true when clicked.
// Skin themes supply colours and fonts; focus and pointer capture persist between frames.
class Ui
{
public:
    UiTheme theme;

    // Call once per frame before any widgets, with the input for this frame.
    void begin(Renderer& renderer, const Input& input);

    void panel(const Rect& area);
    void label(Vec2 position, std::string_view text);
    void label(Vec2 position, std::string_view text, Color color, float scale = 0);
    bool button(const Rect& area, std::string_view text, bool enabled = true);
    // A button that shows as pressed when `on`; returns true when clicked.
    bool toggle(const Rect& area, std::string_view text, bool on);
    bool checkbox(const Rect& area, std::string_view text, bool& value);
    bool slider(const Rect& area, float& value, float minimum, float maximum);
    // One-line UTF-8 editing: mouse/Shift selection, double-click word, triple-click all, Ctrl word
    // jumps and deletes, Ctrl+A/C/X/V, and inline input-method composition. Enter/Esc unfocus.
    bool textBox(std::string_view id, const Rect& area, std::string& value, size_t maxBytes = 4096);
    bool editing(std::string_view id) const { return activeText_ == id; }
    void beginScroll(const Rect& area, float contentHeight, float& offset);
    void endScroll();
    void bar(const Rect& area, float fraction, Color fill);
    // Last `lines` that fit, newest at the bottom.
    void log(const Rect& area, const std::vector<std::string>& lines);
    bool hovered(const Rect& area) const;

    float lineHeight() const { return theme.font ? theme.font->lineHeight() + 6 : Renderer::lineHeight(theme.textScale) + 6; }

private:
    // Shadow, fill, bevel and outline in the theme's frame style.
    void frame(const Rect& area, Color fill, Color outline, bool sunken = false);
    void logWithFont(const Rect& area, const std::vector<std::string>& lines);
    float textWidth(std::string_view text) const;
    size_t textIndexAt(std::string_view text, float x) const;
    void stopTextInput();

    Renderer* renderer_ = nullptr;
    const Input* input_ = nullptr;
    std::string activeText_;
    TextEdit edit_;
    float textScroll_ = 0;
    bool selectingText_ = false;
    const float* activeSlider_ = nullptr;
    struct ClipState { Vec2 offset; Rect clip; };
    ClipState clipState_;
    std::vector<ClipState> clipStack_;
    Vec2 mousePosition() const { return input_->mouse() - clipState_.offset; }
};

}
