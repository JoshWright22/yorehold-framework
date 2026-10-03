#pragma once

#include "TestTerrain.h"

#include <yorehold/framework/assets/Skin.h>
#include <yorehold/framework/audio/Audio.h>
#include <yorehold/framework/graphics/Camera.h>
#include <yorehold/framework/map/Regions.h>
#include <yorehold/framework/testing/TestScene.h>

#include <SDL3/SDL.h>

#include <cstdio>

// Authorable objects and floors, streamed/frozen regions, skinned widgets and audio buses.
class TestSceneWorld : public yh::TestScene
{
public:
    TestSceneWorld()
    {
        files_.mountFolder(YH_FRAMEWORK_ASSETS, "framework");
        skin_ = yh::Skin::load(files_).value_or(yh::Skin{});
        auto loader = [](std::string_view id) {
            auto region = std::make_unique<yh::Region>();
            region->id = id;
            region->map = std::make_unique<yh::TileMap>(28, 18, 32.0f);
            region->map->addLayer("Ground", 0, id == "village" ? testterrain::Grass : testterrain::Sand);
            region->map->addLayer("Upper floor", 1, testterrain::Snow);
            region->map->trackChanges();
            yh::Kit door;
            door.name = "Lever door"; door.prototype.name = "Gate";
            door.prototype.area = {0, 0, 36, 96};
            door.prototype.tags = {"door", "blocksMovement", "blocksSight", "link:gate"};
            door.prototype.door = yh::Door{};
            region->objects.place(door, {450, 250});
            yh::MapObject lever;
            lever.name = "Lever"; lever.tags = {"lever", "link:gate"}; lever.area = {200, 260, 32, 32};
            region->objects.add(lever);
            yh::MapObject crate;
            crate.name = "Crate"; crate.tags = {"container", "destructible", "throwable", "blocksMovement"};
            crate.area = {300, 380, 48, 48}; crate.contents["coin"] = 12; crate.durability = yh::Durability{};
            region->objects.add(crate);
            region->variables["note"] = "A region remembers its objects.";
            return region;
        };
        regions_.add("village", loader); regions_.add("cave", loader);
        regions_.enter(0, current_);
        camera_.setBounds({0, 0, 896, 576}); camera_.jumpTo({448, 288}, 0.8f);
        audioReady_ = audio_.init(&status_);
        if (audioReady_) status_ = "Ready; audio starts when Play is pressed.";
        tone_ = yh::Sound::tone(440, 0.25f);
    }

    bool handleEvent(const SDL_Event& e) override { input_.handle(e); return true; }

    void update(double dt) override
    {
        if (pendingTravel_)
        {
            current_ = current_ == "village" ? "cave" : "village";
            regions_.enter(0, current_, &status_); pendingTravel_ = false;
            note_ = regions_.active(current_)->variables["note"];
        }
        if (pendingRestore_ && !saved_.empty())
        {
            regions_.leave(0); regions_.leave(1);
            regions_.restore(saved_, &status_); regions_.enter(0, current_);
            if (split_) regions_.enter(1, current_ == "village" ? "cave" : "village");
            note_ = regions_.active(current_)->variables["note"];
            pendingRestore_ = false;
        }
        regions_.update(dt, [](yh::Region& r, double seconds) { r.objects.update(seconds); });
        audio_.setMasterVolume(mute_ ? 0 : volume_); if (audioReady_) audio_.update();
        if (assets_)
        {
            reloadTime_ += dt;
            if (reloadTime_ >= 0.25) { assets_->hotReload(); reloadTime_ = 0; }
        }
    }

