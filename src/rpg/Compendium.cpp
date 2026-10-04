#include "yorehold/framework/rpg/Compendium.h"
#include "yorehold/framework/rpg/Action.h"

#include "yorehold/framework/assets/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

bool validId(std::string_view id)
{
    return !id.empty() && id.size() <= 64
        && std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}

Modifier::Op opFromName(const std::string& name)
{
    if (name == "add") return Modifier::Op::Add;
    if (name == "multiply") return Modifier::Op::Multiply;
    if (name == "override") return Modifier::Op::Override;
    throw std::invalid_argument("Unknown modifier op \"" + name + "\"");
}

const char* opName(Modifier::Op op)
{
    return op == Modifier::Op::Multiply ? "multiply" : op == Modifier::Op::Override ? "override" : "add";
}

Color colorFrom(const json& j)
{
    const auto v = j.get<std::vector<int>>();
    if ((v.size() != 3 && v.size() != 4) || std::any_of(v.begin(), v.end(), [](int c) { return c < 0 || c > 255; }))
        throw std::invalid_argument("Colours are [r,g,b] or [r,g,b,a] in 0..255");
    return {static_cast<uint8_t>(v[0]), static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2]), static_cast<uint8_t>(v.size() == 4 ? v[3] : 255)};
}

Item itemFrom(const json& j)
{
    Item item;
    item.id = j.at("id").get<std::string>();
    item.name = j.value("name", item.id);
    item.slot = j.value("slot", "");
    item.damage = j.value("damage", "");
    item.attackAbility = j.value("attackAbility", "str");
    item.hands = j.value("hands", 1);
    if (item.hands < 0 || item.hands > 4) throw std::invalid_argument("Items need 0 to 4 hands");
    item.weight = j.value("weight", 0.0f);
    item.value = j.value("value", 0);
    item.quantity = j.value("quantity", 1);
    item.magic = j.value("magic", false);
    if (j.contains("use"))
    {
        auto use = j.at("use");
        if (!use.is_object() || !item.slot.empty()) throw std::invalid_argument("Item use needs an object on a carried-only item");
        if (!use.contains("id")) use["id"] = item.id;
        if (!use.contains("name")) use["name"] = item.name;
        if (!use.contains("cost")) use["cost"] = item.hands;
        use["general"] = false;
        std::string error;
        auto action = ActionDefinition::fromJson(use.dump(), &error);
        if (!action) throw std::invalid_argument("Item use: " + error);
        if (action->effect.empty() || action->costsHands || action->endsTurn || !action->readies.empty())
            throw std::invalid_argument("Item use needs effects, a numeric cost, and cannot end a turn or ready an action");
        item.use = std::make_shared<ActionDefinition>(std::move(*action));
    }
    for (const json& m : j.value("modifiers", json::array()))
    {
        Modifier mod{m.at("stat").get<std::string>(), opFromName(m.value("op", "add")), m.at("value").get<float>(), ""};
        if (mod.stat.empty() || !std::isfinite(mod.value)) throw std::invalid_argument("Bad item modifier");
        item.modifiers.push_back(std::move(mod));
    }
    if (!validId(item.id)) throw std::invalid_argument("Item ids use a-z, 0-9, - and _");
    if (!item.damage.empty() && !DiceExpression::parse(item.damage)) throw std::invalid_argument("Bad damage dice \"" + item.damage + "\"");
    if (!std::isfinite(item.weight) || item.weight < 0 || item.quantity < 0) throw std::invalid_argument("Bad item weight or quantity");
    return item;
}

