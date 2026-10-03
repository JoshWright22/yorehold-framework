#pragma once

#include "MapTestScene.h"

#include <yorehold/framework/map/Grid.h>

// M2: square / hex / gridless grids, cell picking, snapping and distance rules.
class TestSceneGrid : public MapTestScene
{
public:
    TestSceneGrid()
    {
        camera_.setBounds({0, 0, 4000, 3000});
        camera_.jumpTo({2000, 1500}, 0.8f);
    }

    bool handleEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            switch (event.key.key)
            {
            case SDLK_G:
                showGrid_ = !showGrid_;
                return true;
            case SDLK_H:
                grid_ = yh::Grid(grid_.type() == yh::GridType::Square ? yh::GridType::Hex
                                 : grid_.type() == yh::GridType::Hex   ? yh::GridType::Gridless
                                                                       : yh::GridType::Square, 100);
                return true;
            case SDLK_R:
                grid_.diagonals = yh::next(grid_.diagonals);
                return true;
            default:
                break;
            }
        }
        return MapTestScene::handleEvent(event);
    }

    void update(double deltaSeconds) override
    {
        mouseWorld_ = camera_.screenToWorld(input_.mouse());
        if (input_.clicked(yh::actions::select))
            origin_ = grid_.cellAt(mouseWorld_);
        controls_.update(camera_, input_, deltaSeconds);
        input_.endFrame();
    }

    void draw(yh::Renderer& renderer) override
    {
        camera_.setViewport(renderer.bounds().size());
        renderer.clear({10, 12, 20, 255});

        camera_.apply(renderer);
        renderer.fillRect(camera_.bounds(), {54, 84, 52, 255});
        if (showGrid_)
            grid_.draw(renderer, camera_.visibleWorld().intersect(camera_.bounds()), camera_.zoom(), {0, 0, 0, 90});

        const yh::Cell hover = grid_.cellAt(mouseWorld_);
        const yh::Vec2 snapped = grid_.snap(mouseWorld_);
        const float marker = 24;
        renderer.fillRect({grid_.center(origin_).x - marker / 2, grid_.center(origin_).y - marker / 2, marker, marker}, {255, 214, 120, 255});
        renderer.drawLine(grid_.center(origin_), grid_.center(hover), {255, 214, 120, 200}, 3.0f / camera_.zoom());
        renderer.fillRect({snapped.x - 8, snapped.y - 8, 16, 16}, {255, 255, 255, 255});
        renderer.pop();

        const char* type = grid_.type() == yh::GridType::Square ? "square" : grid_.type() == yh::GridType::Hex ? "hex" : "gridless";
        char line1[96];
        char line2[96];
        std::snprintf(line1, sizeof(line1), "%s grid (H)  lines %s (G)  diagonals %s (R)", type, showGrid_ ? "shown" : "hidden",
            yh::toString(grid_.diagonals));
        std::snprintf(line2, sizeof(line2), "cell %d,%d  from yellow: %.1f sq (%.0f ft)", hover.x, hover.y,
            grid_.distance(origin_, hover), grid_.distance(origin_, hover) * 5);
        drawHud(renderer, {line1, line2, "Click: move yellow. White dot = snap point"});
    }

    const char* help() const override { return "M2: lines, snapping and distances look right"; }

private:
    yh::Grid grid_{yh::GridType::Square, 100};
    bool showGrid_ = true;
    yh::Vec2 mouseWorld_;
    yh::Cell origin_{20, 15};
};
