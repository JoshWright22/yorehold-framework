#include "yorehold/framework/map/TileMap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace yh
{

namespace
{

// Below this many screen pixels per tile, draw the overview instead of individual tiles.
constexpr float overviewBelowPixels = 6.0f;
constexpr int tilePadding = 2;

const char* tilesetError(std::span<const Image> tiles, int tilePixels)
{
    if (tilePixels <= 0 || tilePixels > maxTextureSize - tilePadding * 2)
        return "Tile size must fit within one texture, including padding";
    if (tiles.empty()) return "No tile images";

    const int cellPixels = tilePixels + tilePadding * 2;
    const int tilesPerSide = maxTextureSize / cellPixels;
    const size_t capacity = static_cast<size_t>(tilesPerSide) * tilesPerSide;
    if (tiles.size() > capacity || tiles.size() > std::numeric_limits<TileId>::max())
        return "Too many tiles for one texture";

    for (const Image& tile : tiles)
    {
        if (!tile.valid()) return "Invalid tile image pixels";
    }
    return nullptr;
}

uint8_t averageChannel(double weightedColor, double totalAlpha)
{
    if (totalAlpha == 0) return 0;
    return static_cast<uint8_t>(std::clamp(weightedColor / totalAlpha, 0.0, 255.0));
}

Color averageColor(const Image& image)
{
    double red = 0, green = 0, blue = 0, totalAlpha = 0;
    for (size_t offset = 0; offset < image.rgba.size(); offset += 4)
    {
        const double alpha = image.rgba[offset + 3] / 255.0;
        red += image.rgba[offset] * alpha;
        green += image.rgba[offset + 1] * alpha;
        blue += image.rgba[offset + 2] * alpha;
        totalAlpha += alpha;
    }

    // Transparent pixels contribute to coverage, but their invisible RGB does not affect the colour.
    const double pixelCount = static_cast<double>(image.width) * image.height;
    const auto coverage = static_cast<uint8_t>(std::clamp(totalAlpha / pixelCount * 255.0, 0.0, 255.0) + 0.5);
    return {averageChannel(red, totalAlpha), averageChannel(green, totalAlpha), averageChannel(blue, totalAlpha), coverage};
}

void copyPaddedTile(const Image& tile, Image& atlas, int left, int top)
{
    const int cellPixels = tile.width + tilePadding * 2;
    for (int y = 0; y < cellPixels; ++y)
    {
        for (int x = 0; x < cellPixels; ++x)
        {
            // Outside the tile, repeat its closest edge pixel so filtering never samples a neighbour.
            const int sourceX = std::clamp(x - tilePadding, 0, tile.width - 1);
            const int sourceY = std::clamp(y - tilePadding, 0, tile.height - 1);
            const size_t sourceOffset = (static_cast<size_t>(sourceY) * tile.width + sourceX) * 4;
            const size_t destinationOffset = (static_cast<size_t>(top + y) * atlas.width + left + x) * 4;
            std::memcpy(&atlas.rgba[destinationOffset], &tile.rgba[sourceOffset], 4);
        }
    }
}

void blendOverviewPixel(unsigned char* background, Color foreground)
{
    // Keep integer weights to match the renderer's straight-alpha compositing without rounding each layer.
    const unsigned foregroundWeight = foreground.a * 255u;
    const unsigned backgroundWeight = background[3] * (255u - foreground.a);
    const unsigned combinedWeight = foregroundWeight + backgroundWeight;
    if (combinedWeight == 0) return;

    auto blendChannel = [&](unsigned color, unsigned below) {
        return static_cast<unsigned char>((color * foregroundWeight + below * backgroundWeight) / combinedWeight);
    };
    background[0] = blendChannel(foreground.r, background[0]);
    background[1] = blendChannel(foreground.g, background[1]);
    background[2] = blendChannel(foreground.b, background[2]);
    background[3] = static_cast<unsigned char>((combinedWeight + 127) / 255);
}

int floorDiv(int value, int divisor)
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

}

Rect Tileset::uv(TileId id) const
{
    if (columns <= 0 || rows <= 0 || tilePixels <= 0 || id == 0
        || static_cast<int64_t>(id) > static_cast<int64_t>(columns) * rows) return {};
    const int tileIndex = id - 1;
    const int column = tileIndex % columns;
    const int row = tileIndex / columns;
    if (padding > 0)
    {
        const int cellPixels = tilePixels + padding * 2;
        const float atlasWidth = static_cast<float>(columns * cellPixels);
        const float atlasHeight = static_cast<float>(rows * cellPixels);
        return {
            static_cast<float>(column * cellPixels + padding) / atlasWidth,
            static_cast<float>(row * cellPixels + padding) / atlasHeight,
            static_cast<float>(tilePixels) / atlasWidth,
            static_cast<float>(tilePixels) / atlasHeight,
        };
    }
    const float w = 1.0f / static_cast<float>(columns);
    const float h = 1.0f / static_cast<float>(rows);
    // Inset half a texel so filtering never picks up the neighbouring tile (visible as seams).
    const float insetX = 0.5f / static_cast<float>(columns * tilePixels);
    const float insetY = 0.5f / static_cast<float>(rows * tilePixels);
    return {column * w + insetX, row * h + insetY, w - insetX * 2, h - insetY * 2};
}

std::optional<Tileset> buildTileset(Renderer& renderer, std::span<const Image> tiles, int tilePixels, bool smooth, std::string* error)
{
    if (error) error->clear();
    if (const char* problem = tilesetError(tiles, tilePixels))
    {
        if (error) *error = problem;
        return std::nullopt;
    }

    const int cellPixels = tilePixels + tilePadding * 2;
    const int tilesPerSide = maxTextureSize / cellPixels;
    const int squareColumns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(tiles.size()))));
    Tileset tileset;
    tileset.tilePixels = tilePixels;
    tileset.padding = tilePadding;
    tileset.columns = std::min(tilesPerSide, squareColumns);
    tileset.rows = static_cast<int>((tiles.size() + tileset.columns - 1) / tileset.columns);
    const int atlasWidth = tileset.columns * cellPixels;
    const int atlasHeight = tileset.rows * cellPixels;
    Image atlas{atlasWidth, atlasHeight, std::vector<unsigned char>(static_cast<size_t>(atlasWidth) * atlasHeight * 4, 0)};
    for (size_t index = 0; index < tiles.size(); ++index)
    {
        const Image tile = squareImage(tiles[index], tilePixels);
        const int left = static_cast<int>(index % tileset.columns) * cellPixels;
        const int top = static_cast<int>(index / tileset.columns) * cellPixels;
        copyPaddedTile(tile, atlas, left, top);
        tileset.averageColors.push_back(averageColor(tile));
    }

    tileset.texture = renderer.createTexture(atlasWidth, atlasHeight, atlas.rgba.data(), smooth);
    const auto release = renderer.textureRelease(tileset.texture);
    // Copies can share a tileset. The last copy releases its texture, even if the renderer has shut down.
    tileset.owner = std::shared_ptr<void>(nullptr, [release](void*) { release(); });
    return tileset;
}

