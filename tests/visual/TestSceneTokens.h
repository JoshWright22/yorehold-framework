#pragma once

#include "MapTestScene.h"
#include "TestTerrain.h"

#include <yorehold/framework/map/Grid.h>
#include <yorehold/framework/map/TileMap.h>
#include <yorehold/framework/map/Tokens.h>

// M4: a party of four on a small map. Select, box-select, drag (snapped), click to walk around water and rock.
class TestSceneTokens : public MapTestScene
{
public:
    static constexpr float cell = 100;

    TestSceneTokens()
    {
        ground_ = map_.addLayer("ground");
        decor_ = map_.addLayer("trees");
        testterrain::generate(map_, ground_, decor_, 7);

        // Start the party on the walkable cell nearest the middle.
        yh::Cell start{60, 40};
        for (int r = 0; r < 40 && !passable(start); r++)
            start = {60 + r, 40};

        const yh::Color colors[4] = {{220, 90, 80, 255}, {90, 160, 230, 255}, {120, 210, 110, 255}, {230, 200, 90, 255}};
        const char* names[4] = {"Astra", "Brom", "Cel", "Dax"};
        const yh::Cell offsets[4] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        for (int i = 0; i < 4; i++)
        {
            yh::Token token;
            token.name = names[i];
            token.color = colors[i];
            token.position = grid_.center({start.x + offsets[i].x, start.y + offsets[i].y});
            token.radius = cell * 0.4f;
            token.selected = i == 0;
            tokens_.tokens.push_back(token);
        }
        yh::Token npc;
        npc.name = "Goblin (not yours)";
        npc.color = {170, 110, 200, 255};
        npc.owner = 1;
        npc.radius = cell * 0.4f;
        npc.position = grid_.center({start.x + 4, start.y});
        tokens_.tokens.push_back(npc);
        for (size_t i = 1; i < 4; ++i) tokens_.link(i, i - 1);

        camera_.setBounds(map_.worldBounds());
        camera_.jumpTo(tokens_.tokens[0].position, 0.7f);
    }

    bool handleEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            switch (event.key.key)
            {
            case SDLK_J:
                linked_ = !linked_;
                if (linked_) for (size_t i = 1; i < 4; ++i) tokens_.link(i, i - 1);
                else tokens_.clearLinks();
                return true;
            case SDLK_C:
                tokens_.settings.inCombat = !tokens_.settings.inCombat;
                tokens_.settings.activeTurn = 0;
                return true;
            case SDLK_F9:
                controls_.settings.followSelection = !controls_.settings.followSelection;
                return true;
            case SDLK_F10:
                tokens_.settings.dragSnap = tokens_.settings.dragSnap == yh::DragSnap::OnDrop ? yh::DragSnap::Live : yh::DragSnap::OnDrop;
                return true;
            case SDLK_G:
                showGrid_ = !showGrid_;
                return true;
            case SDLK_R:
                grid_.diagonals = yh::next(grid_.diagonals);
                return true;
            case SDLK_O:
                tokens_.settings.avoidAllies = !tokens_.settings.avoidAllies;
                return true;
            default:
                break;
            }
        }
        return MapTestScene::handleEvent(event);
    }

    void update(double deltaSeconds) override
    {
        tokens_.update(input_, camera_, grid_, [this](yh::Cell c) { return passable(c); }, deltaSeconds);
        controls_.update(camera_, input_, deltaSeconds, tokens_.followTarget());
        input_.endFrame();
    }

    void draw(yh::Renderer& renderer) override
    {
        if (!tilesetReady_)
        {
            map_.setTileset(testterrain::makeTileset(renderer));
            tilesetReady_ = true;
        }

        camera_.setViewport(renderer.bounds().size());
        renderer.clear({6, 8, 14, 255});
        camera_.apply(renderer);
        map_.draw(renderer, camera_.visibleWorld(), camera_.zoom(), 0);
        if (showGrid_)
            grid_.draw(renderer, camera_.visibleWorld().intersect(map_.worldBounds()), camera_.zoom(), {0, 0, 0, 70});
        tokens_.draw(renderer, camera_, grid_);
        renderer.pop();
        tokens_.drawOverlay(renderer, camera_, grid_);

        const bool foundry = scheme_.preset == yh::ControlPreset::Foundry;
        char line1[128];
        std::snprintf(line1, sizeof(line1), "follow %s (F9, Home)  snap %s (F10)  grid (G)",
            controls_.settings.followSelection ? "on" : "off", tokens_.settings.dragSnap == yh::DragSnap::OnDrop ? "on drop" : "live");
        char line2[96];
        std::snprintf(line2, sizeof(line2), "diagonal cost %s (R)  path around allies %s (O)", yh::toString(grid_.diagonals),
            tokens_.settings.avoidAllies ? "on" : "off");
        drawHud(renderer, {line1, line2,
                           foundry ? "Left: select, drag, box. Right: walk/menu" : "Left: select, box, walk. Right: menu",
                           linked_ ? "J: unlink party   C: turn movement   Shift: add" : "J: link party   C: turn movement   Shift: add"});
    }

    const char* help() const override { return "M4: select, drag, walk, group moves, follow"; }

private:
    bool passable(yh::Cell c) const
    {
        if (c.x < 0 || c.y < 0 || c.x >= map_.width() || c.y >= map_.height())
            return false;
        return testterrain::walkable(map_.tile(ground_, c.x, c.y));
    }

    yh::TileMap map_{120, 80, cell};
    yh::Grid grid_{yh::GridType::Square, cell};
    yh::TokenController tokens_;
    int ground_ = 0;
    int decor_ = 0;
    bool showGrid_ = true;
    bool tilesetReady_ = false;
    bool linked_ = true;
};
