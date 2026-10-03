#include "yorehold/framework/assets/Skin.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>

namespace yh
{

std::optional<Skin> Skin::load(FileSystem& files, std::string_view manifest, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto data = files.readText(manifest);
        if (!data) throw std::invalid_argument("Skin manifest is missing");
        const auto j = nlohmann::json::parse(*data);
        Skin skin;
        skin.name = j.at("name").get<std::string>();
        skin.author = j.value("author", std::string{});
        std::string why;
        if (j.contains("ui"))
        {
            const auto theme = UiTheme::fromJson(j.at("ui").dump(), &why);
            if (!theme) throw std::invalid_argument(why);
            skin.theme = *theme;
        }
        skin.bodyFont = j.value("bodyFont", skin.bodyFont);
        skin.bodyPixels = j.value("bodyPixels", skin.bodyPixels);
        skin.animationSeconds = j.value("animationSeconds", skin.animationSeconds);
        if (!std::isfinite(skin.bodyPixels) || skin.bodyPixels <= 0 || skin.bodyPixels > 512
            || !std::isfinite(skin.animationSeconds) || skin.animationSeconds < 0 || skin.animationSeconds > 60)
            throw std::invalid_argument("Invalid skin font size or animation duration");
        skin.animationEase = easeFromName(j.value("animationEase", std::string("outCubic")));
        if (j.contains("effects"))
        {
            const auto& effects = j.at("effects");
            if (!effects.is_object()) throw std::invalid_argument("Skin effects must be an object");
            for (auto it = effects.begin(); it != effects.end(); ++it)
            {
                const auto effectJson = it.value().is_string() ? files.readText(it.value().get<std::string>()) : std::optional(it.value().dump());
                if (!effectJson) throw std::invalid_argument("Missing particle effect: " + it.key());
                const auto effect = ParticleEffect::fromJson(*effectJson, &why);
                if (!effect) throw std::invalid_argument(it.key() + ": " + why);
                skin.effects[it.key()] = *effect;
            }
        }
        return skin;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return std::nullopt; }
}

UiTheme Skin::resolveTheme(Assets& assets) const
{
    UiTheme resolved = theme;
    resolved.font = assets.font(bodyFont, bodyPixels);
    return resolved;
}

}
