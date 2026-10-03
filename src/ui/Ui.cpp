#include "yorehold/framework/ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

namespace yh
{

std::optional<UiTheme> UiTheme::fromJson(std::string_view json, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = nlohmann::json::parse(json);
        if (!j.is_object()) throw std::invalid_argument("Theme must be an object");
        UiTheme theme;
        const std::pair<const char*, Color*> fields[] = {{"panel", &theme.panel}, {"panelBorder", &theme.panelBorder},
            {"button", &theme.button}, {"buttonHover", &theme.buttonHover}, {"buttonPressed", &theme.buttonPressed},
            {"buttonDisabled", &theme.buttonDisabled}, {"text", &theme.text}, {"textDim", &theme.textDim},
            {"accent", &theme.accent}, {"good", &theme.good}, {"bad", &theme.bad}};
        for (const auto& [name, color] : fields)
        {
            if (!j.contains(name)) continue;
            const auto values = j.at(name).get<std::vector<int>>();
            if (values.size() != 4) throw std::invalid_argument("Theme colours need [r,g,b,a]");
            for (int v : values) if (v < 0 || v > 255) throw std::invalid_argument("Theme colour outside 0..255");
            *color = {static_cast<uint8_t>(values[0]), static_cast<uint8_t>(values[1]), static_cast<uint8_t>(values[2]), static_cast<uint8_t>(values[3])};
        }
        theme.textScale = j.value("textScale", theme.textScale);
        if (!std::isfinite(theme.textScale) || theme.textScale <= 0 || theme.textScale > 8) throw std::invalid_argument("Invalid theme text scale");
        return theme;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return std::nullopt; }
}

void Ui::begin(Renderer& renderer, const Input& input)
{
    renderer_ = &renderer;
    input_ = &input;
    clipState_ = {{}, renderer.bounds()};
    clipStack_.clear();
    if (!input.buttonDown(MouseButton::Left)) activeSlider_ = nullptr;
}

bool Ui::hovered(const Rect& area) const
{
    return input_ && input_->mouseInside() && clipState_.clip.contains(input_->mouse()) && area.contains(mousePosition());
}

void Ui::panel(const Rect& area)
{
    renderer_->fillRect(area, theme.panel);
    renderer_->drawRect(area, theme.panelBorder);
}

void Ui::label(Vec2 position, std::string_view text)
{
    label(position, text, theme.text);
}

void Ui::label(Vec2 position, std::string_view text, Color color, float scale)
{
    if (theme.font && scale <= 0)
        theme.font->draw(*renderer_, position, text, color);
    else
        renderer_->drawText(position, text, color, scale > 0 ? scale : theme.textScale);
}

bool Ui::button(const Rect& area, std::string_view text, bool enabled)
{
    const bool over = enabled && hovered(area);
    const Color fill = !enabled ? theme.buttonDisabled
                     : over && input_->buttonDown(MouseButton::Left) ? theme.buttonPressed
                     : over ? theme.buttonHover
                            : theme.button;
    renderer_->fillRect(area, fill);
    renderer_->drawRect(area, theme.panelBorder);

    const Color color = enabled ? theme.text : theme.textDim;
    if (theme.font)
    {
        theme.font->drawCentered(*renderer_, area, text, color);
    }
    else
    {
        const float width = Renderer::textWidth(text, theme.textScale);
        const float height = Renderer::lineHeight(theme.textScale) - 2 * theme.textScale;
        renderer_->drawText({area.x + (area.w - width) / 2, area.y + (area.h - height) / 2}, text, color, theme.textScale);
    }
    return over && input_->buttonClicked(MouseButton::Left)
        && area.contains(input_->buttonPressPosition(MouseButton::Left) - clipState_.offset);
}

bool Ui::checkbox(const Rect& area, std::string_view text, bool& value)
{
    const bool changed = toggle(area, std::string(value ? "[x] " : "[ ] ") + std::string(text), value);
    if (changed) value = !value;
    return changed;
}

