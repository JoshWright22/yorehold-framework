#pragma once

#include "yorehold/framework/rpg/Character.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

// Items that belong to a group rather than one character: a camp chest, a guild vault. Anyone in
// the group may put things in or take them out; who may do it when is the game's.
struct Stash
{
    std::vector<Item> items;

    // Carried-only items (no slot) stack with an identical entry; anything worn or held, or with a
    // use, stays its own entry. Whatever comes in is put away (not equipped).
    void add(Item item);
    // Takes `count` units of an entry (all of it when count is 0 or more than there are). Empty if
    // the index is out of range.
    std::optional<Item> take(size_t index, int count = 1);
    int supplies() const; // supply points held (Item::supplies times quantity)
    bool empty() const { return items.empty(); }

    // Items as in a character's inventory. A bad entry fails the whole stash, with the reason.
    std::string toJson() const;
    static std::optional<Stash> fromJson(std::string_view json, std::string* error = nullptr);
};

// Supply points in a stash and on some sheets (unequipped items only).
int supplyPoints(const Stash& stash, const std::vector<const Character*>& sheets);
// Uses up at least `points` supply points: from the stash first, then each sheet in order, the
// least valuable units first. Units are whole, so a cost that doesn't divide evenly takes a
// little more. Nothing changes and it returns false if there aren't enough.
bool spendSupplies(Stash& stash, const std::vector<Character*>& sheets, int points);

}
