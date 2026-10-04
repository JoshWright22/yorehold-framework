#pragma once

#include "yorehold/framework/graphics/Renderer.h"
#include "yorehold/framework/rpg/Character.h"
#include "yorehold/framework/rpg/Tactics.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

class FileSystem;

// A playable class: what a new character of it starts with.
struct ClassDefinition
{
    std::string id;   // "fighter"
    std::string name; // "Fighter"
    std::string description;
    int hitDie = 8;   // sides
    int speed = 30;   // feet
    int darkvision = 0; // feet seen without light; becomes the "darkvision" stat, so items can add to it
    int bonusHp = 0;  // added to first-level HP
    std::set<std::string> proficiencies;
    std::map<std::string, std::string> proficiencyRanks;
    std::string dcAbility;
    std::map<std::string, Resource> resources;
    std::vector<std::string> items; // item ids, equipped in order where a slot is free
};

// How a creature looks on the map. Players' art plugs in through `image`.
struct TokenLook
{
    Color color{150, 150, 160, 255};
    float size = 0.4f;  // radius in grid cells
    std::string image;  // virtual file path; empty = coloured disc with an initial
};

// A monster or NPC stat block. Unlisted abilities are rolled like a character's.
struct CreatureDefinition
{
    std::string id;   // "goblin"
    std::string name; // "Goblin"
    std::string description;
    int hp = 7;
    int level = 1;
    int armorClass = 12; // final AC, whatever the armour and DEX
    int speed = 30;
    int darkvision = 0; // feet
    std::map<std::string, int> abilities; // "dex": 14
    std::set<std::string> proficiencies;
    std::map<std::string, std::string> proficiencyRanks;
    std::string dcAbility;
    bool deathSaves = false;
    std::map<std::string, Resource> resources;
    std::vector<std::string> items;
    TokenLook token;
    // How it fights when the game plays it, as written in the file (JSON: a profile's name or an
    // object of changes, see AiProfile). Kept as text so it can be resolved again whenever the
    // profiles change. Compendium::aiFor() turns it into numbers.
    std::string ai = "\"cunning\"";
};

// Every class, item, creature and AI profile a game knows about, loaded from one JSON file each:
//   items/<id>.json, classes/<id>.json, creatures/<id>.json, ai/<id>.json
// Loading a second folder (a chapter's own content) adds to the set and replaces entries with
// the same id, so chapters can tweak shared monsters without copying everything.
class Compendium
{
public:
    std::map<std::string, Item> items;
    std::map<std::string, ClassDefinition> classes;
    std::map<std::string, CreatureDefinition> creatures;
    // AI profiles by name: the built-in four, plus (or replaced by) every ai/<id>.json. A file is
    // {"id": "coward", "base": "cunning", "fleeHp": 1}; without a base it starts from nothing.
    std::map<std::string, AiProfile, std::less<>> ai;

    Compendium();
    AiProfile::Lookup aiLookup() const; // valid while this compendium is alive and unchanged
    // A creature's AI as numbers; the cunning preset if its text no longer resolves.
    AiProfile aiFor(const CreatureDefinition& creature) const;

    // Reads every .json under `folder`/items, /classes, /ai and /creatures. All-or-nothing: on any
    // error nothing is added and `error` names the file. Items are checked first, so classes and
    // creatures may only list items that exist (here or loaded before).
    bool load(const FileSystem& files, std::string_view folder, std::string* error = nullptr);

    const Item* item(std::string_view id) const;
    const ClassDefinition* characterClass(std::string_view id) const;
    const CreatureDefinition* creature(std::string_view id) const;

    // A first-level character of a class: rolled abilities, HP = hit die + CON + bonus, the
    // class's gear equipped. Returns nullopt for an unknown class.
    std::optional<Character> makeCharacter(const Ruleset& rules, std::string_view classId, std::string name, Random& random) const;
    // A creature from its stat block; `name` overrides the definition's (for "Snik the goblin").
    std::optional<Character> makeCreature(const Ruleset& rules, std::string_view creatureId, std::string name, Random& random) const;

    static std::optional<Item> itemFromJson(std::string_view json, std::string* error = nullptr);
    static std::optional<ClassDefinition> classFromJson(std::string_view json, std::string* error = nullptr);
    static std::optional<CreatureDefinition> creatureFromJson(std::string_view json, std::string* error = nullptr);
    static std::string itemToJson(const Item& item);
    static std::string classToJson(const ClassDefinition& definition);
    static std::string creatureToJson(const CreatureDefinition& definition);

private:
    void giveItems(Character& character, const std::vector<std::string>& ids) const;
};

}
