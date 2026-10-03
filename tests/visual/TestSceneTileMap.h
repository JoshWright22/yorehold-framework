#pragma once

#include "MapTestScene.h"
#include "TestTerrain.h"

#include <yorehold/framework/map/TileMap.h>

#include <SDL3/SDL_timer.h>

// M3: a 4000 x 4000 tile map (16 million tiles, ~256 km of world at 64 units per tile) with
// two layers and a second floor. Should stay smooth at every zoom.
class TestSceneTileMap : public MapTestScene
{
public:
    static constexpr int size = 4000;
    static constexpr float tileSize = 64;

    TestSceneTileMap()
    {
        ground_ = map_.addLayer("ground");
        decor_ = map_.addLayer("trees");
        upstairs_ = map_.addLayer("upstairs", 1);

        const Uint64 start = SDL_GetTicks();
        testterrain::generate(map_, ground_, decor_, 42);
        // A "building" with a second floor near the middle.
        for (int y = 1980; y < 2020; y++)
        {
            for (int x = 1980; x < 2030; x++)
                map_.setTile(upstairs_, x, y, (x + y) % 7 == 0 ? testterrain::Rock : testterrain::Sand);
        }
        generateMs_ = SDL_GetTicks() - start;

        camera_.setBounds(map_.worldBounds());
        camera_.minZoomFit = 1.0f;
        // Starts fully zoomed out (whole map), so zooming in shows the switch from overview to tiles.
        camera_.jumpTo({2000 * tileSize, 2000 * tileSize}, 0.0f);
    }

    bool handleEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            switch (event.key.key)
            {
            case SDLK_T:
                map_.setLayerVisible(decor_, !map_.layerVisible(decor_));
                return true;
            case SDLK_PAGEUP:
                floor_ = 1;
                return true;
            case SDLK_PAGEDOWN:
                floor_ = 0;
                return true;
            default:
                break;
            }
        }
        return MapTestScene::handleEvent(event);
    }

    void update(double deltaSeconds) override
    {
        controls_.update(camera_, input_, deltaSeconds);
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
        if (floor_ == 1)
        {
            // Upstairs: the ground floor shows dimmed underneath.
            map_.draw(renderer, camera_.visibleWorld(), camera_.zoom(), 0);
            renderer.fillRect(camera_.visibleWorld(), {0, 0, 0, 150});
        }
        map_.draw(renderer, camera_.visibleWorld(), camera_.zoom(), floor_);
        renderer.pop();

        char line1[120];
        std::snprintf(line1, sizeof(line1), "%dx%d tiles, made in %llu ms, drawing %s%s", size, size,
            static_cast<unsigned long long>(generateMs_), map_.lastUsedOverview() ? "overview" : "tiles: ",
            map_.lastUsedOverview() ? "" : std::to_string(map_.lastDrawnTiles()).c_str());
        char line2[120];
        std::snprintf(line2, sizeof(line2), "floor %d (PgUp/PgDn, centre)  trees %s (T)", floor_,
            map_.layerVisible(decor_) ? "shown" : "hidden");
        drawHud(renderer, {line1, line2, "Zoom in and out; F3 shows frame times"});
    }

    const char* help() const override { return "M3: 16M tiles, smooth at every zoom"; }

private:
    yh::TileMap map_{size, size, tileSize};
    int ground_ = 0;
    int decor_ = 0;
    int upstairs_ = 0;
    int floor_ = 0;
    bool tilesetReady_ = false;
    Uint64 generateMs_ = 0;
};
