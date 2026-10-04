#pragma once

#include "yorehold/framework/rpg/Effect.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yh
{

class FileSystem;

// Something a creature can do on its turn, as data: what it costs, what it needs, who it is aimed
// at and what it does. One JSON object each, usually a file of its own (see loadActions).
struct ActionDefinition
{
    enum class Target
    {
        Self,     // nothing to aim: it is about whoever does it
        Creature, // one creature within `range`
    };
    enum class Side { Any, Enemy, Ally };

    std::string id;
    std::string name;
    std::string description;
    int order = 0; // where it comes in a list of actions, lowest first

    int cost = 1;            // actions it takes; 0 = free
    bool costsHands = false; // instead: one action per hand the weapon in use needs
    bool endsTurn = false;   // the turn is over once it is done
    bool general = true;     // every creature has it; otherwise something has to grant it

    // What whoever does it needs: condition flags it must have, flags that bar it, and resources
    // it must hold at least this much of.
    std::vector<std::string> needsFlags;
    std::vector<std::string> barredBy;
    std::vector<std::pair<std::string, int>> needsResources;

    Target target = Target::Self;
    Side side = Side::Enemy; // who a creature target may be
    int range = 1;           // squares; 1 = next to it

    std::string log; // a line for the game's log when it is done; "{name}" is whoever does it
    Effect effect;
    std::string json; // the definition as it was read, in canonical form (for content signatures)

    // Actions it takes this creature (see costsHands), never more than a turn has.
    int costFor(const Character& character, const Ruleset& rules) const;
    // The creature has what it needs. `why` gets a short reason if not.
    bool meets(const Character& character, const Ruleset& rules, std::string* why = nullptr) const;
    // A problem names the field it is in.
    static std::optional<ActionDefinition> fromJson(std::string_view json, std::string* error = nullptr);
};

// The three actions any turn-based fight has, for rulesets that bring no files of their own:
// "strike" (a weapon attack on a creature next to you, costing what the ruleset says a strike
// costs), "stride" (an action for your speed in movement again) and "end-turn".
std::vector<ActionDefinition> basicActions(const Ruleset& rules);

// Adds every `folder`/<id>.json to `actions`, replacing any with the same id, and sorts them by
// `order`. The file name is the id. All-or-nothing: on an error nothing changes and `error` names
// the file. What the effects name (conditions, abilities) is checked against `rules`.
bool loadActions(const FileSystem& files, std::string_view folder, const Ruleset& rules, std::vector<ActionDefinition>& actions,
    std::string* error = nullptr);

const ActionDefinition* findAction(const std::vector<ActionDefinition>& actions, std::string_view id);

}
