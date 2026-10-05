#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

class FileSystem;
struct Ruleset;

// A named effect area like Fire or Grease: what it does to creatures in it, how long it lasts,
// and what ends it. One JSON object each, in a ruleset's `surfaces` list or a file of its own
// (see Ruleset::loadSurfaces).
struct SurfaceDefinition
{
    std::string id;           // "fire", "grease"
    std::string name;         // "Fire"
    std::string description;
    std::vector<std::string> effects; // effect ids that apply to creatures in it
    int duration = 1;                 // rounds it lasts (0 = until end of turn it was created)
    // Events that end it: "damage", "turnEnd", "creatureDeath"...
    std::vector<std::string> ends;
    bool isSpellEffect = false; // can be placed by spells; true allows duration scaling

    bool endsOn(std::string_view event) const;
    std::string toJson() const;
    static std::optional<SurfaceDefinition> fromJson(std::string_view json, std::string* error = nullptr);
};

// The events a surface's `ends` may name.
inline constexpr const char* surfaceEvents[] = {
    "turnEnd", "damage", "creatureDeath", "rested",
};

}
