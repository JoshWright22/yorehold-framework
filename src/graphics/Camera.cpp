#include "yorehold/framework/graphics/Camera.h"

#include "yorehold/framework/graphics/Renderer.h"

#include <algorithm>
#include <cmath>

namespace yh
{

float Camera::minZoom() const
{
    const float fit = std::min(viewport_.x / bounds_.w, viewport_.y / bounds_.h);
    return std::min(fit * minZoomFit, maxZoom);
}

Vec2 Camera::clampPosition(Vec2 p) const
{
    return {std::clamp(p.x, bounds_.x, bounds_.x + bounds_.w), std::clamp(p.y, bounds_.y, bounds_.y + bounds_.h)};
}

void Camera::jumpTo(Vec2 position, float zoom)
{
    zoom_ = targetZoom_ = std::clamp(zoom, minZoom(), maxZoom);
    position_ = targetPosition_ = clampPosition(position);
}

void Camera::moveTo(Vec2 position)
{
    anchored_ = false;
    targetPosition_ = clampPosition(position);
}

void Camera::panByScreen(Vec2 delta)
{
    const Vec2 world = delta / zoom_;
    position_ = clampPosition(position_ - world);
    targetPosition_ = clampPosition(targetPosition_ - world);
    anchorWorld_ = anchorWorld_ - world;
}

void Camera::zoomBy(float factor, Vec2 anchor)
{
    // The anchor's world point stays put for the whole eased zoom; update() derives position from zoom.
    anchorScreen_ = anchor;
    anchorWorld_ = screenToWorld(anchor);
    anchored_ = true;
    targetZoom_ = std::clamp(targetZoom_ * factor, minZoom(), maxZoom);
}

void Camera::update(double deltaSeconds)
{
    // Frame-rate independent easing: the same fraction of the gap closes per second at any fps.
    const float t = 1.0f - std::exp(-smoothing * static_cast<float>(deltaSeconds));
    targetZoom_ = std::clamp(targetZoom_, minZoom(), maxZoom);
    zoom_ += (targetZoom_ - zoom_) * t;
    if (std::abs(targetZoom_ - zoom_) < 0.0005f)
        zoom_ = targetZoom_;

    if (anchored_)
    {
        const Vec2 fromCentre = anchorScreen_ - viewport_ / 2.0f;
        position_ = clampPosition(anchorWorld_ - fromCentre / zoom_);
        targetPosition_ = clampPosition(anchorWorld_ - fromCentre / targetZoom_);
        anchored_ = zoom_ != targetZoom_;
    }
    else
    {
        position_ = position_ + (targetPosition_ - position_) * t;
    }
}

Rect Camera::visibleWorld() const
{
    const Vec2 topLeft = screenToWorld({0, 0});
    return {topLeft.x, topLeft.y, viewport_.x / zoom_, viewport_.y / zoom_};
}

void Camera::apply(Renderer& renderer) const
{
    renderer.pushTransform(viewport_ / 2.0f - position_ * zoom_, zoom_);
}

}
