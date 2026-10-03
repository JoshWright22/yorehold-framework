#include "yorehold/framework/testing/TestBrowser.h"

#include "yorehold/framework/graphics/Renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace yh
{

namespace
{

constexpr float rowHeight = 28.0f;
constexpr float helpHeight = 28.0f;
constexpr float listPadding = 12.0f;

bool sameName(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        if (SDL_tolower(a[i]) != SDL_tolower(b[i]))
            return false;
    }
    return true;
}

}

void TestBrowser::add(std::string name, Factory make)
{
    entries_.push_back({std::move(name), std::move(make)});
}

int TestBrowser::run(int argc, char** argv, HostSettings settings)
{
    if (!settings.stateDir.empty())
    {
        lastSceneFile_ = settings.stateDir + "/scene-" + settings.title + ".txt";
        std::string last;
        std::getline(std::ifstream(lastSceneFile_), last);
        for (size_t j = 0; j < entries_.size(); j++)
        {
            if (entries_[j].name == last)
                current_ = j;
        }
    }

    for (int i = 1; i < argc; i++)
    {
        const std::string_view arg = argv[i];

        if (arg == "--list")
        {
            for (const Entry& entry : entries_)
                std::printf("%s\n", entry.name.c_str());
            return 0;
        }

        if (arg == "--scene" && i + 1 < argc)
        {
            const std::string_view wanted = argv[++i];
            bool found = false;
            for (size_t j = 0; j < entries_.size(); j++)
            {
                if (sameName(entries_[j].name, wanted))
                {
                    current_ = j;
                    found = true;
                }
            }
            if (!found)
            {
                std::fprintf(stderr, "No test scene called '%.*s'. Use --list to see them.\n", static_cast<int>(wanted.size()), wanted.data());
                return 1;
            }
        }
    }

    return yh::run(*this, parseHostArgs(argc, argv, std::move(settings)));
}

void TestBrowser::load()
{
    if (!entries_.empty())
        open(current_, false);
}

void TestBrowser::open(size_t index, bool remember)
{
    current_ = index;
    scene_ = entries_[index].make();

    if (remember && !lastSceneFile_.empty())
    {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(lastSceneFile_).parent_path(), error);
        std::ofstream(lastSceneFile_) << entries_[index].name << '\n';
    }
}

float TestBrowser::listWidth() const
{
    if (!showList_)
        return 0.0f;

    size_t longest = 12;
    for (const Entry& entry : entries_)
        longest = std::max(longest, entry.name.size());
    return Renderer::textWidth(std::string(longest, 'M')) + listPadding * 2;
}

void TestBrowser::update(double deltaSeconds)
{
    if (scene_)
        scene_->update(deltaSeconds);
}

void TestBrowser::draw(Renderer& renderer)
{
    const Vec2 size = renderer.outputSize();
    const float list = listWidth();
    const Color panel{22, 22, 30, 255};

    if (scene_)
    {
        renderer.pushViewport({list, 0, size.x - list, size.y - helpHeight});
        scene_->draw(renderer);
        renderer.pop();
    }

    // Help strip under the scene.
    const Rect strip{list, size.y - helpHeight, size.x - list, helpHeight};
    renderer.fillRect(strip, panel);
    const char* help = scene_ ? scene_->help() : "";
    renderer.drawText({list + listPadding, strip.y + 6}, *help ? help : "Tab: next scene   F5: restart   F1: hide list   F3: frame times   F12: feedback", {170, 170, 190, 255});

    if (!showList_)
        return;

    renderer.fillRect({0, 0, list, size.y}, panel);
    renderer.drawText({listPadding, 8}, "TEST SCENES", {120, 120, 140, 255});

    for (size_t i = 0; i < entries_.size(); i++)
    {
        const float y = rowHeight * static_cast<float>(i + 1);
        if (i == current_)
            renderer.fillRect({0, y, list, rowHeight}, {58, 52, 92, 255});
        renderer.drawText({listPadding, y + 6}, entries_[i].name, {230, 230, 240, 255});
    }
}
bool TestBrowser::handleEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && !entries_.empty())
    {
        const size_t count = entries_.size();
        switch (event.key.key)
        {
        case SDLK_TAB:
            open((event.key.mod & SDL_KMOD_SHIFT) ? (current_ + count - 1) % count : (current_ + 1) % count);
            return true;
        case SDLK_F5:
            open(current_);
            return true;
        case SDLK_F1:
            showList_ = !showList_;
            return true;
        default:
            break;
        }
    }

    const float list = listWidth();

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.x < list)
    {
        const int row = static_cast<int>(event.button.y / rowHeight) - 1;
        if (row >= 0 && row < static_cast<int>(entries_.size()))
            open(static_cast<size_t>(row));
        return true;
    }

    if (!scene_)
        return false;

    // Make mouse positions relative to the scene area.
    SDL_Event local = event;
    switch (local.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        local.motion.x -= list;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        local.button.x -= list;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        local.wheel.mouse_x -= list;
        break;
    default:
        break;
    }
    return scene_->handleEvent(local);
}

std::string TestBrowser::describe() const
{
    return entries_.empty() ? std::string() : "scene: " + entries_[current_].name;
}

}
