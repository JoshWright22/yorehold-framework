// Character choices <-> JSON, rolling new ones, and reading them back out of an older sheet.

#include "yorehold/framework/rpg/CharacterChoices.h"

#include "yorehold/framework/rpg/Dice.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <initializer_list>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

constexpr int choicesVersion = 1;
constexpr int lowestScore = 1;
constexpr int highestScore = 30;
constexpr size_t mostLevels = 1000; // the same ceiling a sheet's level has

[[noreturn]] void fail(const std::string& field, const std::string& what)
{
    throw std::invalid_argument(field + ": " + what);
}

void onlyFields(const json& j, const std::string& path, std::initializer_list<const char*> known)
{
    for (auto it = j.begin(); it != j.end(); ++it)
        if (std::none_of(known.begin(), known.end(), [&](const char* field) { return it.key() == field; }))
            fail(path + it.key(), "unknown field");
}

bool isName(const json& j)
{
    return j.is_string() && !j.get<std::string>().empty() && j.get<std::string>().size() <= 64;
}

std::string text(const json& j, const char* key, size_t longest)
{
    if (!j.contains(key))
        return {};
    if (!j.at(key).is_string() || j.at(key).get<std::string>().size() > longest)
        fail(key, "is text of up to " + std::to_string(longest) + " characters");
    return j.at(key).get<std::string>();
}

CharacterChoices parse(const json& j)
{
    if (!j.is_object()) fail("character", "is a JSON object");
    onlyFields(j, "", {"version", "name", "race", "background", "scoreMethod", "scores", "levels", "xp", "ruleset", "notes"});
    if (j.contains("version") && (!j.at("version").is_number_integer() || j.at("version").get<long long>() < 1))
        fail("version", "is a whole number from 1");
    if (j.value("version", choicesVersion) > choicesVersion)
        fail("version", "was saved by a newer version of the game");

    CharacterChoices c;
    c.name = text(j, "name", 64);
    c.race = text(j, "race", 64);
    c.background = text(j, "background", 64);
    c.ruleset = text(j, "ruleset", 64);
    c.notes = text(j, "notes", 20000);
    if (j.contains("scoreMethod"))
    {
        c.scoreMethod = text(j, "scoreMethod", 64);
        if (std::none_of(std::begin(CharacterChoices::methods), std::end(CharacterChoices::methods), [&](const char* m) { return c.scoreMethod == m; }))
            fail("scoreMethod", "is \"roll\", \"pointBuy\", \"array\" or \"fixed\"");
    }
    if (j.contains("xp"))
    {
        if (!j.at("xp").is_number_integer() || j.at("xp").get<long long>() < 0 || j.at("xp").get<long long>() > 1000000000)
            fail("xp", "is a whole number from 0");
        c.xp = j.at("xp").get<int>();
    }

    if (!j.contains("scores") || !j.at("scores").is_object())
        fail("scores", "maps each ability to a score");
    for (const auto& [ability, score] : j.at("scores").items())
    {
        if (ability.empty() || ability.size() > 64 || !score.is_number_integer()
            || score.get<long long>() < lowestScore || score.get<long long>() > highestScore)
            fail("scores." + ability, "is a score from " + std::to_string(lowestScore) + " to " + std::to_string(highestScore));
        c.scores[ability] = score.get<int>();
    }

    if (!j.contains("levels") || !j.at("levels").is_array() || j.at("levels").empty() || j.at("levels").size() > mostLevels)
        fail("levels", "lists each level the character has, at least one");
    for (size_t i = 0; i < j.at("levels").size(); i++)
    {
        const json& entry = j.at("levels").at(i);
        const std::string path = "levels[" + std::to_string(i) + "].";
        if (!entry.is_object()) fail(path.substr(0, path.size() - 1), "is an object with a class");
        onlyFields(entry, path, {"class", "picks"});
        LevelChoice level;
        if (!entry.contains("class") || !isName(entry.at("class"))) fail(path + "class", "is a class id");
        level.classId = entry.at("class").get<std::string>();
        if (entry.contains("picks"))
        {
            if (!entry.at("picks").is_object()) fail(path + "picks", "maps a kind to a list of ids");
            for (const auto& [kind, ids] : entry.at("picks").items())
            {
                if (kind.empty() || kind.size() > 64 || !ids.is_array() || !std::all_of(ids.begin(), ids.end(), isName))
                    fail(path + "picks." + kind, "is a list of ids, 1 to 64 characters each");
                level.picks[kind] = ids.get<std::vector<std::string>>();
            }
        }
        c.levels.push_back(std::move(level));
    }
    return c;
}

}

