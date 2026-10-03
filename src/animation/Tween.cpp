#include "yorehold/framework/animation/Tween.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace yh
{

namespace
{

float bounceOut(float t)
{
    constexpr float n = 7.5625f, d = 2.75f;
    if (t < 1 / d)
        return n * t * t;
    if (t < 2 / d)
    {
        t -= 1.5f / d;
        return n * t * t + 0.75f;
    }
    if (t < 2.5f / d)
    {
        t -= 2.25f / d;
        return n * t * t + 0.9375f;
    }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

}

float ease(Ease curve, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    switch (curve)
    {
    case Ease::Linear: return t;
    case Ease::InQuad: return t * t;
    case Ease::OutQuad: return 1 - (1 - t) * (1 - t);
    case Ease::InOutQuad: return t < 0.5f ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2.0f) / 2;
    case Ease::OutCubic: return 1 - std::pow(1 - t, 3.0f);
    case Ease::InOutCubic: return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
    case Ease::OutBack:
    {
        constexpr float c1 = 1.70158f, c3 = c1 + 1;
        return 1 + c3 * std::pow(t - 1, 3.0f) + c1 * std::pow(t - 1, 2.0f);
    }
    case Ease::OutElastic:
    {
        if (t == 0 || t == 1)
            return t;
        constexpr float c4 = 2.0943951f; // 2pi/3
        return std::pow(2.0f, -10 * t) * std::sin((t * 10 - 0.75f) * c4) + 1;
    }
    case Ease::OutBounce: return bounceOut(t);
    }
    return t;
}

Ease easeFromName(std::string_view name)
{
    static constexpr std::pair<std::string_view, Ease> names[] = {
        {"linear", Ease::Linear}, {"inQuad", Ease::InQuad}, {"outQuad", Ease::OutQuad}, {"inOutQuad", Ease::InOutQuad},
        {"outCubic", Ease::OutCubic}, {"inOutCubic", Ease::InOutCubic}, {"outBack", Ease::OutBack},
        {"outElastic", Ease::OutElastic}, {"outBounce", Ease::OutBounce},
    };
    for (const auto& [n, e] : names)
    {
        if (n == name)
            return e;
    }
    return Ease::Linear;
}

void Tweens::to(float* target, float end, double seconds, Ease curve, double delay, std::function<void()> done)
{
    if (!target || !std::isfinite(end) || !std::isfinite(seconds) || !std::isfinite(delay))
        throw std::invalid_argument("A tween needs a target and finite values");
    cancel(target);
    tweens_.push_back({target, *target, end, std::max(seconds, 0.0), 0, std::max(delay, 0.0), curve, std::move(done), false});
}

void Tweens::cancel(const float* target)
{
    std::erase_if(tweens_, [&](const Tween& t) { return t.target == target; });
}

bool Tweens::running(const float* target) const
{
    return std::any_of(tweens_.begin(), tweens_.end(), [&](const Tween& t) { return t.target == target; });
}

void Tweens::update(double deltaSeconds)
{
    if (deltaSeconds < 0 || !std::isfinite(deltaSeconds))
        return;
    std::vector<std::function<void()>> finished;
    for (Tween& t : tweens_)
    {
        double step = deltaSeconds;
        if (t.delay > 0)
        {
            const double waited = std::min(t.delay, step);
            t.delay -= waited;
            step -= waited;
            if (t.delay > 0)
                continue;
        }
        if (!t.started)
        {
            // Start from wherever the value is when the delay ends, not when the tween was queued.
            t.start = *t.target;
            t.started = true;
        }
        t.elapsed += step;
        const float progress = t.duration == 0 ? 1.0f : static_cast<float>(t.elapsed / t.duration);
        *t.target = t.start + (t.end - t.start) * ease(t.curve, progress);
        if (progress >= 1)
        {
            *t.target = t.end;
            if (t.done)
                finished.push_back(std::move(t.done));
            t.duration = -1; // marks for removal
        }
    }
    std::erase_if(tweens_, [](const Tween& t) { return t.duration < 0; });
    // Callbacks run last so they can safely start new tweens.
    for (auto& callback : finished)
        callback();
}

}
