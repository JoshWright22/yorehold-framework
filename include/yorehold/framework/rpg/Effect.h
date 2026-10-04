#pragma once

#include "yorehold/framework/rpg/Character.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

// Whoever an effect is done by or to: the host's own number for a creature.
using EffectActor = int;

// How a step grows with the level of whoever does it, or with the slot it was cast from: once for
// each `every` levels above `from`.
struct EffectScale
{
    enum class By { None, Level, Slot };
    By by = By::None;
    int from = 1;
    int every = 1;
    std::string dice; // added to a rolled amount each time ("1d6")
    int value = 0;    // added to an amount, a condition's value or a count each time
};

// One thing an effect does. Which fields matter depends on `kind`; FRAMEWORK.md lists them.
struct EffectStep
{
    enum class Kind
    {
        Damage, Heal, TempHp, Condition, Modifier, Move, Resource, Summon, Light, Surface, Flag,
        Roll,   // an attack, check or save whose result gates the steps under it
        Repeat, // the steps under it, several times
        Choose, // one of several lists of steps, picked through the host
    };
    enum class OnSave
    {
        Full, // the save changes nothing for this step
        Half, // a successful save halves the amount
        None, // a successful save stops it
    };

    Kind kind = Kind::Damage;
    std::string target = "target"; // self, target, area, allies, enemies
    std::string when;              // empty = always; otherwise a result (hit, saveFailed...) or an event (turnStart...)
    OnSave onSave = OnSave::Full;
    EffectScale scale;

    std::string amount; // dice, a number, or a name: "weapon" (the doer's weapon damage), "speed" (its speed in squares)
    std::string id;     // the condition, resource, creature, surface or flag; for a modifier, what it is tracked as
    std::string type;   // damage type
    bool remove = false; // condition and flag: take it off instead
    int duration = Character::definedDuration; // rounds; -1 = until something ends it
    int value = 1;       // a condition's value
    Modifier modifier;
    std::string how;     // move: push, pull, teleport; roll: attack, check, save; resource: spend, restore
    std::string ability; // roll: the ability or skill rolled
    std::string against; // check: the target's passive score in this skill or ability is the DC
    int dc = 10;
    bool casterDc = false; // the DC comes from whoever does it (EffectContext::dc)
    bool critDoubles = true; // damage: a critical hit rolls the dice twice
    int minimum = 0;         // damage: never less than this before a save halves it
    float size = 0;          // light radius or surface size, in squares
    std::vector<EffectStep> steps;                // roll, repeat
    std::vector<std::string> optionNames;         // choose
    std::vector<std::vector<EffectStep>> options; // choose: the steps of each name
};

struct EffectContext
{
    const Ruleset* rules = nullptr;
    Random* random = nullptr;
    EffectActor self = -1;             // who does it
    std::vector<EffectActor> targets;  // who it was aimed at
    std::string source;                // what it is called, for modifiers that name no id
    // Empty for something done now. With an event ("turnStart"...), only the steps that wait for
    // that event run.
    std::string event;
    int level = 0; // 0 = the level on the doer's sheet
    int slot = 0;  // the slot it was cast from, for steps that scale by slot
    int dc = 10;   // the doer's DC, for saves and checks that ask for "caster"
};

// What a step did, in order, for the game to show.
struct EffectEvent
{
    enum class Kind
    {
        Attack, Check, Save,
        Damage, Heal, TempHp,
        ConditionAdded, ConditionRemoved,
        ConditionEnded, // one that was listening for what just happened (being hit, attacking, healed)
        Modifier, Move, Resource, Summon, Light, Surface, Flag, Choice,
    };
    Kind kind = Kind::Damage;
    EffectActor who = -1; // who it happened to (for a roll: who it was made against, or who made the save)
    EffectActor by = -1;  // who did it
    RollResult roll;
    int amount = 0;        // damage dealt, HP gained, the condition's value, the resource change, the choice made
    int dc = 0;            // the armour class or DC rolled against
    bool success = false;  // the attack hit, the check passed, the save was made
    bool critical = false;
    bool dropped = false;  // the damage took them to 0
    std::string id;        // the damage type, condition, resource, flag, ability rolled...
};

struct EffectResult
{
    std::vector<EffectEvent> events;
};

// What an effect needs from the game. Sheets are changed directly; anything that touches the map
// goes through here, and a host that leaves a hook alone simply does not support that step.
class EffectHost
{
public:
    virtual ~EffectHost() = default;
    // The sheet of a creature; null if there is none (the step skips it).
    virtual Character* sheet(EffectActor who) = 0;
    // Who "area", "allies" or "enemies" means for this effect.
    virtual std::vector<EffectActor> group(std::string_view which, const EffectContext& context) = 0;
    // Takes `amount` of damage of `type` off someone and returns what was dealt. The default takes
    // it off the sheet as it is; a game with resistances overrides it.
    virtual int damage(EffectActor who, int amount, std::string_view type, const EffectContext& context);
    // Spends (negative) or restores a resource. The default uses the sheet's resources and fails
    // if it has none by that name.
    virtual bool resource(EffectActor who, std::string_view id, int change, const EffectContext& context);
    virtual bool move(EffectActor /*who*/, std::string_view /*how*/, int /*squares*/, const EffectContext&) { return false; }
    virtual bool summon(std::string_view /*creature*/, int /*count*/, int /*rounds*/, const EffectContext&) { return false; }
    virtual bool light(EffectActor /*who*/, float /*radius*/, int /*rounds*/, const EffectContext&) { return false; }
    virtual bool surface(std::string_view /*id*/, float /*size*/, int /*rounds*/, const EffectContext&) { return false; }
    virtual bool flag(std::string_view /*name*/, bool /*set*/, const EffectContext&) { return false; }
    // Which of the named options a "choose" step takes.
    virtual size_t choose(const std::vector<std::string>& /*options*/, const EffectContext&) { return 0; }
};

// A list of steps, and the save that goes with them: what a spell, an action, an item, a trap or
// a feature does. Read from JSON, checked there, and run against an EffectHost.
struct Effect
{
    struct Save
    {
        std::string ability; // empty = no save
        int dc = 10;
        bool casterDc = false;
    };
    Save save;
    std::vector<EffectStep> steps;

    bool empty() const { return steps.empty(); }
    // Either a list of steps, or an object with `effects` and an optional `save`. A problem names
    // the field it is in ("effects[1].dice: ...").
    static std::optional<Effect> fromJson(std::string_view json, std::string* error = nullptr);
    // What only a ruleset can tell: the conditions, abilities and skills it names exist.
    bool check(const Ruleset& rules, std::string* error = nullptr) const;
    // Runs the steps in order. `context.rules` and `context.random` must be set.
    EffectResult run(EffectHost& host, const EffectContext& context) const;
};

// The names a step's `when` may use. Results come from rolls made earlier in the same effect;
// the events are the condition events (see conditionEvents).
inline constexpr const char* effectResults[] = {
    "hit", "miss", "crit", "saveFailed", "saveSucceeded", "success", "failure",
};

}
