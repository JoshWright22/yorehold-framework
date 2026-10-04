#pragma once

#include "yorehold/framework/map/Grid.h"

#include <span>

namespace yh
{

enum class Cover { None, Half, ThreeQuarters, Full };
using RayBlocked = std::function<bool(Vec2, Vec2)>;

// Two foes within reach on exactly opposite sides of the target's centre. Blocked rays do not count.
bool isFlanked(const Grid& grid, Cell target, std::span<const Cell> foes, float reach = 1, const RayBlocked& blocked = {});
// Rays from the attacker's centre to inset target corners: any blocked ray gives half cover,
// at least three quarters gives three-quarters cover, and all blocked rays give full cover.
Cover coverBetween(const Grid& grid, Cell from, Cell target, const RayBlocked& blocked);

}
