#include "yorehold/framework/map/CameraControls.h"

#include "yorehold/framework/input/ControlScheme.h"

#include <algorithm>
#include <cmath>

namespace yh
{

void CameraControls::update(Camera& camera, const Input& input, double deltaSeconds, std::optional<Vec2> follow)
{
    const float dt = static_cast<float>(deltaSeconds);
    bool panned = false;

    const bool dragging = input.dragging(actions::panDrag);
    if (dragging)
    {
        // On the first drag frame, catch up the few pixels moved before it counted as a drag.
        camera.panByScreen(dragging_ ? input.mouseDelta() : input.mouse() - input.pressPosition(actions::panDrag));
        panned = true;
    }
    dragging_ = dragging;

    // Analog: keys give full speed, a half-tilted stick pans at half speed.
    const Vec2 direction{input.value(actions::panRight) - input.value(actions::panLeft), input.value(actions::panDown) - input.value(actions::panUp)};
    if (direction.x != 0 || direction.y != 0)
    {
        const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
        camera.panByScreen(direction * (-settings.keyPanSpeed * dt / std::max(1.0f, length)));
        panned = true;
    }

    const Vec2 mouse = input.mouse();
    const Vec2 view = camera.viewport();
    const bool overView = view.x > 0 && view.y > 0 && mouse.x >= 0 && mouse.x <= view.x && mouse.y >= 0 && mouse.y <= view.y;
    if (settings.edgeScroll && input.mouseInside() && overView)
    {
        const float margin = settings.edgeScrollMargin;
        Vec2 edge;
        if (mouse.x >= 0 && mouse.x < margin)
            edge.x = -1;
        else if (mouse.x > view.x - margin && mouse.x <= view.x)
            edge.x = 1;
        if (mouse.y >= 0 && mouse.y < margin)
            edge.y = -1;
        else if (mouse.y > view.y - margin && mouse.y <= view.y)
            edge.y = 1;
        if (edge.x != 0 || edge.y != 0)
        {
            camera.panByScreen(edge * (-settings.edgeScrollSpeed * dt));
            panned = true;
        }
    }

    const Vec2 anchor = settings.zoomToCursor ? input.mouse() : camera.viewport() / 2.0f;
    if (input.wheel() != 0)
        camera.zoomBy(std::pow(settings.wheelZoomStep, input.wheel()), anchor);
    if (input.pressed(actions::zoomIn))
        camera.zoomBy(settings.wheelZoomStep * settings.wheelZoomStep);
    if (input.pressed(actions::zoomOut))
        camera.zoomBy(1.0f / (settings.wheelZoomStep * settings.wheelZoomStep));

    if (panned)
        following_ = false;
    if (input.pressed(actions::recenter))
        following_ = true;
    if (follow && following_ && settings.followSelection)
        camera.moveTo(*follow);
    else if (follow && input.pressed(actions::recenter))
        camera.moveTo(*follow);

    camera.update(deltaSeconds);
}

}
