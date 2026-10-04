#pragma once

#include "yorehold/framework/animation/Tween.h"
#include "yorehold/framework/assets/Assets.h"
#include "yorehold/framework/graphics/Particles.h"
#include "yorehold/framework/ui/Ui.h"

namespace yh
{

// skin.json is data: metadata, widget colours, a body font, animation settings and effect files.
// FileSystem's layered mounts supply defaults for assets omitted by a skin.
struct Skin
{
    std::string name;
    std::string author;
    UiTheme theme;
    std::string bodyFont = "fonts/AtkinsonHyperlegible-Regular.ttf";
    float bodyPixels = 20;
    Ease animationEase = Ease::OutCubic;
    double animationSeconds = 0.25;
    std::map<std::string, ParticleEffect, std::less<>> effects;

    static std::optional<Skin> load(FileSystem& files, std::string_view manifest = "skin.json", std::string* error = nullptr);
    // Call again after an asset reload so the Font pointer is refreshed.
    UiTheme resolveTheme(Assets& assets) const;
};

// Fills theme.images from <folder>/<name>.png (or .webp) for every name in UiImages::all():
// panel, button, button-hover, checkbox-on... Files that don't exist leave that widget drawn from
// the theme's colours. Mount a skin over the defaults and it only needs the images it changes.
// Call again after the files change (a skin switch or hot reload).
void loadUiImages(UiTheme& theme, Assets& assets, std::string_view folder = "ui");

}
