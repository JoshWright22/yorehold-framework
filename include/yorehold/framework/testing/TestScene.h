#pragma once

union SDL_Event;

namespace yh
{

class Renderer;

// One self-contained visual test, shown in the TestBrowser. A fresh instance is made each time
// the scene is opened, so state never leaks between runs.
// The browser sets a viewport for the scene's area; renderer.bounds() gives its size.
// Mouse coordinates in events are already relative to that area.
class TestScene
{
public:
    virtual ~TestScene() = default;

    virtual void update(double deltaSeconds) { (void)deltaSeconds; }
    virtual void draw(Renderer& renderer) = 0;
    virtual bool handleEvent(const SDL_Event& event) { (void)event; return false; }

    // One line shown under the scene, e.g. which keys do what.
    virtual const char* help() const { return ""; }
};

}
