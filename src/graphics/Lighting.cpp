#include "yorehold/framework/graphics/Lighting.h"

#include <algorithm>
#include <cmath>

namespace yh
{

namespace
{
constexpr float pi = 3.14159265359f;
float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

float hitDistance(Vec2 origin, Vec2 ray, const Wall& wall)
{
    const Vec2 edge = wall.b - wall.a;
    const Vec2 offset = wall.a - origin;
    const float denominator = cross(ray, edge);
    if (std::abs(denominator) < 1e-7f)
    {
        if (std::abs(cross(offset, ray)) > 1e-5f) return -1;
        const float a = offset.x * ray.x + offset.y * ray.y;
        const Vec2 other = wall.b - origin;
        const float b = other.x * ray.x + other.y * ray.y;
        if (std::max(a, b) < 1e-4f) return -1;
        return std::max(1e-4f, std::min(a, b));
    }
    const float distance = cross(offset, edge) / denominator;
    const float along = cross(offset, ray) / denominator;
    return distance > 1e-4f && along >= 0 && along <= 1 ? distance : -1;
}
}

bool lineOfSight(Vec2 from, Vec2 to, std::span<const Wall> walls)
{
    const Vec2 delta = to - from;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length < 1e-4f) return true;
    const Vec2 ray = delta / length;
    for (const Wall& wall : walls)
    {
        const float hit = hitDistance(from, ray, wall);
        if (hit >= 0 && hit < length - 1e-4f) return false;
    }
    return true;
}

std::span<const Vec2> Visibility::compute(Vec2 origin, float radius, std::span<const Wall> walls, int segments)
{
    angles_.clear();
    polygon_.clear();
    if (radius <= 0 || !std::isfinite(radius)) return polygon_;
    segments = std::clamp(segments, 8, 1024);
    for (int i = 0; i < segments; ++i)
        angles_.push_back(-pi + 2 * pi * i / segments);
    for (const Wall& wall : walls)
        for (Vec2 endpoint : {wall.a, wall.b})
        {
            const float angle = std::atan2(endpoint.y - origin.y, endpoint.x - origin.x);
            for (float offset : {-0.0001f, 0.0f, 0.0001f})
                angles_.push_back(std::remainder(angle + offset, 2 * pi));
        }
    std::sort(angles_.begin(), angles_.end());
    for (float angle : angles_)
    {
        const Vec2 ray{std::cos(angle), std::sin(angle)};
        float distance = radius;
        for (const Wall& wall : walls)
        {
            const float hit = hitDistance(origin, ray, wall);
            if (hit >= 0) distance = std::min(distance, hit);
        }
        polygon_.push_back(origin + ray * distance);
    }
    return polygon_;
}

void Lighting::apply(Renderer& renderer, const Camera& camera, std::span<const Light> lights, std::span<const Wall> walls,
    std::span<const Shade> shaded)
{
    const Vec2 size{std::ceil(camera.viewport().x), std::ceil(camera.viewport().y)};
    if (size.x <= 0 || size.y <= 0) return;
    if (size_ != size)
    {
        if (release_) release_();
        target_ = renderer.createRenderTarget(static_cast<int>(size.x), static_cast<int>(size.y));
        release_ = renderer.textureRelease(target_);
        size_ = size;
    }
    const BlendMode previous = renderer.blendMode();
    renderer.pushTarget(target_, ambient);
    camera.apply(renderer);
    renderer.setBlendMode(BlendMode::Alpha);
    for (const Shade& shade : shaded)
        if (renderer.visible(shade.area))
            renderer.fillRect(shade.area, {shade.ambient.r, shade.ambient.g, shade.ambient.b, 255});
    renderer.setBlendMode(BlendMode::Additive);
    for (const Light& light : lights)
    {
        const Rect area{light.position.x - light.radius, light.position.y - light.radius, light.radius * 2, light.radius * 2};
        if (!renderer.visible(area)) continue;
        const auto rim = visibility_.compute(light.position, light.radius, light.shadows ? walls : std::span<const Wall>{});
        renderer.fillLightFan(light.position, rim, light.radius, light.color);
    }
    renderer.pop();
    renderer.popTarget();
    renderer.setBlendMode(BlendMode::Multiply);
    renderer.drawSprite(target_, {0, 0, size.x, size.y});
    renderer.setBlendMode(previous);
}

}