TileMap::TileMap(int width, int height, float tileSize)
    : width_(width), height_(height), tileSize_(tileSize)
{
    if (width <= 0 || height <= 0 || tileSize <= 0 || !std::isfinite(tileSize)
        || !std::isfinite(width * tileSize) || !std::isfinite(height * tileSize))
        throw std::invalid_argument("Map dimensions and tile size must be positive and finite");
}

TileMap::~TileMap()
{
    for (auto& [floor, pages] : overview_)
        for (auto& [cell, page] : pages)
            if (page.release) page.release();
}

int TileMap::addLayer(std::string name, int floor, TileId defaultTile)
{
    layers_.emplace_back();
    Layer& layer = layers_.back();
    layer.name = std::move(name);
    layer.floor = floor;
    layer.defaultTile = defaultTile;
    overviewDirtyAll();
    return static_cast<int>(layers_.size() - 1);
}

void TileMap::setLayerVisible(int layer, bool visible)
{
    layers_.at(layer).visible = visible;
    overviewDirtyAll();
}

TileId TileMap::tile(int layer, int x, int y) const
{
    const Layer& l = layers_.at(layer);
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
    const auto it = l.chunks.find({floorDiv(x, chunkSize), floorDiv(y, chunkSize)});
    if (it == l.chunks.end())
        return l.defaultTile;
    const int lx = x - floorDiv(x, chunkSize) * chunkSize;
    const int ly = y - floorDiv(y, chunkSize) * chunkSize;
    return it->second->tiles[ly * chunkSize + lx];
}

