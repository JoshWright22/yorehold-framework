#pragma once

#include "yorehold/framework/graphics/Renderer.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

struct AnimationClip
{
    std::vector<int> frames;       // indices into the sheet's frames
    std::vector<float> durations;  // seconds per frame; empty = 1 / fps for every frame
    float fps = 10;
    bool loop = true;
    std::string next;              // clip to play when a non-looping clip ends ("attack" -> "idle")
    std::map<int, std::string> events; // clip frame index -> event name ("hit" on the swing frame)

    float frameSeconds(size_t i) const { return i < durations.size() ? durations[i] : 1.0f / fps; }
};

// Frames cut from one texture (or from an atlas region) plus named clips. Skins describe sheets in
// JSON next to the image:
//   {"frameWidth": 32, "frameHeight": 32,
//    "clips": {"idle": {"frames": "0-3", "fps": 6},
//              "attack": {"frames": [4, 5, 6, 7], "fps": 12, "loop": false, "next": "idle", "events": {"2": "hit"}}}}
// Sheets packed by other tools can list "frames": [{"x":0,"y":0,"w":32,"h":32}, ...] instead of a grid.
struct SpriteSheet
{
    TextureId texture = 0;
    std::vector<Rect> frames; // uv rects
    Vec2 frameSize;           // pixels of the first frame, for default draw sizes
    std::map<std::string, AnimationClip, std::less<>> clips;

    // `region` places the sheet inside part of a texture (an atlas entry) in 0-1 uv space.
    static std::optional<SpriteSheet> fromJson(std::string_view json, Vec2 textureSize, Rect region = {0, 0, 1, 1}, std::string* error = nullptr);
    const AnimationClip* clip(std::string_view name) const;
    void draw(Renderer& renderer, int frame, const Rect& dest, Color tint = {}, bool flipX = false) const;
};

// Playback state for one animated thing. Plain data, so thousands of tokens can each have one.
class SpriteAnimator
{
public:
    using EventHandler = std::function<void(std::string_view event)>;

    // Restarts only when switching clips, or when `restart` is set.
    void play(const SpriteSheet& sheet, std::string_view clip, bool restart = false);
    // Advances, following `next` when a one-shot clip ends. Events fire for every frame entered,
    // even when a long frame step skips past several.
    void update(const SpriteSheet& sheet, double deltaSeconds, const EventHandler& onEvent = {});

    int frame(const SpriteSheet& sheet) const;
    std::string_view clip() const { return clip_; }
    bool finished() const { return finished_; }
    float speed = 1;

private:
    std::string clip_;
    size_t index_ = 0;
    double elapsed_ = 0;
    bool finished_ = false;
    bool started_ = false; // the first frame's event fires on the first update after play()
};

}
