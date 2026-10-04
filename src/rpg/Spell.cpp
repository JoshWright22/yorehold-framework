// Spells from JSON, slots, and concentration.

#include "yorehold/framework/rpg/Spell.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

int whole(const json& j, const char* key, int fallback, int low, int high)
{
    if (!j.contains(key))
        return fallback;
    if (!j.at(key).is_number_integer() || j.at(key).get<long long>() < low || j.at(key).get<long long>() > high)
        throw std::invalid_argument(std::string(key) + ": is a whole number from " + std::to_string(low) + " to " + std::to_string(high));
    return j.at(key).get<int>();
}

bool truth(const json& j, const char* key, bool fallback)
{
    if (!j.contains(key))
        return fallback;
    if (!j.at(key).is_boolean()) throw std::invalid_argument(std::string(key) + ": is true or false");
    return j.at(key).get<bool>();
}

}

std::optional<SpellDefinition> SpellDefinition::fromJson(std::string_view source, std::string* error)
{
    if (error) error->clear();
    try
    {
        // The member `json` hides the type's short name here.
        nlohmann::json j = nlohmann::json::parse(source);
        if (!j.is_object()) throw std::invalid_argument("spell: is a JSON object");
        SpellDefinition spell;
        spell.json = j.dump();
        spell.level = whole(j, "level", 0, 0, 20);
        spell.hands = whole(j, "hands", 1, 0, 4);
        spell.concentration = truth(j, "concentration", false);
        for (const char* own : {"level", "hands", "concentration"})
            j.erase(own);
        for (const char* refused : {"general", "endsTurn", "readies", "requires"})
            if (j.contains(refused)) throw std::invalid_argument(std::string(refused) + ": is not for a spell");
        if (j.contains("cost") && !j.at("cost").is_number_integer())
            throw std::invalid_argument("cost: is a number of actions; left out, a spell costs its hands");
        // The rest is an action: one action per hand unless it says otherwise, listed after the
        // general actions in order of level, and only for those who know it.
        if (!j.contains("cost")) j["cost"] = spell.hands;
        if (!j.contains("order")) j["order"] = 500 + spell.level;
        j["general"] = false;
        std::string problem;
        std::optional<ActionDefinition> action = ActionDefinition::fromJson(j.dump(), &problem);
        if (!action) throw std::invalid_argument(problem);
        if (action->effect.empty()) throw std::invalid_argument("effects: a spell needs at least one step");
        spell.action = std::move(*action);
        return spell;
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

int SpellRules::concentrationDc(int damage) const
{
    return std::max(minimumDc, static_cast<int>(std::floor(static_cast<float>(std::max(0, damage)) * damageShare)));
}

std::string SpellRules::toJson() const
{
    return json{{"hands", hands == Hands::Free ? "free" : "ignored"}, {"slotPrefix", slotPrefix}, {"upcast", upcast},
        {"concentration", {{"onDamage", onDamage == Damage::Save ? "save" : onDamage == Damage::Breaks ? "breaks" : "ignored"},
            {"ability", saveAbility}, {"minimumDc", minimumDc}, {"damageShare", damageShare}, {"endsWhenDown", endsWhenDown}}}}.dump();
}

std::optional<SpellRules> SpellRules::fromJson(std::string_view text, std::string* error)
{
    if (error) error->clear();
    try
    {
        const json j = json::parse(text);
        if (!j.is_object()) throw std::invalid_argument("Spell rules must be an object");
        auto only = [](const json& object, const std::string& path, const std::set<std::string>& fields) {
            for (const auto& [field, value] : object.items())
                if (!fields.contains(field)) throw std::invalid_argument(path + field + ": unknown field");
        };
        only(j, "", {"hands", "slotPrefix", "upcast", "concentration"});
        SpellRules rules;
        const std::string hands = j.value("hands", std::string("free"));
        if (hands != "free" && hands != "ignored") throw std::invalid_argument("hands: is \"free\" or \"ignored\"");
        rules.hands = hands == "free" ? Hands::Free : Hands::Ignored;
        rules.slotPrefix = j.value("slotPrefix", rules.slotPrefix);
        if (rules.slotPrefix.empty() || rules.slotPrefix.size() > 60) throw std::invalid_argument("slotPrefix: is a name of 1 to 60 characters");
        rules.upcast = truth(j, "upcast", rules.upcast);
        if (j.contains("concentration"))
        {
            const json& c = j.at("concentration");
            if (!c.is_object()) throw std::invalid_argument("concentration: is an object");
            only(c, "concentration.", {"onDamage", "ability", "minimumDc", "damageShare", "endsWhenDown"});
            const std::string onDamage = c.value("onDamage", std::string("save"));
            if (onDamage != "save" && onDamage != "breaks" && onDamage != "ignored")
                throw std::invalid_argument("concentration.onDamage: is \"save\", \"breaks\" or \"ignored\"");
            rules.onDamage = onDamage == "save" ? Damage::Save : onDamage == "breaks" ? Damage::Breaks : Damage::Ignored;
            rules.saveAbility = c.value("ability", rules.saveAbility);
            rules.minimumDc = whole(c, "minimumDc", rules.minimumDc, -1000, 1000);
            rules.damageShare = c.value("damageShare", rules.damageShare);
            rules.endsWhenDown = truth(c, "endsWhenDown", rules.endsWhenDown);
            if (!std::isfinite(rules.damageShare) || rules.damageShare < 0 || rules.damageShare > 100 || rules.saveAbility.size() > 64)
                throw std::invalid_argument("concentration: damageShare is 0 to 100 and ability a name");
        }
        return rules;
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

bool SpellRules::check(const Ruleset& rules, std::string* error) const
{
    if (error) error->clear();
    if (onDamage == Damage::Save && !rules.ability(saveAbility))
    {
        if (error) *error = "concentration.ability: unknown ability \"" + saveAbility + "\"";
        return false;
    }
    return true;
}

std::optional<int> slotFor(const Character& caster, const SpellDefinition& spell, const SpellRules& rules, int wanted)
{
    if (spell.level <= 0)
        return 0;
    auto left = [&](int level) {
        const auto found = caster.resources.find(rules.slotPrefix + std::to_string(level));
        return found != caster.resources.end() && found->second.current > 0;
    };
    if (wanted > 0)
        return wanted >= spell.level && (wanted == spell.level || rules.upcast) && left(wanted) ? std::optional(wanted) : std::nullopt;
    const int highest = rules.upcast ? 20 : spell.level;
    for (int level = spell.level; level <= highest; level++)
        if (left(level))
            return level;
    return std::nullopt;
}

bool canCast(const Character& caster, const SpellDefinition& spell, const SpellRules& rules, std::string* why)
{
    if (why) why->clear();
    if (rules.hands == SpellRules::Hands::Free && caster.freeHands() < spell.hands)
    {
        if (why) *why = spell.hands == 1 ? "needs a free hand" : "needs " + std::to_string(spell.hands) + " free hands";
        return false;
    }
    if (!slotFor(caster, spell, rules))
    {
        if (why) *why = "no spell slot left";
        return false;
    }
    return true;
}

bool spendSlot(Character& caster, const SpellRules& rules, int slot)
{
    if (slot <= 0)
        return true;
    const auto found = caster.resources.find(rules.slotPrefix + std::to_string(slot));
    if (found == caster.resources.end() || found->second.current <= 0)
        return false;
    found->second.current--;
    return true;
}

Concentration Concentration::begin(std::string spell, const EffectResult& result)
{
    Concentration made;
    made.spell = std::move(spell);
    for (const EffectEvent& event : result.events)
    {
        Hold hold;
        hold.who = event.who;
        if (event.kind == EffectEvent::Kind::ConditionAdded)
            hold.id = event.id;
        else if (event.kind == EffectEvent::Kind::Modifier)
            hold.id = event.tracked;
        if (!hold.id.empty() && std::find(made.holds.begin(), made.holds.end(), hold) == made.holds.end())
            made.holds.push_back(std::move(hold));
    }
    return made;
}

std::vector<Concentration::Hold> Concentration::end(const Sheets& sheets)
{
    std::vector<Hold> removed;
    for (const Hold& hold : holds)
        if (Character* sheet = sheets(hold.who); sheet && sheet->hasCondition(hold.id))
        {
            sheet->removeCondition(hold.id);
            removed.push_back(hold);
        }
    spell.clear();
    holds.clear();
    return removed;
}

bool Concentration::tidy(const Sheets& sheets)
{
    std::erase_if(holds, [&](const Hold& hold) {
        const Character* sheet = sheets(hold.who);
        return !sheet || !sheet->hasCondition(hold.id);
    });
    if (holds.empty())
        spell.clear();
    return active();
}

std::string Concentration::toJson() const
{
    json list = json::array();
    for (const Hold& hold : holds)
        list.push_back({{"who", hold.who}, {"id", hold.id}});
    return json{{"spell", spell}, {"holds", list}}.dump();
}

std::optional<Concentration> Concentration::fromJson(std::string_view text, std::string* error)
{
    if (error) error->clear();
    try
    {
        const json j = json::parse(text);
        Concentration made;
        made.spell = j.at("spell").get<std::string>();
        for (const json& hold : j.value("holds", json::array()))
            made.holds.push_back({hold.at("who").get<int>(), hold.at("id").get<std::string>()});
        if (made.spell.size() > 64 || made.holds.size() > 10000) throw std::invalid_argument("Invalid concentration");
        if (!made.active()) made.holds.clear();
        return made;
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

ConcentrationCheck concentrationCheck(const Character& caster, const Ruleset& rules, const SpellRules& spells, int damage, Random& random)
{
    ConcentrationCheck check;
    if (damage <= 0 || spells.onDamage == SpellRules::Damage::Ignored)
        return check;
    if (spells.onDamage == SpellRules::Damage::Breaks)
    {
        check.kept = false;
        return check;
    }
    check.rolled = true;
    check.dc = spells.concentrationDc(damage);
    check.roll = caster.rollSave(rules, spells.saveAbility, Advantage::None, random);
    check.kept = check.roll.total >= check.dc;
    return check;
}

}
