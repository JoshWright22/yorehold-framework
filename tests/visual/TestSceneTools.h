#pragma once

#include <yorehold/framework/animation/SpriteSheet.h>
#include <yorehold/framework/assets/Assets.h>
#include <yorehold/framework/assets/Skin.h>
#include <yorehold/framework/debug/Profiler.h>
#include <yorehold/framework/editor/History.h>
#include <yorehold/framework/graphics/Atlas.h>
#include <yorehold/framework/map/Templates.h>
#include <yorehold/framework/net/Session.h>
#include <yorehold/framework/testing/TestScene.h>
#include <yorehold/framework/text/RichText.h>
#include <yorehold/framework/text/Strings.h>
#include <yorehold/framework/ui/Ui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

// Editor and presentation tools: undo/redo painting, spell templates and a ruler, an atlas-packed
// animated sprite, rich text, languages with plural rules, a loopback multiplayer session and gamepads.
class TestSceneTools : public yh::TestScene
{
public:
    TestSceneTools()
    {
        files_.mountFolder(YH_FRAMEWORK_ASSETS, "framework");
        skin_ = yh::Skin::load(files_).value_or(yh::Skin{});
        strings_.add("en", R"({"greeting":"Welcome, traveller","name":"Name","coins":{"one":"{count} gold coin","other":"{count} gold coins"},
            "mode":{"paint":"Paint","template":"Template","ruler":"Ruler"},"undo":"Undo","redo":"Redo"})");
        strings_.add("pt", R"({"greeting":"Bem-vindo, viajante","name":"Nome","coins":{"one":"{count} moeda de ouro","other":"{count} moedas de ouro"},
            "mode":{"paint":"Pintar","template":"Modelo","ruler":"Régua"},"undo":"Desfazer","redo":"Refazer"})");
        strings_.add("fr", R"({"greeting":"Bienvenue, voyageur","name":"Nom","coins":{"one":"{count} pièce d'or","other":"{count} pièces d'or"},
            "mode":{"paint":"Peindre","template":"Gabarit","ruler":"Règle"},"undo":"Annuler"})");
        strings_.use("en");
        grid_.diagonals = yh::DiagonalRule::Alternating;

        // Procedural art packed into one atlas page: a 6-frame hero strip and two icons.
        yh::Image hero{32 * 6, 32, std::vector<unsigned char>(32 * 6 * 32 * 4, 0)};
        for (int f = 0; f < 6; ++f)
        {
            const bool attacking = f >= 4;
            const float bob = attacking ? 0 : std::sin(static_cast<float>(f) * 1.5708f) * 3;
            disc(hero, f * 32 + 16.0f, 18 + bob, 10, {90, 160, 255, 255});
            disc(hero, f * 32 + 16.0f, 8 + bob, 6, {240, 210, 170, 255});
            if (attacking) disc(hero, f * 32 + (f == 4 ? 24.0f : 29.0f), 14, 4, {230, 230, 240, 255});
        }
        yh::Image fire{16, 16, std::vector<unsigned char>(16 * 16 * 4, 0)}, coin = fire;
        disc(fire, 8, 10, 5, {255, 120, 40, 255}); disc(fire, 8, 6, 3, {255, 210, 80, 255});
        disc(coin, 8, 8, 6, {250, 200, 60, 255}); disc(coin, 8, 8, 3, {200, 150, 30, 255});
        atlas_.add("hero", std::move(hero)); atlas_.add("fire", std::move(fire)); atlas_.add("coin", std::move(coin));
        atlas_.pack();

        yh::SessionHost::Rules rules;
        rules.validate = [](yh::PlayerId, std::string_view, std::string_view data, std::string&) { return std::optional<std::string>(data); };
        rules.apply = [this](const yh::NetCommand&) { ++hostValue_; };
        rules.snapshot = [this] { return std::to_string(hostValue_); };
        host_ = std::make_unique<yh::SessionHost>(hub_.host(), "tools", "1", rules);
        yh::SessionClient::Handlers handlers;
        handlers.welcomed = [this](yh::PlayerId, std::string_view snapshot) { clientValue_ = std::stoi(std::string(snapshot)); };
        handlers.apply = [this](const yh::NetCommand&) { ++clientValue_; };
        client_ = std::make_unique<yh::SessionClient>(hub_.connect(), "tools", "1", "client", handlers);
    }

    bool handleEvent(const SDL_Event& e) override { input_.handle(e); return true; }

    void update(double dt) override
    {
        YH_PROFILE("tools.update");
        time_ += dt;
        host_->update(dt); client_->update(dt);
        animator_.update(sheet_, dt, [this](std::string_view event) { if (event == "hit") hitFlash_ = 0.6; });
        if (animator_.finished() || animator_.clip().empty()) animator_.play(sheet_, "walk");
        hitFlash_ = std::max(0.0, hitFlash_ - dt);
        yh::debug::value("tools.painted", static_cast<double>(std::count(painted_.begin(), painted_.end(), true)));
    }

    void draw(yh::Renderer& r) override
    {
        if (!assets_)
        {
            assets_ = std::make_unique<yh::Assets>(files_, r);
            atlas_.upload(r);
            const yh::AtlasRegion* region = atlas_.find("hero");
            const float page = r.textureSize(region->texture).x;
            sheet_ = yh::SpriteSheet::fromJson(R"({"frameWidth":32,"frameHeight":32,"clips":{
                "walk":{"frames":"0-3","fps":6},"attack":{"frames":[4,5,4],"fps":8,"loop":false,"next":"walk","events":{"1":"hit"}}}})",
                {page, page}, region->uv).value_or(yh::SpriteSheet{});
            sheet_.texture = region->texture;
            animator_.play(sheet_, "walk");
        }
        r.clear({13, 18, 25, 255});
        ui_.theme = skin_.resolveTheme(*assets_);
        ui_.begin(r, input_);
        const bool typing = ui_.editing("name");
        if (!typing && input_.shortcutDown() && input_.keyPressed(SDLK_Z)) input_.shiftDown() ? history_.redo() : history_.undo();
        if (!typing && input_.shortcutDown() && input_.keyPressed(SDLK_Y)) history_.redo();
        if (!typing && input_.keyPressed(SDLK_SPACE)) animator_.play(sheet_, "attack", true);

        drawPanel(r);
        drawMap(r);
        input_.endFrame();
    }

    const char* help() const override { return "Tools: paint + Ctrl+Z/Y, templates aim at the mouse, ruler clicks (right-click clears), Space attacks, F3 stats"; }

