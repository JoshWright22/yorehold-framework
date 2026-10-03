#pragma once

#include "yorehold/framework/graphics/Image.h"
#include "yorehold/framework/graphics/Renderer.h"
#include "yorehold/framework/map/Grid.h"

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace yh
{

// 0 is "no tile". Other ids index into the map's Tileset (id 1 = first tile in the atlas).
using TileId = uint16_t;

// An atlas texture of equal-size tiles, laid out left to right, top to bottom.
struct Tileset
{
    TextureId texture = 0;
    int columns = 1;
    int rows = 1;
    int tilePixels = 32; // size of one tile in the texture, used to avoid sampling neighbouring tiles at the edges
    int padding = 0;     // repeated edge pixels around each tile in the texture (buildTileset adds them)
    // One colour per tile, including average alpha, used when tiles are smaller than a few pixels.
    std::vector<Color> averageColors;
    // Set by buildTileset: releases the texture when the last copy of the tileset goes.
    std::shared_ptr<void> owner;

    Rect uv(TileId id) const;
};

// A tileset from separate pictures of any size and format (photos, painted tiles, cells cut
// from a sheet with sliceImage). Each is cut to its centred square and scaled to tilePixels;
// tiles[0] becomes id 1. Edges are padded so smooth filtering never shows seams. Fails when
// there are no tiles, invalid pixels/sizes, or more than one texture can hold.
std::optional<Tileset> buildTileset(Renderer& renderer, std::span<const Image> tiles, int tilePixels = 64, bool smooth = true, std::string* error = nullptr);

// Square tiles stored sparsely in 32x32 chunks: empty areas cost nothing, and drawing only
// touches the chunks on screen. Each layer belongs to a floor; only the viewer's floor is drawn.
// When tiles get too small to see (far zoom), a 1-pixel-per-tile overview texture is drawn instead,
// so even 7000 x 7000 maps draw in a handful of quads.
class TileMap
{
public:
    static constexpr int chunkSize = 32;
    static constexpr int pageSize = 1024; // overview texture page, in tiles

    TileMap(int width, int height, float tileSize);
    ~TileMap();

    int width() const { return width_; }
    int height() const { return height_; }
    float tileSize() const { return tileSize_; }
    Rect worldBounds() const { return {0, 0, width_ * tileSize_, height_ * tileSize_}; }

    void setTileset(Tileset tileset) { tileset_ = std::move(tileset); overviewDirtyAll(); }
    const Tileset& tileset() const { return tileset_; }

    // Returns the new layer's index. Layers draw in the order they were added.
    int addLayer(std::string name, int floor = 0, TileId defaultTile = 0);
    size_t layerCount() const { return layers_.size(); }
    const std::string& layerName(int layer) const { return layers_.at(layer).name; }
    int layerFloor(int layer) const { return layers_.at(layer).floor; }
    bool layerVisible(int layer) const { return layers_.at(layer).visible; }
    void setLayerVisible(int layer, bool visible);
    // Painted floor images are drawn behind tile layers, at every zoom. The path is saved;
    // rebind the TextureId after loading using setLayerImage().
    void setLayerImage(int layer, std::string path, TextureId texture, Rect area);
    const std::string& layerImagePath(int layer) const { return layers_.at(layer).imagePath; }
    Rect layerImageArea(int layer) const { return layers_.at(layer).imageArea; }

    TileId tile(int layer, int x, int y) const;
    void setTile(int layer, int x, int y, TileId id);

    // Draws the floor `floor` through the current renderer transform. `zoom` picks tiles vs overview.
    void draw(Renderer& renderer, const Rect& visibleWorld, float zoom, int floor);

    // Stats from the last draw, for the test scenes.
    size_t lastDrawnTiles() const { return lastDrawnTiles_; }
    bool lastUsedOverview() const { return lastUsedOverview_; }
    size_t storedChunks() const;
    std::string toJson() const;
    static std::unique_ptr<TileMap> fromJson(std::string_view json, std::string* error = nullptr);
    // Enable after loading authored terrain. Saves then contain only changed cells, including
    // removals. applyChanges validates the whole delta before applying it.
    void trackChanges(bool enabled = true) { trackChanges_ = enabled; }
    void clearChanges() { changes_.clear(); }
    std::string changesJson() const;
    bool applyChanges(std::string_view json, std::string* error = nullptr);

private:
    struct Chunk
    {
        std::array<TileId, chunkSize * chunkSize> tiles{};
        int filled = 0;
    };

    struct Layer
    {
        std::string name;
        int floor = 0;
        bool visible = true;
        std::unordered_map<Cell, std::unique_ptr<Chunk>, CellHash> chunks;
        TileId defaultTile = 0;
        std::string imagePath;
        TextureId imageTexture = 0;
        Rect imageArea;
    };

    struct OverviewPage
    {
        TextureId texture = 0;
        bool created = false;
        bool dirty = true;
        std::function<void()> release;
    };

    void overviewDirtyAll();
    void rebuildPage(Renderer& renderer, int floor, Cell page, OverviewPage& entry);

    int width_;
    int height_;
    float tileSize_;
    Tileset tileset_;
    std::deque<Layer> layers_; // deque: growing it never copies layers (their chunk maps hold unique_ptrs)
    // Overview pages per floor, created on first far zoom and rebuilt only when tiles change.
    std::unordered_map<int, std::unordered_map<Cell, OverviewPage, CellHash>> overview_;
    std::vector<unsigned char> pagePixels_;
    size_t lastDrawnTiles_ = 0;
    bool lastUsedOverview_ = false;
    bool trackChanges_ = false;
    std::map<std::tuple<int, int, int>, TileId> changes_;
};

}
