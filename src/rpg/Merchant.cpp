#include "yorehold/framework/rpg/Merchant.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace yh
{
namespace
{

int price(const Item& item, double multiplier, bool roundUp)
{
    const double value = item.value * multiplier;
    if (item.value <= 0 || !std::isfinite(value) || value <= 0 || value > std::numeric_limits<int>::max())
        return -1;
    const int result = static_cast<int>(roundUp ? std::ceil(value) : std::floor(value));
    return result > 0 ? result : -1;
}

bool refuse(std::string* error, const char* text)
{
    if (error) *error = text;
    return false;
}

int integer(const nlohmann::json& object, const char* key, int fallback, int maximum)
{
    if (!object.contains(key)) return fallback;
    const auto& value = object.at(key);
    if (!value.is_number_integer() || value < 0 || value > maximum)
        throw std::invalid_argument(std::string("merchant ") + key + " is outside its integer range");
    return value.get<int>();
}

void add(std::vector<Item>& inventory, Item item)
{
    item.quantity = 1;
    item.equipped = false;
    inventory.push_back(std::move(item));
}

// Removing an entry changes the sources of every later equipped item's modifiers.
void remove(Character& sheet, size_t index)
{
    if (--sheet.inventory[index].quantity > 0)
        return;
    std::vector<bool> worn;
    for (size_t i = index + 1; i < sheet.inventory.size(); i++)
    {
        worn.push_back(sheet.inventory[i].equipped);
        sheet.unequip(i);
    }
    sheet.inventory.erase(sheet.inventory.begin() + static_cast<std::ptrdiff_t>(index));
    for (size_t i = 0; i < worn.size(); i++)
        if (worn[i]) sheet.equip(index + i);
    sheet.hp = std::min(sheet.hp, sheet.maxHp());
}

}

int Merchant::buyPrice(const Item& item) const { return price(item, buyMultiplier, true); }
int Merchant::sellPrice(const Item& item) const { return price(item, sellMultiplier, false); }

bool Merchant::canBuy(const Character& buyer, size_t item, const Ruleset& rules, std::string* error) const
{
    if (error) error->clear();
    if (item >= inventory.size() || inventory[item].quantity <= 0) return refuse(error, "That item is out of stock.");
    const int cost = buyPrice(inventory[item]);
    if (cost < 0) return refuse(error, "That item has no selling price.");
    if (buyer.coins < cost) return refuse(error, "Not enough coins.");
    if (coins < 0 || coins > std::numeric_limits<int>::max() - cost) return refuse(error, "The merchant's purse is full.");
    if (inventory[item].magic && !buyer.roomForMagic(rules, 1)) return refuse(error, "Give up a magic item first.");
    return true;
}

bool Merchant::canSell(const Character& seller, size_t item, std::string* error) const
{
    if (error) error->clear();
    if (item >= seller.inventory.size() || seller.inventory[item].quantity <= 0) return refuse(error, "That item is no longer carried.");
    if (seller.inventory[item].equipped) return refuse(error, "Put that item away before selling it.");
    if (inventory.size() >= 10000) return refuse(error, "The merchant has no room for more stock.");
    const int cost = sellPrice(seller.inventory[item]);
    if (cost < 0) return refuse(error, "The merchant won't buy that item.");
    if (coins < cost) return refuse(error, "The merchant hasn't enough coins.");
    if (seller.coins < 0 || seller.coins > std::numeric_limits<int>::max() - cost) return refuse(error, "Your purse is full.");
    return true;
}

bool Merchant::buy(Character& buyer, size_t item, const Ruleset& rules)
{
    if (!canBuy(buyer, item, rules)) return false;
    const int cost = buyPrice(inventory[item]);
    add(buyer.inventory, inventory[item]);
    if (--inventory[item].quantity == 0)
        inventory.erase(inventory.begin() + static_cast<std::ptrdiff_t>(item));
    buyer.coins -= cost;
    coins += cost;
    return true;
}

bool Merchant::sell(Character& seller, size_t item)
{
    if (!canSell(seller, item)) return false;
    const int cost = sellPrice(seller.inventory[item]);
    add(inventory, seller.inventory[item]);
    remove(seller, item);
    seller.coins += cost;
    coins -= cost;
    return true;
}

std::optional<Merchant> Merchant::fromJson(std::string_view text, const ItemLookup& lookup, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = nlohmann::json::parse(text);
        if (!j.is_object()) throw std::invalid_argument("merchant must be an object");
        for (const auto& [key, value] : j.items())
            if (key != "coins" && key != "buyMultiplier" && key != "sellMultiplier" && key != "stock" && key != "inventory")
                throw std::invalid_argument("unknown merchant field: " + key);
        Merchant merchant;
        merchant.coins = integer(j, "coins", 0, std::numeric_limits<int>::max());
        merchant.buyMultiplier = j.value("buyMultiplier", 1.0);
        merchant.sellMultiplier = j.value("sellMultiplier", 0.5);
        if (merchant.coins < 0) throw std::invalid_argument("merchant coins can't be negative");
        if (!std::isfinite(merchant.buyMultiplier) || !std::isfinite(merchant.sellMultiplier) || merchant.buyMultiplier <= 0
            || merchant.sellMultiplier < 0 || merchant.sellMultiplier > merchant.buyMultiplier)
            throw std::invalid_argument("merchant multipliers need 0 <= sellMultiplier <= buyMultiplier and buyMultiplier > 0");
        if (j.contains("stock") && j.contains("inventory")) throw std::invalid_argument("merchant uses stock or inventory, not both");
        if (j.contains("inventory"))
        {
            if (!j.at("inventory").is_array() || j.at("inventory").size() > 10000) throw std::invalid_argument("merchant inventory must be a list");
            for (const auto& row : j.at("inventory"))
            {
                if (!row.is_object()) throw std::invalid_argument("merchant inventory entries are objects");
                integer(row, "quantity", 1, 1000000);
                integer(row, "value", 0, std::numeric_limits<int>::max());
            }
            const auto holder = Character::fromJson(nlohmann::json{{"inventory", j.at("inventory")}}.dump(), error);
            if (!holder) return std::nullopt;
            merchant.inventory = holder->inventory;
        }
        else
        {
            const auto stock = j.value("stock", nlohmann::json::array());
            if (!stock.is_array() || stock.size() > 10000) throw std::invalid_argument("merchant stock must be a list");
            for (const auto& row : stock)
            {
                if (!row.is_object()) throw std::invalid_argument("merchant stock entries are objects");
                for (const auto& [key, value] : row.items())
                    if (key != "item" && key != "quantity" && key != "value") throw std::invalid_argument("unknown merchant stock field: " + key);
                const std::string id = row.at("item").get<std::string>();
                const Item* definition = lookup ? lookup(id) : nullptr;
                if (!definition) throw std::invalid_argument("unknown merchant item: " + id);
                Item item = *definition;
                item.quantity = integer(row, "quantity", item.quantity, 1000000);
                item.value = integer(row, "value", item.value, std::numeric_limits<int>::max());
                item.equipped = false;
                merchant.inventory.push_back(std::move(item));
            }
        }
        for (const Item& item : merchant.inventory)
            if (item.id.empty() || item.quantity < 1 || item.quantity > 1000000 || item.equipped || item.value < 0
                || !std::isfinite(item.weight) || item.weight < 0)
                throw std::invalid_argument("invalid merchant inventory item");
        return merchant;
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

std::string Merchant::toJson() const
{
    Character holder;
    holder.inventory = inventory;
    return nlohmann::json{{"coins", coins}, {"buyMultiplier", buyMultiplier}, {"sellMultiplier", sellMultiplier},
        {"inventory", nlohmann::json::parse(holder.toJson()).at("inventory")}}.dump();
}

}
