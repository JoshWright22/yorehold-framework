#include "DebugFont.h"

#include <SDL3/SDL.h>

#include <cstring>

namespace yh::debugfont
{

std::vector<unsigned char> buildAtlas()
{
    std::vector<unsigned char> pixels(atlasWidth * atlasHeight * 4, 0);

    // Draw every glyph once with SDL's software renderer, then keep the pixels.
    SDL_Surface* surface = SDL_CreateSurface(atlasWidth, atlasHeight, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    if (!renderer)
    {
        SDL_Log("Debug font atlas failed: %s", SDL_GetError());
        SDL_DestroySurface(surface);
        return pixels;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (int c = firstChar; c < lastChar; c++)
    {
        const int index = c - firstChar;
        const char text[2] = {static_cast<char>(c), '\0'};
        SDL_RenderDebugText(renderer, static_cast<float>(index % columns * glyphSize), static_cast<float>(index / columns * glyphSize), text);
    }
    const SDL_FRect white{whiteX, whiteY, glyphSize, glyphSize};
    SDL_RenderFillRect(renderer, &white);
    SDL_FlushRenderer(renderer);

    for (int y = 0; y < atlasHeight; y++)
        std::memcpy(&pixels[y * atlasWidth * 4], static_cast<unsigned char*>(surface->pixels) + y * surface->pitch, atlasWidth * 4);

    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    return pixels;
}

}
