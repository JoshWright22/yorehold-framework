#include "yorehold/framework/animation/Cutscene.h"

#include "yorehold/framework/graphics/Font.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace yh
{

namespace
{

constexpr double maxStepSeconds = 600;
constexpr float barsPerSecond = 2.5f; // letterbox bars slide in/out in 0.4 s

Color lerp(Color a, Color b, float t)
{
    auto channel = [t](uint8_t x, uint8_t y) { return static_cast<uint8_t>(std::lround(x + (y - x) * t)); };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};
}

Color colorFromJson(const nlohmann::json& j)
{
    const auto values = j.get<std::vector<int>>();
    if (values.size() != 4 || std::any_of(values.begin(), values.end(), [](int v) { return v < 0 || v > 255; }))
        throw std::invalid_argument("Colours are [r,g,b,a] in 0..255");
    return {static_cast<uint8_t>(values[0]), static_cast<uint8_t>(values[1]), static_cast<uint8_t>(values[2]), static_cast<uint8_t>(values[3])};
}

}

std::optional<Cutscene> Cutscene::fromJson(std::string_view json, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = nlohmann::json::parse(json);
        Cutscene cutscene;
        for (const auto& s : j.at("steps"))
        {
            if (!s.is_object()) throw std::invalid_argument("Each step must be an object");
            Step step;
            step.seconds = s.value("seconds", 0.0);
            step.wait = s.value("wait", true);
            if (s.contains("ease")) step.curve = easeFromName(s.at("ease").get<std::string>());
            if (s.contains("camera"))
            {
                const auto at = s.at("camera").get<std::vector<float>>();
                if (at.size() != 2 || !std::isfinite(at[0]) || !std::isfinite(at[1])) throw std::invalid_argument("camera needs [x, y]");
                step.kind = Step::Kind::Camera;
                step.position = {at[0], at[1]};
                step.zoom = s.value("zoom", 0.0f);
                if (!std::isfinite(step.zoom) || step.zoom < 0 || step.zoom > 64) throw std::invalid_argument("Bad camera zoom");
            }
            else if (s.contains("caption") || s.contains("title"))
            {
                step.kind = s.contains("title") ? Step::Kind::Title : Step::Kind::Caption;
                step.text = s.at(s.contains("title") ? "title" : "caption").get<std::string>();
                if (step.text.size() > 2000) throw std::invalid_argument("Caption too long");
                if (!s.contains("seconds")) step.seconds = 3;
            }
            else if (s.contains("fade"))
            {
                step.kind = Step::Kind::Fade;
                step.color = colorFromJson(s.at("fade"));
            }
            else if (s.contains("bars"))
            {
                step.kind = Step::Kind::Bars;
                step.on = s.at("bars").get<bool>();
            }
            else if (s.contains("event"))
            {
                step.kind = Step::Kind::Event;
                step.text = s.at("event").get<std::string>();
                if (step.text.empty()) throw std::invalid_argument("Event needs a name");
            }
            else if (s.contains("pause"))
            {
                step.kind = Step::Kind::Wait;
                step.seconds = s.at("pause").get<double>();
            }
            else
                throw std::invalid_argument("Unknown cutscene step");
            if (!std::isfinite(step.seconds) || step.seconds < 0 || step.seconds > maxStepSeconds)
                throw std::invalid_argument("Step seconds must be 0..600");
            cutscene.steps.push_back(std::move(step));
        }
        return cutscene;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return std::nullopt; }
}

void Cutscene::start()
{
    running_ = !steps.empty();
    next_ = 0;
    active_.clear();
}

void Cutscene::begin(size_t index, Camera& camera, const std::function<void(std::string_view)>& onEvent)
{
    const Step& step = steps[index];
    if (step.kind == Step::Kind::Bars) barsOn_ = step.on;
    if (step.kind == Step::Kind::Event && onEvent) onEvent(step.text);
    active_.push_back({index, 0, camera.position(), camera.zoom(), fade_});
}

