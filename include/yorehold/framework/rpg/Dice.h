#pragma once

#include "yorehold/framework/rpg/Random.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

// One part of a dice expression: "3d6", "4d6kh3" (keep highest 3), "2d20kl1", or a flat number.
struct DiceTerm
{
    int count = 1;
    int sides = 0; // 0 = flat number, `count` is the value
    int keepHighest = 0;
    int keepLowest = 0;
    int sign = 1;
};

// Parsed dice notation like "2d6+3", "1d20-1", "4d6kh3", "d%" (= d100). Case and spaces don't matter.
struct DiceExpression
{
    std::vector<DiceTerm> terms;

    static std::optional<DiceExpression> parse(std::string_view text);
    std::string toString() const;
    int minimum() const;
    int maximum() const;
};

struct DieRoll
{
    int sides = 0;
    int value = 0;
    bool kept = true;
};

struct RollResult
{
    std::string expression;
    int total = 0;
    std::vector<DieRoll> dice;
    int flat = 0; // sum of the flat numbers

    // For single d20 rolls: the kept die showed 20 / 1.
    bool natural20() const;
    bool natural1() const;
    // "2d6+3: [4, 2] + 3 = 9", dropped dice shown in brackets as (1).
    std::string describe() const;
};

enum class Advantage
{
    None,
    Advantage,    // roll 2d20, keep the higher
    Disadvantage, // roll 2d20, keep the lower
};

RollResult roll(const DiceExpression& expression, Random& random);
// Returns an empty result with expression "invalid: ..." if the text doesn't parse.
RollResult roll(std::string_view expression, Random& random);
// 1d20 (or 2d20 keep high/low) + modifier.
RollResult rollD20(int modifier, Advantage advantage, Random& random);

}
