#include "yorehold/framework/rpg/Companions.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace yh
{

namespace
{

constexpr int approvalBound = 1000000; // far past any range a ruleset needs

CompanionDefinition definitionFrom(const nlohmann::json& j)
{
    if (!j.is_object()) throw std::invalid_argument("a companion must be an object");
    CompanionDefinition d;
    d.id = j.at("id").get<std::string>();
    if (d.id.empty()) throw std::invalid_argument("a companion needs an id");
    d.approval = j.value("approval", 0);
    d.joinAt = j.value("joinAt", 0);
    if (j.contains("leaveAt") && !j.at("leaveAt").is_null())
        d.leaveAt = j.at("leaveAt").get<int>();
    const nlohmann::json flags = j.value("flags", nlohmann::json::object());
    if (!flags.is_object()) throw std::invalid_argument("companion " + d.id + ": flags must be an object of flag: change");
    for (const auto& [flag, change] : flags.items())
    {
        if (flag.empty() || !change.is_number_integer()) throw std::invalid_argument("companion " + d.id + ": flags must be an object of flag: change");
        d.flags.emplace_back(flag, change.get<int>());
    }
    auto bad = [](int value) { return value < -approvalBound || value > approvalBound; };
    if (bad(d.approval) || bad(d.joinAt) || (d.leaveAt && bad(*d.leaveAt)))
        throw std::invalid_argument("companion " + d.id + ": approval numbers are out of range");
    return d;
}

nlohmann::json definitionJson(const CompanionDefinition& d)
{
    nlohmann::json j{{"id", d.id}, {"approval", d.approval}, {"joinAt", d.joinAt}};
    if (d.leaveAt) j["leaveAt"] = *d.leaveAt;
    if (!d.flags.empty())
    {
        j["flags"] = nlohmann::json::object();
        for (const auto& [flag, change] : d.flags)
            j["flags"][flag] = change;
    }
    return j;
}

}

std::optional<CompanionDefinition> CompanionDefinition::fromJson(std::string_view json, std::string* error)
{
    try
    {
        return definitionFrom(nlohmann::json::parse(json));
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

std::string CompanionDefinition::toJson() const
{
    return definitionJson(*this).dump();
}

void Companions::define(CompanionDefinition definition)
{
    approval_.try_emplace(definition.id, definition.approval);
    const auto same = std::find_if(definitions_.begin(), definitions_.end(), [&](const CompanionDefinition& d) { return d.id == definition.id; });
    if (same != definitions_.end())
        *same = std::move(definition);
    else
        definitions_.push_back(std::move(definition));
}

const CompanionDefinition* Companions::definition(std::string_view id) const
{
    const auto found = std::find_if(definitions_.begin(), definitions_.end(), [&](const CompanionDefinition& d) { return d.id == id; });
    return found != definitions_.end() ? &*found : nullptr;
}

int Companions::approval(std::string_view id) const
{
    const auto found = approval_.find(id);
    return found != approval_.end() ? found->second : 0;
}

bool Companions::setApproval(const CompanionRules& rules, std::string_view id, int value)
{
    const CompanionDefinition* d = definition(id);
    if (!d)
        return false;
    const int now = std::clamp(value, rules.approvalMin, rules.approvalMax);
    approval_.insert_or_assign(std::string(id), now);
    if (d->leaveAt && now <= *d->leaveAt && member(id))
        return leave(id);
    return false;
}

bool Companions::adjust(const CompanionRules& rules, std::string_view id, int delta)
{
    const long long sum = static_cast<long long>(approval(id)) + delta;
    return setApproval(rules, id, static_cast<int>(std::clamp<long long>(sum, -approvalBound, approvalBound)));
}

CompanionJoin Companions::canJoin(const CompanionRules& rules, std::string_view id, int others) const
{
    const CompanionDefinition* d = definition(id);
    if (!d)
        return CompanionJoin::Unknown;
    if (member(id))
        return CompanionJoin::Already;
    if (approval(id) < d->joinAt)
        return CompanionJoin::LowApproval;
    const int count = static_cast<int>(members_.size());
    if ((rules.limit > 0 && count >= rules.limit) || (rules.partyLimit > 0 && others + count >= rules.partyLimit))
        return CompanionJoin::Full;
    return CompanionJoin::Joined;
}

CompanionJoin Companions::join(const CompanionRules& rules, std::string_view id, int others)
{
    const CompanionJoin result = canJoin(rules, id, others);
    if (result == CompanionJoin::Joined)
        members_.emplace_back(id);
    return result;
}

bool Companions::leave(std::string_view id)
{
    const auto found = std::find(members_.begin(), members_.end(), id);
    if (found == members_.end())
        return false;
    members_.erase(found);
    return true;
}

bool Companions::member(std::string_view id) const
{
    return std::find(members_.begin(), members_.end(), id) != members_.end();
}

std::vector<std::string> Companions::flagsSet(const CompanionRules& rules, const std::set<std::string>& flags)
{
    std::vector<std::string> left;
    for (const CompanionDefinition& d : definitions_)
        for (const auto& [flag, change] : d.flags)
            if (flags.contains(flag) && counted_.emplace(d.id, flag).second && adjust(rules, d.id, change))
                left.push_back(d.id);
    return left;
}

std::string Companions::toJson() const
{
    nlohmann::json j{{"definitions", nlohmann::json::array()}, {"approval", nlohmann::json::object()},
        {"members", members_}, {"counted", nlohmann::json::array()}};
    for (const CompanionDefinition& d : definitions_)
        j["definitions"].push_back(definitionJson(d));
    for (const auto& [id, value] : approval_)
        j["approval"][id] = value;
    for (const auto& [id, flag] : counted_)
        j["counted"].push_back(nlohmann::json::array({id, flag}));
    return j.dump();
}

std::optional<Companions> Companions::fromJson(std::string_view json, std::string* error)
{
    try
    {
        const nlohmann::json j = nlohmann::json::parse(json);
        if (!j.is_object()) throw std::invalid_argument("companions must be an object");
        Companions c;
        for (const nlohmann::json& d : j.value("definitions", nlohmann::json::array()))
        {
            CompanionDefinition read = definitionFrom(d);
            if (c.definition(read.id)) throw std::invalid_argument("companion " + read.id + " is defined twice");
            c.define(std::move(read));
        }
        const nlohmann::json approval = j.value("approval", nlohmann::json::object());
        for (const auto& [id, value] : approval.items())
        {
            if (!c.definition(id)) throw std::invalid_argument("approval for an unknown companion " + id);
            c.approval_[id] = value.get<int>();
        }
        for (const std::string& id : j.value("members", std::vector<std::string>{}))
        {
            if (!c.definition(id) || c.member(id)) throw std::invalid_argument("unknown or repeated member " + id);
            c.members_.push_back(id);
        }
        for (const nlohmann::json& entry : j.value("counted", nlohmann::json::array()))
            c.counted_.emplace(entry.at(0).get<std::string>(), entry.at(1).get<std::string>());
        return c;
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

}
