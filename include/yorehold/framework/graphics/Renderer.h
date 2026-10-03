#pragma once

#include "yorehold/framework/graphics/Types.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

struct SDL_Window;

namespace yh
{

// A texture or render target on the GPU. 0 is the built-in font atlas.
using TextureId = uint32_t;

enum class BlendMode : uint8_t
{
    Alpha,    // normal see-through drawing
    Additive, // lights: adds brightness
    Multiply, // shadows/fog: darkens what's underneath by the colour drawn
    Replace,  // overwrites, alpha included (cutting holes in masks)
};

// What the current frame has cost so far, for the F3 overlay and profiling.
struct RenderStats
{
    uint32_t drawCalls = 0;
    uint32_t passes = 0;
    uint32_t vertices = 0;
    uint32_t textures = 0;   // live textures and render targets
    size_t textureBytes = 0; // their approximate GPU memory
};

// 2D batch renderer on WebGPU (Dawn on native, the browser's WebGPU on the web).
// Everything drawn in a frame goes into one vertex/index buffer; a new draw call only starts when the
// texture, clip rect, blend mode or target changes.
//
// Coordinates are pixels from the top-left of the current viewport, scaled by the current transform.
class Renderer
{
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(SDL_Window* window, bool vsync);
    void shutdown();
    void resize(int width, int height);

    // Frame lifecycle, called by the Host.
    void beginFrame(Color clearColor);
    // Sends everything drawn so far to the GPU. Drawing can continue afterwards (e.g. overlays).
    void flush();
    // Saves the frame as drawn up to the last flush(). Slow (waits for the GPU); for screenshots only.
    bool saveScreenshot(const std::string& path);
    void endFrame();

    Vec2 outputSize() const;
    // Totals for everything flushed since beginFrame().
    RenderStats stats() const;

    // The area being drawn into, in local coordinates (after viewports and transforms).
    Rect bounds() const;

    // Restricts drawing to `area` and makes its top-left the new origin.
    void pushViewport(const Rect& area);
    // Moves and scales everything drawn until pop(). Used for cameras.
    void pushTransform(Vec2 translate, float scale);
    void pop();

    // Draw into an off-screen texture instead of the window until popTarget(). Optionally clears it first.
    // Inside, coordinates start at the target's top-left with no transform.
    void pushTarget(TextureId target, std::optional<Color> clear = std::nullopt);
    void popTarget();

    void setBlendMode(BlendMode mode);
    BlendMode blendMode() const;

    // False if the rect is entirely outside the current clip area, so callers can skip work.
    bool visible(const Rect& rect) const;

    void clear(Color color);
    void fillRect(const Rect& rect, Color color);
    void fillRects(std::span<const Rect> rects, Color color);
    void drawRect(const Rect& rect, Color color, float thickness = 1.0f);
    void drawLine(Vec2 from, Vec2 to, Color color, float thickness = 1.0f);
    void fillCircle(Vec2 center, float radius, Color color, int segments = 32);
    // A fan of triangles from `center` to each consecutive pair of `rim` points (closed), with colours
    // blended from centre to rim. Visibility polygons and soft lights are drawn this way.
    void fillFan(Vec2 center, std::span<const Vec2> rim, Color centerColor, Color rimColor);
    // Radial alpha falloff evaluated at each rim vertex, including wall intersections.
    void fillLightFan(Vec2 center, std::span<const Vec2> rim, float radius, Color color);
    void drawSprite(TextureId texture, const Rect& dest, Color tint = {});
    // Part of a texture (an atlas cell); uv is in 0-1 texture coordinates.
    void drawSpriteRegion(TextureId texture, const Rect& dest, const Rect& uv, Color tint = {});
    void drawSpriteRotated(TextureId texture, Vec2 center, Vec2 size, float radians, Color tint = {});

    // Built-in 8x8 pixel font, for debug text. Real text goes through Font.
    void drawText(Vec2 position, std::string_view text, Color color, float scale = 2.0f);
    static float textWidth(std::string_view text, float scale = 2.0f);
    static float lineHeight(float scale = 2.0f);

    // RGBA8 pixels, row by row. `smooth` = linear filtering (photos, scaled art); off = crisp pixels.
    TextureId createTexture(int width, int height, const void* rgba, bool smooth = false);
    // Any image decodeImage reads (PNG, JPEG, WebP, ...) from memory, e.g. read through the asset
    // system. Sides over maxTextureSize are scaled down. Empty on failure.
    std::optional<TextureId> loadTexture(std::span<const unsigned char> fileBytes, bool smooth = false);
    // Replaces all pixels of an existing texture (same size).
    void updateTexture(TextureId texture, const void* rgba);
    Vec2 textureSize(TextureId texture) const;
    TextureId createRenderTarget(int width, int height);
    // A lifetime-safe release callback; it also works after the renderer has gone away.
    // Call once when an owned texture is no longer needed. GPU work already queued can finish.
    std::function<void()> textureRelease(TextureId texture);

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}
