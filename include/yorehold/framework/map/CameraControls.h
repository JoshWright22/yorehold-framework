#pragma once

#include "yorehold/framework/graphics/Camera.h"
#include "yorehold/framework/input/Input.h"

#include <optional>

namespace yh
{

// Player-adjustable camera settings.
struct CameraSettings
{
    bool zoomToCursor = false; // default zooms toward the screen centre
    bool edgeScroll = false;
    bool followSelection = true; // BG3-style: follow the moving character until you pan away
    float keyPanSpeed = 900.0f;  // screen pixels per second
    float edgeScrollSpeed = 900.0f;
    float edgeScrollMargin = 12.0f;
    float wheelZoomStep = 1.15f;
};

// Turns input into camera movement: drag/key/edge panning, wheel/key zoom, follow and recenter.
class CameraControls
{
public:
    CameraSettings settings;

    // Call once per frame before drawing. `follow` is where the selected character is, if any.
    void update(Camera& camera, const Input& input, double deltaSeconds, std::optional<Vec2> follow = {});

    // True while the camera tracks `follow`; any manual pan turns it off, recenter turns it back on.
    bool following() const { return following_; }

private:
    bool following_ = true;
    bool dragging_ = false;
};

}
