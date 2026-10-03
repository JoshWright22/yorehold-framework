#include "yorehold/framework/input/Input.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

#include <algorithm>
#include <cmath>

namespace yh
{

void InputMap::bind(std::string_view action, Binding binding)
{
    if (action.empty() || (binding.type == Binding::Type::Mouse && (static_cast<size_t>(binding.button) == 0 || static_cast<size_t>(binding.button) >= 6)))
        return;
    auto it = actions_.find(action);
    if (it == actions_.end())
        it = actions_.emplace(std::string(action), std::vector<Binding>{}).first;
    if (binding.type == Binding::Type::GamepadButton && binding.pad >= SDL_GAMEPAD_BUTTON_COUNT) return;
    if (binding.type == Binding::Type::GamepadAxis && binding.pad >= SDL_GAMEPAD_AXIS_COUNT) return;
    for (const auto& b : it->second)
        if (b == binding) return;
    it->second.push_back(binding);
}

void InputMap::unbind(std::string_view action)
{
    if (auto it = actions_.find(action); it != actions_.end())
        it->second.clear();
}

const std::vector<Binding>& InputMap::bindings(std::string_view action) const
{
    static const std::vector<Binding> none;
    const auto it = actions_.find(action);
    return it == actions_.end() ? none : it->second;
}

void Input::handle(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
        if (event.key.repeat) keysRepeated_.insert(event.key.key);
        if (!event.key.repeat)
        {
            keysDown_.insert(event.key.key);
            keysPressed_.insert(event.key.key);
        }
        break;
    case SDL_EVENT_KEY_UP:
        keysDown_.erase(event.key.key);
        keysReleased_.insert(event.key.key);
        break;
    case SDL_EVENT_MOUSE_MOTION:
    {
        const Vec2 now{event.motion.x, event.motion.y};
        mouseDelta_ = mouseDelta_ + (now - mouse_);
        mouse_ = now;
        mouseInside_ = true;
        for (ButtonState& button : buttons_)
        {
            const Vec2 moved = now - button.pressedAt;
            if (button.down && std::sqrt(moved.x * moved.x + moved.y * moved.y) > dragThreshold)
                button.dragged = true;
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button < buttons_.size())
        {
            ButtonState& button = buttons_[event.button.button];
            button = {true, true, false, false, {event.button.x, event.button.y}, event.button.clicks};
            mouse_ = button.pressedAt;
            mouseInside_ = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button < buttons_.size())
        {
            ButtonState& button = buttons_[event.button.button];
            const Vec2 at{event.button.x, event.button.y};
            const Vec2 moved = at - button.pressedAt;
            button.dragged |= moved.x * moved.x + moved.y * moved.y > dragThreshold * dragThreshold;
            button.released = button.down;
            button.down = false;
            mouse_ = at;
            mouseInside_ = true;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        wheel_ += event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (event.text.text) text_ += event.text.text;
        composition_.clear();
        compositionCursor_ = 0;
        break;
    case SDL_EVENT_TEXT_EDITING:
        composition_ = event.edit.text ? event.edit.text : "";
        compositionCursor_ = event.edit.start;
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        mouseInside_ = false;
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if (std::none_of(pads_.begin(), pads_.end(), [&](const Pad& p) { return p.id == event.gdevice.which; }))
            if (SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which))
                pads_.push_back({event.gdevice.which, std::shared_ptr<SDL_Gamepad>(pad, SDL_CloseGamepad)});
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        std::erase_if(pads_, [&](const Pad& p) { return p.id == event.gdevice.which; });
        updatePads();
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (event.gbutton.button < 32)
        {
            padFor(event.gbutton.which).buttons[event.gbutton.button] = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            updatePads();
        }
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (event.gaxis.axis < 6)
        {
            padFor(event.gaxis.which).axes[event.gaxis.axis] = std::clamp(event.gaxis.value / 32767.0f, -1.0f, 1.0f);
            updatePads();
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // Pads keep their handles, but held buttons and tilted sticks are forgotten like keys.
        for (Pad& pad : pads_) { pad.buttons = {}; pad.axes = {}; }
        updatePads();
        mouseInside_ = false;
        keysDown_.clear();
        keysPressed_.clear();
        keysRepeated_.clear();
        keysReleased_.clear();
        text_.clear();
        composition_.clear();
        buttons_ = {};
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        // Keyboard focus does not tell us whether the pointer is inside the window.
        break;
    default:
        break;
    }
}

void Input::endFrame()
{
    keysPressed_.clear();
    keysReleased_.clear();
    keysRepeated_.clear();
    text_.clear();
    for (ButtonState& button : buttons_)
    {
        button.pressed = false;
        // Keep `dragged` after release for one frame so clicked() can tell drags apart.
        if (button.released)
        {
            button.released = false;
            button.dragged = false;
        }
    }
    for (ButtonState& button : padButtons_) button.pressed = button.released = false;
    for (ButtonState& half : axisHalves_) half.pressed = half.released = false;
    mouseDelta_ = {};
    wheel_ = 0;
}

bool Input::shortcutDown() const
{
#if defined(__APPLE__)
    return keysDown_.contains(SDLK_LGUI) || keysDown_.contains(SDLK_RGUI);
#else
    return keysDown_.contains(SDLK_LCTRL) || keysDown_.contains(SDLK_RCTRL);
#endif
}

bool Input::shiftDown() const
{
    return keysDown_.contains(SDLK_LSHIFT) || keysDown_.contains(SDLK_RSHIFT);
}

Input::Pad& Input::padFor(uint32_t id)
{
    for (Pad& pad : pads_) if (pad.id == id) return pad;
    // Events can arrive before (or without) the added event, e.g. in scripted tests.
    return pads_.emplace_back(Pad{id, nullptr});
}

void Input::updatePads()
{
    auto set = [](ButtonState& state, bool down) {
        state.pressed |= down && !state.down;
        state.released |= !down && state.down;
        state.down = down;
    };
    for (size_t b = 0; b < padButtons_.size(); ++b)
        set(padButtons_[b], std::any_of(pads_.begin(), pads_.end(), [&](const Pad& p) { return p.buttons[b]; }));
    for (size_t a = 0; a < axes_.size(); ++a)
    {
        // The most-pushed pad wins, so a resting second pad can't cancel the first.
        float strongest = 0;
        for (const Pad& pad : pads_) if (std::abs(pad.axes[a]) > std::abs(strongest)) strongest = pad.axes[a];
        const float magnitude = std::abs(strongest);
        axes_[a] = magnitude <= stickDeadZone ? 0 : std::copysign((magnitude - stickDeadZone) / (1 - stickDeadZone), strongest);
        set(axisHalves_[a * 2], strongest <= -axisPressThreshold);
        set(axisHalves_[a * 2 + 1], strongest >= axisPressThreshold);
    }
}

float Input::axis(uint8_t sdlAxis) const
{
    return sdlAxis < axes_.size() ? axes_[sdlAxis] : 0;
}

float Input::value(std::string_view action) const
{
    float best = 0;
    for (const Binding& binding : map_.bindings(action))
    {
        if (binding.type == Binding::Type::GamepadAxis) best = std::max(best, axis(binding.pad) * binding.direction);
        else if (const ButtonState* button = buttonFor(binding); button ? button->down : keysDown_.contains(binding.key)) return 1;
    }
    return best;
}

const Input::ButtonState* Input::buttonFor(const Binding& binding) const
{
    switch (binding.type)
    {
    case Binding::Type::Mouse: return &buttons_[static_cast<size_t>(binding.button)];
    case Binding::Type::GamepadButton: return &padButtons_[binding.pad % padButtons_.size()];
    case Binding::Type::GamepadAxis: return &axisHalves_[(binding.pad % axes_.size()) * 2 + (binding.direction > 0)];
    case Binding::Type::Key: break;
    }
    return nullptr;
}

bool Input::down(std::string_view action) const
{
    for (const Binding& binding : map_.bindings(action))
    {
        const ButtonState* button = buttonFor(binding);
        if (button ? button->down : keysDown_.contains(binding.key))
            return true;
    }
    return false;
}

bool Input::pressed(std::string_view action) const
{
    for (const Binding& binding : map_.bindings(action))
    {
        const ButtonState* button = buttonFor(binding);
        if (button ? button->pressed : keysPressed_.contains(binding.key))
            return true;
    }
    return false;
}

bool Input::released(std::string_view action) const
{
    for (const Binding& binding : map_.bindings(action))
    {
        const ButtonState* button = buttonFor(binding);
        if (button ? button->released : keysReleased_.contains(binding.key))
            return true;
    }
    return false;
}

bool Input::clicked(std::string_view action) const
{
    for (const Binding& binding : map_.bindings(action))
    {
        // Mouse clicks fire on release (a press may become a drag); keys and pad buttons on press.
        const ButtonState* button = buttonFor(binding);
        if (binding.type == Binding::Type::Mouse ? button->released && !button->dragged : button ? button->pressed : keysPressed_.contains(binding.key))
            return true;
    }
    return false;
}

bool Input::dragging(std::string_view action) const
{
    for (const Binding& binding : map_.bindings(action))
    {
        const ButtonState* button = buttonFor(binding);
        if (button && button->down && button->dragged)
            return true;
    }
    return false;
}

Vec2 Input::pressPosition(std::string_view action) const
{
    for (const Binding& binding : map_.bindings(action))
    {
        if (binding.type == Binding::Type::Mouse)
            return buttonFor(binding)->pressedAt;
    }
    return mouse_;
}

}
