#pragma once

#include "MapTestScene.h"

#include <yorehold/framework/assets/Assets.h>
#include <yorehold/framework/map/TileMap.h>
#include <yorehold/framework/map/Tokens.h>

#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>

// F8: actual image files through the map and token APIs; drop your own file to try it.
class TestSceneImages : public MapTestScene
{
public:
    TestSceneImages()
    {
        files_.mountFolder(YH_FRAMEWORK_ASSETS, "framework");
        ground_ = map_.addLayer("JPEG stone", 0, 1);
        decor_ = map_.addLayer("PNG decorations");
        for (int x = 0; x < map_.width(); x += 3) map_.setTile(decor_, x, 1, 2);
        const char* names[] = {"PNG portrait", "PNG cutout", "JPEG portrait", "JPEG cutout"};
        for (int i = 0; i < 4; ++i)
        {
            yh::Token token;
            token.name = names[i];
            token.position = {240.0f + i * 160, 320};
            token.radius = 55;
            token.color = i < 2 ? yh::Color{90, 170, 240, 255} : yh::Color{240, 180, 75, 255};
            token.image = i < 2 ? "images/token.png" : "images/tile.jpg";
            token.imageStyle = i % 2 == 0 ? yh::TokenImageStyle::Portrait : yh::TokenImageStyle::Cutout;
            token.selected = i == 0;
            tokens_.tokens.push_back(std::move(token));
        }
        camera_.setBounds(map_.worldBounds());
        camera_.minZoomFit = 0.02f;
        camera_.jumpTo({480, 320}, 1);
        controls_.settings.edgeScroll = false;
    }

    bool handleEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_DROP_FILE && event.drop.data)
        {
            pendingFile_ = event.drop.data;
            return true;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_Z)
        {
            overview_ = !overview_;
            camera_.jumpTo({480, 320}, overview_ ? 0.05f : 1.0f);
            return true;
        }
        return MapTestScene::handleEvent(event);
    }

    void update(double seconds) override
    {
        tokens_.update(input_, camera_, grid_, [this](yh::Cell c) {
            return c.x >= 0 && c.y >= 0 && c.x < map_.width() && c.y < map_.height();
        }, seconds);
        controls_.update(camera_, input_, seconds);
        input_.endFrame();
    }

    void draw(yh::Renderer& renderer) override
    {
        if (!assets_) loadExampleArt(renderer);
        if (!pendingFile_.empty())
        {
            loadDroppedImage(renderer, pendingFile_);
            pendingFile_.clear();
        }

        camera_.setViewport(renderer.bounds().size());
        renderer.clear({20, 24, 34, 255});
        camera_.apply(renderer);
        map_.draw(renderer, camera_.visibleWorld(), camera_.zoom(), 0);
        tokens_.draw(renderer, camera_, grid_);
        for (const auto& token : tokens_.tokens)
        {
            renderer.drawText({token.position.x - 55, token.position.y + 68}, token.name, {255, 255, 255, 255}, 1.2f);
        }
        renderer.pop();
        tokens_.drawOverlay(renderer, camera_, grid_);
        drawInstructions(renderer);
    }

    const char* help() const override { return "Image tiles, portraits and transparent tokens"; }

private:
    void loadExampleArt(yh::Renderer& renderer)
    {
        assets_ = std::make_unique<yh::Assets>(files_, renderer);
        tokens_.useAssets(*assets_);
        const auto stone = assets_->image("images/tile.jpg");
        const auto token = assets_->image("images/token.png");
        if (!stone || !token) throw std::runtime_error("Image fixtures did not decode");

        const std::array<yh::Image, 2> tiles{*stone, *token};
        const auto atlas = yh::buildTileset(renderer, tiles, 64, true);
        if (!atlas || atlas->averageColors[1].a >= 255 || renderer.textureSize(atlas->texture) != yh::Vec2{136, 68})
            throw std::runtime_error("Image atlas lost transparency or padding");

        map_.setTileset(*atlas);
        checkTextureVariants(renderer);
    }

    void checkTextureVariants(yh::Renderer& renderer)
    {
        const auto plain = assets_->texture("images/token.png");
        const auto smooth = assets_->texture("images/token.png", true);
        const auto portrait = assets_->circleTexture("images/token.png");
        if (plain == smooth || portrait == smooth || renderer.textureSize(portrait) != yh::Vec2{256, 256}
            || smooth != assets_->texture("images/token.png", true))
            throw std::runtime_error("Image texture variants were not cached separately");
    }

    void loadDroppedImage(yh::Renderer& renderer, const std::string& filePath)
    {
        const std::filesystem::path file(filePath);
        files_.mountFolder(file.parent_path().generic_string(), "dropped images");
        const std::string name = file.filename().generic_string();
        const auto image = assets_->image(name);
        if (!image)
        {
            status_ = "Could not decode " + name;
            return;
        }

        const std::array<yh::Image, 1> tiles{*image};
        std::string error;
        auto atlas = yh::buildTileset(renderer, tiles, 64, true, &error);
        if (!atlas)
        {
            status_ = "Could not build tiles: " + error;
            return;
        }

        // A different mount can give an existing path new pixels. Clear its cached texture variants.
        assets_->clear();
        map_.setTileset(std::move(*atlas));
        for (int x = 0; x < map_.width(); x += 3) map_.setTile(decor_, x, 1, 0);
        for (auto& token : tokens_.tokens)
        {
            token.image = name;
            token.name = token.imageStyle == yh::TokenImageStyle::Portrait ? "Portrait" : "Cutout";
        }
        status_ = "Loaded " + name;
    }

    void drawInstructions(yh::Renderer& renderer)
    {
        renderer.fillRect({8, 8, renderer.bounds().w - 16, 70}, {0, 0, 0, 180});
        renderer.drawText({18, 16}, "Drop PNG/JPEG/WebP/GIF/BMP/TGA art for tiles and tokens", {255, 214, 120, 255}, 1.25f);
        renderer.drawText({18, 36}, "Z: overview zoom | Left: select/drag | Right: walk", {230, 230, 240, 255}, 1.25f);
        renderer.drawText({18, 56}, status_, {230, 230, 240, 255}, 1.25f);
    }

    yh::FileSystem files_;
    std::unique_ptr<yh::Assets> assets_;
    yh::TileMap map_{12, 8, 80};
    yh::Grid grid_{yh::GridType::Square, 80};
    yh::TokenController tokens_;
    int ground_ = 0;
    int decor_ = 0;
    bool overview_ = false;
    std::string pendingFile_;
    std::string status_ = "JPEG floor with transparent PNG decoration tiles";
};
