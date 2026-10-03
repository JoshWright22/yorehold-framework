#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

// A change to a stat from gear, a condition, a spell... Removed by `source` when that ends.
struct Modifier
{
    enum class Op
    {
        Add,      // +2 strength
        Multiply, // x0.5 speed
        Override, // armour sets base AC to 14
    };

    std::string stat;
    Op op = Op::Add;
    float value = 0;
    std::string source;
};

// Named numbers ("str", "ac", "speed"...) with a base value plus stacked modifiers.
// Final value = override if any (highest wins), else (base + adds) * multipliers.
class StatBlock
{
public:
    void setBase(std::string_view stat, float value);
    float base(std::string_view stat) const;
    float value(std::string_view stat) const;
    int integer(std::string_view stat) const;

    void addModifier(Modifier modifier);
    void removeSource(std::string_view source);
    const std::vector<Modifier>& modifiers() const { return modifiers_; }
    const std::map<std::string, float, std::less<>>& bases() const { return base_; }

private:
    std::map<std::string, float, std::less<>> base_;
    std::vector<Modifier> modifiers_;
};

}
