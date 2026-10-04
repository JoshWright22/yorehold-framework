#include "yorehold/framework/rpg/Dialogue.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace yh
{

namespace
{

using Json = nlohmann::json;

bool fail(std::string* error, std::string message)
{
    if (error) *error = std::move(message);
    return false;
}

bool validFlags(const std::vector<std::string>& flags)
{
    std::set<std::string> seen;
    return std::all_of(flags.begin(), flags.end(), [&](const auto& flag) {
        return !flag.empty() && seen.insert(flag).second;
    });
}

bool overlap(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    return std::any_of(a.begin(), a.end(), [&](const auto& flag) {
        return std::find(b.begin(), b.end(), flag) != b.end();
    });
}

bool validChanges(const DialogueFlags& flags)
{
    return validFlags(flags.set) && validFlags(flags.clear) && !overlap(flags.set, flags.clear)
        && std::none_of(flags.actions.begin(), flags.actions.end(), [](const std::string& action) { return action.empty(); });
}

DialogueFlags readFlags(const Json& json)
{
    return {json.value("set", std::vector<std::string>{}), json.value("clear", std::vector<std::string>{}),
        json.value("do", std::vector<std::string>{})};
}

void writeFlags(Json& json, const DialogueFlags& flags)
{
    if (!flags.set.empty()) json["set"] = flags.set;
    if (!flags.clear.empty()) json["clear"] = flags.clear;
    if (!flags.actions.empty()) json["do"] = flags.actions;
}

}

const DialogueNode* Dialogue::node(std::string_view nodeId) const
{
    const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const auto& node) { return node.id == nodeId; });
    return found == nodes.end() ? nullptr : &*found;
}

bool Dialogue::validate(std::string* error) const
{
    if (error) error->clear();
    if (id.empty()) return fail(error, "Dialogue needs an id");
    if (nodes.empty() || nodes.size() > 4096) return fail(error, "Dialogue must contain 1 to 4096 nodes");

    std::set<std::string> ids;
    for (const auto& node : nodes)
    {
        if (node.id.empty() || !ids.insert(node.id).second) return fail(error, "Empty or duplicate dialogue node id: " + node.id);
        if (!validChanges(node.flags)) return fail(error, "Invalid flags on node: " + node.id);
    }
    if (start.empty() || !ids.contains(start)) return fail(error, "Dialogue start node does not exist: " + start);

    const auto destinationExists = [&](const std::string& target) { return target.empty() || ids.contains(target); };
    for (const auto& node : nodes)
    {
        if (node.choices.size() > 128) return fail(error, "Too many choices on node: " + node.id);
        std::set<std::string> choices;
        for (const auto& choice : node.choices)
        {
            const std::string where = node.id + "/" + choice.id;
            if (choice.id.empty() || !choices.insert(choice.id).second || choice.text.empty())
                return fail(error, "Empty or duplicate choice: " + where);
            if (!validChanges(choice.flags) || !validFlags(choice.require) || !validFlags(choice.forbid)
                || overlap(choice.require, choice.forbid))
                return fail(error, "Invalid flags on choice: " + where);
            if (choice.check)
            {
                const auto& check = *choice.check;
                if (!choice.next.empty()) return fail(error, "Checked choice cannot also have next: " + where);
                if (check.skill.empty() || check.difficulty < 0 || check.difficulty > 100000)
                    return fail(error, "Invalid dialogue check: " + where);
                if (!destinationExists(check.success) || !destinationExists(check.failure))
                    return fail(error, "Check destination does not exist: " + where);
            }
            else if (!destinationExists(choice.next)) return fail(error, "Choice destination does not exist: " + where);
        }
    }
    return true;
}

std::string Dialogue::toJson() const
{
    Json json{{"id", id}, {"start", start}, {"nodes", Json::array()}};
    for (const auto& node : nodes)
    {
        Json entry{{"id", node.id}, {"speaker", node.speaker}, {"text", node.text}, {"choices", Json::array()}};
        writeFlags(entry, node.flags);
        for (const auto& choice : node.choices)
        {
            Json item{{"id", choice.id}, {"text", choice.text}};
            if (!choice.require.empty()) item["require"] = choice.require;
            if (!choice.forbid.empty()) item["forbid"] = choice.forbid;
            writeFlags(item, choice.flags);
            if (choice.check)
            {
                const auto& check = *choice.check;
                item["check"] = {{"skill", check.skill}, {"difficulty", check.difficulty}, {"success", check.success}, {"failure", check.failure}};
            }
            else item["next"] = choice.next;
            entry["choices"].push_back(std::move(item));
        }
        json["nodes"].push_back(std::move(entry));
    }
    return json.dump(2);
}