bool Ui::slider(const Rect& area, float& value, float minimum, float maximum)
{
    if (area.w <= 0 || maximum <= minimum) return false;
    if (hovered(area) && input_->buttonPressed(MouseButton::Left)) activeSlider_ = &value;
    const float old = value;
    if (activeSlider_ == &value && input_->buttonDown(MouseButton::Left))
        value = minimum + (maximum - minimum) * std::clamp((mousePosition().x - area.x) / area.w, 0.0f, 1.0f);
    value = std::clamp(value, minimum, maximum);
    bar(area, (value - minimum) / (maximum - minimum), theme.accent);
    const float x = area.x + area.w * (value - minimum) / (maximum - minimum);
    renderer_->fillRect({x - 3, area.y, 6, area.h}, theme.text);
    return old != value;
}

float Ui::textWidth(std::string_view text) const
{
    return theme.font ? theme.font->measure(text) : Renderer::textWidth(text, theme.textScale);
}

size_t Ui::textIndexAt(std::string_view text, float x) const
{
    // Nearest code point boundary; text boxes are short, so measuring each prefix is fine.
    size_t best = 0;
    float bestDistance = std::abs(x);
    for (size_t i = TextEdit::next(text, 0); i <= text.size(); i = TextEdit::next(text, i))
    {
        const float distance = std::abs(textWidth(text.substr(0, i)) - x);
        if (distance < bestDistance) { best = i; bestDistance = distance; }
        if (i == text.size()) break;
    }
    return best;
}

void Ui::stopTextInput()
{
    activeText_.clear();
    selectingText_ = false;
    if (SDL_Window* window = SDL_GetKeyboardFocus()) SDL_StopTextInput(window);
}

