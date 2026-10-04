#include "yorehold/framework/rpg/Compendium.h"

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
        if (c.dcAbility.size() > 64) throw std::invalid_argument("DC ability is too long");
        c.items = j.value("items", std::vector<std::string>{});
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
        c.armorClass = j.value("armorClass", c.armorClass);
        c.speed = j.value("speed", c.speed);
        c.darkvision = j.value("darkvision", c.darkvision);
        c.abilities = j.value("abilities", std::map<std::string, int>{});
        c.proficiencies = j.value("proficiencies", std::set<std::string>{});
        c.proficiencyRanks = ranksFrom(j);
        c.dcAbility = j.value("dcAbility", std::string{});
        if (c.dcAbility.size() > 64) throw std::invalid_argument("DC ability is too long");
        c.items = j.value("items", std::vector<std::string>{});
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
    return json{{"id", item.id}, {"name", item.name}, {"slot", item.slot}, {"damage", item.damage},
        {"attackAbility", item.attackAbility}, {"hands", item.hands}, {"weight", item.weight}, {"value", item.value},
        {"quantity", item.quantity}, {"modifiers", modifiers}}.dump(2);
}

std::string Compendium::classToJson(const ClassDefinition& c)
{
    return json{{"id", c.id}, {"name", c.name}, {"description", c.description}, {"hitDie", c.hitDie}, {"speed", c.speed},
        {"darkvision", c.darkvision}, {"bonusHp", c.bonusHp}, {"proficiencies", c.proficiencies}, {"items", c.items},
        {"proficiencyRanks", c.proficiencyRanks}, {"dcAbility", c.dcAbility}}.dump(2);
}

std::string Compendium::creatureToJson(const CreatureDefinition& c)
{
    const Color k = c.token.color;
    return json{{"id", c.id}, {"name", c.name}, {"description", c.description}, {"hp", c.hp}, {"level", c.level}, {"armorClass", c.armorClass},
        {"speed", c.speed}, {"darkvision", c.darkvision}, {"abilities", c.abilities}, {"proficiencies", c.proficiencies}, {"items", c.items},
        {"token", {{"color", {k.r, k.g, k.b, k.a}}, {"size", c.token.size}, {"image", c.token.image}}},
        {"ai", json::parse(c.ai, nullptr, false)}, {"proficiencyRanks", c.proficiencyRanks}, {"dcAbility", c.dcAbility}}.dump(2);
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
        if (!AiProfile::fromJson(c->ai, &e, next.aiLookup())) return e;
        next.creatures[c->id] = std::move(*c);
        return {};
    });
    if (!creaturesOk)
        return false;
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

std::optional<Character> Compendium::makeCharacter(const Ruleset& rules, std::string_view classId, std::string name, Random& random) const
{
    const ClassDefinition* definition = characterClass(classId);
    if (!definition)
        return std::nullopt;
    Character c = makeRandomCharacter(rules, std::move(name), definition->name, random);
    c.hitDie = "1d" + std::to_string(definition->hitDie);
    c.stats.setBase("speed", static_cast<float>(definition->speed));
    c.stats.setBase("darkvision", static_cast<float>(definition->darkvision));
    const int hp = std::max(1, definition->hitDie + definition->bonusHp + c.abilityModifier(rules, "con"));
    c.stats.setBase("maxHp", static_cast<float>(hp));
    c.hp = hp;
    c.proficiencies = definition->proficiencies;
    c.proficiencyRanks = definition->proficiencyRanks;
    c.dcAbility = definition->dcAbility;
    if (!c.checkProficiencyRanks(rules)) return std::nullopt;
    giveItems(c, definition->items);
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
    if (!c.checkProficiencyRanks(rules)) return std::nullopt;
    giveItems(c, definition->items);
    // Stat blocks give the final AC, so creatures' gear shouldn't carry AC modifiers.
    c.stats.setBase("ac", static_cast<float>(abilityAdjustedArmor(rules, c, definition->armorClass)));
    return c;
}

}
