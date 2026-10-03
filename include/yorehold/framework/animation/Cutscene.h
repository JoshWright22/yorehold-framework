#pragma once

#include "yorehold/framework/animation/Tween.h"
#include "yorehold/framework/graphics/Camera.h"
#include "yorehold/framework/graphics/Renderer.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

class Font;

// A scripted sequence as data, so chapter writers can make intros and endings without code:
//
//   {"steps": [
//     {"bars": true},
//     {"camera": [1800, 900], "zoom": 1.4, "seconds": 3, "ease": "inOutCubic"},
//     {"caption": "The goblins are gone.", "seconds": 3},
//     {"title": "The End", "seconds": 4},
//     {"event": "credits"},
//     {"fade": [0, 0, 0, 255], "seconds": 1.5}
//   ]}
//
// Steps run in order; "wait": false starts the next step at the same time (a camera move under a
// caption). Captions and titles fade in and out over their time. "event" steps call the client
// back (sounds, unlocking things). Times are in seconds; positions are world units.
class Cutscene
{
public:
    struct Step
    {
        enum class Kind { Wait, Camera, Caption, Title, Fade, Bars, Event };
        Kind kind = Kind::Wait;
        double seconds = 0;
        bool wait = true;
        Vec2 position;            // Camera
        float zoom = 0;           // Camera; 0 keeps the current zoom
        Ease curve = Ease::InOutCubic;
        std::string text;         // Caption/Title text, Event name
        Color color{0, 0, 0, 255}; // Fade target (alpha 0 fades back in)
        bool on = true;           // Bars
    };

    std::vector<Step> steps;

    static std::optional<Cutscene> fromJson(std::string_view json, std::string* error = nullptr);

    // Starts from the first step. The camera is driven from wherever it is now.
    void start();
    // Moves time on. `camera` is steered by camera steps; events go to `onEvent`.
    void update(double deltaSeconds, Camera& camera, const std::function<void(std::string_view)>& onEvent = {});
    // Screen-space overlay: letterbox bars, captions, titles and the fade. Fonts may be null
    // (the debug font is used instead).
    void draw(Renderer& renderer, Font* bodyFont, Font* titleFont) const;

    bool running() const { return running_; }
    // Jumps to the end: the camera lands on its last target and every event still fires.
    void skip(Camera& camera, const std::function<void(std::string_view)>& onEvent = {});

private:
    struct Active
    {
        size_t step;
        double elapsed = 0;
        Vec2 fromPosition;
        float fromZoom = 1;
        Color fromColor;
    };

    void begin(size_t index, Camera& camera, const std::function<void(std::string_view)>& onEvent);
    bool advance(Active& active, double deltaSeconds, Camera& camera);

    bool running_ = false;
    size_t next_ = 0;
    std::vector<Active> active_;
    Color fade_{0, 0, 0, 0};
    float bars_ = 0; // 0..1, eased
    bool barsOn_ = false;
};

}
