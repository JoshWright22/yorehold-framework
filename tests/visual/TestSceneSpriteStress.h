#pragma once

#include <yorehold/framework/graphics/Renderer.h>
#include <yorehold/framework/testing/TestScene.h>

#include <SDL3/SDL_events.h>

#include <cstdio>
#include <random>
#include <vector>

// Lots of bouncing quads, to watch frame times (F3) as the count goes up.
// Positions and velocities live in separate arrays (structure of arrays) so the update loop stays cache-friendly.
class TestSceneSpriteStress : public yh::TestScene
{
public:
    TestSceneSpriteStress() { resize(5000); }

    void update(double deltaSeconds) override
    {
        const float dt = static_cast<float>(deltaSeconds);
        const float maxX = width_ - size;
        const float maxY = height_ - size;

        for (size_t i = 0; i < x_.size(); i++)
        {
            x_[i] += vx_[i] * dt;
            y_[i] += vy_[i] * dt;
            if (x_[i] < 0 || x_[i] > maxX)
                vx_[i] = -vx_[i];
            if (y_[i] < 0 || y_[i] > maxY)
                vy_[i] = -vy_[i];
        }
    }

    void draw(yh::Renderer& renderer) override
    {
        const yh::Rect area = renderer.bounds();
        width_ = area.w;
        height_ = area.h;

        renderer.clear({14, 18, 32, 255});

        rects_.resize(x_.size());
        for (size_t i = 0; i < x_.size(); i++)
            rects_[i] = {x_[i], y_[i], size, size};
        renderer.fillRects(rects_, {120, 180, 255, 255});

        std::snprintf(label_, sizeof(label_), "Up/Down: +/-1000 quads (now %zu). Press F3 for frame times.", x_.size());
    }

    bool handleEvent(const SDL_Event& event) override
    {
        if (event.type != SDL_EVENT_KEY_DOWN)
            return false;
        if (event.key.key == SDLK_UP)
            resize(x_.size() + 1000);
        else if (event.key.key == SDLK_DOWN && x_.size() >= 1000)
            resize(x_.size() - 1000);
        else
            return false;
        return true;
    }

    const char* help() const override { return label_; }

private:
    static constexpr float size = 6.0f;

    void resize(size_t count)
    {
        std::uniform_real_distribution<float> positionX(0.0f, width_ - size);
        std::uniform_real_distribution<float> positionY(0.0f, height_ - size);
        std::uniform_real_distribution<float> speed(-200.0f, 200.0f);
        while (x_.size() < count)
        {
            x_.push_back(positionX(random_));
            y_.push_back(positionY(random_));
            vx_.push_back(speed(random_));
            vy_.push_back(speed(random_));
        }
        x_.resize(count);
        y_.resize(count);
        vx_.resize(count);
        vy_.resize(count);
    }

    std::mt19937 random_{1234};
    std::vector<float> x_, y_, vx_, vy_;
    std::vector<yh::Rect> rects_;
    float width_ = 1000;
    float height_ = 680;
    char label_[96] = "";
};
