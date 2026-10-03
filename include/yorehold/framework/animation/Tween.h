#pragma once

#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

namespace yh
{

enum class Ease
{
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
    OutCubic,
    InOutCubic,
    OutBack,    // overshoots a little, then settles
    OutElastic, // springy
    OutBounce,  // drops and bounces
};

// 0-1 in, eased 0-1 out (OutBack/OutElastic briefly go past 1).
float ease(Ease curve, float t);
Ease easeFromName(std::string_view name); // "outBack" etc., for skin data

// Animates floats over time. Each tween writes straight into the float it was given, so the
// caller must keep that float alive (or cancel()) until the tween ends.
class Tweens
{
public:
    // Starting a new tween on a float replaces any running one on it.
    void to(float* target, float end, double seconds, Ease curve = Ease::OutCubic, double delay = 0, std::function<void()> done = {});
    void cancel(const float* target);
    void clear() { tweens_.clear(); }
    bool running(const float* target) const;
    void update(double deltaSeconds);
    size_t count() const { return tweens_.size(); }

private:
    struct Tween
    {
        float* target;
        float start;
        float end;
        double duration;
        double elapsed;
        double delay;
        Ease curve;
        std::function<void()> done;
        bool started;
    };
    std::vector<Tween> tweens_;
};

}
