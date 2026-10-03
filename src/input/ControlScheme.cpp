#include "yorehold/framework/input/ControlScheme.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keycode.h>

namespace yh
{

ControlScheme makeControlScheme(ControlPreset preset)
{
    ControlScheme scheme;
    scheme.preset = preset;
    InputMap& map = scheme.map;

    // Shared by both presets.
    map.bind(actions::panUp, Binding::keyboard(SDLK_UP));
    map.bind(actions::panDown, Binding::keyboard(SDLK_DOWN));
    map.bind(actions::panLeft, Binding::keyboard(SDLK_LEFT));
    map.bind(actions::panRight, Binding::keyboard(SDLK_RIGHT));
    map.bind(actions::zoomIn, Binding::keyboard(SDLK_EQUALS));
    map.bind(actions::zoomIn, Binding::keyboard(SDLK_KP_PLUS));
    map.bind(actions::zoomOut, Binding::keyboard(SDLK_MINUS));
    map.bind(actions::zoomOut, Binding::keyboard(SDLK_KP_MINUS));
    map.bind(actions::recenter, Binding::keyboard(SDLK_HOME));
    map.bind(actions::select, Binding::mouse(MouseButton::Left));
    map.bind(actions::addToSelection, Binding::keyboard(SDLK_LSHIFT));
    map.bind(actions::addToSelection, Binding::keyboard(SDLK_RSHIFT));
    // Gamepad camera: either stick or the d-pad pans, shoulders zoom, Back recentres.
    for (const SDL_GamepadAxis stick : {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_RIGHTX})
    {
        map.bind(actions::panLeft, Binding::gamepadAxis(static_cast<uint8_t>(stick), -1));
        map.bind(actions::panRight, Binding::gamepadAxis(static_cast<uint8_t>(stick), 1));
        map.bind(actions::panUp, Binding::gamepadAxis(static_cast<uint8_t>(stick + 1), -1));
        map.bind(actions::panDown, Binding::gamepadAxis(static_cast<uint8_t>(stick + 1), 1));
    }
    map.bind(actions::panUp, Binding::gamepadButton(SDL_GAMEPAD_BUTTON_DPAD_UP));
    map.bind(actions::panDown, Binding::gamepadButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    map.bind(actions::panLeft, Binding::gamepadButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
    map.bind(actions::panRight, Binding::gamepadButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    map.bind(actions::zoomIn, Binding::gamepadButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    map.bind(actions::zoomOut, Binding::gamepadButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    map.bind(actions::recenter, Binding::gamepadButton(SDL_GAMEPAD_BUTTON_BACK));

    switch (preset)
    {
    case ControlPreset::Foundry:
        scheme.name = "Foundry";
        map.bind(actions::panDrag, Binding::mouse(MouseButton::Right));
        // Right-click without dragging: walk there (or a menu when clicking a token).
        map.bind(actions::moveTo, Binding::mouse(MouseButton::Right));
        map.bind(actions::contextMenu, Binding::mouse(MouseButton::Right));
        break;

    case ControlPreset::BG3:
        scheme.name = "BG3";
        scheme.edgeScroll = true;
        map.bind(actions::panDrag, Binding::mouse(MouseButton::Middle));
        map.bind(actions::panUp, Binding::keyboard(SDLK_W));
        map.bind(actions::panDown, Binding::keyboard(SDLK_S));
        map.bind(actions::panLeft, Binding::keyboard(SDLK_A));
        map.bind(actions::panRight, Binding::keyboard(SDLK_D));
        map.bind(actions::moveTo, Binding::mouse(MouseButton::Left));
        map.bind(actions::contextMenu, Binding::mouse(MouseButton::Right));
        break;
    }
    return scheme;
}

}
