#pragma once

#include "yorehold/framework/graphics/Types.h"

namespace yh
{

class Renderer;

// A 2D camera over a world measured in world units (pixels at zoom 1).
// Zoom and keyboard pans ease toward a target so motion feels smooth; drag pans move instantly
// so the map stays glued to the cursor.
class Camera
{
public:
    // Where the camera may look. The view's centre is kept inside this rect, and by default
    // you can't zoom out further than fitting the whole rect on screen.
    void setBounds(const Rect& world) { bounds_ = world; }
    const Rect& bounds() const { return bounds_; }

    // Size of the area the camera draws into, in screen pixels. Set every frame.
    void setViewport(Vec2 size) { viewport_ = size; }
    Vec2 viewport() const { return viewport_; }

    Vec2 position() const { return position_; }
    float zoom() const { return zoom_; }
    float targetZoom() const { return targetZoom_; }

    void jumpTo(Vec2 position, float zoom);
    // Eases there.
    void moveTo(Vec2 position);
    // Instant pan by a screen-space distance (drags).
    void panByScreen(Vec2 delta);
    // Eased zoom by a factor; `anchor` (screen space) stays under the same world point.
    void zoomBy(float factor, Vec2 anchor);
    void zoomBy(float factor) { zoomBy(factor, viewport_ / 2.0f); }

    // Zoom limits: maxZoom in-close; minZoomFit scales "whole world fits on screen" (1 = exactly fits,
    // 0.5 = can zoom out to half that).
    float maxZoom = 4.0f;
    float minZoomFit = 1.0f;
    // How quickly easing catches up; higher is snappier.
    float smoothing = 14.0f;

    void update(double deltaSeconds);

    Vec2 worldToScreen(Vec2 world) const { return (world - position_) * zoom_ + viewport_ / 2.0f; }
    Vec2 screenToWorld(Vec2 screen) const { return (screen - viewport_ / 2.0f) / zoom_ + position_; }
    // The part of the world currently on screen.
    Rect visibleWorld() const;

    // Draws everything until renderer.pop() through this camera.
    void apply(Renderer& renderer) const;

private:
    float minZoom() const;
    Vec2 clampPosition(Vec2 position) const;

    Rect bounds_{0, 0, 1000, 1000};
    Vec2 viewport_{1280, 720};
    Vec2 position_{500, 500};
    Vec2 targetPosition_{500, 500};
    float zoom_ = 1.0f;
    float targetZoom_ = 1.0f;
    bool anchored_ = false;
    Vec2 anchorScreen_;
    Vec2 anchorWorld_;
};

}
