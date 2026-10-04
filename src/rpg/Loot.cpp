#include "yorehold/framework/rpg/Loot.h"

#include "yorehold/framework/rpg/Dice.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

[[noreturn]] void fail(const std::string& field, const std::string& what)
{
    throw std::invalid_argument(field + ": " + what);
}

}

std::string LootTable::toJson() const
{
    json j = json::object();
    if (!coins.empty()) j["coins"] = coins;
    if (!items.empty())
    {
        j["items"] = json::array();
        for (const LootEntry& entry : items)
            j["items"].push_back({{"item", entry.item}, {"chance", entry.chance}, {"quantity", entry.quantity}});
    }
    return j.dump();
}

std::optional<LootTable> LootTable::fromJson(std::string_view text, std::string* error)
{
    if (error) error->clear();
    try
    {
        const json j = json::parse(text);
        if (!j.is_object()) fail("loot", "is an object with coins and items");
        for (auto it = j.begin(); it != j.end(); ++it)
            if (it.key() != "coins" && it.key() != "items") fail("loot." + it.key(), "unknown field");
        LootTable table;
        if (j.contains("coins"))
        {
            const json& coins = j.at("coins");
            if (coins.is_number_integer() && coins.get<long long>() >= 0 && coins.get<long long>() <= 100000000)
                table.coins = std::to_string(coins.get<int>());
            else if (coins.is_string() && DiceExpression::parse(coins.get<std::string>()))
                table.coins = coins.get<std::string>();
            else
                fail("loot.coins", "is a number from 0 or dice like \"2d6\"");
            if (table.coins == "0") table.coins.clear();
        }
        if (j.contains("items"))
        {
            if (!j.at("items").is_array() || j.at("items").size() > 1000) fail("loot.items", "is a list of item ids or entries");
            for (size_t i = 0; i < j.at("items").size(); i++)
            {
                const json& entry = j.at("items").at(i);
                const std::string path = "loot.items[" + std::to_string(i) + "]";
                LootEntry item;
                if (entry.is_string())
                    item.item = entry.get<std::string>();
                else if (entry.is_object())
                {
                    for (auto it = entry.begin(); it != entry.end(); ++it)
                        if (it.key() != "item" && it.key() != "chance" && it.key() != "quantity") fail(path + "." + it.key(), "unknown field");
                    if (!entry.contains("item") || !entry.at("item").is_string()) fail(path + ".item", "is an item id");
                    item.item = entry.at("item").get<std::string>();
                    if (entry.contains("chance"))
                    {
                        if (!entry.at("chance").is_number()) fail(path + ".chance", "is a number from 0 to 1");
                        item.chance = entry.at("chance").get<float>();
                    }
                    if (entry.contains("quantity"))
                    {
                        if (!entry.at("quantity").is_number_integer()) fail(path + ".quantity", "is a whole number from 1 to 1000");
                        item.quantity = entry.at("quantity").get<int>();
                    }
                }
                else
                    fail(path, "is an item id or an object with one");
                if (item.item.empty() || item.item.size() > 64) fail(path + ".item", "is an item id");
                if (!std::isfinite(item.chance) || item.chance < 0 || item.chance > 1) fail(path + ".chance", "is a number from 0 to 1");
                if (item.quantity < 1 || item.quantity > 1000) fail(path + ".quantity", "is a whole number from 1 to 1000");
                table.items.push_back(std::move(item));
            }
        }
        return table;
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

LootRoll rollLoot(const LootTable& table, Random& random)
{
    LootRoll found;
    if (!table.coins.empty())
        found.coins = std::max(0, roll(table.coins, random).total);
    for (const LootEntry& entry : table.items)
    {
        // A certain entry takes no roll, so adding one doesn't move the dice of the others.
        if (entry.chance < 1 && random.range(0, 9999) >= static_cast<int>(entry.chance * 10000))
            continue;
        found.items.emplace_back(entry.item, entry.quantity);
    }
    return found;
}

}
