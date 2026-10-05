#include "yorehold/framework/rpg/Stash.h"

#include "yorehold/framework/rpg/Compendium.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace yh
{

namespace
{

// Two entries are the same thing when everything but the count matches.
bool sameThing(const Item& a, const Item& b)
{
    Item x = a, y = b;
    x.quantity = y.quantity = 1;
    x.equipped = y.equipped = false;
    return Compendium::itemToJson(x) == Compendium::itemToJson(y);
}

// Where a supply unit sits: the stash (sheet -1) or a sheet's inventory.
struct SupplyUnit
{
    int sheet = -1;
    size_t index = 0;
    int points = 0;
};

}

void Stash::add(Item item)
{
    item.equipped = false;
    if (item.quantity < 1)
        return;
    if (item.slot.empty() && !item.use)
        for (Item& have : items)
            if (have.id == item.id && sameThing(have, item))
            {
                have.quantity += item.quantity;
                return;
            }
    items.push_back(std::move(item));
}

std::optional<Item> Stash::take(size_t index, int count)
{
    if (index >= items.size())
        return std::nullopt;
    Item& have = items[index];
    if (count <= 0 || count >= have.quantity)
    {
        Item all = std::move(have);
        items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
        return all;
    }
    Item part = have;
    part.quantity = count;
    have.quantity -= count;
    return part;
}

int Stash::supplies() const
{
    int points = 0;
    for (const Item& item : items)
        points += item.supplies * std::max(0, item.quantity);
    return points;
}

std::string Stash::toJson() const
{
    // Items are written exactly as a sheet's inventory writes them.
    Character holder;
    holder.inventory = items;
    nlohmann::json j = nlohmann::json::parse(holder.toJson());
    return nlohmann::json{{"items", j.value("inventory", nlohmann::json::array())}}.dump();
}

std::optional<Stash> Stash::fromJson(std::string_view json, std::string* error)
{
    if (error) error->clear();
    const nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.value("items", nlohmann::json::array()).is_array())
    {
        if (error) *error = "a stash is {\"items\": [...]}";
        return std::nullopt;
    }
    const nlohmann::json list = j.value("items", nlohmann::json::array());
    if (list.size() > 10000)
    {
        if (error) *error = "too many items in the stash";
        return std::nullopt;
    }
    std::optional<Character> holder = Character::fromJson(nlohmann::json{{"inventory", list}}.dump(), error);
    if (!holder)
        return std::nullopt;
    Stash stash;
    for (Item& item : holder->inventory)
    {
        if (item.id.empty() || item.quantity < 1)
        {
            if (error) *error = "a stash item needs an id and a quantity of at least 1";
            return std::nullopt;
        }
        item.equipped = false;
        stash.items.push_back(std::move(item));
    }
    return stash;
}

int supplyPoints(const Stash& stash, const std::vector<const Character*>& sheets)
{
    int points = stash.supplies();
    for (const Character* sheet : sheets)
        if (sheet)
            for (const Item& item : sheet->inventory)
                if (!item.equipped)
                    points += item.supplies * std::max(0, item.quantity);
    return points;
}

bool spendSupplies(Stash& stash, const std::vector<Character*>& sheets, int points)
{
    if (points <= 0)
        return true;
    std::vector<const Character*> view(sheets.begin(), sheets.end());
    if (supplyPoints(stash, view) < points)
        return false;

    // Pick whole units, stash first, then sheet by sheet; in each place the cheapest units go first
    // so a big meal isn't spent where scraps would do.
    std::vector<SupplyUnit> plan;
    auto pickFrom = [&](const std::vector<Item>& list, int sheet) {
        std::vector<size_t> order;
        for (size_t i = 0; i < list.size(); i++)
            if (list[i].supplies > 0 && !list[i].equipped && list[i].quantity > 0)
                order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return list[a].supplies < list[b].supplies; });
        for (size_t i : order)
            for (int n = 0; n < list[i].quantity && points > 0; n++)
            {
                plan.push_back({sheet, i, list[i].supplies});
                points -= list[i].supplies;
            }
    };
    pickFrom(stash.items, -1);
    for (size_t s = 0; s < sheets.size() && points > 0; s++)
        if (sheets[s])
            pickFrom(sheets[s]->inventory, static_cast<int>(s));

    // Take them out from the back so the indexes stay good.
    std::stable_sort(plan.begin(), plan.end(), [](const SupplyUnit& a, const SupplyUnit& b) {
        return a.sheet != b.sheet ? a.sheet < b.sheet : a.index > b.index;
    });
    for (const SupplyUnit& unit : plan)
    {
        if (unit.sheet < 0)
            stash.take(unit.index, 1);
        else
            sheets[static_cast<size_t>(unit.sheet)]->removeItem(unit.index);
    }
    return true;
}

}