template <typename T, typename Parse>
std::optional<T> parse(std::string_view text, std::string* error, Parse parseObject)
{
    if (error) error->clear();
    try
    {
        const json j = json::parse(text);
        if (!j.is_object()) throw std::invalid_argument("Expected a JSON object");
        return parseObject(j);
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

std::map<std::string, std::string> ranksFrom(const json& j)
{
    const auto ranks = j.value("proficiencyRanks", json::object());
    if (!ranks.is_object() || ranks.size() > 1000) throw std::invalid_argument("Proficiency ranks must be an object");
    auto choices = ranks.get<std::map<std::string, std::string>>();
    for (const auto& [target, rank] : choices)
        if (target.empty() || target.size() > 64 || rank.empty() || rank.size() > 64) throw std::invalid_argument("Invalid proficiency choice");
    return choices;
}

int abilityAdjustedArmor(const Ruleset& rules, const Character& c, int armorClass)
{
    // The ruleset adds an ability to AC; take it back off so the final AC is what the file says.
    return armorClass - (rules.armorClassAbility.empty() ? 0 : c.abilityModifier(rules, rules.armorClassAbility))
        - (rules.proficiencyRanks.empty() ? 0 : c.proficiencyModifier(rules, "armor"));
}

std::map<std::string, Resource> resourcesFrom(const json& j)
{
    const auto data = j.value("resources", json::object());
    if (!data.is_object() || data.size() > 1000) throw std::invalid_argument("Resources must be an object");
    std::map<std::string, Resource> resources;
    for (const auto& [id, value] : data.items())
    {
        const int maximum = value.at("max").get<int>();
        const int current = value.value("current", maximum);
        if (id.empty() || id.size() > 64 || maximum < 0 || maximum > 100000 || current < 0 || current > maximum)
            throw std::invalid_argument("Invalid resource: " + id);
        resources[id] = {current, maximum};
    }
    return resources;
}

json resourcesJson(const std::map<std::string, Resource>& resources)
{
    json data = json::object();
    for (const auto& [id, value] : resources) data[id] = {{"current", value.current}, {"max", value.max}};
    return data;
}

}

std::optional<Item> Compendium::itemFromJson(std::string_view text, std::string* error)
{
    return parse<Item>(text, error, itemFrom);
}

std::optional<ClassDefinition> Compendium::classFromJson(std::string_view text, std::string* error)
{
    return parse<ClassDefinition>(text, error, [](const json& j) {
        ClassDefinition c;
        c.id = j.at("id").get<std::string>();
        c.name = j.value("name", c.id);
        c.description = j.value("description", "");
        c.hitDie = j.value("hitDie", c.hitDie);
        c.speed = j.value("speed", c.speed);
        c.darkvision = j.value("darkvision", c.darkvision);
        c.bonusHp = j.value("bonusHp", c.bonusHp);
        c.proficiencies = j.value("proficiencies", std::set<std::string>{});
        c.proficiencyRanks = ranksFrom(j);
        c.dcAbility = j.value("dcAbility", std::string{});
        c.resources = resourcesFrom(j);
        if (c.dcAbility.size() > 64) throw std::invalid_argument("DC ability is too long");
        c.items = j.value("items", std::vector<std::string>{});
        c.casting = j.value("casting", std::string{});
        c.prepareLimit = j.value("prepareLimit", 0);
        if (j.contains("spells"))
        {
            if (!j.at("spells").is_object()) throw std::invalid_argument("spells: maps a spell level to a list of spell ids");
            for (const auto& [level, ids] : j.at("spells").items())
            {
                const bool digits = !level.empty() && level.size() <= 2 && std::all_of(level.begin(), level.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
                if (!digits || !ids.is_array()) throw std::invalid_argument("spells." + level + ": is a spell level with a list of spell ids");
                for (const json& id : ids)
                {
                    if (!id.is_string() || !validId(id.get<std::string>())) throw std::invalid_argument("spells." + level + ": spell ids use a-z, 0-9, - and _");
                    c.spells[std::stoi(level)].push_back(id.get<std::string>());
                }
            }
        }
        if (j.contains("levels"))
        {
            std::string e;
            std::optional<std::vector<ClassLevel>> levels = classLevelsFromJson(j.at("levels").dump(), &e);
            if (!levels) throw std::invalid_argument(e);
            c.levels = std::move(*levels);
        }
        if (!validId(c.id)) throw std::invalid_argument("Class ids use a-z, 0-9, - and _");
        if (c.hitDie < 1 || c.hitDie > 100 || c.speed < 0 || c.speed > 1000 || c.bonusHp < 0 || c.bonusHp > 1000
            || c.darkvision < 0 || c.darkvision > 10000)
            throw std::invalid_argument("Bad class numbers");
        return c;
    });
}

std::optional<CreatureDefinition> Compendium::creatureFromJson(std::string_view text, std::string* error)
{
    return parse<CreatureDefinition>(text, error, [](const json& j) {
        CreatureDefinition c;
        c.id = j.at("id").get<std::string>();
        c.name = j.value("name", c.id);
        c.description = j.value("description", "");
        c.hp = j.value("hp", c.hp);
        c.level = j.value("level", c.level);
        c.deathSaves = j.value("deathSaves", false);
        c.resources = resourcesFrom(j);
        c.armorClass = j.value("armorClass", c.armorClass);
        c.speed = j.value("speed", c.speed);
        c.darkvision = j.value("darkvision", c.darkvision);
        c.abilities = j.value("abilities", std::map<std::string, int>{});
        c.proficiencies = j.value("proficiencies", std::set<std::string>{});
        c.proficiencyRanks = ranksFrom(j);
        c.dcAbility = j.value("dcAbility", std::string{});
        if (c.dcAbility.size() > 64) throw std::invalid_argument("DC ability is too long");
        c.items = j.value("items", std::vector<std::string>{});
        if (j.contains("loot"))
        {
            std::string e;
            std::optional<LootTable> loot = LootTable::fromJson(j.at("loot").dump(), &e);
            if (!loot) throw std::invalid_argument(e);
            c.loot = std::move(*loot);
        }
        if (j.contains("token"))
        {
            const json& t = j.at("token");
            if (t.contains("color")) c.token.color = colorFrom(t.at("color"));
            c.token.size = t.value("size", c.token.size);
            c.token.image = t.value("image", "");
        }
        if (j.contains("ai"))
        {
            if (!j.at("ai").is_string() && !j.at("ai").is_object()) throw std::invalid_argument("ai is a profile name or an object");
            c.ai = j.at("ai").dump();
        }
        if (!validId(c.id)) throw std::invalid_argument("Creature ids use a-z, 0-9, - and _");
        if (c.level < 1 || c.level > 1000 || c.hp < 1 || c.hp > 100000 || c.armorClass < 0 || c.armorClass > 100 || c.speed < 0 || c.speed > 1000
            || c.darkvision < 0 || c.darkvision > 10000 || !std::isfinite(c.token.size) || c.token.size <= 0 || c.token.size > 10)
            throw std::invalid_argument("Bad creature numbers");
        return c;
    });
}

std::string Compendium::itemToJson(const Item& item)
{
    json modifiers = json::array();
    for (const Modifier& m : item.modifiers)
        modifiers.push_back({{"stat", m.stat}, {"op", opName(m.op)}, {"value", m.value}});
    json j{{"id", item.id}, {"name", item.name}, {"slot", item.slot}, {"damage", item.damage},
        {"attackAbility", item.attackAbility}, {"hands", item.hands}, {"weight", item.weight}, {"value", item.value},
        {"quantity", item.quantity}, {"magic", item.magic}, {"modifiers", modifiers}};
    if (item.use) j["use"] = json::parse(item.use->json);
    return j.dump(2);
}

std::string Compendium::classToJson(const ClassDefinition& c)
{
    json j{{"id", c.id}, {"name", c.name}, {"description", c.description}, {"hitDie", c.hitDie}, {"speed", c.speed},
        {"darkvision", c.darkvision}, {"bonusHp", c.bonusHp}, {"proficiencies", c.proficiencies}, {"items", c.items},
        {"proficiencyRanks", c.proficiencyRanks}, {"dcAbility", c.dcAbility}, {"resources", resourcesJson(c.resources)}};
    if (!c.levels.empty()) j["levels"] = json::parse(classLevelsToJson(c.levels));
    for (const auto& [level, ids] : c.spells)
        j["spells"][std::to_string(level)] = ids;
    return j.dump(2);
}

std::string Compendium::creatureToJson(const CreatureDefinition& c)
{
    const Color k = c.token.color;
    json j{{"id", c.id}, {"name", c.name}, {"description", c.description}, {"hp", c.hp}, {"level", c.level}, {"armorClass", c.armorClass},
        {"speed", c.speed}, {"darkvision", c.darkvision}, {"abilities", c.abilities}, {"proficiencies", c.proficiencies}, {"items", c.items},
        {"token", {{"color", {k.r, k.g, k.b, k.a}}, {"size", c.token.size}, {"image", c.token.image}}},
        {"ai", json::parse(c.ai, nullptr, false)}, {"proficiencyRanks", c.proficiencyRanks}, {"dcAbility", c.dcAbility},
        {"deathSaves", c.deathSaves}, {"resources", resourcesJson(c.resources)}};
    if (!c.loot.empty()) j["loot"] = json::parse(c.loot.toJson());
    return j.dump(2);
}

bool Compendium::checkLoot(const LootTable& table, std::string* error) const
{
    for (const LootEntry& entry : table.items)
        if (!items.contains(entry.item))
        {
            if (error) *error = "unknown item \"" + entry.item + "\" in loot";
            return false;
        }
    return true;
}

std::vector<Item> Compendium::lootItems(const LootRoll& found) const
{
    std::vector<Item> made;
    for (const auto& [id, quantity] : found.items)
        if (const Item* definition = item(id))
        {
            made.push_back(*definition);
            made.back().quantity = quantity;
            made.back().equipped = false;
        }
    return made;
}

bool Compendium::load(const FileSystem& files, std::string_view folder, std::string* error)
{
    if (error) error->clear();
    const std::string base = folder.empty() ? std::string() : std::string(folder) + "/";
    Compendium next = *this; // all-or-nothing: work on a copy
    std::string problem;

    // The file name is the id ("creatures/goblin.json" is "goblin"), so a mismatch is an error.
    auto each = [&](const char* kind, auto add) {
        for (const std::string& path : files.list(base + kind))
        {
            if (!path.ends_with(".json"))
                continue;
            const std::string stem = path.substr(path.rfind('/') + 1, path.size() - path.rfind('/') - 1 - 5);
            const std::optional<std::string> text = files.readText(path);
            if (!text)
                problem = "can't read";
            else
                problem = add(*text, stem);
            if (!problem.empty())
            {
                if (error) *error = path + ": " + problem;
                return false;
            }
        }
        return true;
    };

    const bool ok = each("items", [&](const std::string& text, const std::string& stem) -> std::string {
        std::string e;
        std::optional<Item> item = itemFromJson(text, &e);
        if (!item) return e;
        if (item->id != stem) return "id \"" + item->id + "\" doesn't match the file name";
        next.items[item->id] = std::move(*item);
        return {};
    }) && each("classes", [&](const std::string& text, const std::string& stem) -> std::string {
        std::string e;
        std::optional<ClassDefinition> c = classFromJson(text, &e);
        if (!c) return e;
        if (c->id != stem) return "id \"" + c->id + "\" doesn't match the file name";
        for (const std::string& id : c->items)
            if (!next.items.contains(id)) return "unknown item \"" + id + "\"";
        next.classes[c->id] = std::move(*c);
        return {};
    });
    if (!ok)
        return false;

    // AI profiles can build on each other in any order, so keep resolving until nothing is left
    // (or nothing more can be: a base that doesn't exist, or two that name each other).
    std::map<std::string, std::pair<std::string, std::string>> waiting; // id -> path, text
    const bool read = each("ai", [&](const std::string& text, const std::string& stem) -> std::string {
        const json j = json::parse(text, nullptr, false);
        if (!j.is_object() || !j.contains("id") || !j.at("id").is_string()) return "AI profiles are objects with an id";
        if (j.at("id") != stem) return "id doesn't match the file name";
        if (!validId(stem)) return "AI ids use a-z, 0-9, - and _";
        waiting[stem] = {base + "ai/" + stem + ".json", text};
        return {};
    });
    if (!read)
        return false;
    const AiProfile blank = [] { AiProfile p; p.base = "custom"; return p; }();
    while (!waiting.empty())
    {
        std::string failed, why;
        size_t resolved = 0;
        for (auto it = waiting.begin(); it != waiting.end();)
        {
            const std::string& id = it->first;
            // A profile may not build on itself, or on one that is still waiting.
            const AiProfile::Lookup known = [&](std::string_view name) -> const AiProfile* {
                const auto found = next.ai.find(name);
                return found == next.ai.end() || waiting.contains(std::string(name)) ? nullptr : &found->second;
            };
            std::string e;
            std::optional<AiProfile> profile = AiProfile::fromJson(it->second.second, &e, known, &blank);
            if (!profile)
            {
                failed = it->second.first;
                why = e;
                ++it;
                continue;
            }
            profile->base = id;
            next.ai[id] = std::move(*profile);
            it = waiting.erase(it);
            resolved++;
        }
        if (resolved == 0)
        {
            if (error) *error = failed + ": " + why;
            return false;
        }
    }

    const bool creaturesOk = each("creatures", [&](const std::string& text, const std::string& stem) -> std::string {
        std::string e;
        std::optional<CreatureDefinition> c = creatureFromJson(text, &e);
        if (!c) return e;
        if (c->id != stem) return "id \"" + c->id + "\" doesn't match the file name";
        for (const std::string& id : c->items)
            if (!next.items.contains(id)) return "unknown item \"" + id + "\"";
        if (!next.checkLoot(c->loot, &e)) return e;
        if (!AiProfile::fromJson(c->ai, &e, next.aiLookup())) return e;
        next.creatures[c->id] = std::move(*c);
        return {};
    });
    if (!creaturesOk)
        return false;
    *this = std::move(next);
    return true;
}

bool Compendium::loadOptions(const FileSystem& files, std::string_view folder, std::string* error)
{
    if (error) error->clear();
    const std::string base = folder.empty() ? std::string() : std::string(folder) + "/";
    Compendium next = *this;

    // Each file read through `add`, which returns a problem or nothing; the file name is the id.
    auto each = [&](const char* kind, auto add) {
        for (const std::string& path : files.list(base + kind))
        {
            if (!path.ends_with(".json"))
                continue;
            const std::string stem = path.substr(path.rfind('/') + 1, path.size() - path.rfind('/') - 1 - 5);
            const std::optional<std::string> text = files.readText(path);
            std::string problem = text ? add(*text, stem) : "can't read";
            if (!problem.empty())
            {
                if (error) *error = path + ": " + problem;
                return false;
            }
        }
        return true;
    };
    auto read = [&](auto fromJson, auto& into) {
        return [fromJson, target = &into](const std::string& text, const std::string& stem) -> std::string {
            std::string e;
            auto definition = fromJson(text, &e);
            if (!definition) return e;
            if (definition->id != stem) return "id \"" + definition->id + "\" doesn't match the file name";
            (*target)[definition->id] = std::move(*definition);
            return {};
        };
    };
    if (!each("feats", read(featFromJson, next.feats)) || !each("races", read(raceFromJson, next.races))
        || !each("backgrounds", read(backgroundFromJson, next.backgrounds)))
        return false;
    const bool spellsRead = each("spells", [&](const std::string& text, const std::string& stem) -> std::string {
        std::string e;
        std::optional<SpellDefinition> spell = SpellDefinition::fromJson(text, &e);
        if (!spell) return e;
        if (spell->id() != stem) return "id \"" + spell->id() + "\" doesn't match the file name";
        if (!validId(stem)) return "Spell ids use a-z, 0-9, - and _";
        next.spells[stem] = std::move(*spell);
        return {};
    });
    if (!spellsRead)
        return false;

    // Cross-references, now everything is in.
    auto problem = [&](const std::string& path, const std::string& what) {
        if (error) *error = base + path + ": " + what;
        return false;
    };
    for (const auto& [id, feat] : next.feats)
    {
        for (const std::string& race : feat.needs.races)
            if (!next.races.contains(race)) return problem("feats/" + id + ".json", "requires.races: no race \"" + race + "\"");
        for (const std::string& characterClass : feat.needs.classes)
            if (!next.classes.contains(characterClass)) return problem("feats/" + id + ".json", "requires.classes: no class \"" + characterClass + "\"");
    }
    for (const auto& [id, race] : next.races)
        for (const std::string& feat : race.feats)
            if (!next.feats.contains(feat)) return problem("races/" + id + ".json", "feats: no feat \"" + feat + "\"");
    for (const auto& [id, background] : next.backgrounds)
    {
        for (const std::string& feat : background.feats)
            if (!next.feats.contains(feat)) return problem("backgrounds/" + id + ".json", "feats: no feat \"" + feat + "\"");
        for (const std::string& item : background.items)
            if (!next.items.contains(item)) return problem("backgrounds/" + id + ".json", "items: no item \"" + item + "\"");
    }
    // Class files sit with the shared content, so their paths are not under this folder. A ruleset
    // with no spells at all simply has no casting: shared classes still load under it.
    for (const auto& [id, definition] : next.spells.empty() ? std::map<std::string, ClassDefinition>{} : next.classes)
        for (const auto& [level, ids] : definition.spells)
            for (const std::string& spell : ids)
            {
                const auto found = next.spells.find(spell);
                const std::string where = "classes/" + id + ".json: spells." + std::to_string(level) + ": ";
                if (found == next.spells.end() || found->second.level != level)
                {
                    if (error) *error = where + (found == next.spells.end() ? "no spell \"" + spell + "\""
                        : "\"" + spell + "\" is a level " + std::to_string(found->second.level) + " spell");
                    return false;
                }
            }
    *this = std::move(next);
    return true;
}

Compendium::Compendium()
{
    for (const char* name : {"mindless", "animal", "cunning", "tactical"})
        ai[name] = *AiProfile::preset(name);
}

AiProfile::Lookup Compendium::aiLookup() const
{
    return [this](std::string_view name) -> const AiProfile* {
        const auto found = ai.find(name);
        return found == ai.end() ? nullptr : &found->second;
    };
}

AiProfile Compendium::aiFor(const CreatureDefinition& creature) const
{
    if (const std::optional<AiProfile> profile = AiProfile::fromJson(creature.ai, nullptr, aiLookup()))
        return *profile;
    return *AiProfile::preset("cunning");
}

const Item* Compendium::item(std::string_view id) const
{
    const auto found = items.find(std::string(id));
    return found == items.end() ? nullptr : &found->second;
}

const ClassDefinition* Compendium::characterClass(std::string_view id) const
{
    const auto found = classes.find(std::string(id));
    return found == classes.end() ? nullptr : &found->second;
}

const CreatureDefinition* Compendium::creature(std::string_view id) const
{
    const auto found = creatures.find(std::string(id));
    return found == creatures.end() ? nullptr : &found->second;
}

const RaceDefinition* Compendium::race(std::string_view id) const
{
    const auto found = races.find(std::string(id));
    return found == races.end() ? nullptr : &found->second;
}

const BackgroundDefinition* Compendium::background(std::string_view id) const
{
    const auto found = backgrounds.find(std::string(id));
    return found == backgrounds.end() ? nullptr : &found->second;
}

const FeatDefinition* Compendium::feat(std::string_view id) const
{
    const auto found = feats.find(std::string(id));
    return found == feats.end() ? nullptr : &found->second;
}

const SpellDefinition* Compendium::spell(std::string_view id) const
{
    const auto found = spells.find(std::string(id));
    return found == spells.end() ? nullptr : &found->second;
}

std::optional<Character> Compendium::makeCharacter(const Ruleset& rules, std::string_view classId, std::string name, Random& random) const
{
    if (!characterClass(classId))
        return std::nullopt;
    return build(rules, rollChoices(rules, std::move(name), std::string(classId), random));
}

std::optional<Character> Compendium::build(const Ruleset& rules, const CharacterChoices& choices, std::string* error) const
{
    if (!choices.check(rules, error))
        return std::nullopt;
    std::vector<const ClassDefinition*> classes_;
    for (size_t i = 0; i < choices.levels.size(); i++)
    {
        classes_.push_back(characterClass(choices.levels[i].classId));
        if (!classes_.back())
        {
            if (error) *error = "levels[" + std::to_string(i) + "].class: no class \"" + choices.levels[i].classId + "\"";
            return std::nullopt;
        }
    }
    const ClassDefinition& first = *classes_.front();
    auto fail = [&](std::string what) -> std::optional<Character> {
        if (error) *error = std::move(what);
        return std::nullopt;
    };
    const RaceDefinition* race_ = choices.race.empty() ? nullptr : race(choices.race);
    if (!choices.race.empty() && !race_)
        return fail("race: no race \"" + choices.race + "\"");
    const BackgroundDefinition* background_ = choices.background.empty() ? nullptr : background(choices.background);
    if (!choices.background.empty() && !background_)
        return fail("background: no background \"" + choices.background + "\"");

    Character c;
    c.name = choices.name;
    c.ancestry = race_ ? race_->name : choices.race;
    c.notes = choices.notes;
    c.level = choices.level();
    c.xp = choices.xp;
    std::set<std::string> named;
    for (const ClassDefinition* definition : classes_)
        if (named.insert(definition->id).second)
            c.characterClass += (c.characterClass.empty() ? "" : " / ") + definition->name;
    for (const auto& [ability, score] : choices.scores)
        c.stats.setBase(ability, static_cast<float>(score));
    c.stats.setBase("ac", static_cast<float>(rules.baseArmorClass));
    c.hitDie = "1d" + std::to_string(first.hitDie);
    c.stats.setBase("speed", static_cast<float>(first.speed));
    c.stats.setBase("darkvision", static_cast<float>(first.darkvision));
    c.proficiencies = first.proficiencies;
    c.proficiencyRanks = first.proficiencyRanks;
    c.dcAbility = first.dcAbility;
    c.resources = first.resources;

    // Race and background: score changes, skills, speed and senses.
    auto adjust = [&](const std::map<std::string, int>& abilities, const std::string& field) -> bool {
        for (const auto& [ability, change] : abilities)
        {
            if (!rules.ability(ability))
            {
                if (error) *error = field + ": \"" + ability + "\" isn't an ability of this ruleset";
                return false;
            }
            c.stats.setBase(ability, c.stats.base(ability) + static_cast<float>(change));
        }
        return true;
    };
    if (race_)
    {
        if (!adjust(race_->abilities, "race")) return std::nullopt;
        c.proficiencies.insert(race_->proficiencies.begin(), race_->proficiencies.end());
        if (race_->speed > 0) c.stats.setBase("speed", static_cast<float>(race_->speed));
        c.stats.setBase("darkvision", static_cast<float>(std::max(first.darkvision, race_->darkvision)));
    }
    if (background_)
    {
        if (!adjust(background_->abilities, "background")) return std::nullopt;
        c.proficiencies.insert(background_->proficiencies.begin(), background_->proficiencies.end());
    }

    // Feats: what the race and background give, then each level's picks in order, so a feat's
    // requirements are judged on the character as it stood when it was taken.
    auto rankIndex = [&](std::string_view rank) {
        for (size_t i = 0; i < rules.proficiencyRanks.size(); i++)
            if (rules.proficiencyRanks[i].id == rank) return static_cast<int>(i);
        return -1;
    };
    auto raise = [&](const std::map<std::string, std::string>& ranks) {
        for (const auto& [target, rank] : ranks)
            if (!c.proficiencyRanks.contains(target) || rules.proficiencyRanks.empty() || rankIndex(rank) > rankIndex(c.proficiencyRank(rules, target)))
                c.proficiencyRanks[target] = rank;
    };
    auto grant = [&](const Grants& gives, const std::string& source) {
        for (Modifier modifier : gives.modifiers)
        {
            modifier.source = source;
            c.stats.addModifier(std::move(modifier));
        }
        c.proficiencies.insert(gives.proficiencies.begin(), gives.proficiencies.end());
        raise(gives.ranks);
        for (const auto& [id, resource] : gives.resources)
        {
            c.resources[id].max += resource.max;
            c.resources[id].current += resource.max;
        }
    };
    std::set<std::string> taken;
    auto take = [&](const FeatDefinition& feat) {
        taken.insert(feat.id);
        grant(feat.gives, "build:feat:" + feat.id);
    };
    for (const auto* given : {race_ ? &race_->feats : nullptr, background_ ? &background_->feats : nullptr})
        for (const std::string& id : given ? *given : std::vector<std::string>{})
            if (const FeatDefinition* found = feat(id); found && !taken.contains(id))
                take(*found);

    std::set<std::string> classesSoFar;
    std::map<std::string, int> classLevels;
    std::map<std::string, std::map<int, int>> slotsByClass; // each class's slot row at its level
    for (size_t i = 0; i < choices.levels.size(); i++)
    {
        const LevelChoice& level = choices.levels[i];
        const ClassDefinition& definition = *classes_[i];
        classesSoFar.insert(level.classId);
        const int classLevel = ++classLevels[level.classId];
        const bool tabled = !definition.levels.empty();
        const ClassLevel* row = classLevel <= static_cast<int>(definition.levels.size()) ? &definition.levels[classLevel - 1] : nullptr;
        if (row)
        {
            for (const ClassFeature& feature : row->features)
                grant(feature.gives, "build:feature:" + definition.id + ":" + feature.id);
            raise(row->ranks);
            if (!row->slots.empty()) slotsByClass[definition.id] = row->slots;
        }

        const std::string at = "levels[" + std::to_string(i) + "].picks.";
        if (const auto skills = level.picks.find("skills"); skills != level.picks.end())
        {
            const size_t offered = row ? static_cast<size_t>(row->skills) : 0;
            if (tabled && skills->second.size() > offered)
                return fail(at + "skills: " + std::to_string(skills->second.size()) + " picked, this level offers " + std::to_string(offered));
            c.proficiencies.insert(skills->second.begin(), skills->second.end());
        }
        const auto picked = level.picks.find("feats");
        if (picked == level.picks.end())
            continue;
        const std::string field = at + "feats: ";
        std::vector<std::string> open = row ? row->feats : std::vector<std::string>{}; // kinds not yet used at this level
        for (const std::string& id : picked->second)
        {
            const FeatDefinition* found = feat(id);
            if (!found)
                return fail(field + "no feat \"" + id + "\"");
            const FeatDefinition::Requirements& needs = found->needs;
            if (tabled)
            {
                const auto slot = std::find(open.begin(), open.end(), found->kind);
                if (slot == open.end())
                    return fail(field + "\"" + id + "\" is a " + found->kind + " feat, and this level has no " + found->kind + " feat to choose");
                open.erase(slot);
            }
            if (taken.contains(id) && !found->repeatable)
                return fail(field + "\"" + id + "\" is already taken");
            if (static_cast<int>(i) + 1 < needs.level)
                return fail(field + "\"" + id + "\" needs level " + std::to_string(needs.level));
            if (!needs.races.empty() && std::find(needs.races.begin(), needs.races.end(), choices.race) == needs.races.end())
                return fail(field + "\"" + id + "\" is for another race");
            if (!needs.classes.empty() && std::none_of(needs.classes.begin(), needs.classes.end(), [&](const std::string& k) { return classesSoFar.contains(k); }))
                return fail(field + "\"" + id + "\" is for another class");
            for (const auto& [ability, least] : needs.abilities)
                if (c.abilityScore(ability) < least)
                    return fail(field + "\"" + id + "\" needs " + ability + " " + std::to_string(least));
            for (const std::string& target : needs.proficiencies)
            {
                const bool trained = rules.proficiencyRanks.empty() ? c.proficiencies.contains(target)
                                                                    : rankIndex(c.proficiencyRank(rules, target)) >= std::max(0, rankIndex(rules.proficientRank));
                if (!trained)
                    return fail(field + "\"" + id + "\" needs training in " + target);
            }
            take(*found);
        }
    }
    // Spell slots: for each slot level, the most any one class's row gives, so a second casting
    // class widens the choice of spells rather than stacking slots.
    std::map<int, int> slots;
    for (const auto& [classId, row] : slotsByClass)
        for (const auto& [slotLevel, count] : row)
            slots[slotLevel] = std::max(slots[slotLevel], count);
    for (const auto& [slotLevel, count] : slots)
        if (count > 0) c.resources["slots-" + std::to_string(slotLevel)] = {count, count};
    // Spells: each class's cantrips, and what it lists at the levels the sheet has slots for.
    int highestSlot = 0;
    for (const auto& [slotLevel, count] : slots)
        if (count > 0) highestSlot = std::max(highestSlot, slotLevel);
    std::set<const ClassDefinition*> listed;
    for (const ClassDefinition* definition : classes_)
    {
        if (!listed.insert(definition).second)
            continue;
        for (const auto& [spellLevel, ids] : definition->spells)
            for (const std::string& id : ids)
                if (spellLevel <= highestSlot && spells.contains(id) && std::find(c.spells.begin(), c.spells.end(), id) == c.spells.end())
                    c.spells.push_back(id);
    }
    // Prepared casters: set up preparable spells and the prepare limit.
    for (const ClassDefinition* definition : classes_)
    {
        if (definition->casting == "prepared")
        {
            for (const auto& [spellLevel, ids] : definition->spells)
                for (const std::string& id : ids)
                    if (spellLevel <= highestSlot && spells.contains(id) && std::find(c.preparable.begin(), c.preparable.end(), id) == c.preparable.end())
                        c.preparable.push_back(id);
            c.prepareLimit = std::max(c.prepareLimit, definition->prepareLimit);
            // Initially prepared spells = all preparable (can be chosen later)
            c.prepared = c.preparable;
        }
    }

    // HP last, so race and feat changes to CON count.
    const int con = c.abilityModifier(rules, "con");
    int hp = std::max(1, first.hitDie + first.bonusHp + (race_ ? race_->bonusHp : 0) + con);
    for (size_t i = 1; i < classes_.size(); i++)
        hp += std::max(1, classes_[i]->hitDie / 2 + 1 + con);
    c.stats.setBase("maxHp", static_cast<float>(hp));
    c.hp = c.maxHp();

    if (!c.checkProficiencyRanks(rules, error)) return std::nullopt;
    giveItems(c, first.items);
    if (background_) giveItems(c, background_->items);
    return c;
}

void Compendium::giveItems(Character& c, const std::vector<std::string>& ids) const
{
    for (const std::string& id : ids)
    {
        const Item* found = item(id);
        if (!found)
            continue;
        c.inventory.push_back(*found);
        // Equip into empty slots only, so the first weapon listed stays in hand.
        const bool slotFree = std::none_of(c.inventory.begin(), c.inventory.end() - 1,
            [&](const Item& held) { return held.equipped && held.slot == found->slot; });
        if (slotFree)
            c.equip(c.inventory.size() - 1);
    }
}

std::optional<Character> Compendium::makeCreature(const Ruleset& rules, std::string_view creatureId, std::string name, Random& random) const
{
    const CreatureDefinition* definition = creature(creatureId);
    if (!definition)
        return std::nullopt;
    Character c = makeRandomCharacter(rules, name.empty() ? definition->name : std::move(name), definition->name, random);
    for (const auto& [ability, score] : definition->abilities)
        c.stats.setBase(ability, static_cast<float>(score));
    c.stats.setBase("speed", static_cast<float>(definition->speed));
    c.stats.setBase("darkvision", static_cast<float>(definition->darkvision));
    c.stats.setBase("maxHp", static_cast<float>(definition->hp));
    c.hp = definition->hp;
    c.proficiencies = definition->proficiencies;
    c.proficiencyRanks = definition->proficiencyRanks;
    c.dcAbility = definition->dcAbility;
    c.level = definition->level;
    c.death.saves = definition->deathSaves;
    c.resources = definition->resources;
    if (!c.checkProficiencyRanks(rules)) return std::nullopt;
    giveItems(c, definition->items);
    // Stat blocks give the final AC, so creatures' gear shouldn't carry AC modifiers.
    c.stats.setBase("ac", static_cast<float>(abilityAdjustedArmor(rules, c, definition->armorClass)));
    return c;
}

}
