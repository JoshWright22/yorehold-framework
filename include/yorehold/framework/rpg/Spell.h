#pragma once

#include "yorehold/framework/rpg/Action.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

// A spell as data: an action (what it costs, who it is aimed at, the area, the save, the effects)
// plus what makes it a spell. One JSON object each, a file of its own in a ruleset's spells/.
struct SpellDefinition
{
    int level = 0;  // 0 = a cantrip, which spends no slot
    int hands = 1;  // hands the casting needs; also its cost in actions unless the file gives a cost
    bool concentration = false; // what it leaves on creatures lasts only while the caster concentrates
    std::vector<std::string> spends; // resource ids it costs in addition to slots (empty = none)
    ActionDefinition action;    // id, name, description, target, area, save and effects
    std::string json;           // as it was read, in canonical form (for content signatures)

    const std::string& id() const { return action.id; }
    const std::string& name() const { return action.name; }
    // A problem names the field it is in.
    static std::optional<SpellDefinition> fromJson(std::string_view json, std::string* error = nullptr);
};

// How a ruleset casts: every choice here is one a table might make differently.
struct SpellRules
{
    enum class Hands
    {
        Free,    // the spell's hands must be empty: a shield or a weapon in them is in the way
        Ignored, // hands only set the cost
    };
    enum class Damage
    {
        Save,    // damage forces a save; failing it ends the spell
        Breaks,  // any damage ends it
        Ignored, // damage does not touch it
    };

    Hands hands = Hands::Free;
    std::string slotPrefix = "slots-"; // a slot of level N is the resource `slotPrefix` + N
    bool upcast = true;                // a spell may spend a slot of a higher level than its own

    Damage onDamage = Damage::Save;
    std::string saveAbility = "con";
    int minimumDc = 10;       // the save's DC is the larger of this and...
    float damageShare = 0.5f; // ...this share of the damage taken, rounded down
    bool endsWhenDown = true; // dropping to 0 HP ends it

    std::vector<std::string> prepareAfter; // rest ids after which prepared casters can re-choose their spells; empty = never (default)

    int concentrationDc(int damage) const;
    std::string toJson() const;
    // Unknown fields are refused with the field named.
    static std::optional<SpellRules> fromJson(std::string_view json, std::string* error = nullptr);
    // What only a ruleset can tell: the save's ability exists.
    bool check(const Ruleset& rules, std::string* error = nullptr) const;
};

// The slot level a casting would spend: 0 for a cantrip, otherwise the lowest level from the
// spell's own that still has a slot (only its own without upcasting). `wanted` above 0 asks for
// that level exactly. Empty if there is none to spend.
std::optional<int> slotFor(const Character& caster, const SpellDefinition& spell, const SpellRules& rules, int wanted = 0);
// The caster has a slot for it and, where the rules ask, the hands free. `why` gets a short reason if not.
bool canCast(const Character& caster, const SpellDefinition& spell, const SpellRules& rules, std::string* why = nullptr);
// Spends a slot of that level and any custom resources the spell costs. Level 0 spends nothing.
void spendCasting(Character& caster, const SpellDefinition& spell, const SpellRules& rules, int slot);
// Takes one slot of that level off the sheet. Level 0 spends nothing. False if there was none.
bool spendSlot(Character& caster, const SpellRules& rules, int slot);

// What a caster is holding in place by concentrating: the conditions and modifiers one casting
// left on creatures. A caster concentrates on one spell at a time; the game keeps one of these
// per caster and ends it when another begins, when damage breaks it or when nothing is left.
struct Concentration
{
    struct Hold
    {
        EffectActor who = -1;
        std::string id; // what Character::removeCondition takes off
        bool operator==(const Hold&) const = default;
    };
    std::string spell; // empty = not concentrating
    std::vector<Hold> holds;

    bool active() const { return !spell.empty(); }
    // Starts concentrating on what `result` left behind.
    static Concentration begin(std::string spell, const EffectResult& result);
    // The game's sheet for one of its creatures; null if there is none.
    using Sheets = std::function<Character*(EffectActor)>;
    // Takes everything it held off the sheets and stops. Returns what came off.
    std::vector<Hold> end(const Sheets& sheets);
    // Forgets holds that are no longer on their sheets (they ran out, or something removed them)
    // and stops once none are left. True while still concentrating.
    bool tidy(const Sheets& sheets);

    std::string toJson() const;
    static std::optional<Concentration> fromJson(std::string_view json, std::string* error = nullptr);
};

// The check a concentrating caster makes on taking damage.
struct ConcentrationCheck
{
    bool rolled = false; // a save was made (`roll` and `dc` say how it went)
    bool kept = true;
    RollResult roll;
    int dc = 0;
};
ConcentrationCheck concentrationCheck(const Character& caster, const Ruleset& rules, const SpellRules& spells, int damage, Random& random);

}
