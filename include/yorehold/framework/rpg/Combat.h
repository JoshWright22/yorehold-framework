#pragma once

#include "yorehold/framework/rpg/Character.h"

#include <string>
#include <vector>

namespace yh
{

// What a combatant still has this turn. How many actions a turn brings is up to the ruleset.
struct TurnBudget
{
    int actions = 1;
    bool bonusAction = true; // rulesets without bonus actions start the turn with this false
    bool reaction = true; // refreshes at the start of your own turn
    int movementLeft = 0; // squares
};

struct Combatant
{
    Character* character = nullptr; // not owned
    int team = 0;                    // same team = allies
    int initiative = 0;
    RollResult initiativeRoll;
    TurnBudget budget;
    bool out = false;       // left the fight without going down (gave up, got away): no more turns, can't be attacked
    bool surprised = false; // caught unaware: loses its first turn
    bool turnDone = false; // shared turns: ended or skipped in this round
    bool standing() const { return !out && !character->down(); }
};

struct AttackResult
{
    RollResult attackRoll;
    RollResult damageRoll;
    bool hit = false;
    bool critical = false;
    bool targetDropped = false;
};

// Turn-based combat: initiative order, rounds, and each combatant's action economy.
// Every roll goes through the encounter's Random, so a seeded encounter replays identically.
class Encounter
{
public:
    Encounter(const Ruleset& rules, uint64_t seed) : rules_(rules), random_(seed) {}

    void add(Character& character, int team);
    // Rolls initiative (d20 + initiative modifier, ties broken by the modifier) and starts round 1.
    void start();
    bool started() const { return started_; }

    int round() const { return round_; }
    size_t currentIndex() const { return current_; }
    Combatant& current() { return order_.at(current_); }
    const std::vector<Combatant>& order() const { return order_; }
    // The active block is [blockFirst, blockEnd). Without shared turns it contains only current.
    size_t blockFirst() const { return rules_.sharedTurns ? blockFirst_ : current_; }
    size_t blockEnd() const { return rules_.sharedTurns ? blockEnd_ : current_ + 1; }
    uint64_t blockSerial() const { return blockSerial_; }
    bool canSelectTurn(size_t index) const;
    bool selectTurn(size_t index); // preserves every member's budgets and conditions

    // Joins a fight already going (reinforcements): rolls initiative and takes its place in the order.
    void join(Character& character, int team);
    // Before start(): everyone on `team` loses their first turn.
    void surprise(int team);
    // Takes a combatant out of the fight for good without dropping it (surrendered, fled).
    // If it's their turn, call nextTurn() to pass it on.
    void withdraw(size_t index, const std::string& why = {});

    // Ends the current turn; skips anyone who's down, out or surprised. Ticks conditions when a round ends.
    // Conditions follow the fight: they hear turnStart, turnEnd, attack and damage as those happen,
    // and one flagged "cantAct" or "cantMove" takes the turn's actions or movement.
    void nextTurn();

    // The current combatant has `cost` actions left.
    bool canAct(int cost = 1) const
    {
        return started_ && !order_.empty() && order_[current_].standing() && !finished() && order_[current_].budget.actions >= cost;
    }
    // What a Strike costs the current combatant (see Character::strikeCost).
    int strikeCost() const;
    bool canStrike() const { return canAct(strikeCost()); }
    // Spends strikeCost() actions. Attack roll vs AC; a natural 20 always hits and doubles the dice.
    AttackResult attack(size_t targetIndex);
    // Stride: spends an action to move its speed again.
    bool dash();
    // Spends actions on anything else (Defend, Help, Interact...); false if there aren't enough.
    bool spendActions(int count);
    // Spends `squares` of movement; false if there isn't enough.
    bool spendMovement(int squares);
    // Spends a combatant's reaction (an opportunity attack, a readied action). It comes back at the
    // start of their own turn. False if it's already gone or they can't act.
    bool useReaction(size_t index);

    // True once only one team is left standing.
    bool finished() const;
    int winningTeam() const;

    const std::vector<std::string>& log() const { return log_; }
    Random& random() { return random_; }

private:
    void beginTurn();
    void refreshTurn(Combatant& combatant);
    void deathTurn(Combatant& combatant);
    void nextSharedTurn();
    void addLog(std::string line);
    void conditionsEnded(const Character& character, const std::vector<std::string>& ids); // into the log

    const Ruleset& rules_;
    Random random_;
    std::vector<Combatant> order_;
    size_t current_ = 0;
    int round_ = 0;
    bool started_ = false;
    size_t blockFirst_ = 0, blockEnd_ = 0;
    int blockTeam_ = 0;
    uint64_t blockSerial_ = 0;
    std::vector<std::string> log_;
};

}
