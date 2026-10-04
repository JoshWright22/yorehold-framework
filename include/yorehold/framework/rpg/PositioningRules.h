#pragma once

#include "yorehold/framework/rpg/Ruleset.h"

namespace yh
{

// Optional positioning.json. A missing file leaves positional rules disabled.
struct PositioningRules
{
    bool enabled = false;
    std::string flankingCondition;
    float flankingReach = 1;
    int halfCoverArmorClass = 0;
    int threeQuartersCoverArmorClass = 0;
    bool creaturesProvideCover = false;
    bool coverAgainstMelee = false;

    std::string toJson() const;
    static std::optional<PositioningRules> fromJson(std::string_view text, std::string* error = nullptr);
    bool check(const Ruleset& rules, std::string* error = nullptr) const;
};

}