void TileMap::setTile(int layer, int x, int y, TileId id)
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
        return;

    Layer& l = layers_.at(layer);
    const Cell key{x / chunkSize, y / chunkSize};
    auto it = l.chunks.find(key);
    if (it == l.chunks.end())
    {
        if (id == l.defaultTile)
            return;
        it = l.chunks.emplace(key, std::make_unique<Chunk>()).first;
        it->second->tiles.fill(l.defaultTile);
    }

    Chunk& chunk = *it->second;
    TileId& slot = chunk.tiles[(y % chunkSize) * chunkSize + x % chunkSize];
    if (slot == id) return;
    chunk.filled += (id != l.defaultTile) - (slot != l.defaultTile);
    slot = id;
    if (trackChanges_) changes_[{layer, x, y}] = id;
    if (chunk.filled == 0)
        l.chunks.erase(it);

    if (auto floor = overview_.find(l.floor); floor != overview_.end())
    {
        if (auto page = floor->second.find({x / pageSize, y / pageSize}); page != floor->second.end())
            page->second.dirty = true;
    }
}

void TileMap::overviewDirtyAll()
{
    for (auto& [floor, pages] : overview_)
    {
        for (auto& [key, page] : pages)
            page.dirty = true;
    }
}

void TileMap::setLayerImage(int layer, std::string path, TextureId texture, Rect area)
{
    if (!std::isfinite(area.x) || !std::isfinite(area.y) || !std::isfinite(area.w) || !std::isfinite(area.h)
        || area.w <= 0 || area.h <= 0) throw std::invalid_argument("Invalid floor image area");
    Layer& l = layers_.at(layer);
    l.imagePath = std::move(path);
    l.imageTexture = texture;
    l.imageArea = area;
}

size_t TileMap::storedChunks() const
{
    size_t count = 0;
    for (const Layer& layer : layers_) count += layer.chunks.size();
    return count;
}

std::string TileMap::toJson() const
{
    using Json = nlohmann::json;
    Json j{{"width", width_}, {"height", height_}, {"tileSize", tileSize_}, {"layers", Json::array()}, {"tracking", trackChanges_}};
    if (trackChanges_) j["delta"] = Json::parse(changesJson());
    for (const Layer& l : layers_)
    {
        Json layer{{"name", l.name}, {"floor", l.floor}, {"visible", l.visible}, {"default", l.defaultTile}, {"chunks", Json::array()}};
        if (!l.imagePath.empty()) layer["image"] = {{"path", l.imagePath}, {"area", {l.imageArea.x, l.imageArea.y, l.imageArea.w, l.imageArea.h}}};
        std::map<std::pair<int, int>, const Chunk*> ordered;
        for (const auto& [cell, chunk] : l.chunks) ordered[{cell.x, cell.y}] = chunk.get();
        for (const auto& [cell, chunk] : ordered)
        {
            Json tiles = Json::array();
            for (int i = 0; i < chunkSize * chunkSize; ++i)
                if (chunk->tiles[i] != l.defaultTile) tiles.push_back({i, chunk->tiles[i]});
            layer["chunks"].push_back({{"x", cell.first}, {"y", cell.second}, {"tiles", std::move(tiles)}});
        }
        j["layers"].push_back(std::move(layer));
    }
    return j.dump();
}

