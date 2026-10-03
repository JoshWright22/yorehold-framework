#pragma once

#include <yorehold/framework/graphics/Camera.h>
#include <yorehold/framework/graphics/Renderer.h>
#include <yorehold/framework/input/ControlScheme.h>
#include <yorehold/framework/map/CameraControls.h>
#include <yorehold/framework/testing/TestScene.h>

#include <SDL3/SDL_events.h>

#include <cstdio>
#include <string>
#include <vector>

// Shared base for the map test scenes: input, camera, control scheme switching and a HUD.
// F6 switches Foundry/BG3 controls, F7 zoom toward centre/cursor, F8 edge scrolling.
class MapTestScene : public yh::TestScene
{
public:
    MapTestScene() { applyScheme(yh::ControlPreset::Foundry); }

    bool handleEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            switch (event.key.key)
            {
            case SDLK_F6:
                applyScheme(scheme_.preset == yh::ControlPreset::Foundry ? yh::ControlPreset::BG3 : yh::ControlPreset::Foundry);
                return true;
            case SDLK_F7:
                controls_.settings.zoomToCursor = !controls_.settings.zoomToCursor;
                return true;
            case SDLK_F8:
                controls_.settings.edgeScroll = !controls_.settings.edgeScroll;
                return true;
            default:
                break;
            }
        }
        input_.handle(event);
        return true;
    }

protected:
    void applyScheme(yh::ControlPreset preset)
    {
        scheme_ = yh::makeControlScheme(preset);
        input_.setMap(scheme_.map);
        controls_.settings.edgeScroll = scheme_.edgeScroll;
    }

    // Lines shown in the top-left panel, after the shared camera/controls line.
    void drawHud(yh::Renderer& renderer, const std::vector<std::string>& extra)
    {
        char line[160];
        std::snprintf(line, sizeof(line), "%s controls (F6)  zoom to %s (F7)  edge scroll %s (F8)  %.0f%%",
            scheme_.name, controls_.settings.zoomToCursor ? "cursor" : "centre", controls_.settings.edgeScroll ? "on" : "off",
            camera_.zoom() * 100);

        std::vector<std::string> lines{line};
        lines.insert(lines.end(), extra.begin(), extra.end());
        float width = 0;
        for (const std::string& l : lines)
            width = std::max(width, yh::Renderer::textWidth(l));
        const float lineHeight = yh::Renderer::lineHeight() + 4;
        renderer.fillRect({8, 8, width + 20, lines.size() * lineHeight + 14}, {0, 0, 0, 170});
        for (size_t i = 0; i < lines.size(); i++)
            renderer.drawText({18, 16 + i * lineHeight}, lines[i], i == 0 ? yh::Color{255, 214, 120, 255} : yh::Color{230, 230, 240, 255});
    }

    yh::Input input_;
    yh::Camera camera_;
    yh::CameraControls controls_;
    yh::ControlScheme scheme_;
};
