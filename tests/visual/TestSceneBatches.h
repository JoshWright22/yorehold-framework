#pragma once

#include <yorehold/framework/graphics/Renderer.h>
#include <yorehold/framework/testing/TestScene.h>

#include <functional>

// Fixed colour swatches expose batches overwriting earlier geometry or uniforms.
// The targets deliberately differ from each other and from the window's size.
class TestSceneBatches : public yh::TestScene
{
public:
    ~TestSceneBatches() override
    {
        if (release_) release_();
    }

    void draw(yh::Renderer& renderer) override
    {
        if (!release_)
        {
            target_ = renderer.createRenderTarget(32, 64);
            release_ = renderer.textureRelease(target_);
        }
        renderer.clear({14, 18, 32, 255});
        renderer.fillRect({40, 80, 120, 120}, {220, 80, 70, 255});
        renderer.flush();

        renderer.pushTarget(target_, yh::Color{90, 200, 120, 255});
        renderer.popTarget(); // A clear-only batch must still upload the target size.
        renderer.drawSprite(target_, {200, 80, 120, 120});
        renderer.flush();

        renderer.pushTarget(target_, yh::Color{150, 110, 220, 255});
        renderer.fillRect({0, 0, 16, 64}, {240, 200, 80, 255});
        renderer.popTarget();
        renderer.drawSprite(target_, {360, 80, 120, 120});
        renderer.fillRect({520, 80, 120, 120}, {90, 160, 230, 255});
        renderer.flush();

        // Force a buffer to grow after earlier draws have already referenced it.
        for (int y = 0; y < 50; ++y)
            for (int x = 0; x < 80; ++x)
                renderer.fillRect({40 + x * 7.0f, 280 + y * 4.0f, 6, 3}, {60, 70, 90, 255});
        renderer.drawText({40, 30}, "Red, green, yellow/purple, blue: every batch must survive.", {230, 230, 230, 255});
    }

    const char* help() const override { return "Repeated flushes, target reuse, clear-only passes and buffer growth; no F3 needed"; }

private:
    yh::TextureId target_ = 0;
    std::function<void()> release_;
};
