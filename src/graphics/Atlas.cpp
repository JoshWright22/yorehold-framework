#include "yorehold/framework/graphics/Atlas.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace yh
{

RectPacker::RectPacker(int width, int height) : width_(width), height_(height)
{
    clear();
}

void RectPacker::clear()
{
    skyline_.assign(1, Span{0, 0, width_});
    used_ = 0;
}

float RectPacker::occupancy() const
{
    return width_ > 0 && height_ > 0 ? static_cast<float>(used_) / (static_cast<float>(width_) * static_cast<float>(height_)) : 0;
}

std::optional<PixelRect> RectPacker::insert(int width, int height)
{
    if (width <= 0 || height <= 0 || width > width_ || height > height_) return std::nullopt;
    size_t best = skyline_.size();
    int bestY = std::numeric_limits<int>::max(), bestWaste = std::numeric_limits<int>::max();
    for (size_t i = 0; i < skyline_.size(); ++i)
    {
        const int x = skyline_[i].x;
        if (x + width > width_) break;
        // The rectangle rests on the highest span it covers.
        int y = 0, left = width;
        for (size_t j = i; left > 0 && j < skyline_.size(); ++j)
        {
            y = std::max(y, skyline_[j].y);
            left -= skyline_[j].w;
        }
        if (y + height > height_) continue;
        const int waste = skyline_[i].w >= width ? skyline_[i].w - width : 0;
        if (y < bestY || (y == bestY && waste < bestWaste)) { best = i; bestY = y; bestWaste = waste; }
    }
    if (best == skyline_.size()) return std::nullopt;

    const PixelRect placed{skyline_[best].x, bestY, width, height};
    skyline_.insert(skyline_.begin() + static_cast<std::ptrdiff_t>(best), Span{placed.x, placed.y + height, width});
    // Trim spans now hidden under the new one.
    for (size_t k = best + 1; k < skyline_.size();)
    {
        const Span& previous = skyline_[k - 1];
        const int overlap = previous.x + previous.w - skyline_[k].x;
        if (overlap <= 0) break;
        skyline_[k].x += overlap;
        skyline_[k].w -= overlap;
        if (skyline_[k].w > 0) break;
        skyline_.erase(skyline_.begin() + static_cast<std::ptrdiff_t>(k));
    }
    for (size_t k = 1; k < skyline_.size();)
    {
        if (skyline_[k - 1].y == skyline_[k].y) { skyline_[k - 1].w += skyline_[k].w; skyline_.erase(skyline_.begin() + static_cast<std::ptrdiff_t>(k)); }
        else ++k;
    }
    used_ += static_cast<long long>(width) * height;
    return placed;
}

Atlas::Atlas(int pageSize, int padding) : pageSize_(std::max(16, pageSize)), padding_(std::max(0, padding)) {}

Atlas::~Atlas()
{
    releaseAll();
}

Atlas::Atlas(Atlas&& other) noexcept
    : pageSize_(other.pageSize_), padding_(other.padding_), pages_(std::move(other.pages_)), pending_(std::move(other.pending_)), regions_(std::move(other.regions_))
{
    other.pages_.clear();
}

Atlas& Atlas::operator=(Atlas&& other) noexcept
{
    if (this == &other) return *this;
    releaseAll();
    pageSize_ = other.pageSize_;
    padding_ = other.padding_;
    pages_ = std::move(other.pages_);
    pending_ = std::move(other.pending_);
    regions_ = std::move(other.regions_);
    other.pages_.clear();
    return *this;
}

void Atlas::releaseAll()
{
    for (Page& page : pages_)
        if (page.release) { page.release(); page.release = nullptr; }
}

bool Atlas::add(std::string name, Image image)
{
    const int needed = 2 * padding_;
    if (image.width <= 0 || image.height <= 0 || image.width + needed > pageSize_ || image.height + needed > pageSize_) return false;
    if (image.rgba.size() != static_cast<size_t>(image.width) * image.height * 4) return false;
    if (regions_.contains(name) || std::any_of(pending_.begin(), pending_.end(), [&](const auto& p) { return p.first == name; })) return false;
    pending_.emplace_back(std::move(name), std::move(image));
    return true;
}

void Atlas::pack()
{
    // Tallest first packs skylines most evenly.
    std::stable_sort(pending_.begin(), pending_.end(), [](const auto& a, const auto& b) { return a.second.height > b.second.height; });
    for (auto& [name, image] : pending_)
    {
        std::optional<PixelRect> spot;
        size_t pageIndex = 0;
        for (; pageIndex < pages_.size() && !spot; ++pageIndex)
            spot = pages_[pageIndex].packer.insert(image.width + 2 * padding_, image.height + 2 * padding_);
        if (spot) --pageIndex;
        else
        {
            pages_.push_back({RectPacker(pageSize_, pageSize_), Image{pageSize_, pageSize_, std::vector<unsigned char>(static_cast<size_t>(pageSize_) * pageSize_ * 4, 0)}});
            pageIndex = pages_.size() - 1;
            spot = pages_.back().packer.insert(image.width + 2 * padding_, image.height + 2 * padding_);
        }
        Page& page = pages_[pageIndex];
        page.dirty = true;
        const int left = spot->x + padding_, top = spot->y + padding_;
        // Copy with the edge pixels stretched across the padding.
        for (int y = -padding_; y < image.height + padding_; ++y)
        {
            const int sy = std::clamp(y, 0, image.height - 1);
            for (int x = -padding_; x < image.width + padding_; ++x)
            {
                const int sx = std::clamp(x, 0, image.width - 1);
                std::memcpy(&page.image.rgba[(static_cast<size_t>(top + y) * pageSize_ + static_cast<size_t>(left + x)) * 4],
                    &image.rgba[(static_cast<size_t>(sy) * image.width + static_cast<size_t>(sx)) * 4], 4);
            }
        }
        const float size = static_cast<float>(pageSize_);
        regions_[name] = {static_cast<int>(pageIndex), page.texture,
            {left / size, top / size, image.width / size, image.height / size}, {left, top, image.width, image.height}};
    }
    pending_.clear();
}

void Atlas::upload(Renderer& renderer, bool smooth)
{
    pack();
    for (Page& page : pages_)
    {
        if (!page.dirty) continue;
        if (page.texture == 0)
        {
            page.texture = renderer.createTexture(pageSize_, pageSize_, page.image.rgba.data(), smooth);
            page.release = renderer.textureRelease(page.texture);
        }
        else renderer.updateTexture(page.texture, page.image.rgba.data());
        page.dirty = false;
    }
    for (auto& [name, region] : regions_) region.texture = pages_[static_cast<size_t>(region.page)].texture;
}

const AtlasRegion* Atlas::find(std::string_view name) const
{
    const auto region = regions_.find(std::string(name));
    return region == regions_.end() ? nullptr : &region->second;
}

void Atlas::draw(Renderer& renderer, std::string_view name, const Rect& dest, Color tint) const
{
    if (const AtlasRegion* region = find(name); region && region->texture != 0)
        renderer.drawSpriteRegion(region->texture, dest, region->uv, tint);
}

}
