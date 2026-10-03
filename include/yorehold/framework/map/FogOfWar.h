#pragma once

#include "yorehold/framework/graphics/Lighting.h"
#include "yorehold/framework/map/Grid.h"

#include <bitset>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace yh
{

struct Vision
{
    Vec2 position;
    float radius = 256;
};

enum class FogState { Unexplored, Explored, Visible };

// Sparse square fog cells, independent of the movement grid. Teams can share a view or each
// player can use their own team id. Floors and exploration are stored separately.
class FogOfWar
{
public:
    FogOfWar(int width, int height, float cellSize);
    void update(int team, int floor, std::span<const Vision> observers, std::span<const Wall> walls);
    FogState state(int team, int floor, Cell cell) const;
    void reveal(int team, int floor, Cell cell);
    void reset(int team);
    void draw(Renderer& renderer, const Rect& visibleWorld, int team, int floor,
        Color unexplored = {0, 0, 0, 255}, Color explored = {0, 0, 0, 170});
    std::string toJson() const;
    static std::optional<FogOfWar> fromJson(std::string_view json, std::string* error = nullptr);

private:
    struct Chunk
    {
        std::bitset<1024> explored;
        std::bitset<1024> visible;
    };
    using Chunks = std::unordered_map<Cell, Chunk, CellHash>;
    int width_, height_;
    float cellSize_;
    std::map<std::pair<int, int>, Chunks> views_;
    std::vector<Rect> unexploredRects_, exploredRects_;
};

}
