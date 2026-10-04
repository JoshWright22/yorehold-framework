#pragma once

#include "yorehold/framework/graphics/Camera.h"
#include "yorehold/framework/graphics/Renderer.h"

#include <span>
#include <vector>

namespace yh
{

struct Wall
{
    Vec2 a;
    Vec2 b;
};

bool lineOfSight(Vec2 from, Vec2 to, std::span<const Wall> walls);

// Rays around the observer and either side of wall endpoints form an occluded polygon.
// Scratch arrays are reused; the returned span lasts until the next compute().
class Visibility
{
public:
    std::span<const Vec2> compute(Vec2 origin, float radius, std::span<const Wall> walls, int segments = 96);

private:
    std::vector<float> angles_;
    std::vector<Vec2> polygon_;
};

struct Light
{
    Vec2 position;
    float radius = 256;
    Color color{255, 200, 120, 255};
    bool shadows = true;
};

// A part of the world with its own ambient light: the inside of a building on a sunny day.
struct Shade
{
    Rect area; // world units
    Color ambient;
};

// Draw the world first, then apply(): an additive light target is multiplied over the world.
// UI drawn afterwards stays bright. Ambient colour supports daylight/night transitions.
class Lighting
{
public:
    ~Lighting() { if (release_) release_(); }
    Lighting() = default;
    Lighting(const Lighting&) = delete;
    Lighting& operator=(const Lighting&) = delete;
    Color ambient{35, 38, 50, 255};
    // Lights add on top of the ambient, shaded areas included.
    void apply(Renderer& renderer, const Camera& camera, std::span<const Light> lights, std::span<const Wall> walls,
        std::span<const Shade> shaded = {});

private:
    Visibility visibility_;
    TextureId target_ = 0;
    Vec2 size_;
    std::function<void()> release_;
};

}
