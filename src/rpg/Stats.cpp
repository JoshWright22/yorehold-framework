#include "yorehold/framework/rpg/Stats.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace yh
{

void StatBlock::setBase(std::string_view stat, float value)
{
    if (auto it = base_.find(stat); it != base_.end())
        it->second = value;
    else
        base_.emplace(std::string(stat), value);
}

float StatBlock::base(std::string_view stat) const
{
    const auto it = base_.find(stat);
    return it == base_.end() ? 0.0f : it->second;
}

float StatBlock::value(std::string_view stat) const
{
    float add = 0;
    float multiply = 1;
    std::optional<float> override;
    for (const Modifier& m : modifiers_)
    {
        if (m.stat != stat)
            continue;
        switch (m.op)
        {
        case Modifier::Op::Add:
            add += m.value;
            break;
        case Modifier::Op::Multiply:
            multiply *= m.value;
            break;
        case Modifier::Op::Override:
            override = std::max(override.value_or(m.value), m.value);
            break;
        }
    }
    return (override.value_or(base(stat)) + add) * multiply;
}

int StatBlock::integer(std::string_view stat) const
{
    return static_cast<int>(std::floor(value(stat) + 0.0001f));
}

void StatBlock::addModifier(Modifier modifier)
{
    modifiers_.push_back(std::move(modifier));
}

void StatBlock::removeSource(std::string_view source)
{
    std::erase_if(modifiers_, [&](const Modifier& m) { return m.source == source; });
}

}
