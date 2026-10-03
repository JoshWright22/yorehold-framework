#include "yorehold/framework/Scenes.h"

#include <stdexcept>

namespace yh
{

void Scenes::push(std::unique_ptr<Game> scene, bool drawBelow, bool updateBelow)
{
    if (!scene) throw std::invalid_argument("Cannot push an empty scene");
    pending_.push_back({Command::Push, {std::move(scene), drawBelow, updateBelow}});
}
void Scenes::pop() { pending_.push_back({Command::Pop, {nullptr, false, false}}); }
void Scenes::clear() { pending_.push_back({Command::Clear, {nullptr, false, false}}); }
void Scenes::load() { loaded_ = true; applyPending(); }

void Scenes::applyPending()
{
    // load/unload callbacks may queue more commands, which wait for the next traversal.
    auto commands = std::move(pending_);
    pending_.clear();
    for (auto& c : commands)
    {
        if (c.kind == Command::Push)
        {
            scenes_.push_back(std::move(c.entry));
            if (loaded_) scenes_.back().scene->load();
        }
        else if (c.kind == Command::Pop && !scenes_.empty())
        {
            if (loaded_) scenes_.back().scene->unload();
            scenes_.pop_back();
        }
        else if (c.kind == Command::Clear)
        {
            while (!scenes_.empty())
            {
                if (loaded_) scenes_.back().scene->unload();
                scenes_.pop_back();
            }
        }
    }
}
void Scenes::unload()
{
    if (loaded_)
        for (size_t i = scenes_.size(); i-- > 0;) scenes_[i].scene->unload();
    loaded_ = false;
    scenes_.clear();
    pending_.clear();
}
void Scenes::update(double dt)
{
    applyPending();
    if (scenes_.empty()) return;
    size_t first = scenes_.size() - 1;
    while (first > 0 && scenes_[first].updateBelow) --first;
    for (size_t i = first; i < scenes_.size(); ++i) scenes_[i].scene->update(dt);
    applyPending();
}
void Scenes::draw(Renderer& renderer)
{
    if (scenes_.empty()) return;
    size_t first = scenes_.size() - 1;
    while (first > 0 && scenes_[first].drawBelow) --first;
    for (size_t i = first; i < scenes_.size(); ++i) scenes_[i].scene->draw(renderer);
}
bool Scenes::handleEvent(const SDL_Event& event)
{
    bool used = false;
    for (size_t i = scenes_.size(); i-- > 0;)
    {
        if (scenes_[i].scene->handleEvent(event)) { used = true; break; }
        if (!scenes_[i].updateBelow) break;
    }
    applyPending();
    return used;
}
std::string Scenes::describe() const { return scenes_.empty() ? std::string{} : scenes_.back().scene->describe(); }

}