std::unique_ptr<TileMap> TileMap::fromJson(std::string_view json, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = nlohmann::json::parse(json);
        auto map = std::make_unique<TileMap>(j.at("width").get<int>(), j.at("height").get<int>(), j.at("tileSize").get<float>());
        for (const auto& l : j.at("layers"))
        {
            const int base = l.value("default", 0);
            if (base < 0 || base > UINT16_MAX) throw std::invalid_argument("Invalid default tile");
            const int layer = map->addLayer(l.at("name").get<std::string>(), l.value("floor", 0), static_cast<TileId>(base));
            map->setLayerVisible(layer, l.value("visible", true));
            if (l.contains("image"))
            {
                const auto a = l.at("image").at("area").get<std::vector<float>>();
                if (a.size() != 4) throw std::invalid_argument("Image area needs four numbers");
                map->setLayerImage(layer, l.at("image").at("path").get<std::string>(), 0, {a[0], a[1], a[2], a[3]});
            }
            for (const auto& chunk : l.at("chunks"))
            {
                const int cx = chunk.at("x").get<int>(), cy = chunk.at("y").get<int>();
                if (cx < 0 || cy < 0 || cx > (map->width_ - 1) / chunkSize || cy > (map->height_ - 1) / chunkSize)
                    throw std::invalid_argument("Tile chunk outside map");
                for (const auto& tile : chunk.at("tiles"))
                {
                    const auto entry = tile.get<std::vector<int>>();
                    if (entry.size() != 2 || entry[0] < 0 || entry[0] >= chunkSize * chunkSize || entry[1] < 0 || entry[1] > UINT16_MAX)
                        throw std::invalid_argument("Invalid tile entry");
                    const int x = cx * chunkSize + entry[0] % chunkSize, y = cy * chunkSize + entry[0] / chunkSize;
                    if (x >= map->width_ || y >= map->height_) throw std::invalid_argument("Tile outside map");
                    map->setTile(layer, x, y, static_cast<TileId>(entry[1]));
                }
            }
        }
        map->trackChanges(j.value("tracking", false));
        if (j.contains("delta"))
        {
            std::string why;
            if (!map->applyChanges(j.at("delta").dump(), &why)) throw std::invalid_argument(why);
        }
        return map;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return nullptr; }
}

std::string TileMap::changesJson() const
{
    nlohmann::json j{{"width", width_}, {"height", height_}, {"tileSize", tileSize_}, {"changes", nlohmann::json::array()}};
    for (const auto& [key, id] : changes_)
        j["changes"].push_back({std::get<0>(key), std::get<1>(key), std::get<2>(key), id});
    return j.dump();
}

bool TileMap::applyChanges(std::string_view json, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = nlohmann::json::parse(json);
        if (j.at("width").get<int>() != width_ || j.at("height").get<int>() != height_ || j.at("tileSize").get<float>() != tileSize_)
            throw std::invalid_argument("Delta belongs to a different map");
        std::vector<std::array<int, 4>> changes;
        for (const auto& entry : j.at("changes"))
        {
            const auto c = entry.get<std::array<int, 4>>();
            if (c[0] < 0 || static_cast<size_t>(c[0]) >= layers_.size() || c[1] < 0 || c[2] < 0 || c[1] >= width_ || c[2] >= height_
                || c[3] < 0 || c[3] > UINT16_MAX) throw std::invalid_argument("Invalid map delta");
            changes.push_back(c);
        }
        for (const auto& c : changes)
        {
            setTile(c[0], c[1], c[2], static_cast<TileId>(c[3]));
            if (trackChanges_) changes_[{c[0], c[1], c[2]}] = static_cast<TileId>(c[3]);
        }
        return true;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return false; }
}

void TileMap::rebuildPage(Renderer& renderer, int floor, Cell page, OverviewPage& entry)
{
    pagePixels_.assign(static_cast<size_t>(pageSize) * pageSize * 4, 0);
    const int originX = page.x * pageSize;
    const int originY = page.y * pageSize;
    const int pageWidth = std::min(pageSize, width_ - originX);
    const int pageHeight = std::min(pageSize, height_ - originY);
    const int chunkColumns = (pageWidth + chunkSize - 1) / chunkSize;
    const int chunkRows = (pageHeight + chunkSize - 1) / chunkSize;

    // Walk chunks, not tiles: later layers paint over earlier ones, empty chunks are skipped entirely.
    for (const Layer& layer : layers_)
    {
        if (layer.floor != floor || !layer.visible)
            continue;
        for (int cy = 0; cy < chunkRows; cy++)
        {
            const int chunkTop = cy * chunkSize;
            const int chunkHeight = std::min(chunkSize, pageHeight - chunkTop);
            for (int cx = 0; cx < chunkColumns; cx++)
            {
                const int chunkLeft = cx * chunkSize;
                const int chunkWidth = std::min(chunkSize, pageWidth - chunkLeft);
                const auto it = layer.chunks.find({originX / chunkSize + cx, originY / chunkSize + cy});
                if (it == layer.chunks.end() && layer.defaultTile == 0)
                    continue;
                const Chunk* chunk = it == layer.chunks.end() ? nullptr : it->second.get();
                for (int ty = 0; ty < chunkHeight; ty++)
                {
                    for (int tx = 0; tx < chunkWidth; tx++)
                    {
                        const TileId id = chunk ? chunk->tiles[ty * chunkSize + tx] : layer.defaultTile;
                        if (id == 0 || id > tileset_.averageColors.size())
                            continue;
                        const size_t pixelIndex = static_cast<size_t>(chunkTop + ty) * pageSize + chunkLeft + tx;
                        unsigned char* pixel = &pagePixels_[pixelIndex * 4];
                        blendOverviewPixel(pixel, tileset_.averageColors[id - 1]);
                    }
                }
            }
        }
    }

    if (!entry.created)
    {
        entry.texture = renderer.createTexture(pageSize, pageSize, pagePixels_.data());
        entry.created = true;
        entry.release = renderer.textureRelease(entry.texture);
    }
    else
    {
        renderer.updateTexture(entry.texture, pagePixels_.data());
    }
    entry.dirty = false;
}

