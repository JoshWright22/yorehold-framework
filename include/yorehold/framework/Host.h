#pragma once

struct SDL_Renderer;

namespace yh
{

class Game
{
public:
    virtual ~Game() = default;

    virtual void load() {}
    virtual void update(double deltaSeconds) { (void)deltaSeconds; }
    virtual void draw(SDL_Renderer* renderer) { (void)renderer; }
};

struct HostSettings
{
    const char* title = "Yorehold";
    int width = 1280;
    int height = 720;
};

// Opens the window and runs the game loop until the window closes. Returns the process exit code.
int run(Game& game, const HostSettings& settings = {});

}
