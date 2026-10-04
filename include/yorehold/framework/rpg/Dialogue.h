#pragma once

#include "yorehold/framework/rpg/Dice.h"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yh
{

struct DialogueFlags
{
    std::vector<std::string> set;
    std::vector<std::string> clear;
    // "do": things for the game to carry out ("release", "attack"...). The game decides what each
    // means; ones it doesn't know are ignored. Collected with DialogueSession::takeActions().
    std::vector<std::string> actions;
};

struct DialogueCheck
{
    std::string skill;
    int difficulty = 10;
    std::string success; // node id; empty ends the conversation
    std::string failure;
};

struct DialogueChoice
{
    std::string id;
    std::string text;
    std::string next; // empty ends the conversation; a check uses its own destinations
    std::vector<std::string> require; // all must be set
    std::vector<std::string> forbid;  // none may be set
    DialogueFlags flags;
    std::optional<DialogueCheck> check;
};

struct DialogueNode
{
    std::string id;
    std::string speaker;
    std::string text;
    DialogueFlags flags; // applied when this node is entered
    std::vector<DialogueChoice> choices; // no choices = the final line
};

// Authored chapter data. Node ids stay stable when writers edit the text.
struct Dialogue
{
    std::string id;
    std::string start;
    std::vector<DialogueNode> nodes;

    const DialogueNode* node(std::string_view nodeId) const;
    bool validate(std::string* error = nullptr) const;
    std::string toJson() const;
    static std::optional<Dialogue> fromJson(std::string_view json, std::string* error = nullptr);
};

struct DialogueResult
{
    std::string from;
    std::string choice;
    std::string to;
    std::optional<RollResult> roll;
    bool passed = false; // only meaningful when roll is present
};

// Owns the dialogue so its node/choice pointers survive the author's original data going away.
// The client supplies checks (Character::rollCheck, another ruleset, or a deterministic test).
class DialogueSession
{
public:
    using CheckRoll = std::function<RollResult(std::string_view skill)>;

    explicit DialogueSession(Dialogue dialogue, std::set<std::string> flags = {});

    const DialogueNode* current() const;
    bool finished() const;
    const std::set<std::string>& flags() const { return flags_; }
    const std::vector<DialogueResult>& history() const { return history_; }
    std::vector<const DialogueChoice*> choices() const;

    // A hidden/unknown choice, or a check without a roller, changes nothing and returns nullopt.
    // The roller runs before flags or history change. Skill checks compare total >= difficulty;
    // natural 1/20 do not automatically override a skill check.
    std::optional<DialogueResult> choose(std::string_view choice, const CheckRoll& rollCheck = {});
    void close() { current_.clear(); }
    // The "do" actions reached since the last call, in order.
    std::vector<std::string> takeActions() { return std::exchange(actions_, {}); }

    // Versioned state for a chapter save. Restore validates the document before changing anything.
    // Node entry effects are already in the saved flags and are not applied a second time.
    // History and the client's random generator are separate; restore clears the local history.
    std::string snapshot() const;
    bool restore(std::string_view json, std::string* error = nullptr);

private:
    bool available(const DialogueChoice& choice) const;
    void apply(const DialogueFlags& changes);
    void enter(std::string id);

    Dialogue dialogue_;
    std::string current_;
    std::set<std::string> flags_;
    std::vector<DialogueResult> history_;
    std::vector<std::string> actions_;
};

}
