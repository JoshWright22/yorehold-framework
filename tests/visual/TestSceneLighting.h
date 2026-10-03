#pragma once

#include <yorehold/framework/graphics/Lighting.h>
#include <yorehold/framework/map/FogOfWar.h>
#include <yorehold/framework/input/Input.h>
#include <yorehold/framework/testing/TestScene.h>

#include <SDL3/SDL.h>

#include <array>

class TestSceneLighting : public yh::TestScene
{
public:
    TestSceneLighting()
    {
        camera_.setBounds({0, 0, 960, 640});
        camera_.jumpTo({480, 320}, 1);
    }
    bool handleEvent(const SDL_Event& e) override
    {
        input_.handle(e);
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
        {
            if (e.key.key == SDLK_F) fogOn_ = !fogOn_;
            if (e.key.key == SDLK_R) fog_.reset(0);
            if (e.key.key == SDLK_L) lightsOn_ = !lightsOn_;
        }
        return true;
    }
    void update(double) override
    {
        if (input_.mouseInside() && input_.mouseDelta() != yh::Vec2{}) observer_ = camera_.screenToWorld(input_.mouse());
        const std::array<yh::Vision, 1> vision{{{observer_, 230}}};
        fog_.update(0, 0, vision, walls_);
        input_.endFrame();
    }
    void draw(yh::Renderer& r) override
    {
        camera_.setViewport(r.bounds().size());
        r.clear({16, 20, 30, 255});
        camera_.apply(r);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 30; ++x)
                r.fillRect({x * 32.0f, y * 32.0f, 31, 31}, (x + y) % 2 ? yh::Color{125, 120, 102, 255} : yh::Color{150, 145, 120, 255});
        r.fillCircle({600, 340}, 22, {180, 80, 75, 255});
        r.fillCircle(observer_, 14, {90, 170, 255, 255});
        for (const auto& wall : walls_) r.drawLine(wall.a, wall.b, {45, 42, 38, 255}, 10);
        r.pop();
        const std::array<yh::Light, 3> lights{{{observer_, 280, {255, 220, 165, 255}}, {{650, 180}, 220, {100, 145, 255, 255}}, {{700, 510}, 170, {255, 90, 50, 255}}}};
        if (lightsOn_) lighting_.apply(r, camera_, lights, walls_);
        if (fogOn_)
        {
            camera_.apply(r);
            fog_.draw(r, camera_.visibleWorld(), 0, 0);
            r.pop();
        }
        r.drawText({16, 16}, "Move mouse: vision + torch   F: fog   L: lights   R: forget exploration", {255, 255, 255, 255}, 1.5f);
    }
    const char* help() const override { return "F5: shadowed coloured lights, team/floor fog and remembered exploration"; }
private:
    yh::Input input_;
    yh::Camera camera_;
    yh::Lighting lighting_;
    yh::FogOfWar fog_{30, 20, 32};
    yh::Vec2 observer_{310, 300};
    std::array<yh::Wall, 5> walls_{{{{450, 120}, {450, 430}}, {{450, 120}, {740, 120}}, {{450, 430}, {550, 430}}, {{640, 430}, {740, 430}}, {{740, 120}, {740, 430}}}};
    bool fogOn_ = true;
    bool lightsOn_ = true;
};
