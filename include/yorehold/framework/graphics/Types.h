#pragma once

#include <algorithm>
#include <cstdint>

namespace yh
{

struct Vec2
{
    float x = 0;
    float y = 0;
    constexpr bool operator==(const Vec2&) const = default;

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
};

struct Rect
{
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;

    constexpr Vec2 position() const { return {x, y}; }
    constexpr Vec2 size() const { return {w, h}; }
    constexpr bool contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }

    constexpr Rect intersect(const Rect& o) const
    {
        const float left = std::max(x, o.x);
        const float top = std::max(y, o.y);
        const float right = std::min(x + w, o.x + o.w);
        const float bottom = std::min(y + h, o.y + o.h);
        return {left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
    }
};

// 8-bit RGBA, straight (not premultiplied) alpha.
struct Color
{
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
    constexpr bool operator==(const Color&) const = default;

    constexpr uint32_t packed() const { return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24; }
};

}
