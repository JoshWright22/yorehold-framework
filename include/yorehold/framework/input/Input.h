#pragma once

#include "yorehold/framework/graphics/Types.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

union SDL_Event;
struct SDL_Gamepad;

namespace yh
{

// Same numbers as SDL_BUTTON_LEFT / MIDDLE / RIGHT.
enum class MouseButton : uint8_t
{
    Left = 1,
    Middle = 2,
    Right = 3,
};

// One key, mouse button, gamepad button or half of a gamepad axis that triggers an action.
struct Binding
{
    enum class Type : uint8_t { Key, Mouse, GamepadButton, GamepadAxis };

    Type type = Type::Key;
    uint32_t key = 0; // SDL_Keycode
    MouseButton button = MouseButton::Left;
    uint8_t pad = 0;        // SDL_GamepadButton or SDL_GamepadAxis
    int8_t direction = 1;   // which half of an axis: +1 (right/down/pulled) or -1 (left/up)

    static Binding keyboard(uint32_t key) { return {Type::Key, key, MouseButton::Left}; }
    static Binding mouse(MouseButton button) { return {Type::Mouse, 0, button}; }
    static Binding gamepadButton(uint8_t sdlButton) { return {Type::GamepadButton, 0, MouseButton::Left, sdlButton, 1}; }
    static Binding gamepadAxis(uint8_t sdlAxis, int8_t direction) { return {Type::GamepadAxis, 0, MouseButton::Left, sdlAxis, direction < 0 ? int8_t(-1) : int8_t(1)}; }
    bool operator==(const Binding&) const = default;
};

// Named actions ("camera.pan", "select"...) and the buttons bound to each.
// Game code asks about actions, never raw keys, so every control can be rebound.
class InputMap
{
public:
    void bind(std::string_view action, Binding binding);
    void unbind(std::string_view action);
    const std::vector<Binding>& bindings(std::string_view action) const;
    std::string toJson() const;
    static std::optional<InputMap> fromJson(std::string_view json, std::string* error = nullptr);

private:
    std::map<std::string, std::vector<Binding>, std::less<>> actions_;
};

// Tracks what's held this frame. Feed it every event with handle(), and call endFrame()
// once per frame after reading it, which clears the "this frame only" state.
class Input
{
public:
    // A press that moves further than this (in pixels) counts as a drag, not a click.
    static constexpr float dragThreshold = 5.0f;
    // Stick movement below this is ignored (worn sticks rest slightly off centre).
    static constexpr float stickDeadZone = 0.2f;
    // How far an axis must move to count as a press of its bound action.
    static constexpr float axisPressThreshold = 0.5f;

    void setMap(InputMap map) { map_ = std::move(map); }
    const InputMap& map() const { return map_; }

    void handle(const SDL_Event& event);
    void endFrame();

    bool down(std::string_view action) const;
    bool pressed(std::string_view action) const;
    bool released(std::string_view action) const;
    // Released without having been dragged.
    bool clicked(std::string_view action) const;
    // Held on a mouse button and moved past dragThreshold since the press.
    bool dragging(std::string_view action) const;
    Vec2 pressPosition(std::string_view action) const;
    // 0..1: analog for gamepad axes (past the dead zone), 1 for any held key or button.
    float value(std::string_view action) const;

    // Gamepads are opened when SDL reports them (Host initialises SDL's gamepad support).
    // Buttons and axes combine across all connected pads.
    size_t gamepads() const { return pads_.size(); }
    // -1..1 with the dead zone removed; SDL_GamepadAxis numbering. Triggers are 0..1.
    float axis(uint8_t sdlAxis) const;
    bool gamepadButtonDown(uint8_t sdlButton) const { return sdlButton < padButtons_.size() && padButtons_[sdlButton].down; }

    Vec2 mouse() const { return mouse_; }
    Vec2 mouseDelta() const { return mouseDelta_; }
    float wheel() const { return wheel_; }
    // Pointer motion or a button event establishes its position; leave/focus loss clears this.
    bool mouseInside() const { return mouseInside_; }
    bool keyDown(uint32_t key) const { return keysDown_.contains(key); }
    bool keyPressed(uint32_t key) const { return keysPressed_.contains(key) || keysRepeated_.contains(key); }
    bool buttonPressed(MouseButton button) const { return buttons_[static_cast<size_t>(button)].pressed; }
    Vec2 buttonPressPosition(MouseButton button) const { return buttons_[static_cast<size_t>(button)].pressedAt; }
    std::string_view text() const { return text_; }
    // Text still being composed in an input method (Japanese, Chinese, Korean, dead keys...).
    // It isn't committed yet; show it at the caret, underlined. Cursor is in code points.
    std::string_view composition() const { return composition_; }
    int compositionCursor() const { return compositionCursor_; }
    // Ctrl, or Cmd on macOS: the shortcut modifier.
    bool shortcutDown() const;
    bool shiftDown() const;
    // 1 for a single click, 2 for a double click... as counted by the OS for the latest press.
    int buttonClicks(MouseButton button) const { return buttons_[static_cast<size_t>(button)].clicks; }
    // Raw buttons, for UI widgets that don't go through rebindable actions.
    bool buttonDown(MouseButton button) const { return buttons_[static_cast<size_t>(button)].down; }
    bool buttonClicked(MouseButton button) const
    {
        const ButtonState& b = buttons_[static_cast<size_t>(button)];
        return b.released && !b.dragged;
    }

private:
    struct ButtonState
    {
        bool down = false;
        bool pressed = false;
        bool released = false;
        bool dragged = false;
        Vec2 pressedAt;
        int clicks = 0;
    };

    const ButtonState* buttonFor(const Binding& binding) const;
    struct Pad;
    Pad& padFor(uint32_t id);
    void updatePads();

    struct Pad
    {
        uint32_t id;
        std::shared_ptr<SDL_Gamepad> handle; // shared so copies of Input don't close it twice
        std::array<float, 6> axes{};
        std::array<bool, 32> buttons{};
    };
    std::vector<Pad> pads_;
    std::array<float, 6> axes_{};
    std::array<ButtonState, 32> padButtons_{};
    // Each axis half (axis * 2 + positive) acts like a button for pressed()/released().
    std::array<ButtonState, 12> axisHalves_{};

    InputMap map_;
    std::unordered_set<uint32_t> keysDown_;
    std::unordered_set<uint32_t> keysPressed_;
    std::unordered_set<uint32_t> keysReleased_;
    std::unordered_set<uint32_t> keysRepeated_;
    std::string text_;
    std::string composition_;
    int compositionCursor_ = 0;
    std::array<ButtonState, 6> buttons_{};
    Vec2 mouse_;
    Vec2 mouseDelta_;
    float wheel_ = 0;
    bool mouseInside_ = false;
};

}
