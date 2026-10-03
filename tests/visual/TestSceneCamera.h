#pragma once

#include "MapTestScene.h"

// M1: panning and zooming a 6000 x 4000 world of labelled blocks.
class TestSceneCamera : public MapTestScene
{
public:
    TestSceneCamera()
    {
        camera_.setBounds({0, 0, 6000, 4000});
        camera_.jumpTo({3000, 2000}, 0.5f);
    }

    void update(double deltaSeconds) override
    {
        controls_.update(camera_, input_, deltaSeconds);
        input_.endFrame();
    }

    void draw(yh::Renderer& renderer) override
    {
        camera_.setViewport(renderer.bounds().size());
        renderer.clear({10, 12, 20, 255});

        camera_.apply(renderer);
        renderer.fillRect(camera_.bounds(), {24, 30, 46, 255});
        const yh::Rect visible = camera_.visibleWorld();
        for (int y = 0; y < 8; y++)
        {
            for (int x = 0; x < 12; x++)
            {
                const yh::Rect block{x * 500.0f + 40, y * 500.0f + 40, 420, 420};
                if (block.intersect(visible).w <= 0)
                    continue;
                const uint8_t shade = static_cast<uint8_t>(60 + ((x + y) % 2) * 40);
                renderer.fillRect(block, {shade, static_cast<uint8_t>(shade + 30), static_cast<uint8_t>(120 + x * 10), 255});
                char label[8];
                std::snprintf(label, sizeof(label), "%c%d", 'A' + x, y + 1);
                renderer.drawText({block.x + 20, block.y + 20}, label, {255, 255, 255, 255}, 8.0f);
            }
        }
        renderer.drawRect(camera_.bounds(), {255, 214, 120, 255}, 4.0f / camera_.zoom());
        renderer.pop();

        drawHud(renderer, {"Pan: arrows, right-drag (Foundry)", "     WASD, middle-drag, screen edges (BG3)", "Zoom: wheel or +/-, stops at whole map"});
    }

    const char* help() const override { return "M1: smooth pan/zoom, stays on the map"; }
};
