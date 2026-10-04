// Surface definitions <-> JSON, and loading a folder of them into a ruleset.

#include "yorehold/framework/rpg/Ruleset.h"

#include "yorehold/framework/assets/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

bool validName(std::string_view text)
{
    return !text.empty() && text.size() <= 64;
}

std::vector<std::string> names(const json& j, const char* key)
{
    const std::vector<std::string> list = j.value(key, std::vector<std::string>{});
    if (!std::all_of(list.begin(), list.end(), validName) || std::set<std::string>(list.begin(), list.end()).size() != list.size())
        throw std::invalid_argument(std::string(key) + " is a list of different names, 1 to 64 characters each");
    return list;
}

SurfaceDefinition parse(const json& s)
{
    if (!s.is_object()) throw std::invalid_argument("A surface is a JSON object");
    SurfaceDefinition def;
    def.id = s.at("id").get<std::string>();
    def.name = s.value("name", def.id);
    def.description = s.value("description", std::string{});
    def.isSpellEffect = s.value("isSpellEffect", false);
    if (!validName(def.id) || !validName(def.name)) throw std::invalid_argument("Surface ids and names are 1 to 64 characters");
    def.effects = s.value("effects", std::vector<std::string>{});
    def.duration = s.value("duration", def.duration);
    if (def.duration < 0 || def.duration > 100000)
        throw std::invalid_argument("duration is a non-negative number of rounds");
    def.ends = names(s, "ends");
    for (const std::string& event : def.ends)
        if (std::none_of(std::begin(surfaceEvents), std::end(surfaceEvents), [&](const char* known) { return event == known; }))
            throw std::invalid_argument("Unknown event \"" + event + "\" in ends");
    return def;
}

json toObject(const SurfaceDefinition& s)
{
    json j{{"id", s.id}, {"name", s.name}, {"description", s.description}, {"effects", s.effects},
        {"duration", s.duration}, {"ends", s.ends}, {"isSpellEffect", s.isSpellEffect}};
    return j;
}

// What only the whole set can tell: effects are validated by the game when they are used.
void checkTogether(const Ruleset& rules)
{
    // Surfaces themselves don't reference other surfaces, so not much to check here.
    // The game will validate effect ids when surfaces are applied.
    (void)rules;
}

}

bool SurfaceDefinition::endsOn(std::string_view event) const
{
    return std::find(ends.begin(), ends.end(), event) != ends.end();
}

std::string SurfaceDefinition::toJson() const
{
    return toObject(*this).dump();
}

std::optional<SurfaceDefinition> SurfaceDefinition::fromJson(std::string_view text, std::string* error)
{
    if (error) error->clear();
    try
    {
        return parse(json::parse(text));
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

bool Ruleset::loadSurfaces(const FileSystem& files, std::string_view folder, std::string* error)
{
    if (error) error->clear();
    Ruleset next = *this; // all-or-nothing: work on a copy
    std::string where(folder);
    try
    {
        std::vector<std::string> paths = files.list(folder);
        std::sort(paths.begin(), paths.end()); // the same order on every machine
        for (const std::string& path : paths)
        {
            if (!path.ends_with(".json"))
                continue;
            where = path;
            const size_t slash = path.rfind('/');
            const std::string stem = path.substr(slash == std::string::npos ? 0 : slash + 1, path.size() - (slash == std::string::npos ? 0 : slash + 1) - 5);
            const std::optional<std::string> text = files.readText(path);
            if (!text) throw std::invalid_argument("can't read");
            SurfaceDefinition def = parse(json::parse(*text));
            if (def.id != stem) throw std::invalid_argument("id \"" + def.id + "\" doesn't match the file name");
            const auto old = std::find_if(next.surfaces.begin(), next.surfaces.end(), [&](const SurfaceDefinition& s) { return s.id == def.id; });
            if (old != next.surfaces.end())
                *old = std::move(def);
            else
                next.surfaces.push_back(std::move(def));
        }
        where = std::string(folder);
        checkTogether(next);
    }
    catch (const std::exception& e)
    {
        if (error) *error = where + ": " + e.what();
        return false;
    }
    *this = std::move(next);
    return true;
}

}