private:
    enum class Mode { Paint, Template, Ruler };
    static constexpr int columns = 22, rows = 16;
    static constexpr float cell = 32;

    static void disc(yh::Image& image, float cx, float cy, float radius, yh::Color color)
    {
        for (int y = 0; y < image.height; ++y)
            for (int x = 0; x < image.width; ++x)
            {
                const float dx = static_cast<float>(x) + 0.5f - cx, dy = static_cast<float>(y) + 0.5f - cy;
                if (dx * dx + dy * dy > radius * radius) continue;
                unsigned char* p = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
                p[0] = color.r; p[1] = color.g; p[2] = color.b; p[3] = color.a;
            }
    }

    void drawPanel(yh::Renderer& r)
    {
        const float x = 24, w = 316;
        ui_.panel({12, 12, 340, r.bounds().h - 24});
        ui_.label({x, 20}, "Tools + text", ui_.theme.accent);
        const char* locales[] = {"en", "pt", "fr"};
        for (int i = 0; i < 3; ++i)
            if (ui_.toggle({x + i * 106.0f, 50, 100, 30}, locales[i], strings_.locale() == locales[i])) strings_.use(locales[i]);
        ui_.label({x, 88}, strings_.get("greeting"));
        ui_.label({x, 114}, strings_.plural("coins", static_cast<long long>(std::round(coins_))), ui_.theme.textDim);
        ui_.slider({x, 142, w, 18}, coins_, 0, 25);
        ui_.label({x, 172}, strings_.get("name") + ":");
        ui_.textBox("name", {x + 70, 168, w - 70, 32}, name_, 40);

        const yh::RichText::Palette palette{{"accent", ui_.theme.accent}, {"good", ui_.theme.good}, {"bad", ui_.theme.bad}};
        auto rich = yh::RichText::parse("[wave]Hail[/wave], [color=accent]" + yh::RichText::escape(name_) + "[/color]! [icon=fire] "
            "[color=#ff8040]Fireball[/color] deals [color=bad]28[/color] damage. [shake]The floor trembles.[/shake] "
            "Loot: [icon=coin] [rainbow]Legendary ring[/rainbow]", &palette);
        rich.layout(ui_.theme.font, w);
        rich.draw(r, {x, 212}, ui_.theme.text, time_, [this](yh::Renderer& renderer, std::string_view icon, const yh::Rect& dest) {
            atlas_.draw(renderer, icon, dest);
        });

        float y = 222 + rich.size().y;
        const char* modeKeys[] = {"mode.paint", "mode.template", "mode.ruler"};
        for (int i = 0; i < 3; ++i)
            if (ui_.toggle({x + i * 106.0f, y, 100, 30}, strings_.get(modeKeys[i]), static_cast<int>(mode_) == i)) { mode_ = static_cast<Mode>(i); waypoints_.clear(); }
        y += 38;
        const char* shapes[] = {"Circle", "Cone", "Line", "Square"};
        for (int i = 0; i < 4; ++i)
            if (ui_.toggle({x + i * 79.0f, y, 75, 28}, shapes[i], static_cast<int>(shape_) == i)) { shape_ = static_cast<yh::TemplateShape>(i); mode_ = Mode::Template; }
        y += 36;
        if (ui_.button({x, y, 100, 30}, strings_.get("undo"), history_.canUndo())) history_.undo();
        if (ui_.button({x + 106, y, 100, 30}, strings_.get("redo"), history_.canRedo())) history_.redo();
        ui_.label({x + 214, y + 4}, history_.dirty() ? "edited" : "saved", ui_.theme.textDim);
        if (ui_.button({x + 270, y, 46, 30}, "Save")) history_.markSaved();
        y += 40;
        char line[128];
        std::snprintf(line, sizeof line, "Host %d   Client %d (seq %llu)", hostValue_, clientValue_, static_cast<unsigned long long>(client_->sequence()));
        ui_.label({x, y}, line);
        if (ui_.button({x + 214, y - 4, 102, 30}, "Client +1")) client_->submit("add", "1");
        y += 34;
        std::snprintf(line, sizeof line, "Gamepads %zu   stick %.2f, %.2f", input_.gamepads(), input_.axis(static_cast<uint8_t>(SDL_GAMEPAD_AXIS_LEFTX)), input_.axis(static_cast<uint8_t>(SDL_GAMEPAD_AXIS_LEFTY)));
        ui_.label({x, y}, line, ui_.theme.textDim);
    }

    void drawMap(yh::Renderer& r)
    {
        const yh::Rect view{364, 12, std::max(1.0f, r.bounds().w - 376), r.bounds().h - 24};
        r.pushViewport(view);
        r.clear({24, 30, 38, 255});
        const yh::Vec2 mouse = input_.mouse() - view.position();
        const bool overMap = yh::Rect{0, 0, view.w, view.h}.contains(mouse) && input_.mouseInside();
        const yh::Cell under = grid_.cellAt(mouse);
        const bool onGrid = overMap && under.x >= 0 && under.y >= 0 && under.x < columns && under.y < rows;

        if (mode_ == Mode::Paint)
        {
            if (onGrid && input_.buttonPressed(yh::MouseButton::Left)) strokeValue_ = !painted_[index(under)];
            if (onGrid && input_.buttonDown(yh::MouseButton::Left) && painted_[index(under)] != strokeValue_)
            {
                const size_t i = index(under);
                const bool to = strokeValue_;
                history_.perform("Paint", [this, i, to] { painted_[i] = to; }, [this, i, to] { painted_[i] = !to; }, "stroke");
            }
            if (!input_.buttonDown(yh::MouseButton::Left)) history_.breakMerge();
        }
        if (mode_ == Mode::Ruler && overMap)
        {
            if (input_.buttonClicked(yh::MouseButton::Left)) waypoints_.push_back(grid_.center(under));
            if (input_.buttonClicked(yh::MouseButton::Right)) waypoints_.clear();
        }

        for (int cy = 0; cy < rows; ++cy)
            for (int cx = 0; cx < columns; ++cx)
                if (painted_[index({cx, cy})]) r.fillRect({cx * cell, cy * cell, cell, cell}, {60, 150, 140, 255});
        grid_.draw(r, {0, 0, columns * cell, rows * cell}, 1, {255, 255, 255, 40});

        const yh::Vec2 caster = grid_.center({4, 8});
        if (mode_ == Mode::Template)
        {
            const bool aimed = shape_ == yh::TemplateShape::Cone || shape_ == yh::TemplateShape::Line;
            // Bursts sit on grid intersections; cones and lines start at the caster.
            const yh::Vec2 corner{std::round(mouse.x / cell) * cell, std::round(mouse.y / cell) * cell};
            const float size = shape_ == yh::TemplateShape::Circle ? 2 * cell : shape_ == yh::TemplateShape::Square ? 3 * cell : 6 * cell;
            const yh::AreaTemplate area = yh::AreaTemplate::aimed(shape_, aimed ? caster : corner, aimed ? mouse : corner + yh::Vec2{1, 0}, size);
            for (yh::Cell c : area.cells(grid_)) r.fillRect({c.x * cell + 2, c.y * cell + 2, cell - 4, cell - 4}, {255, 120, 60, 90});
            area.draw(r, grid_, {255, 140, 60, 40}, {255, 170, 90, 255});
        }
        if (mode_ == Mode::Ruler && !waypoints_.empty())
        {
            std::vector<yh::Vec2> route = waypoints_;
            if (overMap) route.push_back(grid_.center(under));
            yh::drawRuler(r, grid_, route, 5, "ft", {120, 220, 255, 255});
        }

        sheet_.draw(r, animator_.frame(sheet_), {caster.x - 32, caster.y - 40, 64, 64});
        if (hitFlash_ > 0) r.drawText({caster.x + 30, caster.y - 50 - static_cast<float>(0.6 - hitFlash_) * 40}, "Hit!", {255, 220, 120, 255}, 2);
        r.drawText({8, view.h - 22}, "Diagonals: 5-10-5", {255, 255, 255, 160}, 1.4f);
        r.pop();
    }

    static size_t index(yh::Cell c) { return static_cast<size_t>(c.y) * columns + static_cast<size_t>(c.x); }

    yh::FileSystem files_;
    yh::Skin skin_;
    std::unique_ptr<yh::Assets> assets_;
    yh::Input input_;
    yh::Ui ui_;
    yh::Strings strings_;
    yh::Atlas atlas_{512, 2};
    yh::SpriteSheet sheet_;
    yh::SpriteAnimator animator_;
    yh::History history_;
    yh::Grid grid_{yh::GridType::Square, cell};
    std::array<bool, columns * rows> painted_{};
    bool strokeValue_ = true;
    Mode mode_ = Mode::Template;
    yh::TemplateShape shape_ = yh::TemplateShape::Cone;
    std::vector<yh::Vec2> waypoints_;
    yh::LoopbackHub hub_;
    std::unique_ptr<yh::SessionHost> host_;
    std::unique_ptr<yh::SessionClient> client_;
    int hostValue_ = 0, clientValue_ = 0;
    float coins_ = 1;
    std::string name_ = "Ana";
    double time_ = 0, hitFlash_ = 0;
};
