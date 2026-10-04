#pragma once

#include "yorehold/framework/rpg/Character.h"

#include <functional>
#include <string_view>

namespace yh
{

// A finite stock and purse. Prices are per unit; every trade moves one unit, never a whole stack.
struct Merchant
{
    int coins = 0;
    double buyMultiplier = 1;
    double sellMultiplier = 0.5;
    std::vector<Item> inventory;

    // -1 means no price (worthless items or a price outside the currency's range).
    int buyPrice(const Item& item) const;
    int sellPrice(const Item& item) const;
    bool canBuy(const Character& buyer, size_t item, const Ruleset& rules, std::string* error = nullptr) const;
    bool canSell(const Character& seller, size_t item, std::string* error = nullptr) const;
    bool buy(Character& buyer, size_t item, const Ruleset& rules);
    bool sell(Character& seller, size_t item);

    using ItemLookup = std::function<const Item*(std::string_view)>;
    // Content uses stock:[{item,quantity,value?}]; saves use full inventory entries instead.
    static std::optional<Merchant> fromJson(std::string_view text, const ItemLookup& lookup, std::string* error = nullptr);
    std::string toJson() const;
};

}
