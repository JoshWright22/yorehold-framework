#pragma once

#include "yorehold/framework/graphics/Image.h"
#include "yorehold/framework/graphics/Renderer.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yh
{

struct PixelRect
{
    int x = 0, y = 0, w = 0, h = 0;
};

// Skyline bottom-left packing into one fixed-size page. Good density for sprites and glyphs,
// and fast enough to pack thousands of images at load time.
class RectPacker
{
public:
    RectPacker(int width, int height);
    // Where a w x h rectangle fits, or nothing when the page is full.
    std::optional<PixelRect> insert(int width, int height);
    void clear();
    // Fraction of the page area in use.
    float occupancy() const;

private:
    struct Span { int x, y, w; };
    int width_, height_;
    long long used_ = 0;
    std::vector<Span> skyline_;
};

// Where one packed image ended up.
struct AtlasRegion
{
    int page = 0;
    TextureId texture = 0; // set once uploaded
    Rect uv;               // 0-1 texture coordinates, for Renderer::drawSpriteRegion
    PixelRect pixels;
};

// Packs many small images (tokens, tiles, icons, animation frames) into a few large textures so
// the renderer can draw them in one batch instead of switching textures per sprite.
// Owns its textures: they're released when the atlas is destroyed.
class Atlas
{
public:
    // `padding` pixels around each image repeat its edge pixels, so smooth filtering and
    // zoomed-out sampling don't bleed neighbours into each other.
    explicit Atlas(int pageSize = 2048, int padding = 2);
    ~Atlas();
    Atlas(Atlas&&) noexcept;
    Atlas& operator=(Atlas&&) noexcept;
    Atlas(const Atlas&) = delete;
    Atlas& operator=(const Atlas&) = delete;

    // Queues an image; returns false when it can never fit on a page (or the name is taken).
    bool add(std::string name, Image image);
    // Packs everything queued (largest first) into page pixel buffers. Already-packed images stay put.
    void pack();
    // Packs if needed and uploads the pages. Call again after adding more images.
    void upload(Renderer& renderer, bool smooth = false);

    const AtlasRegion* find(std::string_view name) const;
    void draw(Renderer& renderer, std::string_view name, const Rect& dest, Color tint = {}) const;
    size_t pages() const { return pages_.size(); }
    const Image& pageImage(size_t page) const { return pages_[page].image; }
    size_t size() const { return regions_.size(); }

private:
    struct Page
    {
        RectPacker packer;
        Image image;
        TextureId texture = 0;
        bool dirty = true;
        std::function<void()> release;
    };
    void releaseAll();

    int pageSize_, padding_;
    std::vector<Page> pages_;
    std::vector<std::pair<std::string, Image>> pending_;
    std::unordered_map<std::string, AtlasRegion> regions_;
};

}
