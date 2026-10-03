#pragma once

#include <yorehold/framework/animation/Tween.h>
#include <yorehold/framework/assets/FileSystem.h>
#include <yorehold/framework/graphics/Particles.h>
#include <yorehold/framework/input/Input.h>
#include <yorehold/framework/testing/TestScene.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>

class TestSceneAnimation : public yh::TestScene
{
public:
    TestSceneAnimation()
    {
        files_.mountFolder(YH_FRAMEWORK_ASSETS, "framework");
        if (const auto json = files_.readText("particles/embers.json"))
            if (const auto effect = yh::ParticleEffect::fromJson(*json)) effect_ = *effect;
        emitter_ = particles_.start(effect_, {380, 360}, 42);
        restart();
    }

    bool handleEvent(const SDL_Event& event) override
    {
        input_.handle(event);
        return true;
    }

    void update(double dt) override
    {
        clock_ += dt;
        tweens_.update(dt);
        particles_.move(emitter_, {380 + 120 * static_cast<float>(std::sin(clock_)), 360});
        particles_.update(dt);
        if (clock_ > next_) { restart(); next_ = clock_ + 3; }
        if (input_.buttonClicked(yh::MouseButton::Left))
        {
            particles_.burst(effect_, input_.mouse(), 180);
            scale_ = 0.5f;
            tweens_.to(&scale_, 1.0f, 0.8, yh::Ease::OutElastic);
        }
        input_.endFrame();
    }

    void draw(yh::Renderer& r) override
    {
        r.clear({10, 12, 22, 255});
        const char* names[] = {"linear", "inQuad", "outQuad", "inOutQuad", "outCubic", "inOutCubic", "outBack", "outElastic", "outBounce"};
        for (int i = 0; i < 9; ++i)
        {
            const float y = 34.0f + i * 30;
            r.drawText({16, y - 8}, names[i], {175, 180, 200, 255}, 1.5f);
            r.drawLine({180, y}, {540, y}, {45, 50, 70, 255});
            r.fillCircle({180 + values_[i] * 360, y}, 8, {255, 190, 80, 255});
        }
        particles_.draw(r);
        r.fillCircle({660, 400}, 36 * scale_, {110, 165, 255, 255});
        char stats[160];
        std::snprintf(stats, sizeof(stats), "%zu particles / %zu emitters   Click: burst + spring tween", particles_.liveParticles(), particles_.liveEmitters());
        r.drawText({16, 520}, stats, {200, 205, 220, 255}, 1.5f);
    }

    const char* help() const override { return "F4: nine easing curves, JSON particles, seeded emitters; click for bursts"; }

private:
    void restart()
    {
        for (int i = 0; i < 9; ++i)
        {
            values_[i] = 0;
            tweens_.to(&values_[i], 1, 1.5, static_cast<yh::Ease>(i), 0.25);
        }
    }
    yh::FileSystem files_;
    yh::Input input_;
    yh::Tweens tweens_;
    yh::ParticleSystem particles_;
    yh::ParticleEffect effect_;
    yh::ParticleSystem::EmitterId emitter_ = 0;
    float values_[9]{};
    float scale_ = 1;
    double clock_ = 0;
    double next_ = 3;
};
