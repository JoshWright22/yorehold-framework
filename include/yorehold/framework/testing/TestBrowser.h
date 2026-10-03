#pragma once

#include "yorehold/framework/Host.h"
#include "yorehold/framework/testing/TestScene.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yh
{

// A window listing every registered TestScene, like osu-framework's visual test browser.
// Tab / Shift+Tab or clicking the list switches scenes, F5 restarts the current one, F1 hides the list.
class TestBrowser : public Game
{
public:
    using Factory = std::function<std::unique_ptr<TestScene>()>;

    template <typename T>
    void add(std::string name)
    {
        add(std::move(name), [] { return std::make_unique<T>(); });
    }

    void add(std::string name, Factory make);

    // Handles --list and --scene NAME, plus the host's arguments (see parseHostArgs).
    // Without --scene it reopens the last scene, if settings.stateDir is set.
    int run(int argc, char** argv, HostSettings settings = {});

    void load() override;
    void unload() override { scene_.reset(); }
    void update(double deltaSeconds) override;
    void draw(Renderer& renderer) override;
    bool handleEvent(const SDL_Event& event) override;
    std::string describe() const override;

private:
    struct Entry
    {
        std::string name;
        Factory make;
    };

    void open(size_t index, bool remember = true);
    float listWidth() const;

    std::vector<Entry> entries_;
    std::unique_ptr<TestScene> scene_;
    std::string lastSceneFile_;
    size_t current_ = 0;
    bool showList_ = true;
};

}