bool CharacterChoices::check(const Ruleset& rules, std::string* error) const
{
    if (error) error->clear();
    auto problem = [&](std::string what) {
        if (error) *error = std::move(what);
        return false;
    };
    if (levels.empty())
        return problem("levels: at least one is needed");
    for (const AbilityDefinition& ability : rules.abilities)
    {
        const auto found = scores.find(ability.id);
        if (found == scores.end())
            return problem("scores." + ability.id + ": missing");
        if (found->second < lowestScore || found->second > highestScore)
            return problem("scores." + ability.id + ": out of range");
    }
    for (const auto& [ability, score] : scores)
        if (std::none_of(rules.abilities.begin(), rules.abilities.end(), [&](const AbilityDefinition& a) { return a.id == ability; }))
            return problem("scores." + ability + ": not an ability of this ruleset");
    if (scoreMethod == "pointBuy")
    {
        const int cost = pointBuyCost(rules, scores);
        if (rules.scoreMethods.pointCosts.empty())
            return problem("scoreMethod: this ruleset has no point buy");
        if (cost < 0)
            return problem("scores: point buy only buys scores " + std::to_string(rules.scoreMethods.pointCosts.begin()->first) + " to "
                + std::to_string(rules.scoreMethods.pointCosts.rbegin()->first));
        if (cost > rules.scoreMethods.pointBudget)
            return problem("scores: cost " + std::to_string(cost) + " points, the budget is " + std::to_string(rules.scoreMethods.pointBudget));
    }
    if (scoreMethod == "array")
    {
        std::vector<int> given, wanted = rules.scoreMethods.standardArray;
        for (const auto& [ability, score] : scores)
            given.push_back(score);
        std::sort(given.begin(), given.end());
        std::sort(wanted.begin(), wanted.end());
        if (given != wanted)
            return problem("scores: the standard array uses each of its values once");
    }
    return true;
}

int pointBuyCost(const Ruleset& rules, const std::map<std::string, int>& scores)
{
    int total = 0;
    for (const auto& [ability, score] : scores)
    {
        const auto cost = rules.scoreMethods.pointCosts.find(score);
        if (cost == rules.scoreMethods.pointCosts.end())
            return -1;
        total += cost->second;
    }
    return total;
}

std::string CharacterChoices::toJson() const
{
    json j;
    j["version"] = choicesVersion;
    j["name"] = name;
    if (!race.empty()) j["race"] = race;
    if (!background.empty()) j["background"] = background;
    j["scoreMethod"] = scoreMethod;
    j["scores"] = scores;
    json levels_ = json::array();
    for (const LevelChoice& level : levels)
    {
        json entry{{"class", level.classId}};
        if (!level.picks.empty()) entry["picks"] = level.picks;
        levels_.push_back(std::move(entry));
    }
    j["levels"] = levels_;
    j["xp"] = xp;
    if (!ruleset.empty()) j["ruleset"] = ruleset;
    if (!notes.empty()) j["notes"] = notes;
    return j.dump(2);
}

std::optional<CharacterChoices> CharacterChoices::fromJson(std::string_view source, std::string* error)
{
    if (error) error->clear();
    try
    {
        return parse(json::parse(source));
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

CharacterChoices rollChoices(const Ruleset& rules, std::string name, std::string classId, Random& random)
{
    CharacterChoices c;
    c.name = std::move(name);
    c.scoreMethod = "roll";
    c.ruleset = rules.id;
    for (const AbilityDefinition& ability : rules.abilities)
        c.scores[ability.id] = roll(rules.scoreMethods.roll, random).total;
    c.levels.push_back({std::move(classId), {}});
    return c;
}

CharacterChoices choicesFromSheet(const Ruleset& rules, const Character& sheet, std::string classId)
{
    CharacterChoices c;
    c.name = sheet.name;
    c.scoreMethod = "fixed"; // the ancestry on an old sheet is a display name, not a race id, so it stays behind
    c.ruleset = rules.id;
    c.xp = std::max(0, sheet.xp);
    c.notes = sheet.notes;
    for (const AbilityDefinition& ability : rules.abilities)
        c.scores[ability.id] = std::clamp(sheet.abilityScore(ability.id), lowestScore, highestScore);
    c.levels.assign(static_cast<size_t>(std::clamp(sheet.level, 1, static_cast<int>(mostLevels))), LevelChoice{classId, {}});
    return c;
}

}
