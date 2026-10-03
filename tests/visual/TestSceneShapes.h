#pragma once

#include <yorehold/framework/graphics/Renderer.h>
#include <yorehold/framework/testing/TestScene.h>

#include <cmath>

// Renderer sanity check: fills, outlines, lines and alpha blending in fixed spots.
class TestSceneShapes : public yh::TestScene
{
public:
    void update(double deltaSeconds) override { time_ += deltaSeconds; }

    void draw(yh::Renderer& renderer) override
    {
        renderer.clear({14, 18, 32, 255});

        renderer.fillRect({40, 40, 160, 100}, {220, 80, 70, 255});
        renderer.drawRect({240, 40, 160, 100}, {90, 200, 120, 255}, 2);

        for (int i = 0; i <= 10; i++)
            renderer.drawLine({440, 40.0f + i * 10.0f}, {600, 140.0f - i * 10.0f}, {240, 200, 80, 255});

        // Two overlapping translucent squares: the overlap should look purple.
        renderer.fillRect({40, 180, 140, 140}, {255, 40, 40, 140});
        renderer.fillRect({110, 250, 140, 140}, {40, 80, 255, 140});

        // Spinning bar to show the loop is running.
        const float angle = static_cast<float>(time_) * 2.0f;
        renderer.drawLine({420, 280}, {420 + std::cos(angle) * 80, 280 + std::sin(angle) * 80}, {255, 255, 255, 255}, 3);

        renderer.drawText({40, 420}, "The quick brown fox jumps over the lazy dog 0123456789", {230, 230, 240, 255});
        renderer.drawText({40, 450}, "Small text at scale 1", {170, 170, 190, 255}, 1.0f);
    }

    const char* help() const override { return "Red/green/yellow shapes, purple overlap, spinning white bar, readable text"; }

private:
    double time_ = 0;
};