    void draw(yh::Renderer& r) override
    {
        if (!assets_)
        {
            assets_ = std::make_unique<yh::Assets>(files_, r);
            tileset_ = testterrain::makeTileset(r);
            releaseTiles_ = r.textureRelease(tileset_.texture);
        }
        yh::Region& region = *regions_.active(current_);
        if (region.map->tileset().texture != tileset_.texture) region.map->setTileset(tileset_);
        r.clear({13, 18, 25, 255});
        ui_.theme = skin_.resolveTheme(*assets_); ui_.begin(r, input_);
        ui_.panel({12, 12, 330, r.bounds().h - 24});
        ui_.label({24, 22}, "World / UI / audio", ui_.theme.accent);
        ui_.label({24, 55}, "Region: " + current_);
        char clock[100];
        std::snprintf(clock, sizeof(clock), "Time %.1fs   Active regions %zu", region.simulatedSeconds, regions_.activeCount());
        ui_.label({24, 81}, clock);
        if (ui_.button({24, 113, 300, 34}, "Travel to other region")) pendingTravel_ = true;
        if (ui_.checkbox({24, 158, 300, 28}, "Second co-op player active", split_))
        {
            if (split_) regions_.enter(1, current_ == "village" ? "cave" : "village"); else regions_.leave(1);
        }
        if (ui_.button({24, 196, 145, 34}, "Save snapshot")) { saved_ = regions_.toJson(); status_ = "Snapshot stored in memory."; }
        if (ui_.button({179, 196, 145, 34}, "Restore", !saved_.empty())) pendingRestore_ = true;
        if (ui_.button({24, 240, 145, 34}, "Pull lever")) region.objects.interact(2);
        if (ui_.button({179, 240, 145, 34}, "Damage crate")) region.objects.damage(3, 3);
        if (ui_.button({24, 284, 145, 34}, "Throw crate")) region.objects.throwTo(3, {650, 400}, 1, 100);
        if (ui_.button({179, 284, 145, 34}, "Change floor")) floor_ = 1 - floor_;
        ui_.label({24, 329}, "Saved note (UTF-8):");
        if (ui_.textBox("region-note", {24, 357, 300, 34}, note_)) region.variables["note"] = note_;
        if (ui_.button({24, 405, 145, 34}, "Play tone", audioReady_)) audio_.play(tone_, yh::AudioBus::Ui);
        ui_.checkbox({179, 408, 145, 28}, "Mute", mute_);
        ui_.slider({24, 455, 300, 24}, volume_, 0, 1);
        const float listHeight = std::max(42.0f, r.bounds().h - 532);
        ui_.beginScroll({24, 495, 300, listHeight}, 400, scroll_);
        for (int i = 0; i < 12; ++i) ui_.label({0, i * 30.0f}, "Object kit " + std::to_string(i + 1) + " (scroll)");
        ui_.endScroll();
        const yh::Rect mapView{356, 12, std::max(1.0f, r.bounds().w - 368), r.bounds().h - 24};
        r.pushViewport(mapView); camera_.setViewport(mapView.size()); camera_.apply(r);
        region.map->draw(r, camera_.visibleWorld(), camera_.zoom(), floor_);
        region.objects.draw(r, floor_);
        for (const auto& [id, o] : region.objects.all())
        {
            (void)id;
            if (o.floor == floor_ && !o.destroyed) r.drawText({o.area.x, o.area.y + o.area.h + 6}, o.name, {255, 255, 255, 255}, 1.4f);
        }
        r.pop();
        r.drawText({10, 10}, "Floor " + std::to_string(floor_) + " / ground costs 0 chunks", {255, 255, 255, 255}, 1.4f);
        r.pop();
        input_.endFrame();
    }

    ~TestSceneWorld() override { if (releaseTiles_) releaseTiles_(); }
    const char* help() const override { return "World: doors, containers, throws, floors, region saves, co-op, themed widgets and sound"; }

private:
    yh::FileSystem files_;
    yh::Skin skin_;
    std::unique_ptr<yh::Assets> assets_;
    yh::Regions regions_;
    yh::Camera camera_;
    yh::Input input_;
    yh::Ui ui_;
    yh::Tileset tileset_;
    std::function<void()> releaseTiles_;
    yh::Audio audio_;
    std::shared_ptr<const yh::Sound> tone_;
    std::string current_ = "village", note_ = "A region remembers its objects.", saved_, status_;
    bool split_ = false, pendingTravel_ = false, pendingRestore_ = false, audioReady_ = false, mute_ = false;
    int floor_ = 0;
    float volume_ = 0.5f, scroll_ = 0;
    double reloadTime_ = 0;
};