bool Cutscene::advance(Active& active, double deltaSeconds, Camera& camera)
{
    const Step& step = steps[active.step];
    active.elapsed += deltaSeconds;
    const float t = step.seconds > 0 ? static_cast<float>(std::min(1.0, active.elapsed / step.seconds)) : 1.0f;
    const float e = ease(step.curve, t);
    if (step.kind == Step::Kind::Camera)
    {
        const Vec2 position = active.fromPosition + (step.position - active.fromPosition) * e;
        const float zoom = step.zoom > 0 ? active.fromZoom + (step.zoom - active.fromZoom) * e : active.fromZoom;
        camera.jumpTo(position, zoom); // clamped to the map like the player's camera
    }
    else if (step.kind == Step::Kind::Fade)
        fade_ = lerp(active.fromColor, step.color, t);
    return t >= 1;
}

void Cutscene::update(double deltaSeconds, Camera& camera, const std::function<void(std::string_view)>& onEvent)
{
    const float barTarget = barsOn_ ? 1.0f : 0.0f;
    bars_ += std::clamp(barTarget - bars_, -barsPerSecond * static_cast<float>(deltaSeconds), barsPerSecond * static_cast<float>(deltaSeconds));
    if (!running_)
        return;

    // Zero-length steps (events, bars) finish at once, so keep starting steps until one takes time.
    double dt = std::max(0.0, deltaSeconds);
    for (size_t guard = 0; guard <= steps.size(); guard++)
    {
        while (next_ < steps.size() && (active_.empty() || !steps[active_.back().step].wait))
            begin(next_++, camera, onEvent);
        const size_t before = active_.size();
        std::erase_if(active_, [&](Active& a) { return advance(a, dt, camera); });
        dt = 0;
        if (active_.size() == before)
            break;
    }
    if (active_.empty() && next_ >= steps.size())
        running_ = false;
}

void Cutscene::skip(Camera& camera, const std::function<void(std::string_view)>& onEvent)
{
    if (!running_)
        return;
    for (Active& a : active_)
        advance(a, maxStepSeconds, camera);
    active_.clear();
    for (; next_ < steps.size(); next_++)
    {
        begin(next_, camera, onEvent);
        advance(active_.back(), maxStepSeconds, camera);
        active_.clear();
    }
    bars_ = barsOn_ ? 1.0f : 0.0f;
    running_ = false;
}

void Cutscene::draw(Renderer& renderer, Font* bodyFont, Font* titleFont) const
{
    const Rect screen = renderer.bounds();
    if (fade_.a > 0)
        renderer.fillRect(screen, fade_);
    const float bar = screen.h * 0.11f * ease(Ease::InOutQuad, bars_);
    if (bar > 0)
    {
        renderer.fillRect({0, 0, screen.w, bar}, {0, 0, 0, 255});
        renderer.fillRect({0, screen.h - bar, screen.w, bar}, {0, 0, 0, 255});
    }

    for (const Active& a : active_)
    {
        const Step& step = steps[a.step];
        if (step.kind != Step::Kind::Caption && step.kind != Step::Kind::Title)
            continue;
        // Fade in and out over a third of the step (at most 0.6 s each way).
        const double edge = std::min(0.6, step.seconds / 3);
        const double alpha = edge <= 0 ? 1 : std::clamp(std::min(a.elapsed, step.seconds - a.elapsed) / edge, 0.0, 1.0);
        const bool title = step.kind == Step::Kind::Title;
        const Color color = title ? Color{255, 214, 140, static_cast<uint8_t>(255 * alpha)} : Color{240, 236, 226, static_cast<uint8_t>(255 * alpha)};
        Font* font = title ? titleFont : bodyFont;
        const Rect area = title ? Rect{0, screen.h * 0.5f - 60, screen.w, 120}
                                : Rect{screen.w * 0.1f, screen.h - std::max(bar, screen.h * 0.08f) - 70, screen.w * 0.8f, 60};
        if (font)
            font->drawCentered(renderer, area, step.text, color);
        else
        {
            const float scale = title ? 4.0f : 2.0f;
            const Vec2 at{area.x + (area.w - Renderer::textWidth(step.text, scale)) / 2, area.y + (area.h - Renderer::lineHeight(scale)) / 2};
            renderer.drawText(at, step.text, color, scale);
        }
    }
}

}