std::optional<Dialogue> Dialogue::fromJson(std::string_view text, std::string* error)
{
    if (error) error->clear();
    // Chapter dialogue is text, not an unbounded archive. Refuse it before parsing huge documents.
    if (text.size() > 4 * 1024 * 1024) { fail(error, "Dialogue document is too large"); return std::nullopt; }
    try
    {
        const auto json = Json::parse(text);
        Dialogue dialogue;
        dialogue.id = json.at("id").get<std::string>();
        dialogue.start = json.at("start").get<std::string>();
        const auto& nodes = json.at("nodes");
        if (!nodes.is_array() || nodes.empty() || nodes.size() > 4096)
            throw std::invalid_argument("Dialogue nodes must be an array of 1 to 4096 entries");
        for (const auto& entry : nodes)
        {
            if (!entry.is_object()) throw std::invalid_argument("Dialogue node must be an object");
            DialogueNode node;
            node.id = entry.at("id").get<std::string>();
            node.speaker = entry.value("speaker", std::string{});
            node.text = entry.value("text", std::string{});
            node.flags = readFlags(entry);
            if (entry.contains("choices"))
            {
                const auto& choices = entry.at("choices");
                if (!choices.is_array() || choices.size() > 128)
                    throw std::invalid_argument("Dialogue choices must be an array of at most 128 entries");
                for (const auto& item : choices)
                {
                    if (!item.is_object()) throw std::invalid_argument("Dialogue choice must be an object");
                    DialogueChoice choice;
                    choice.id = item.at("id").get<std::string>();
                    choice.text = item.at("text").get<std::string>();
                    choice.next = item.value("next", std::string{});
                    choice.require = item.value("require", std::vector<std::string>{});
                    choice.forbid = item.value("forbid", std::vector<std::string>{});
                    choice.flags = readFlags(item);
                    if (item.contains("check"))
                    {
                        const auto& check = item.at("check");
                        const auto& difficulty = check.at("difficulty");
                        if (!difficulty.is_number_integer() || difficulty < 0 || difficulty > 100000)
                            throw std::invalid_argument("Dialogue check difficulty must be an integer from 0 to 100000");
                        choice.check = DialogueCheck{check.at("skill").get<std::string>(), check.at("difficulty").get<int>(),
                            check.at("success").get<std::string>(), check.at("failure").get<std::string>()};
                    }
                    node.choices.push_back(std::move(choice));
                }
            }
            dialogue.nodes.push_back(std::move(node));
        }
        if (!dialogue.validate(error)) return std::nullopt;
        return dialogue;
    }
    catch (const std::exception& exception)
    {
        fail(error, exception.what());
        return std::nullopt;
    }
}

DialogueSession::DialogueSession(Dialogue dialogue, std::set<std::string> flags)
    : dialogue_(std::move(dialogue)), flags_(std::move(flags))
{
    std::string error;
    if (!dialogue_.validate(&error)) throw std::invalid_argument(error);
    if (flags_.contains("")) throw std::invalid_argument("Story flags cannot be empty");
    enter(dialogue_.start);
}

const DialogueNode* DialogueSession::current() const
{
    return current_.empty() ? nullptr : dialogue_.node(current_);
}

bool DialogueSession::finished() const
{
    const auto* node = current();
    return !node || node->choices.empty();
}

bool DialogueSession::available(const DialogueChoice& choice) const
{
    return std::all_of(choice.require.begin(), choice.require.end(), [&](const auto& flag) { return flags_.contains(flag); })
        && std::none_of(choice.forbid.begin(), choice.forbid.end(), [&](const auto& flag) { return flags_.contains(flag); });
}

std::vector<const DialogueChoice*> DialogueSession::choices() const
{
    std::vector<const DialogueChoice*> result;
    if (const auto* node = current())
        for (const auto& choice : node->choices)
            if (available(choice)) result.push_back(&choice);
    return result;
}

void DialogueSession::apply(const DialogueFlags& changes)
{
    for (const auto& flag : changes.clear) flags_.erase(flag);
    flags_.insert(changes.set.begin(), changes.set.end());
    actions_.insert(actions_.end(), changes.actions.begin(), changes.actions.end());
}

void DialogueSession::enter(std::string id)
{
    current_ = std::move(id);
    if (const auto* node = current()) apply(node->flags);
}

std::optional<DialogueResult> DialogueSession::choose(std::string_view id, const CheckRoll& rollCheck)
{
    const auto* node = current();
    if (!node) return std::nullopt;
    const auto found = std::find_if(node->choices.begin(), node->choices.end(), [&](const auto& choice) { return choice.id == id; });
    if (found == node->choices.end() || !available(*found)) return std::nullopt;
    const auto& choice = *found;
    DialogueResult result;
    result.from = current_;
    result.choice = choice.id;
    result.to = choice.next;
    if (choice.check)
    {
        if (!rollCheck) return std::nullopt;
        result.roll = rollCheck(choice.check->skill);
        result.passed = result.roll->total >= choice.check->difficulty;
        result.to = result.passed ? choice.check->success : choice.check->failure;
    }
    apply(choice.flags);
    enter(result.to);
    history_.push_back(result);
    return result;
}

std::string DialogueSession::snapshot() const
{
    return Json{{"version", 1}, {"dialogue", dialogue_.id}, {"current", current_}, {"flags", flags_}}.dump(2);
}

bool DialogueSession::restore(std::string_view text, std::string* error)
{
    if (error) error->clear();
    if (text.size() > 4 * 1024 * 1024) return fail(error, "Dialogue checkpoint is too large");
    try
    {
        const auto json = Json::parse(text);
        const auto& version = json.at("version");
        if (!version.is_number_integer() || version != 1) return fail(error, "Unsupported dialogue checkpoint version");
        if (json.at("dialogue").get<std::string>() != dialogue_.id)
            return fail(error, "Checkpoint belongs to a different dialogue");

        std::string current = json.at("current").get<std::string>();
        if (!current.empty() && !dialogue_.node(current)) return fail(error, "Saved dialogue node no longer exists: " + current);
        const auto flags = json.at("flags").get<std::vector<std::string>>();
        if (!validFlags(flags)) return fail(error, "Checkpoint contains empty or duplicate story flags");
        std::set<std::string> restoredFlags(flags.begin(), flags.end());

        // All validation and allocation is finished. No entry effects run while restoring.
        current_ = std::move(current);
        flags_ = std::move(restoredFlags);
        history_.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        return fail(error, exception.what());
    }
}

}
