#include "yorehold/framework/map/Surfaces.h"

#include <nlohmann/json.hpp>
#include <cmath>

namespace yh
{

const std::vector<Surface> Surfaces::empty_ = {};

Surfaces::Surfaces(int width, int height, float cellSize)
    : width_(width), height_(height), cellSize_(cellSize)
{
}

void Surfaces::create(Cell at, std::string_view id, float radius, int rounds)
{
    if (rounds <= 0 || radius <= 0)
        return;

    Surface surface;
    surface.id = std::string(id);
    surface.roundsLeft = rounds;
    surface.radius = radius;

    addToAllCells(at, radius, surface);
}

const std::vector<Surface>& Surfaces::at(Cell cell) const
{
    auto it = surfaces_.find(cell);
    return it != surfaces_.end() ? it->second : empty_;
}

bool Surfaces::has(Cell cell, std::string_view id) const
{
    const auto& surfs = at(cell);
    for (const auto& surf : surfs)
    {
        if (surf.id == id)
            return true;
    }
    return false;
}

bool Surfaces::remove(Cell cell, std::string_view id)
{
    auto it = surfaces_.find(cell);
    if (it == surfaces_.end())
        return false;

    auto& surfs = it->second;
    auto iter = surfs.begin();
    while (iter != surfs.end())
    {
        if (iter->id == id)
        {
            iter = surfs.erase(iter);
            return true;
        }
        else
        {
            ++iter;
        }
    }

    if (surfs.empty())
        surfaces_.erase(it);

    return false;
}

std::unordered_map<Cell, std::vector<std::string>, CellHash> Surfaces::endRound()
{
    std::unordered_map<Cell, std::vector<std::string>, CellHash> expired;

    auto it = surfaces_.begin();
    while (it != surfaces_.end())
    {
        auto& surfs = it->second;
        auto siter = surfs.begin();

        while (siter != surfs.end())
        {
            siter->endRound();
            if (siter->expired())
            {
                expired[it->first].push_back(siter->id);
                siter = surfs.erase(siter);
            }
            else
            {
                ++siter;
            }
        }

        if (surfs.empty())
            it = surfaces_.erase(it);
        else
            ++it;
    }

    return expired;
}

void Surfaces::addToAllCells(Cell center, float radius, const Surface& surface)
{
    int iradius = static_cast<int>(std::ceil(radius));
    for (int dy = -iradius; dy <= iradius; ++dy)
    {
        for (int dx = -iradius; dx <= iradius; ++dx)
        {
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= radius)
            {
                Cell cell = center.offset(dx, dy);
                if (cell.x >= 0 && cell.x < width_ && cell.y >= 0 && cell.y < height_)
                {
                    surfaces_[cell].push_back(surface);
                }
            }
        }
    }
}

std::string Surfaces::toJson() const
{
    using json = nlohmann::json;
    json root = json::object();

    for (const auto& [cell, surfs] : surfaces_)
    {
        json surfaceArray = json::array();
        for (const auto& surf : surfs)
        {
            surfaceArray.push_back(json::object({
                {"id", surf.id},
                {"rounds", surf.roundsLeft},
                {"radius", surf.radius}
            }));
        }
        root[std::to_string(cell.x) + "," + std::to_string(cell.y)] = surfaceArray;
    }

    return root.dump();
}

bool Surfaces::fromJson(std::string_view text, std::string* error)
{
    try
    {
        using json = nlohmann::json;
        surfaces_.clear();

        auto root = json::parse(text);

        for (auto& [key, surfaceArray] : root.items())
        {
            // Parse "x,y" key
            size_t comma = key.find(',');
            if (comma == std::string::npos)
            {
                if (error) *error = "Invalid cell key: " + key;
                return false;
            }

            int x = std::stoi(key.substr(0, comma));
            int y = std::stoi(key.substr(comma + 1));
            Cell cell{x, y};

            if (!surfaceArray.is_array())
            {
                if (error) *error = "Expected array of surfaces";
                return false;
            }

            for (const auto& surfJson : surfaceArray)
            {
                Surface surf;
                surf.id = surfJson.at("id");
                surf.roundsLeft = surfJson.at("rounds");
                surf.radius = surfJson.at("radius");
                surfaces_[cell].push_back(surf);
            }
        }

        return true;
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return false;
    }
}

}
