#include "yorehold/framework/Host.h"

#include <SDL3/SDL.h>

namespace yh
{

int run(Game& game, const HostSettings& settings)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(settings.title, settings.width, settings.height, SDL_WINDOW_RESIZABLE, &window, &renderer))
    {
        SDL_Log("Window failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    game.load();

    Uint64 last = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    bool running = true;

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
        }

        const Uint64 now = SDL_GetPerformanceCounter();
        game.update(static_cast<double>(now - last) / frequency);
        last = now;

        game.draw(renderer);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}
