#include "yorehold/framework/map/Regions.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>

namespace yh
{

std::string Region::toJson() const
{
    nlohmann::json j{{"id", id}, {"objects", nlohmann::json::parse(objects.toJson())}, {"variables", variables}, {"simulatedSeconds", simulatedSeconds}};
    if (map) j["map"] = nlohmann::json::parse(map->toJson());
    return j.dump();
}

std::unique_ptr<Region> Region::fromJson(std::string_view json, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = nlohmann::json::parse(json);
        auto region = std::make_unique<Region>();
        region->id = j.at("id").get<std::string>();
        if (region->id.empty()) throw std::invalid_argument("Empty region id");
        std::string why;
        auto objects = Objects::fromJson(j.at("objects").dump(), &why);
        if (!objects) throw std::invalid_argument(why);
        region->objects = std::move(*objects);
        if (j.contains("map"))
        {
            region->map = TileMap::fromJson(j.at("map").dump(), &why);
            if (!region->map) throw std::invalid_argument(why);
        }
        region->variables = j.value("variables", decltype(region->variables){});
        region->simulatedSeconds = j.value("simulatedSeconds", 0.0);
        if (!std::isfinite(region->simulatedSeconds) || region->simulatedSeconds < 0) throw std::invalid_argument("Invalid region clock");
        return region;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return nullptr; }
}

void Regions::add(std::string id, Loader loader)
{
    if (updating_) throw std::logic_error("Region membership cannot change during update");
    if (id.empty() || !loader || slots_.contains(id)) throw std::invalid_argument("Region needs a unique id and loader");
    slots_.try_emplace(std::move(id), Slot{std::move(loader), {}, {}, {}});
}

bool Regions::enter(int player, std::string_view id, std::string* error)
{
    if (error) error->clear();
    if (updating_) { if (error) *error = "Region membership cannot change during update"; return false; }
    const auto destination = slots_.find(id);
    if (destination == slots_.end()) { if (error) *error = "Unknown region"; return false; }
    const auto current = players_.find(player);
    if (current != players_.end() && current->second == id) return true;
    Slot& slot = destination->second;
    try
    {
        // Load before leaving, so a broken destination cannot strand a player.
        if (!slot.region)
        {
            auto region = slot.saved.empty() ? slot.loader(id) : Region::fromJson(slot.saved, error);
            if (!region) { if (error && error->empty()) *error = "Region loader failed"; return false; }
            if (region->id != id) throw std::invalid_argument("Region loader returned the wrong id");
            if (region->map) region->map->trackChanges();
            slot.region = std::move(region);
        }
        leave(player);
        slot.players.insert(player);
        players_[player] = std::string(id);
        return true;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return false; }
}

void Regions::leave(int player)
{
    if (updating_) throw std::logic_error("Region membership cannot change during update");
    const auto current = players_.find(player);
    if (current == players_.end()) return;
    Slot& slot = slots_.at(current->second);
    if (slot.players.size() == 1 && slot.region)
    {
        slot.saved = slot.region->toJson();
        slot.region.reset();
    }
    slot.players.erase(player);
    players_.erase(current);
}

Region* Regions::active(std::string_view id)
{
    const auto slot = slots_.find(id);
    return slot == slots_.end() ? nullptr : slot->second.region.get();
}

std::optional<std::string> Regions::playerRegion(int player) const
{
    const auto current = players_.find(player);
    return current == players_.end() ? std::nullopt : std::optional(current->second);
}

size_t Regions::activeCount() const
{
    size_t count = 0;
    for (const auto& [id, slot] : slots_) if (slot.region && !slot.players.empty()) ++count;
    return count;
}

void Regions::update(double dt, const Tick& tick)
{
    if (dt <= 0 || !std::isfinite(dt)) return;
    if (updating_) throw std::logic_error("Region update is not reentrant");
    updating_ = true;
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard{updating_};
    for (auto& [id, slot] : slots_)
        if (slot.region && !slot.players.empty())
        {
            slot.region->simulatedSeconds += dt;
            if (tick) tick(*slot.region, dt);
        }
}

