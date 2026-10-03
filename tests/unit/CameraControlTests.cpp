#include "Checks.h"
#include "CameraControlTests.h"

#include <yorehold/framework/map/CameraControls.h>

#include <SDL3/SDL_events.h>
#include <array>

namespace regression
{

void cameraControls()
{
    yh::Camera camera;
    camera.setBounds({0, 0, 3000, 3000});
    camera.setViewport({800, 600});
    const yh::Vec2 start{1500, 1500};
    camera.jumpTo(start, 1);
    yh::CameraControls controls;
    controls.settings.edgeScroll = true;
    yh::Input input;

    controls.update(camera, input, 0.5);
    CHECK(camera.position() == start && controls.following() && !input.mouseInside());
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    input.handle(event);
    controls.update(camera, input, 0.5);
    CHECK(camera.position() == start && controls.following() && !input.mouseInside());

    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = 1;
    event.motion.y = 300;
    input.handle(event);
    controls.update(camera, input, 0.5);
    CHECK(camera.position().x < start.x && !controls.following() && input.mouseInside());
    input.endFrame();

    event.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
    input.handle(event);
    camera.jumpTo(start, 1);
    controls.resumeFollowing();
    controls.update(camera, input, 0.5);
    CHECK(camera.position() == start && controls.following());

    // Browser events are relative to the scene. Hovering a sidebar must not scroll its map.
    const std::array<yh::Vec2, 4> outside{{{-100, 1}, {801, 1}, {1, -100}, {1, 601}}};
    for (const auto point : outside)
    {
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.x = point.x;
        event.motion.y = point.y;
        input.handle(event);
        controls.update(camera, input, 0.5);
        CHECK(camera.position() == start && controls.following());
        input.endFrame();
    }

    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    input.handle(event);
    CHECK(!input.mouseInside());
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 1;
    event.button.y = 1;
    input.handle(event);
    controls.update(camera, input, 0.5);
    CHECK(input.mouseInside() && camera.position().x < start.x && camera.position().y < start.y && !controls.following());
}

}
