#include "yorehold/framework/rpg/Ruleset.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace yh
{

namespace
{

constexpr std::pair<Recovery::Kind, const char*> recoveryKinds[] = {
    {Recovery::Kind::None, "none"}, {Recovery::Kind::Full, "full"}, {Recovery::Kind::Fraction, "fraction"},
    {Recovery::Kind::Flat, "flat"}, {Recovery::Kind::HitDice, "hitDice"},
};

nlohmann::json recoveryToJson(const Recovery& r)
{
    const char* kind = "none";
    for (const auto& [k, name] : recoveryKinds)
        if (k == r.kind) kind = name;
    return {{"kind", kind}, {"fraction", r.fraction}, {"amount", r.amount}, {"reviveDowned", r.reviveDowned}};
}

Recovery recoveryFromJson(const nlohmann::json& j)
{
    Recovery r;
    const auto kind = j.value("kind", std::string("none"));
    const auto found = std::find_if(std::begin(recoveryKinds), std::end(recoveryKinds), [&](const auto& k) { return kind == k.second; });
    if (found == std::end(recoveryKinds)) throw std::invalid_argument("Unknown recovery kind");
    r.kind = found->first;
    r.fraction = j.value("fraction", r.fraction);
    r.amount = j.value("amount", r.amount);
    r.reviveDowned = j.value("reviveDowned", r.reviveDowned);
    if (!std::isfinite(r.fraction) || r.fraction < 0 || r.fraction > 1 || r.amount < 0 || r.amount > 100000)
        throw std::invalid_argument("Invalid recovery amount");
    return r;
}

}

std::string Ruleset::toJson() const
{
    using J = nlohmann::json;
    J j{{"version", 1}, {"id", id}, {"name", name},
        {"modifierTable", modifierTable == ModifierTable::Classic ? "classic" : "d20"},
        {"scoreMin", scoreMin}, {"scoreMax", scoreMax}, {"baseArmorClass", baseArmorClass},
        {"armorClassAbility", armorClassAbility}, {"initiativeAbility", initiativeAbility},
        {"proficiencyByLevel", proficiencyByLevel}, {"xpForLevel", xpForLevel},
        {"actionsPerTurn", actionsPerTurn}, {"bonusActions", bonusActions}, {"strikeCostsHands", strikeCostsHands}, {"sharedTurns", sharedTurns},
        {"feetPerSquare", feetPerSquare}, {"carryPerStrength", carryPerStrength},
        {"magicItemLimit", magicItemLimit}, {"passiveBase", passiveBase}};
    j["proficiencyRanks"] = J::array();
    for (const auto& rank : proficiencyRanks)
        j["proficiencyRanks"].push_back({{"id", rank.id}, {"name", rank.name}, {"bonus", rank.bonus}, {"addsLevel", rank.addsLevel}});
    j["proficientRank"] = proficientRank; j["untrainedRank"] = untrainedRank; j["baseDc"] = baseDc;
    j["abilities"] = J::array(); j["skills"] = J::array(); j["conditions"] = J::array(); j["rests"] = J::array();
    for (const auto& r : rests)
        j["rests"].push_back({{"id", r.id}, {"name", r.name}, {"perAdventure", r.perAdventure}, {"recovery", recoveryToJson(r.recovery)}});
    j["afterVictory"] = recoveryToJson(afterVictory);
    j["reviveAfterVictory"] = reviveAfterVictory;
    j["defaultHitDie"] = defaultHitDie;
    j["hitDieAbility"] = hitDieAbility;
    j["hitDieByClass"] = J::object();
    for (const auto& [className, sides] : hitDieByClass) j["hitDieByClass"][className] = sides;
    for (const auto& a : abilities) j["abilities"].push_back({{"id", a.id}, {"name", a.name}});
    for (const auto& s : skills) j["skills"].push_back({{"id", s.id}, {"name", s.name}, {"ability", s.ability}});
    for (const auto& c : conditions)
        j["conditions"].push_back(J::parse(c.toJson()));
    return j.dump();
}

std::optional<Ruleset> Ruleset::fromJson(std::string_view json, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = nlohmann::json::parse(json);
        if (j.value("version", 1) != 1) throw std::invalid_argument("Unsupported ruleset version");
        Ruleset r;
        r.id = j.at("id").get<std::string>(); r.name = j.at("name").get<std::string>();
        if (r.id.empty()) throw std::invalid_argument("Ruleset id is empty");
        const auto table = j.value("modifierTable", std::string("d20"));
        if (table != "d20" && table != "classic") throw std::invalid_argument("Unknown modifier table");
        r.modifierTable = table == "classic" ? ModifierTable::Classic : ModifierTable::D20;
        r.scoreMin = j.value("scoreMin", r.scoreMin); r.scoreMax = j.value("scoreMax", r.scoreMax);
        r.baseArmorClass = j.value("baseArmorClass", r.baseArmorClass);
        r.armorClassAbility = j.value("armorClassAbility", r.armorClassAbility);
        r.initiativeAbility = j.value("initiativeAbility", r.initiativeAbility);
        r.actionsPerTurn = j.value("actionsPerTurn", r.actionsPerTurn);
        r.bonusActions = j.value("bonusActions", r.bonusActions);
        r.strikeCostsHands = j.value("strikeCostsHands", r.strikeCostsHands);
        r.sharedTurns = j.value("sharedTurns", r.sharedTurns);
        if (r.actionsPerTurn < 1 || r.actionsPerTurn > 10) throw std::invalid_argument("actionsPerTurn is 1 to 10");
        r.feetPerSquare = j.value("feetPerSquare", r.feetPerSquare);
        r.carryPerStrength = j.value("carryPerStrength", r.carryPerStrength);
        r.magicItemLimit = j.value("magicItemLimit", r.magicItemLimit);
        r.passiveBase = j.value("passiveBase", r.passiveBase);
        if (r.magicItemLimit < 0 || r.magicItemLimit > 1000 || r.passiveBase < -1000 || r.passiveBase > 1000)
            throw std::invalid_argument("Invalid magicItemLimit or passiveBase");
        r.proficiencyByLevel = j.value("proficiencyByLevel", std::vector<int>{});
        r.proficientRank = j.value("proficientRank", r.proficientRank);
        r.untrainedRank = j.value("untrainedRank", r.untrainedRank);
        r.baseDc = j.value("baseDc", r.baseDc);
        std::set<std::string> rankIds;
        const auto rankList = j.value("proficiencyRanks", nlohmann::json::array());
        if (!rankList.is_array() || rankList.size() > 100) throw std::invalid_argument("proficiencyRanks is an array of at most 100 ranks");
        for (const auto& entry : rankList)
        {
            ProficiencyRankDefinition rank;
            rank.id = entry.at("id").get<std::string>(); rank.name = entry.value("name", rank.id);
            rank.bonus = entry.value("bonus", 0); rank.addsLevel = entry.value("addsLevel", false);
            if (rank.id.empty() || rank.id.size() > 64 || rank.name.size() > 64 || !rankIds.insert(rank.id).second
                || rank.bonus < 0 || rank.bonus > 100) throw std::invalid_argument("Invalid proficiency rank");
            r.proficiencyRanks.push_back(std::move(rank));
        }
        if ((!r.proficiencyRanks.empty() && (!r.proficiencyRank(r.proficientRank) || !r.proficiencyRank(r.untrainedRank)))
            || r.baseDc < 0 || r.baseDc > 1000) throw std::invalid_argument("Invalid proficiency defaults or baseDc");
        r.xpForLevel = j.value("xpForLevel", std::vector<int>{});
        if (r.scoreMin > r.scoreMax || r.scoreMin < -100000 || r.scoreMax > 100000 || r.feetPerSquare <= 0 || r.carryPerStrength < 0
            || !std::is_sorted(r.xpForLevel.begin(), r.xpForLevel.end())
            || std::any_of(r.xpForLevel.begin(), r.xpForLevel.end(), [](int n) { return n < 0; }))
            throw std::invalid_argument("Invalid ruleset bounds or progression");
        std::set<std::string> ids;
        for (const auto& a : j.at("abilities"))
        {
            AbilityDefinition def{a.at("id").get<std::string>(), a.at("name").get<std::string>()};
            if (def.id.empty() || !ids.insert(def.id).second) throw std::invalid_argument("Duplicate/empty ability id");
            r.abilities.push_back(std::move(def));
        }
        if (r.abilities.empty() || (!r.armorClassAbility.empty() && !r.ability(r.armorClassAbility))
            || (!r.initiativeAbility.empty() && !r.ability(r.initiativeAbility))) throw std::invalid_argument("Unknown derived-stat ability");
        ids.clear();
        for (const auto& s : j.value("skills", nlohmann::json::array()))
        {
            SkillDefinition def{s.at("id").get<std::string>(), s.at("name").get<std::string>(), s.at("ability").get<std::string>()};
            if (def.id.empty() || !ids.insert(def.id).second || !r.ability(def.ability)) throw std::invalid_argument("Invalid skill definition");
            r.skills.push_back(std::move(def));
        }
        ids.clear();
        for (const auto& c : j.value("conditions", nlohmann::json::array()))
        {
            std::string problem;
            std::optional<ConditionDefinition> def = ConditionDefinition::fromJson(c.dump(), &problem);
            if (!def) throw std::invalid_argument(problem);
            if (!ids.insert(def->id).second) throw std::invalid_argument("Duplicate condition id");
            r.conditions.push_back(std::move(*def));
        }
        for (const ConditionDefinition& c : r.conditions)
        {
            if (!c.saveAbility.empty() && !r.ability(c.saveAbility)) throw std::invalid_argument("Condition " + c.id + " saves with an unknown ability");
            for (const std::string& other : c.removes)
                if (!r.condition(other)) throw std::invalid_argument("Condition " + c.id + " removes an unknown condition");
        }
        ids.clear();
        for (const auto& rest : j.value("rests", nlohmann::json::array()))
        {
            RestDefinition def{rest.at("id").get<std::string>(), rest.value("name", std::string{}),
                recoveryFromJson(rest.value("recovery", nlohmann::json::object())), rest.value("perAdventure", 0)};
            if (def.id.empty() || !ids.insert(def.id).second || def.perAdventure < 0) throw std::invalid_argument("Invalid rest definition");
            r.rests.push_back(std::move(def));
        }
        r.afterVictory = recoveryFromJson(j.value("afterVictory", nlohmann::json::object()));
        r.reviveAfterVictory = j.value("reviveAfterVictory", 0);
        r.defaultHitDie = j.value("defaultHitDie", r.defaultHitDie);
        r.hitDieAbility = j.value("hitDieAbility", r.hitDieAbility);
        const nlohmann::json dice = j.value("hitDieByClass", nlohmann::json::object());
        if (!dice.is_object()) throw std::invalid_argument("hitDieByClass must be an object");
        for (const auto& [name, sides] : dice.items())
            r.hitDieByClass.emplace_back(name, sides.get<int>());
        auto badDie = [](int sides) { return sides < 1 || sides > 1000; };
        if (r.reviveAfterVictory < 0 || badDie(r.defaultHitDie) || (!r.hitDieAbility.empty() && !r.ability(r.hitDieAbility))
            || std::any_of(r.hitDieByClass.begin(), r.hitDieByClass.end(), [&](const auto& c) { return badDie(c.second); }))
            throw std::invalid_argument("Invalid healing rules");
        return r;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return std::nullopt; }
}

}