bool Ui::textBox(std::string_view id, const Rect& area, std::string& value, size_t maxBytes)
{
    if (id.empty()) return false;
    constexpr float padding = 8;
    const bool over = hovered(area);
    bool focused = activeText_ == id;
    auto pointerIndex = [&] { return textIndexAt(value, mousePosition().x - area.x - padding + (focused ? textScroll_ : 0)); };
    if (input_->buttonPressed(MouseButton::Left))
    {
        if (over)
        {
            if (!focused)
            {
                activeText_ = id;
                textScroll_ = 0;
                edit_ = {};
                focused = true;
                if (SDL_Window* window = SDL_GetKeyboardFocus()) SDL_StartTextInput(window);
            }
            const size_t index = pointerIndex();
            if (input_->buttonClicks(MouseButton::Left) >= 3) edit_.selectAll(value);
            else if (input_->buttonClicks(MouseButton::Left) == 2) edit_.selectWord(value, index);
            else edit_.place(index, input_->shiftDown());
            selectingText_ = input_->buttonClicks(MouseButton::Left) < 2;
        }
        else if (focused)
        {
            stopTextInput();
            focused = false;
        }
    }
    bool changed = false;
    if (focused)
    {
        edit_.clamp(value);
        if (selectingText_ && input_->buttonDown(MouseButton::Left)) edit_.place(pointerIndex(), true);
        else selectingText_ = false;
        // While an input method is composing, it owns the arrow and delete keys.
        if (input_->composition().empty())
        {
            const bool shift = input_->shiftDown(), word = input_->shortcutDown();
            if (input_->keyPressed(SDLK_LEFT)) edit_.left(value, shift, word);
            if (input_->keyPressed(SDLK_RIGHT)) edit_.right(value, shift, word);
            if (input_->keyPressed(SDLK_HOME)) edit_.home(shift);
            if (input_->keyPressed(SDLK_END)) edit_.end(value, shift);
            if (input_->keyPressed(SDLK_BACKSPACE)) changed |= edit_.erase(value, false, word);
            if (input_->keyPressed(SDLK_DELETE)) changed |= edit_.erase(value, true, word);
            if (word && input_->keyPressed(SDLK_A)) edit_.selectAll(value);
            if (word && (input_->keyPressed(SDLK_C) || input_->keyPressed(SDLK_X)) && edit_.hasSelection())
            {
                SDL_SetClipboardText(std::string(edit_.selected(value)).c_str());
                if (input_->keyPressed(SDLK_X)) changed |= edit_.erase(value, false, false);
            }
            if (word && input_->keyPressed(SDLK_V))
                if (char* pasted = SDL_GetClipboardText())
                {
                    if (*pasted) changed |= edit_.insert(value, pasted, maxBytes);
                    SDL_free(pasted);
                }
        }
        if (!input_->text().empty()) changed |= edit_.insert(value, input_->text(), maxBytes);
        if (input_->keyPressed(SDLK_ESCAPE) || input_->keyPressed(SDLK_RETURN) || input_->keyPressed(SDLK_KP_ENTER))
        {
            stopTextInput();
            focused = false;
        }
    }

    // Composition text shows inline at the caret until the input method commits it.
    const std::string_view composing = focused ? input_->composition() : std::string_view();
    std::string shown = value;
    const size_t caret = focused ? edit_.caret : 0;
    size_t caretShown = caret;
    if (!composing.empty())
    {
        shown.insert(caret, composing);
        caretShown = caret;
        for (int i = 0; i < input_->compositionCursor() && caretShown < caret + composing.size(); ++i) caretShown = TextEdit::next(shown, caretShown);
    }
    const float caretX = textWidth(std::string_view(shown).substr(0, caretShown));
    const float visible = area.w - 2 * padding;
    if (!focused) textScroll_ = 0;
    else if (caretX - textScroll_ > visible) textScroll_ = caretX - visible;
    else if (caretX < textScroll_) textScroll_ = caretX;

    renderer_->fillRect(area, theme.buttonDisabled);
    renderer_->drawRect(area, focused ? theme.accent : theme.panelBorder);
    renderer_->pushViewport(area);
    const float left = padding - (focused ? textScroll_ : 0);
    const float lineH = theme.font ? theme.font->lineHeight() : Renderer::lineHeight(theme.textScale);
    if (focused && edit_.hasSelection() && composing.empty())
    {
        const auto [from, to] = edit_.selection();
        const float x0 = textWidth(std::string_view(value).substr(0, from)), x1 = textWidth(std::string_view(value).substr(0, to));
        renderer_->fillRect({left + x0, 4, x1 - x0, area.h - 8}, {theme.accent.r, theme.accent.g, theme.accent.b, 90});
    }
    label({left, 6}, shown);
    if (!composing.empty())
    {
        const float x0 = textWidth(std::string_view(shown).substr(0, caret));
        const float x1 = textWidth(std::string_view(shown).substr(0, caret + composing.size()));
        renderer_->drawLine({left + x0, 6 + lineH}, {left + x1, 6 + lineH}, theme.accent, 2);
    }
    if (focused)
    {
        renderer_->drawLine({left + caretX, 5}, {left + caretX, area.h - 5}, theme.accent);
        // Tell the OS where the caret is so input method candidate lists open next to it.
        if (SDL_Window* window = SDL_GetKeyboardFocus())
        {
            const float density = std::max(SDL_GetWindowPixelDensity(window), 0.01f);
            const Vec2 screen = clipState_.offset + area.position();
            const SDL_Rect where{static_cast<int>(screen.x / density), static_cast<int>(screen.y / density),
                static_cast<int>(area.w / density), static_cast<int>(area.h / density)};
            SDL_SetTextInputArea(window, &where, static_cast<int>((padding + caretX - textScroll_) / density));
        }
    }
    renderer_->pop();
    return changed;
}

void Ui::beginScroll(const Rect& area, float contentHeight, float& offset)
{
    if (hovered(area)) offset -= input_->wheel() * lineHeight() * 3;
    offset = std::clamp(offset, 0.0f, std::max(0.0f, contentHeight - area.h));
    clipStack_.push_back(clipState_);
    const Rect screen{area.x + clipState_.offset.x, area.y + clipState_.offset.y, area.w, area.h};
    clipState_.clip = clipState_.clip.intersect(screen);
    clipState_.offset = clipState_.offset + area.position() - Vec2{0, offset};
    renderer_->pushViewport(area);
    renderer_->pushTransform({0, -offset}, 1);
}

