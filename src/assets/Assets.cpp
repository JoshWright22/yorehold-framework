#include "yorehold/framework/assets/Assets.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace yh
{

size_t Assets::TextureKeyHash::operator()(const TextureKey& key) const
{
    const size_t pathHash = std::hash<std::string>{}(key.path);
    const size_t filterHash = std::hash<bool>{}(key.smooth);
    const size_t sizeHash = std::hash<int>{}(key.circleSize);
    return pathHash ^ (filterHash << 1) ^ (sizeHash << 2);
}

Assets::~Assets()
{
    clear();
    if (releaseMissing_) releaseMissing_();
}

TextureId Assets::missingTexture()
{
    if (!haveMissing_)
    {
        std::vector<unsigned char> pixels(16 * 16 * 4);
        for (int y = 0; y < 16; y++)
        {
            for (int x = 0; x < 16; x++)
            {
                const bool magenta = ((x / 4) + (y / 4)) % 2 == 0;
                unsigned char* p = &pixels[(y * 16 + x) * 4];
                p[0] = magenta ? 255 : 0;
                p[1] = 0;
                p[2] = magenta ? 255 : 0;
                p[3] = 255;
            }
        }
        missing_ = renderer_.createTexture(16, 16, pixels.data());
        haveMissing_ = true;
        releaseMissing_ = renderer_.textureRelease(missing_);
    }
    return missing_;
}

TextureId Assets::load(const TextureKey& key)
{
    if (auto it = textures_.find(key); it != textures_.end())
        return it->second;

    TextureId texture = missingTexture();
    if (const auto loaded = loadTexture(key))
    {
        texture = *loaded;
        releases_[texture] = renderer_.textureRelease(texture);
    }
    textures_[key] = texture;
    return texture;
}

std::optional<TextureId> Assets::loadTexture(const TextureKey& key)
{
    const auto bytes = files_.read(key.path);
    if (!bytes) return std::nullopt;

    if (key.circleSize == 0)
        return renderer_.loadTexture(*bytes, key.smooth);

    const auto image = decodeImage(*bytes);
    if (!image) return std::nullopt;

    const Image portrait = circleImage(*image, key.circleSize);
    return renderer_.createTexture(portrait.width, portrait.height, portrait.rgba.data(), true);
}

TextureId Assets::texture(std::string_view path, bool smooth)
{
    return load({FileSystem::normalize(path), smooth, 0});
}

TextureId Assets::circleTexture(std::string_view path, int size)
{
    return load({FileSystem::normalize(path), true, std::clamp(size, 1, maxTextureSize)});
}

std::optional<Image> Assets::image(std::string_view path)
{
    if (std::optional<std::vector<unsigned char>> bytes = files_.read(FileSystem::normalize(path)))
        return decodeImage(*bytes);
    return std::nullopt;
}

Font* Assets::font(std::string_view path, float pixelHeight)
{
    if (!std::isfinite(pixelHeight) || pixelHeight <= 0 || pixelHeight > 512) return nullptr;
    const std::pair<std::string, int> key{FileSystem::normalize(path), static_cast<int>(pixelHeight * 4)};
    if (auto it = fonts_.find(key); it != fonts_.end())
        return it->second.get();

    std::unique_ptr<Font> loaded;
    if (std::optional<std::vector<unsigned char>> bytes = files_.read(key.first))
        loaded = Font::load(renderer_, std::move(*bytes), pixelHeight);
    Font* result = loaded.get();
    fonts_[key] = std::move(loaded);
    return result;
}

int Assets::hotReload()
{
    int reloaded = 0;
    for (const std::string& path : files_.pollChanges())
    {
        reloaded += reloadTextures(path);
        reloaded += reloadFonts(path);
    }
    return reloaded;
}

void Assets::releaseTexture(TextureId texture)
{
    const auto release = releases_.find(texture);
    if (release == releases_.end()) return; // The shared missing-image texture is kept until destruction.

    release->second();
    releases_.erase(release);
}

int Assets::reloadTextures(const std::string& path)
{
    // Collect keys first: rebuilding entries while walking the hash map would invalidate its iterators.
    std::vector<TextureKey> variants;
    for (const auto& [key, texture] : textures_)
    {
        if (key.path == path) variants.push_back(key);
    }
    for (const TextureKey& key : variants)
    {
        releaseTexture(textures_.at(key));
        textures_.erase(key);
        load(key);
    }
    return static_cast<int>(variants.size());
}

int Assets::reloadFonts(const std::string& path)
{
    int reloaded = 0;
    for (auto font = fonts_.begin(); font != fonts_.end();)
    {
        if (font->first.first == path)
        {
            font = fonts_.erase(font);
            ++reloaded;
        }
        else
        {
            ++font;
        }
    }
    return reloaded;
}

void Assets::clear()
{
    for (auto& [id, release] : releases_) release();
    releases_.clear();
    textures_.clear();
    fonts_.clear();
}

}
