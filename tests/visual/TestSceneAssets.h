#pragma once

#include <yorehold/framework/assets/Assets.h>
#include <yorehold/framework/assets/FileSystem.h>
#include <yorehold/framework/graphics/Renderer.h>
#include <yorehold/framework/input/Input.h>
#include <yorehold/framework/testing/TestScene.h>
#include <yorehold/framework/ui/Ui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

// F2 + F3: skins as folders or .yoreskin zips layered over the defaults, hot reload, and real fonts.
// Writes a few generated skins into .dev/skins the first time it opens.
class TestSceneAssets : public yh::TestScene
{
public:
    TestSceneAssets()
    {
        root_ = std::string(YH_DEV_STATE_DIR) + "/skins";
        writeSkin(root_ + "/default", {80, 140, 230, 255}, "Default");
        writeSkin(root_ + "/ember", {230, 90, 60, 255}, "Ember");
        writeSkin(root_ + "/gilded-src", {230, 190, 70, 255}, "Gilded");
        if (!std::filesystem::exists(root_ + "/gilded.yoreskin"))
            yh::FileSystem::packFolder(root_ + "/gilded-src", root_ + "/gilded.yoreskin");

        files_.mountFolder(YH_FRAMEWORK_ASSETS, "framework");
        files_.mountFolder(root_ + "/default", "default skin");
    }

    bool handleEvent(const SDL_Event& event) override
    {
        input_.handle(event);
        return true;
    }

    void update(double deltaSeconds) override
    {
        if (pendingSkin_ >= 0 && assets_)
        {
            setSkin(pendingSkin_);
            pendingSkin_ = -1;
        }
        reloadTimer_ += deltaSeconds;
        if (assets_ && reloadTimer_ > 0.25)
        {
            reloadTimer_ = 0;
            if (const int n = assets_->hotReload())
                status_ = "Hot reloaded " + std::to_string(n) + " file(s)";
        }
    }

    void draw(yh::Renderer& renderer) override
    {
        if (!assets_)
            assets_ = std::make_unique<yh::Assets>(files_, renderer);
        yh::Font* body = assets_->font("fonts/AtkinsonHyperlegible-Regular.ttf", 20);
        yh::Font* title = assets_->font("fonts/Cinzel.ttf", 44);
        yh::Font* small = assets_->font("fonts/AtkinsonHyperlegible-Regular.ttf", 15);
        ui_.theme.font = body;
        ui_.begin(renderer, input_);
        renderer.clear({14, 16, 24, 255});

        const float x = 24;
        float y = 16;
        if (title)
            title->draw(renderer, {x, y}, "Yorehold", {255, 214, 140, 255});
        y += 64;

        // Skin switching: later mounts override earlier ones.
        const char* names[3] = {"Default", "Ember (folder)", "Gilded (.yoreskin)"};
        for (int i = 0; i < 3; i++)
        {
            if (ui_.toggle({x + i * 210.0f, y, 200, 36}, names[i], skin_ == i))
                pendingSkin_ = i; // applied in update(): clearing assets mid-draw would free fonts in use
        }
        y += 52;

        const yh::TextureId token = assets_->texture("token.png", true);
        renderer.drawSprite(token, {x, y, 160, 160});
        renderer.drawSprite(assets_->texture("does-not-exist.png"), {x + 180, y + 40, 80, 80});
        if (small)
        {
            small->draw(renderer, {x + 180, y + 124}, "missing file", {150, 150, 170, 255});
            small->draw(renderer, {x, y + 168}, "token.png from: " + files_.source("token.png").value_or("?"), {150, 150, 170, 255});
        }
        if (ui_.button({x + 300, y + 40, 260, 36}, "Repaint default token.png"))
        {
            repaint_ = (repaint_ + 1) % 4;
            const yh::Color colors[4] = {{80, 140, 230, 255}, {120, 210, 110, 255}, {200, 110, 220, 255}, {240, 240, 240, 255}};
            writeToken(root_ + "/default/token.png", colors[repaint_]);
            status_ = "Wrote token.png; waiting for hot reload...";
        }
        if (small)
            small->draw(renderer, {x + 300, y + 84}, "(edit any file in .dev/skins/default and it reloads)", {150, 150, 170, 255});
        y += 200;

        if (body)
        {
            const std::string paragraph =
                "Real fonts at any size, with kerning and wrapping. Accents work too: café, naïve, Æsir, Þórr. "
                "The dice say 2d6+3 = 11, and the goblin flees.";
            float ty = y;
            for (const std::string& line : body->wrap(paragraph, 560))
            {
                body->draw(renderer, {x, ty}, line, {230, 230, 240, 255});
                ty += body->lineHeight();
            }
            y = ty + 10;
        }
        if (small)
            small->draw(renderer, {x, y}, status_, {255, 196, 64, 255});

        input_.endFrame();
    }

    const char* help() const override { return "F2/F3: skins (folder + zip), hot reload, TTF text"; }

private:
    static void writeToken(const std::string& path, yh::Color color)
    {
        constexpr int size = 128;
        SDL_Surface* s = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
        for (int y = 0; y < size; y++)
        {
            for (int x = 0; x < size; x++)
            {
                const float dx = x - size / 2.0f + 0.5f, dy = y - size / 2.0f + 0.5f;
                const float d = std::sqrt(dx * dx + dy * dy);
                const float alpha = std::clamp(size / 2.0f - 2 - d, 0.0f, 1.0f);
                const bool rim = d > size / 2.0f - 10;
                unsigned char* p = static_cast<unsigned char*>(s->pixels) + y * s->pitch + x * 4;
                p[0] = rim ? 30 : color.r;
                p[1] = rim ? 30 : color.g;
                p[2] = rim ? 36 : color.b;
                p[3] = static_cast<unsigned char>(alpha * 255);
            }
        }
        SDL_SavePNG(s, path.c_str());
        SDL_DestroySurface(s);
    }

    static void writeSkin(const std::string& folder, yh::Color color, const std::string& name)
    {
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (!std::filesystem::exists(folder + "/token.png"))
            writeToken(folder + "/token.png", color);
        if (!std::filesystem::exists(folder + "/skin.json"))
            std::ofstream(folder + "/skin.json") << "{ \"name\": \"" << name << "\", \"author\": \"test\" }\n";
    }

    void setSkin(int skin)
    {
        files_.unmount("skin");
        if (skin == 1)
            files_.mountFolder(root_ + "/ember", "skin");
        else if (skin == 2)
            files_.mountZip(root_ + "/gilded.yoreskin", "skin");
        skin_ = skin;
        assets_->clear();
        status_ = "Switched skin";
    }

    yh::FileSystem files_;
    std::unique_ptr<yh::Assets> assets_;
    yh::Input input_;
    yh::Ui ui_;
    std::string root_;
    std::string status_ = "Ready";
    int skin_ = 0;
    int pendingSkin_ = -1;
    int repaint_ = 0;
    double reloadTimer_ = 0;
};
