#pragma once

#include "yorehold/framework/rpg/Ruleset.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yh
{

// Someone the players' party can take along: when they are willing to join and what they think
// of the story so far. Who they are on the map is the game's.
struct CompanionDefinition
{
    std::string id;
    int approval = 0;           // where their approval starts
    int joinAt = 0;             // the least approval they join with
    std::optional<int> leaveAt; // at or below this a member leaves; none = only when sent away
    // Story flags they care about: approval changes the first time each one is set.
    std::vector<std::pair<std::string, int>> flags;

    static std::optional<CompanionDefinition> fromJson(std::string_view json, std::string* error = nullptr);
    std::string toJson() const;
};

enum class CompanionJoin
{
    Joined,
    Already,     // a member already
    Unknown,     // no definition with that id
    LowApproval, // below joinAt
    Full,        // the ruleset's limit or party limit is reached
};

// Approval and membership for every companion the party has met, kept for the whole adventure.
class Companions
{
public:
    // Adds a definition, or replaces one with the same id. Approval starts at the definition's
    // the first time an id is seen and is kept after that.
    void define(CompanionDefinition definition);
    const CompanionDefinition* definition(std::string_view id) const;
    const std::vector<CompanionDefinition>& definitions() const { return definitions_; }

    int approval(std::string_view id) const; // 0 for an unknown id
    // Moves approval within the ruleset's range. A member who ends at or below leaveAt leaves;
    // returns true when that happened. Unknown ids change nothing.
    bool adjust(const CompanionRules& rules, std::string_view id, int delta);
    bool setApproval(const CompanionRules& rules, std::string_view id, int value);

    // `others`: how many in the party aren't companions (the players' characters).
    CompanionJoin canJoin(const CompanionRules& rules, std::string_view id, int others) const;
    CompanionJoin join(const CompanionRules& rules, std::string_view id, int others);
    bool leave(std::string_view id); // false if they weren't a member
    bool member(std::string_view id) const;
    const std::vector<std::string>& members() const { return members_; } // in the order they joined

    // The story's flags as they are now. Each definition's flags that are set and weren't counted
    // before change approval once. Returns the ids of members who left because of it.
    std::vector<std::string> flagsSet(const CompanionRules& rules, const std::set<std::string>& flags);

    // Definitions, approval, members and counted flags. A bad document fails as a whole.
    std::string toJson() const;
    static std::optional<Companions> fromJson(std::string_view json, std::string* error = nullptr);

private:
    std::vector<CompanionDefinition> definitions_;
    std::map<std::string, int, std::less<>> approval_;
    std::vector<std::string> members_;
    std::set<std::pair<std::string, std::string>> counted_; // companion id, flag
};

}
