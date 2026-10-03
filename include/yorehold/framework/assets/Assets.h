#pragma once

#include "yorehold/framework/assets/FileSystem.h"
#include "yorehold/framework/graphics/Font.h"
#include "yorehold/framework/graphics/Image.h"
#include "yorehold/framework/graphics/Renderer.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace yh
{

// Loads and caches textures and fonts by virtual path, and reloads them when their files change.
// Ask for assets by path every time (it's a hash lookup) rather than keeping TextureIds: after a skin
// switch or hot reload the path may point at a new texture.
class Assets
{
public:
    Assets(FileSystem& files, Renderer& renderer) : files_(files), renderer_(renderer) {}
    ~Assets();

    // Missing files give a magenta/black checkerboard, so they're obvious instead of invisible.
    // Any format decodeImage reads works (PNG, JPEG, WebP, GIF, ...).
    TextureId texture(std::string_view path, bool smooth = false);
    Vec2 textureSize(std::string_view path) { return renderer_.textureSize(texture(path)); }
    // The image's centred square cut to a circle, smooth-filtered: token portraits from any
    // picture. `size` is the texture's side in pixels.
    TextureId circleTexture(std::string_view path, int size = 256);
    // Decoded pixels without a texture (building tilesets, atlases). Not cached.
    std::optional<Image> image(std::string_view path);
    // nullptr if the font file is missing or broken.
    Font* font(std::string_view path, float pixelHeight);

    // Reloads anything whose file changed. Returns how many assets were reloaded.
    int hotReload();
    // Forget everything (e.g. after mounting a different skin), so the next request reloads.
    // Frees fonts: don't call while text from this frame still holds Font pointers.
    void clear();

    FileSystem& files() { return files_; }

private:
    // How a cached texture was made from its file, so hot reload can make it again.
    struct TextureKey
    {
        std::string path;
        bool smooth = false;
        int circleSize = 0; // 0 keeps the original shape; otherwise this is the portrait's side in pixels.
        bool operator==(const TextureKey&) const = default;
    };
    struct TextureKeyHash
    {
        size_t operator()(const TextureKey& key) const;
    };

    TextureId missingTexture();
    TextureId load(const TextureKey& key);
    std::optional<TextureId> loadTexture(const TextureKey& key);
    void releaseTexture(TextureId texture);
    int reloadTextures(const std::string& path);
    int reloadFonts(const std::string& path);

    FileSystem& files_;
    Renderer& renderer_;
    std::unordered_map<TextureKey, TextureId, TextureKeyHash> textures_;
    std::map<std::pair<std::string, int>, std::unique_ptr<Font>> fonts_;
    TextureId missing_ = 0;
    bool haveMissing_ = false;
    std::unordered_map<TextureId, std::function<void()>> releases_;
    std::function<void()> releaseMissing_;
};

}