void Ui::endScroll()
{
    if (clipStack_.empty()) return;
    renderer_->pop();
    renderer_->pop();
    clipState_ = clipStack_.back();
    clipStack_.pop_back();
}

bool Ui::toggle(const Rect& area, std::string_view text, bool on)
{
    const bool clicked = button(area, text);
    if (on)
        renderer_->drawRect(area, theme.accent, 2);
    return clicked;
}

void Ui::bar(const Rect& area, float fraction, Color fill)
{
    renderer_->fillRect(area, theme.buttonDisabled);
    renderer_->fillRect({area.x, area.y, area.w * std::clamp(fraction, 0.0f, 1.0f), area.h}, fill);
    renderer_->drawRect(area, theme.panelBorder);
}

void Ui::log(const Rect& area, const std::vector<std::string>& lines)
{
    panel(area);
    if (theme.font)
    {
        logWithFont(area, lines);
        return;
    }
    const float scale = theme.textScale * 0.75f;
    const float line = Renderer::lineHeight(scale) + 2;
    const size_t fits = static_cast<size_t>(std::max(0.0f, (area.h - 12) / line));
    const size_t maxChars = static_cast<size_t>(std::max(8.0f, (area.w - 16) / Renderer::textWidth("M", scale)));

    // Wrap only as many recent entries as can show; long entries break at spaces.
    struct Wrapped
    {
        std::string text;
        bool newest;
    };
    std::vector<Wrapped> wrapped;
    for (size_t i = lines.size(); i-- > 0 && wrapped.size() < fits;)
    {
        std::vector<std::string> parts;
        std::string_view rest = lines[i];
        while (rest.size() > maxChars)
        {
            size_t cut = rest.rfind(' ', maxChars);
            if (cut == std::string_view::npos || cut == 0)
                cut = maxChars;
            parts.emplace_back(rest.substr(0, cut));
            rest = rest.substr(std::min(rest.size(), cut + 1));
        }
        parts.emplace_back(std::string("  ").substr(0, parts.empty() ? 0 : 2) + std::string(rest));
        for (size_t p = parts.size(); p-- > 0;)
            wrapped.push_back({parts[p], i + 1 == lines.size()});
    }

    renderer_->pushViewport(area);
    const size_t shown = std::min(wrapped.size(), fits);
    for (size_t k = 0; k < shown; k++)
    {
        const Wrapped& w = wrapped[shown - 1 - k];
        renderer_->drawText({8, 6 + k * line}, w.text, w.newest ? theme.text : theme.textDim, scale);
    }
    renderer_->pop();
}

void Ui::logWithFont(const Rect& area, const std::vector<std::string>& lines)
{
    Font& font = *theme.font;
    const float line = font.lineHeight() + 2;
    const size_t fits = static_cast<size_t>(std::max(0.0f, (area.h - 12) / line));
    std::vector<std::pair<std::string, bool>> wrapped; // newest first
    for (size_t i = lines.size(); i-- > 0 && wrapped.size() < fits;)
    {
        std::vector<std::string> parts = font.wrap(lines[i], area.w - 20);
        for (size_t p = parts.size(); p-- > 0;)
            wrapped.push_back({p == 0 ? parts[p] : "  " + parts[p], i + 1 == lines.size()});
    }
    renderer_->pushViewport(area);
    const size_t shown = std::min(wrapped.size(), fits);
    for (size_t k = 0; k < shown; k++)
    {
        const auto& [text, newest] = wrapped[shown - 1 - k];
        font.draw(*renderer_, {8, 6 + k * line}, text, newest ? theme.text : theme.textDim);
    }
    renderer_->pop();
}

}