void TileMap::draw(Renderer& renderer, const Rect& visibleWorld, float zoom, int floor)
{
    const Rect area = visibleWorld.intersect(worldBounds());
    lastDrawnTiles_ = 0;
    lastUsedOverview_ = tileSize_ * zoom < overviewBelowPixels;
    if (area.w <= 0 || area.h <= 0)
        return;

    for (const Layer& layer : layers_)
        if (layer.floor == floor && layer.visible && !layer.imagePath.empty() && layer.imageTexture != 0)
            renderer.drawSprite(layer.imageTexture, layer.imageArea);

    if (lastUsedOverview_)
    {
        const float pageWorld = pageSize * tileSize_;
        auto& pages = overview_[floor];
        const int x0 = static_cast<int>(area.x / pageWorld);
        const int y0 = static_cast<int>(area.y / pageWorld);
        const int x1 = std::min((width_ - 1) / pageSize, static_cast<int>((area.x + area.w) / pageWorld));
        const int y1 = std::min((height_ - 1) / pageSize, static_cast<int>((area.y + area.h) / pageWorld));
        for (int py = y0; py <= y1; py++)
        {
            for (int px = x0; px <= x1; px++)
            {
                OverviewPage& page = pages[{px, py}];
                if (page.dirty)
                    rebuildPage(renderer, floor, {px, py}, page);
                renderer.drawSprite(page.texture, {px * pageWorld, py * pageWorld, pageWorld, pageWorld});
            }
        }
        return;
    }

    const int x0 = static_cast<int>(area.x / tileSize_);
    const int y0 = static_cast<int>(area.y / tileSize_);
    const int x1 = std::min(width_ - 1, static_cast<int>((area.x + area.w) / tileSize_));
    const int y1 = std::min(height_ - 1, static_cast<int>((area.y + area.h) / tileSize_));

    for (const Layer& layer : layers_)
    {
        if (layer.floor != floor || !layer.visible)
            continue;
        for (int cy = y0 / chunkSize; cy <= y1 / chunkSize; cy++)
        {
            for (int cx = x0 / chunkSize; cx <= x1 / chunkSize; cx++)
            {
                const auto it = layer.chunks.find({cx, cy});
                if (it == layer.chunks.end() && layer.defaultTile == 0)
                    continue;
                const Chunk* chunk = it == layer.chunks.end() ? nullptr : it->second.get();
                // Only the part of this chunk that's on screen.
                const int tx0 = std::max(x0, cx * chunkSize);
                const int ty0 = std::max(y0, cy * chunkSize);
                const int tx1 = std::min(x1, cx * chunkSize + chunkSize - 1);
                const int ty1 = std::min(y1, cy * chunkSize + chunkSize - 1);
                for (int ty = ty0; ty <= ty1; ty++)
                {
                    for (int tx = tx0; tx <= tx1; tx++)
                    {
                        const TileId id = chunk ? chunk->tiles[(ty - cy * chunkSize) * chunkSize + (tx - cx * chunkSize)] : layer.defaultTile;
                        if (id == 0)
                            continue;
                        renderer.drawSpriteRegion(tileset_.texture, {tx * tileSize_, ty * tileSize_, tileSize_, tileSize_}, tileset_.uv(id));
                        lastDrawnTiles_++;
                    }
                }
            }
        }
    }
}

}
