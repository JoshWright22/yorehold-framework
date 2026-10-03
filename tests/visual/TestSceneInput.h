#pragma once

#include <yorehold/framework/graphics/Renderer.h>
#include <yorehold/framework/testing/TestScene.h>

#include <SDL3/SDL_events.h>

#include <cstdio>
#include <deque>

// Shows what input the framework sees: a mouse trail, click markers and the last key.
class TestSceneInput : public yh::TestScene
{
public:
    void draw(yh::Renderer& renderer) override
    {
        renderer.clear({14, 18, 32, 255});

        for (size_t i = 1; i < trail_.size(); i++)
            renderer.drawLine(trail_[i - 1], trail_[i], {90, 200, 120, 255}, 2);

        for (const yh::Vec2& click : clicks_)
            renderer.drawRect({click.x - 6, click.y - 6, 12, 12}, {240, 200, 80, 255}, 2);

        renderer.drawText({20, 20}, status_, {230, 230, 240, 255});
    }

    bool handleEvent(const SDL_Event& event) override
    {
        switch (event.type)
        {
        case SDL_EVENT_MOUSE_MOTION:
            trail_.push_back({event.motion.x, event.motion.y});
            if (trail_.size() > 64)
                trail_.pop_front();
            std::snprintf(status_, sizeof(status_), "mouse %.0f, %.0f   last key: %s", event.motion.x, event.motion.y, lastKey_);
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            clicks_.push_back({event.button.x, event.button.y});
            if (clicks_.size() > 16)
                clicks_.pop_front();
            return true;
        case SDL_EVENT_KEY_DOWN:
            std::snprintf(lastKey_, sizeof(lastKey_), "%s", SDL_GetKeyName(event.key.key));
            std::snprintf(status_, sizeof(status_), "last key: %s", lastKey_);
            return true;
        default:
            return false;
        }
    }

    const char* help() const override { return "Move, click and type: the scene should follow exactly"; }

private:
    std::deque<yh::Vec2> trail_;
    std::deque<yh::Vec2> clicks_;
    char lastKey_[32] = "none";
    char status_[96] = "move the mouse";
};
