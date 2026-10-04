#include "yorehold/framework/rpg/PositioningRules.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <set>
#include <stdexcept>

namespace yh
{

std::string PositioningRules::toJson() const
{
    return nlohmann::json{{"enabled", enabled}, {"flankingCondition", flankingCondition}, {"flankingReach", flankingReach},
        {"halfCoverArmorClass", halfCoverArmorClass}, {"threeQuartersCoverArmorClass", threeQuartersCoverArmorClass},
        {"creaturesProvideCover", creaturesProvideCover}, {"coverAgainstMelee", coverAgainstMelee}}.dump();
}

std::optional<PositioningRules> PositioningRules::fromJson(std::string_view text, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = nlohmann::json::parse(text);
        const std::set<std::string> fields{"enabled", "flankingCondition", "flankingReach", "halfCoverArmorClass",
            "threeQuartersCoverArmorClass", "creaturesProvideCover", "coverAgainstMelee"};
        if (!j.is_object()) throw std::invalid_argument("Positioning rules must be an object");
        for (const auto& [field, value] : j.items())
            if (!fields.contains(field)) throw std::invalid_argument("Unknown positioning field: " + field);
        PositioningRules rules;
        rules.enabled = j.value("enabled", true);
        rules.flankingCondition = j.value("flankingCondition", rules.flankingCondition);
        rules.flankingReach = j.value("flankingReach", rules.flankingReach);
        rules.halfCoverArmorClass = j.value("halfCoverArmorClass", rules.halfCoverArmorClass);
        rules.threeQuartersCoverArmorClass = j.value("threeQuartersCoverArmorClass", rules.threeQuartersCoverArmorClass);
        rules.creaturesProvideCover = j.value("creaturesProvideCover", rules.creaturesProvideCover);
        rules.coverAgainstMelee = j.value("coverAgainstMelee", rules.coverAgainstMelee);
        if (!std::isfinite(rules.flankingReach) || rules.flankingReach <= 0 || rules.flankingReach > 100
            || rules.halfCoverArmorClass < 0 || rules.halfCoverArmorClass > 100
            || rules.threeQuartersCoverArmorClass < rules.halfCoverArmorClass || rules.threeQuartersCoverArmorClass > 100
            || rules.flankingCondition.size() > 64)
            throw std::invalid_argument("Invalid positioning bounds");
        return rules;
    }
    catch (const std::exception& exception)
    {
        if (error) *error = exception.what();
        return std::nullopt;
    }
}

bool PositioningRules::check(const Ruleset& rules, std::string* error) const
{
    if (error) error->clear();
    if (!flankingCondition.empty() && !rules.condition(flankingCondition))
    {
        if (error) *error = "Unknown flanking condition: " + flankingCondition;
        return false;
    }
    return true;
}

}
