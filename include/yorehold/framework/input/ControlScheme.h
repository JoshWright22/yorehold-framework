#pragma once

#include "yorehold/framework/input/Input.h"

namespace yh
{

// Action names shared by the map controls. Bindings for them come from a ControlScheme.
namespace actions
{
inline constexpr std::string_view panDrag = "camera.panDrag"; // hold and drag to pan
inline constexpr std::string_view panUp = "camera.panUp";
inline constexpr std::string_view panDown = "camera.panDown";
inline constexpr std::string_view panLeft = "camera.panLeft";
inline constexpr std::string_view panRight = "camera.panRight";
inline constexpr std::string_view zoomIn = "camera.zoomIn";
inline constexpr std::string_view zoomOut = "camera.zoomOut";
inline constexpr std::string_view recenter = "camera.recenter"; // snap back to the selected character
inline constexpr std::string_view select = "select";              // click a token, drag to box-select or move it
inline constexpr std::string_view moveTo = "moveTo";              // click the ground to walk the selection there
inline constexpr std::string_view contextMenu = "contextMenu";
inline constexpr std::string_view addToSelection = "addToSelection"; // held while selecting
}

enum class ControlPreset
{
    Foundry, // right-drag pans, left selects/drags tokens, right-click ground walks there
    BG3,     // left selects and walks, right-click menu, WASD / middle-drag / screen edges pan
};

struct ControlScheme
{
    ControlPreset preset = ControlPreset::Foundry;
    const char* name = "";
    InputMap map;
    bool edgeScroll = false;
};

ControlScheme makeControlScheme(ControlPreset preset);

}
