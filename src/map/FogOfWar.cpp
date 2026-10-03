#include "yorehold/framework/map/FogOfWar.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace yh
{

FogOfWar::FogOfWar(int width, int height, float cellSize) : width_(width), height_(height), cellSize_(cellSize)
{
    if (width <= 0 || height <= 0 || cellSize <= 0 || !std::isfinite(cellSize))
        throw std::invalid_argument("Fog dimensions and cell size must be positive");
}

FogState FogOfWar::state(int team, int floor, Cell cell) const
{
    if (cell.x < 0 || cell.y < 0 || cell.x >= width_ || cell.y >= height_) return FogState::Unexplored;
    const auto view = views_.find({team, floor});
    if (view == views_.end()) return FogState::Unexplored;
    const auto chunk = view->second.find({cell.x / 32, cell.y / 32});
    if (chunk == view->second.end()) return FogState::Unexplored;
    const size_t bit = (cell.y % 32) * 32 + cell.x % 32;
    return chunk->second.visible[bit] ? FogState::Visible : chunk->second.explored[bit] ? FogState::Explored : FogState::Unexplored;
}

void FogOfWar::reveal(int team, int floor, Cell cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.x >= width_ || cell.y >= height_) return;
    auto& chunk = views_[{team, floor}][{cell.x / 32, cell.y / 32}];
    const size_t bit = (cell.y % 32) * 32 + cell.x % 32;
    chunk.visible.set(bit);
    chunk.explored.set(bit);
}

void FogOfWar::update(int team, int floor, std::span<const Vision> observers, std::span<const Wall> walls)
{
    for (auto& [key, chunk] : views_[{team, floor}]) chunk.visible.reset();
    for (const Vision& v : observers)
    {
        if (v.radius <= 0 || !std::isfinite(v.radius) || !std::isfinite(v.position.x) || !std::isfinite(v.position.y)) continue;
        auto cellX = [this](float x) { return static_cast<int>(std::clamp(std::floor(x / cellSize_), 0.0f, static_cast<float>(width_ - 1))); };
        auto cellY = [this](float y) { return static_cast<int>(std::clamp(std::floor(y / cellSize_), 0.0f, static_cast<float>(height_ - 1))); };
        for (int y = cellY(v.position.y - v.radius); y <= cellY(v.position.y + v.radius); ++y)
            for (int x = cellX(v.position.x - v.radius); x <= cellX(v.position.x + v.radius); ++x)
            {
                const Vec2 center{(x + 0.5f) * cellSize_, (y + 0.5f) * cellSize_};
                const Vec2 delta = center - v.position;
                if (delta.x * delta.x + delta.y * delta.y <= v.radius * v.radius && lineOfSight(v.position, center, walls))
                    reveal(team, floor, {x, y});
            }
    }
}

void FogOfWar::reset(int team)
{
    std::erase_if(views_, [team](const auto& entry) { return entry.first.first == team; });
}

void FogOfWar::draw(Renderer& renderer, const Rect& visibleWorld, int team, int floor, Color unexplored, Color explored)
{
    unexploredRects_.clear();
    exploredRects_.clear();
    const int x0 = static_cast<int>(std::clamp(std::floor(visibleWorld.x / cellSize_), 0.0f, static_cast<float>(width_)));
    const int y0 = static_cast<int>(std::clamp(std::floor(visibleWorld.y / cellSize_), 0.0f, static_cast<float>(height_)));
    const int x1 = static_cast<int>(std::clamp(std::ceil((visibleWorld.x + visibleWorld.w) / cellSize_), 0.0f, static_cast<float>(width_)));
    const int y1 = static_cast<int>(std::clamp(std::ceil((visibleWorld.y + visibleWorld.h) / cellSize_), 0.0f, static_cast<float>(height_)));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const FogState s = state(team, floor, {x, y});
            if (s == FogState::Visible) continue;
            auto& rects = s == FogState::Unexplored ? unexploredRects_ : exploredRects_;
            rects.push_back({x * cellSize_, y * cellSize_, cellSize_, cellSize_});
        }
    renderer.fillRects(unexploredRects_, unexplored);
    renderer.fillRects(exploredRects_, explored);
}

std::string FogOfWar::toJson() const
{
    nlohmann::json j{{"width", width_}, {"height", height_}, {"cellSize", cellSize_}, {"views", nlohmann::json::array()}};
    for (const auto& [key, chunks] : views_)
    {
        nlohmann::json view{{"team", key.first}, {"floor", key.second}, {"chunks", nlohmann::json::array()}};
        // Sorting sparse keys makes saves deterministic across hash-table layouts.
        std::map<std::pair<int, int>, const Chunk*> ordered;
        for (const auto& [cell, chunk] : chunks) ordered[{cell.x, cell.y}] = &chunk;
        for (const auto& [cell, chunk] : ordered)
            view["chunks"].push_back({{"x", cell.first}, {"y", cell.second}, {"explored", chunk->explored.to_string()}});
        j["views"].push_back(std::move(view));
    }
    return j.dump();
}

std::optional<FogOfWar> FogOfWar::fromJson(std::string_view json, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = nlohmann::json::parse(json);
        FogOfWar fog(j.at("width").get<int>(), j.at("height").get<int>(), j.at("cellSize").get<float>());
        for (const auto& view : j.at("views"))
        {
            const auto key = std::pair{view.at("team").get<int>(), view.at("floor").get<int>()};
            for (const auto& chunk : view.at("chunks"))
            {
                const Cell cell{chunk.at("x").get<int>(), chunk.at("y").get<int>()};
                if (cell.x < 0 || cell.y < 0 || cell.x > (fog.width_ - 1) / 32 || cell.y > (fog.height_ - 1) / 32)
                    throw std::invalid_argument("Fog chunk outside map");
                const auto bits = chunk.at("explored").get<std::string>();
                if (bits.size() != 1024 || bits.find_first_not_of("01") != std::string::npos)
                    throw std::invalid_argument("Invalid fog exploration bits");
                fog.views_[key][cell].explored = std::bitset<1024>(bits);
            }
        }
        return fog;
    }
    catch (const std::exception& ex)
    {
        if (error) *error = ex.what();
        return std::nullopt;
    }
}

}
