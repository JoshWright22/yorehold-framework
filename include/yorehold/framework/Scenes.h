#pragma once

#include "yorehold/framework/Host.h"

#include <memory>
#include <vector>

namespace yh
{

// Screens use the same lifecycle as Game. Stack operations requested inside callbacks are
// deferred until the callback traversal ends, so a screen can safely close itself.
class Scenes : public Game
{
public:
    void push(std::unique_ptr<Game> scene, bool drawBelow = false, bool updateBelow = false);
    void pop();
    void clear();
    size_t count() const { return scenes_.size(); }
    void load() override;
    void unload() override;
    void update(double dt) override;
    void draw(Renderer& renderer) override;
    bool handleEvent(const SDL_Event& event) override;
    std::string describe() const override;

private:
    struct Entry { std::unique_ptr<Game> scene; bool drawBelow, updateBelow; };
    struct Command { enum Kind { Push, Pop, Clear } kind; Entry entry; };
    void applyPending();
    std::vector<Entry> scenes_;
    std::vector<Command> pending_;
    bool loaded_ = false;
};

}