std::string Regions::toJson() const
{
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [id, slot] : slots_)
    {
        const std::string snapshot = slot.region ? slot.region->toJson() : slot.saved;
        if (!snapshot.empty()) j[id] = nlohmann::json::parse(snapshot);
    }
    return j.dump();
}

bool Regions::restore(std::string_view json, std::string* error)
{
    if (error) error->clear();
    if (updating_) { if (error) *error = "Cannot restore regions during update"; return false; }
    if (!players_.empty()) { if (error) *error = "Players must leave before restoring the world"; return false; }
    try
    {
        const auto j = nlohmann::json::parse(json);
        if (!j.is_object()) throw std::invalid_argument("Region save must be an object");
        std::map<std::string, std::string> snapshots;
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            const auto slot = slots_.find(it.key());
            if (slot == slots_.end() || !slot->second.players.empty()) throw std::invalid_argument("Unknown or occupied saved region");
            std::string why;
            auto region = Region::fromJson(it.value().dump(), &why);
            if (!region || region->id != it.key()) throw std::invalid_argument(why.empty() ? "Saved region id mismatch" : why);
            snapshots[it.key()] = region->toJson();
        }
        for (auto& [id, slot] : slots_) { slot.saved.clear(); slot.region.reset(); }
        for (const auto& [id, snapshot] : snapshots) slots_.at(id).saved = snapshot;
        return true;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return false; }
}

std::string Regions::changesJson() const
{
    using J = nlohmann::json;
    J result = J::object();
    for (const auto& [id, slot] : slots_)
    {
        auto frozen = !slot.region && !slot.saved.empty() ? Region::fromJson(slot.saved) : nullptr;
        const Region* region = slot.region ? slot.region.get() : frozen.get();
        if (!region) continue;
        J state{{"id", id}, {"objects", J::parse(region->objects.toJson())}, {"variables", region->variables},
            {"simulatedSeconds", region->simulatedSeconds}};
        if (region->map) state["map"] = J::parse(region->map->changesJson());
        result[id] = std::move(state);
    }
    return result.dump();
}

bool Regions::restoreChanges(std::string_view json, std::string* error)
{
    if (error) error->clear();
    if (updating_) { if (error) *error = "Cannot restore regions during update"; return false; }
    if (!players_.empty()) { if (error) *error = "Players must leave before restoring the world"; return false; }
    try
    {
        const auto j = nlohmann::json::parse(json);
        if (!j.is_object()) throw std::invalid_argument("Region changes must be an object");
        std::map<std::string, std::string> snapshots;
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            const auto slot = slots_.find(it.key());
            if (slot == slots_.end() || !slot->second.players.empty()) throw std::invalid_argument("Unknown or occupied saved region");
            const auto& saved = it.value();
            if (saved.at("id").get<std::string>() != it.key()) throw std::invalid_argument("Saved region id mismatch");
            auto region = slot->second.loader(it.key());
            if (!region || region->id != it.key()) throw std::invalid_argument("Region loader failed");
            std::string why;
            if (saved.contains("map"))
            {
                if (!region->map) throw std::invalid_argument("Authored region has no map");
                region->map->clearChanges(); region->map->trackChanges();
                if (!region->map->applyChanges(saved.at("map").dump(), &why)) throw std::invalid_argument(why);
            }
            auto objects = Objects::fromJson(saved.at("objects").dump(), &why);
            if (!objects) throw std::invalid_argument(why);
            region->objects = std::move(*objects);
            region->variables = saved.value("variables", decltype(region->variables){});
            region->simulatedSeconds = saved.value("simulatedSeconds", 0.0);
            if (!std::isfinite(region->simulatedSeconds) || region->simulatedSeconds < 0) throw std::invalid_argument("Invalid region clock");
            snapshots[it.key()] = region->toJson();
        }
        for (auto& [id, slot] : slots_) { slot.saved.clear(); slot.region.reset(); }
        for (const auto& [id, snapshot] : snapshots) slots_.at(id).saved = snapshot;
        return true;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return false; }
}

}
