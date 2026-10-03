#include "yorehold/framework/animation/SpriteSheet.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

// "0-3,5,9-7" -> 0 1 2 3 5 9 8 7. Ranges may run backwards for reversed playback.
std::vector<int> parseFrameList(const json& j)
{
    std::vector<int> frames;
    if (j.is_array())
    {
        for (const json& f : j) frames.push_back(f.get<int>());
        return frames;
    }
    const std::string text = j.get<std::string>();
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t comma = std::min(text.find(',', start), text.size());
        const std::string part = text.substr(start, comma - start);
        const size_t dash = part.find('-', 1);
        size_t used = 0;
        const int from = std::stoi(part, &used);
        const int to = dash == std::string::npos ? from : std::stoi(part.substr(dash + 1));
        if (dash == std::string::npos && used != part.size()) throw std::invalid_argument("Bad frame list");
        for (int f = from;; f += from <= to ? 1 : -1)
        {
            frames.push_back(f);
            if (f == to || frames.size() > 100000) break;
        }
        start = comma + 1;
    }
    return frames;
}

}

std::optional<SpriteSheet> SpriteSheet::fromJson(std::string_view text, Vec2 textureSize, Rect region, std::string* error)
{
    if (error) error->clear();
    try
    {
        const json j = json::parse(text);
        if (!j.is_object()) throw std::invalid_argument("Sprite sheet must be an object");
        if (textureSize.x <= 0 || textureSize.y <= 0) throw std::invalid_argument("Sprite sheet texture has no size");
        SpriteSheet sheet;
        const Vec2 area{region.w * textureSize.x, region.h * textureSize.y}; // region size in pixels
        auto uv = [&](float x, float y, float w, float h) {
            if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > area.x + 0.5f || y + h > area.y + 0.5f) throw std::invalid_argument("Frame outside the sheet");
            return Rect{region.x + x / textureSize.x, region.y + y / textureSize.y, w / textureSize.x, h / textureSize.y};
        };
        if (j.contains("frames"))
        {
            for (const json& f : j.at("frames"))
            {
                const float w = f.at("w").get<float>(), h = f.at("h").get<float>();
                sheet.frames.push_back(uv(f.at("x").get<float>(), f.at("y").get<float>(), w, h));
                if (sheet.frames.size() == 1) sheet.frameSize = {w, h};
            }
        }
        else
        {
            const int fw = j.at("frameWidth").get<int>(), fh = j.at("frameHeight").get<int>();
            const int margin = j.value("margin", 0), spacing = j.value("spacing", 0);
            if (fw <= 0 || fh <= 0 || margin < 0 || spacing < 0) throw std::invalid_argument("Invalid frame size");
            const int columns = (static_cast<int>(area.x) - 2 * margin + spacing) / (fw + spacing);
            const int rows = (static_cast<int>(area.y) - 2 * margin + spacing) / (fh + spacing);
            const int count = std::min(j.value("count", columns * rows), columns * rows);
            for (int i = 0; i < count; ++i)
                sheet.frames.push_back(uv(static_cast<float>(margin + i % columns * (fw + spacing)), static_cast<float>(margin + i / columns * (fh + spacing)),
                    static_cast<float>(fw), static_cast<float>(fh)));
            sheet.frameSize = {static_cast<float>(fw), static_cast<float>(fh)};
        }
        if (sheet.frames.empty()) throw std::invalid_argument("Sprite sheet has no frames");
        if (j.contains("clips"))
        {
            for (auto it = j.at("clips").begin(); it != j.at("clips").end(); ++it)
            {
                const json& c = it.value();
                AnimationClip clip;
                clip.frames = parseFrameList(c.at("frames"));
                clip.fps = c.value("fps", 10.0f);
                clip.loop = c.value("loop", true);
                clip.next = c.value("next", "");
                if (c.contains("durations")) clip.durations = c.at("durations").get<std::vector<float>>();
                if (clip.frames.empty() || !std::isfinite(clip.fps) || clip.fps <= 0 || clip.fps > 1000) throw std::invalid_argument("Clip \"" + it.key() + "\" needs frames and a positive fps");
                for (int f : clip.frames) if (f < 0 || static_cast<size_t>(f) >= sheet.frames.size()) throw std::invalid_argument("Clip \"" + it.key() + "\" uses a missing frame");
                if (!clip.durations.empty() && clip.durations.size() != clip.frames.size()) throw std::invalid_argument("Clip durations must match its frames");
                for (float d : clip.durations) if (!std::isfinite(d) || d <= 0) throw std::invalid_argument("Frame durations must be positive");
                if (c.contains("events"))
                    for (auto e = c.at("events").begin(); e != c.at("events").end(); ++e)
                    {
                        const int at = std::stoi(e.key());
                        if (at < 0 || static_cast<size_t>(at) >= clip.frames.size()) throw std::invalid_argument("Event on a missing clip frame");
                        clip.events[at] = e.value().get<std::string>();
                    }
                sheet.clips[it.key()] = std::move(clip);
            }
            for (const auto& [name, clip] : sheet.clips)
                if (!clip.next.empty() && !sheet.clips.contains(clip.next)) throw std::invalid_argument("Clip \"" + name + "\" continues into a missing clip");
        }
        return sheet;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return std::nullopt; }
}

const AnimationClip* SpriteSheet::clip(std::string_view name) const
{
    const auto found = clips.find(name);
    return found == clips.end() ? nullptr : &found->second;
}

void SpriteSheet::draw(Renderer& renderer, int frame, const Rect& dest, Color tint, bool flipX) const
{
    if (frame < 0 || static_cast<size_t>(frame) >= frames.size()) return;
    Rect uv = frames[static_cast<size_t>(frame)];
    if (flipX) { uv.x += uv.w; uv.w = -uv.w; }
    renderer.drawSpriteRegion(texture, dest, uv, tint);
}

void SpriteAnimator::play(const SpriteSheet& sheet, std::string_view clip, bool restart)
{
    if (clip == clip_ && !restart) return;
    if (!sheet.clip(clip)) return;
    clip_ = clip;
    index_ = 0;
    elapsed_ = 0;
    finished_ = false;
    started_ = false;
}

void SpriteAnimator::update(const SpriteSheet& sheet, double deltaSeconds, const EventHandler& onEvent)
{
    const AnimationClip* clip = sheet.clip(clip_);
    if (!clip || finished_) return;
    auto enter = [&] {
        if (!onEvent) return;
        if (const auto event = clip->events.find(static_cast<int>(index_)); event != clip->events.end()) onEvent(event->second);
    };
    if (!started_) { started_ = true; enter(); }
    if (speed <= 0 || !std::isfinite(deltaSeconds) || deltaSeconds <= 0) return;
    elapsed_ += deltaSeconds * speed;
    for (int guard = 0; guard < 100000 && elapsed_ >= clip->frameSeconds(index_); ++guard)
    {
        elapsed_ -= clip->frameSeconds(index_);
        if (index_ + 1 < clip->frames.size()) ++index_;
        else if (clip->loop) index_ = 0;
        else if (!clip->next.empty() && sheet.clip(clip->next))
        {
            clip_ = clip->next;
            clip = sheet.clip(clip_);
            index_ = 0;
        }
        else
        {
            finished_ = true;
            elapsed_ = 0;
            return;
        }
        enter();
    }
}

int SpriteAnimator::frame(const SpriteSheet& sheet) const
{
    const AnimationClip* clip = sheet.clip(clip_);
    return clip && index_ < clip->frames.size() ? clip->frames[index_] : 0;
}

}
