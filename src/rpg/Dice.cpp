#include "yorehold/framework/rpg/Dice.h"

#include <algorithm>
#include <cctype>
#include <numeric>
#include <limits>

namespace yh
{

namespace
{

// Reads digits at `pos`; returns -1 if there are none.
int readNumber(std::string_view text, size_t& pos)
{
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos])))
        return -1;
    int value = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
    {
        value = value * 10 + (text[pos] - '0');
        if (value > 100000)
            return -1; // nobody rolls that many dice; stops overflow
        pos++;
    }
    return value;
}

}

std::optional<DiceExpression> DiceExpression::parse(std::string_view input)
{
    if (input.size() > 4096) return std::nullopt;
    std::string text;
    for (const char c : input)
    {
        if (!std::isspace(static_cast<unsigned char>(c)))
            text += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (text.empty())
        return std::nullopt;

    DiceExpression expression;
    size_t pos = 0;
    int sign = 1;
    int64_t magnitude = 0;
    int totalDice = 0;
    while (pos < text.size())
    {
        if (text[pos] == '+' || text[pos] == '-')
        {
            sign = text[pos] == '-' ? -1 : 1;
            pos++;
        }

        DiceTerm term;
        term.sign = sign;
        const int number = readNumber(text, pos);
        if (pos < text.size() && text[pos] == 'd')
        {
            pos++;
            term.count = number < 0 ? 1 : number;
            if (pos < text.size() && text[pos] == '%')
            {
                term.sides = 100;
                pos++;
            }
            else
            {
                term.sides = readNumber(text, pos);
            }
            if (term.sides < 1 || term.count < 1 || term.count > 1000)
                return std::nullopt;

            if (pos + 1 < text.size() && text[pos] == 'k' && (text[pos + 1] == 'h' || text[pos + 1] == 'l'))
            {
                const bool highest = text[pos + 1] == 'h';
                pos += 2;
                const int keep = readNumber(text, pos);
                if (keep < 1 || keep > term.count)
                    return std::nullopt;
                (highest ? term.keepHighest : term.keepLowest) = keep;
            }
        }
        else
        {
            if (number < 0)
                return std::nullopt;
            term.count = number;
            term.sides = 0;
        }
        const int kept = term.keepHighest ? term.keepHighest : term.keepLowest ? term.keepLowest : term.count;
        magnitude += term.sides == 0 ? term.count : static_cast<int64_t>(kept) * term.sides;
        totalDice += term.sides == 0 ? 0 : term.count;
        // Leave room for critical damage doubling and modifiers; keep a malformed
        // expression from overflowing totals or allocating millions of rolls.
        if (magnitude > std::numeric_limits<int>::max() / 4 || totalDice > 10000 || expression.terms.size() >= 128)
            return std::nullopt;
        expression.terms.push_back(term);

        if (pos < text.size() && text[pos] != '+' && text[pos] != '-')
            return std::nullopt;
        sign = 1;
    }
    return expression;
}

std::string DiceExpression::toString() const
{
    std::string out;
    for (const DiceTerm& term : terms)
    {
        if (!out.empty() || term.sign < 0)
            out += term.sign < 0 ? "-" : "+";
        if (term.sides == 0)
        {
            out += std::to_string(term.count);
            continue;
        }
        out += std::to_string(term.count) + "d" + std::to_string(term.sides);
        if (term.keepHighest)
            out += "kh" + std::to_string(term.keepHighest);
        if (term.keepLowest)
            out += "kl" + std::to_string(term.keepLowest);
    }
    return out;
}

int DiceExpression::minimum() const
{
    int total = 0;
    for (const DiceTerm& term : terms)
    {
        const int kept = term.keepHighest ? term.keepHighest : term.keepLowest ? term.keepLowest : term.count;
        const int low = term.sides == 0 ? term.count : kept;
        const int high = term.sides == 0 ? term.count : kept * term.sides;
        total += term.sign > 0 ? low : -high;
    }
    return total;
}

int DiceExpression::maximum() const
{
    int total = 0;
    for (const DiceTerm& term : terms)
    {
        const int kept = term.keepHighest ? term.keepHighest : term.keepLowest ? term.keepLowest : term.count;
        const int low = term.sides == 0 ? term.count : kept;
        const int high = term.sides == 0 ? term.count : kept * term.sides;
        total += term.sign > 0 ? high : -low;
    }
    return total;
}

bool RollResult::natural20() const
{
    for (const DieRoll& die : dice)
    {
        if (die.kept && die.sides == 20)
            return die.value == 20;
    }
    return false;
}

bool RollResult::natural1() const
{
    for (const DieRoll& die : dice)
    {
        if (die.kept && die.sides == 20)
            return die.value == 1;
    }
    return false;
}

std::string RollResult::describe() const
{
    std::string out = expression + ": ";
    if (!dice.empty())
    {
        out += "[";
        for (size_t i = 0; i < dice.size(); i++)
        {
            if (i > 0)
                out += ", ";
            out += dice[i].kept ? std::to_string(dice[i].value) : "(" + std::to_string(dice[i].value) + ")";
        }
        out += "]";
    }
    if (flat != 0)
        out += (flat > 0 ? " + " : " - ") + std::to_string(std::abs(flat));
    return out + " = " + std::to_string(total);
}

RollResult roll(const DiceExpression& expression, Random& random)
{
    RollResult result;
    result.expression = expression.toString();
    for (const DiceTerm& term : expression.terms)
    {
        if (term.sides == 0)
        {
            result.flat += term.sign * term.count;
            result.total += term.sign * term.count;
            continue;
        }

        const size_t first = result.dice.size();
        for (int i = 0; i < term.count; i++)
            result.dice.push_back({term.sides, random.range(1, term.sides), true});

        if (term.keepHighest || term.keepLowest)
        {
            // Sort indices by value, then mark everything outside the kept slice as dropped.
            std::vector<size_t> order(term.count);
            std::iota(order.begin(), order.end(), first);
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return result.dice[a].value > result.dice[b].value; });
            const int keep = term.keepHighest ? term.keepHighest : term.keepLowest;
            for (int i = 0; i < term.count; i++)
            {
                const bool kept = term.keepHighest ? i < keep : i >= term.count - keep;
                result.dice[order[i]].kept = kept;
            }
        }

        for (size_t i = first; i < result.dice.size(); i++)
        {
            if (result.dice[i].kept)
                result.total += term.sign * result.dice[i].value;
        }
    }
    return result;
}

RollResult roll(std::string_view expression, Random& random)
{
    const std::optional<DiceExpression> parsed = DiceExpression::parse(expression);
    if (!parsed)
    {
        RollResult invalid;
        invalid.expression = "invalid: " + std::string(expression);
        return invalid;
    }
    return roll(*parsed, random);
}

RollResult rollD20(int modifier, Advantage advantage, Random& random)
{
    DiceExpression expression;
    DiceTerm d20{1, 20, 0, 0, 1};
    if (advantage != Advantage::None)
    {
        d20.count = 2;
        (advantage == Advantage::Advantage ? d20.keepHighest : d20.keepLowest) = 1;
    }
    expression.terms.push_back(d20);
    if (modifier != 0)
        expression.terms.push_back({std::abs(modifier), 0, 0, 0, modifier < 0 ? -1 : 1});
    return roll(expression, random);
}

}
