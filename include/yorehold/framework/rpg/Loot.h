#pragma once

#include "yorehold/framework/rpg/Random.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yh
{

// One thing a loot table may hold: an item id, how likely it is to be there, and how many.
struct LootEntry
{
    std::string item;
    float chance = 1; // 0 to 1
    int quantity = 1;
};

// What a creature leaves behind, or a container holds, beyond the items it lists outright: coins
// as dice and items that are each there by chance. JSON:
//   {"coins": "2d6", "items": ["torch", {"item": "dagger", "chance": 0.25, "quantity": 2}]}
// Coins are counted in the game's smallest coin; "coins" may also be a plain number.
struct LootTable
{
    std::string coins; // dice ("2d6", "40"); empty = none
    std::vector<LootEntry> items;

    bool empty() const { return coins.empty() && items.empty(); }
    std::string toJson() const;
    // Unknown fields, bad dice, chances outside 0 to 1 and quantities outside 1 to 1000 are
    // refused with the field named. Item ids are checked by whoever knows the items.
    static std::optional<LootTable> fromJson(std::string_view json, std::string* error = nullptr);
};

// What a table gave this time: coins, and item ids with how many of each.
struct LootRoll
{
    int coins = 0;
    std::vector<std::pair<std::string, int>> items;
};

// Rolls the coins, then each entry in order, so the same seed finds the same things.
LootRoll rollLoot(const LootTable& table, Random& random);

}
