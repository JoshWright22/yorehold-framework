#include "yorehold/framework/graphics/Particles.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace yh
{

namespace
{
constexpr float pi = 3.14159265359f;

Range rangeValue(const nlohmann::json& j, std::string_view name, Range fallback)
{
    auto it = j.find(std::string(name));
    if (it == j.end())
        return fallback;
    Range r;
    if (it->is_number())
        r.min = r.max = it->get<float>();
    else if (it->is_array() && it->size() == 2)
        r = {(*it)[0].get<float>(), (*it)[1].get<float>()};
    else
        throw std::invalid_argument(std::string(name) + " needs a number or [min, max]");
    if (!std::isfinite(r.min) || !std::isfinite(r.max) || r.min > r.max)
        throw std::invalid_argument(std::string(name) + " has an invalid range");
    return r;
}

Color colorValue(const nlohmann::json& j, const char* name, Color fallback)
{
    if (!j.contains(name))
        return fallback;
    const auto& a = j.at(name);
    if (!a.is_array() || (a.size() != 3 && a.size() != 4))
        throw std::invalid_argument(std::string(name) + " needs [r,g,b,a]");
    uint8_t values[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (!a[i].is_number_integer())
            throw std::invalid_argument("Colour channels must be integers");
        const int v = a[i].get<int>();
        if (v < 0 || v > 255)
            throw std::invalid_argument("Colour channels must be 0..255");
        values[i] = static_cast<uint8_t>(v);
    }
    return {values[0], values[1], values[2], values[3]};
}

bool valid(const ParticleEffect& e)
{
    const Range ranges[] = {e.life, e.speed, e.angle, e.size, e.spin};
    for (const Range& r : ranges)
        if (!std::isfinite(r.min) || !std::isfinite(r.max) || r.min > r.max)
            return false;
    const float scalars[] = {e.rate, e.duration, e.endSize, e.drag, e.spawnRadius, e.gravity.x, e.gravity.y};
    for (float v : scalars)
        if (!std::isfinite(v))
            return false;
    return e.life.min > 0 && e.size.min >= 0 && e.rate >= 0 && e.rate <= 100000
        && e.burst >= 0 && e.burst <= 100000 && e.duration >= -1 && e.endSize >= 0 && e.drag >= 0 && e.spawnRadius >= 0;
}

Color mix(Color a, Color b, float t)
{
    auto channel = [t](uint8_t x, uint8_t y) { return static_cast<uint8_t>(x + (y - x) * t); };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};
}
}

std::optional<ParticleEffect> ParticleEffect::fromJson(std::string_view json, std::string* error)
{
    if (error)
        error->clear();
    try
    {
        const auto j = nlohmann::json::parse(json);
        if (!j.is_object())
            throw std::invalid_argument("An effect must be an object");
        ParticleEffect e;
        e.name = j.value("name", e.name);
        e.texture = j.value("texture", e.texture);
        e.rate = j.value("rate", e.rate);
        e.burst = j.value("burst", e.burst);
        e.duration = j.value("duration", e.duration);
        e.life = rangeValue(j, "life", e.life);
        e.speed = rangeValue(j, "speed", e.speed);
        e.angle = rangeValue(j, "angle", e.angle);
        e.size = rangeValue(j, "size", e.size);
        e.spin = rangeValue(j, "spin", e.spin);
        e.endSize = j.value("endSize", e.endSize);
        e.drag = j.value("drag", e.drag);
        e.spawnRadius = j.value("spawnRadius", e.spawnRadius);
        if (j.contains("gravity"))
        {
            const auto& g = j.at("gravity");
            if (!g.is_array() || g.size() != 2)
                throw std::invalid_argument("gravity needs [x,y]");
            e.gravity = {g[0].get<float>(), g[1].get<float>()};
        }
        e.startColor = colorValue(j, "startColor", e.startColor);
        e.endColor = colorValue(j, "endColor", e.endColor);
        const std::string blend = j.value("blend", std::string("additive"));
        if (blend == "alpha") e.blend = BlendMode::Alpha;
        else if (blend == "additive") e.blend = BlendMode::Additive;
        else if (blend == "multiply") e.blend = BlendMode::Multiply;
        else throw std::invalid_argument("Unknown particle blend mode");
        if (!valid(e))
            throw std::invalid_argument("Invalid particle effect values");
        return e;
    }
    catch (const std::exception& ex)
    {
        if (error) *error = ex.what();
        return std::nullopt;
    }
}

ParticleSystem::ParticleSystem(size_t capacity) : capacity_(capacity)
{
    for (auto* array : {&x_, &y_, &vx_, &vy_, &age_, &life_, &size_, &rotation_, &spin_})
        array->reserve(capacity);
    style_.reserve(capacity);
    emitters_.reserve(64);
    styles_.reserve(32);
}

float ParticleSystem::random01(uint64_t& rng)
{
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return static_cast<float>((rng * 2685821657736338717ULL) >> 40) / 16777216.0f;
}

uint16_t ParticleSystem::styleFor(const ParticleEffect& effect)
{
    const auto it = std::find(styles_.begin(), styles_.end(), effect);
    if (it != styles_.end())
        return static_cast<uint16_t>(it - styles_.begin());
    if (styles_.size() >= std::numeric_limits<uint16_t>::max())
        throw std::length_error("Too many particle styles; clear the system before loading more");
    styles_.push_back(effect);
    return static_cast<uint16_t>(styles_.size() - 1);
}

void ParticleSystem::spawn(const ParticleEffect& e, Vec2 p, uint16_t style, uint64_t& rng, float age)
{
    if (x_.size() >= capacity_)
        return;
    auto sample = [&rng](Range r) { return r.min + (r.max - r.min) * random01(rng); };
    const float life = sample(e.life);
    const float angle = sample(e.angle) * pi / 180;
    const float speed = sample(e.speed);
    const float radius = std::sqrt(random01(rng)) * e.spawnRadius;
    const float direction = random01(rng) * 2 * pi;
    const float size = sample(e.size);
    const float spin = sample(e.spin) * pi / 180;
    const float rotation = random01(rng) * pi * 2;
    // Consume a whole spawn's random samples even if it died within a long update.
    if (age >= life) return;
    p = p + Vec2{std::cos(direction), std::sin(direction)} * radius;
    float vx = std::cos(angle) * speed, vy = std::sin(angle) * speed;
    // Closed-form drag/gravity integration also handles particles born between updates.
    const float decay = std::exp(-e.drag * age);
    const float travel = e.drag > 0 ? -std::expm1(-e.drag * age) / e.drag : age;
    const float accel = e.drag > 0 ? (age - travel) / e.drag : age * age * 0.5f;
    x_.push_back(p.x + vx * travel + e.gravity.x * accel);
    y_.push_back(p.y + vy * travel + e.gravity.y * accel);
    vx_.push_back(vx * decay + e.gravity.x * travel);
    vy_.push_back(vy * decay + e.gravity.y * travel);
    age_.push_back(age);
    life_.push_back(life);
    size_.push_back(size);
    rotation_.push_back(rotation + spin * age);
    spin_.push_back(spin);
    style_.push_back(style);
}

ParticleSystem::EmitterId ParticleSystem::start(const ParticleEffect& effect, Vec2 position, uint64_t seed)
{
    if (!valid(effect) || !std::isfinite(position.x) || !std::isfinite(position.y))
        throw std::invalid_argument("Invalid particle effect or position");
    if (nextId_ == 0)
        throw std::overflow_error("Particle emitter ids exhausted");
    Emitter emitter{nextId_++, effect, position};
    emitter.rng = seed ? seed : (rng_ += 0x9E3779B97F4A7C15ULL);
    if (!emitter.rng) emitter.rng = 1;
    emitter.style = styleFor(effect);
    for (int i = 0; i < effect.burst && x_.size() < capacity_; ++i)
        spawn(effect, position, emitter.style, emitter.rng);
    emitters_.push_back(std::move(emitter));
    return emitters_.back().id;
}

void ParticleSystem::burst(const ParticleEffect& effect, Vec2 position, int count)
{
    ParticleEffect one = effect;
    one.rate = 0;
    one.duration = 0;
    if (count >= 0) one.burst = count;
    start(one, position);
}

void ParticleSystem::move(EmitterId id, Vec2 position)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) return;
    for (auto& e : emitters_)
        if (e.id == id) e.position = position;
}

void ParticleSystem::stop(EmitterId id)
{
    for (auto& e : emitters_)
        if (e.id == id) e.running = false;
}

void ParticleSystem::clear()
{
    emitters_.clear();
    styles_.clear();
    for (auto* a : {&x_, &y_, &vx_, &vy_, &age_, &life_, &size_, &rotation_, &spin_}) a->clear();
    style_.clear();
}

size_t ParticleSystem::liveEmitters() const
{
    return static_cast<size_t>(std::count_if(emitters_.begin(), emitters_.end(), [](const Emitter& e) { return e.running; }));
}

void ParticleSystem::update(double deltaSeconds)
{
    if (deltaSeconds <= 0 || !std::isfinite(deltaSeconds)) return;
    const float dt = static_cast<float>(std::min(deltaSeconds, static_cast<double>(std::numeric_limits<float>::max())));
    for (size_t i = 0; i < x_.size();)
    {
        age_[i] += dt;
        if (age_[i] >= life_[i])
        {
            for (auto* a : {&x_, &y_, &vx_, &vy_, &age_, &life_, &size_, &rotation_, &spin_})
            {
                (*a)[i] = a->back();
                a->pop_back();
            }
            style_[i] = style_.back();
            style_.pop_back();
            continue;
        }
        const auto& e = styles_[style_[i]];
        const float decay = std::exp(-e.drag * dt);
        const float travel = e.drag > 0 ? -std::expm1(-e.drag * dt) / e.drag : dt;
        const float accel = e.drag > 0 ? (dt - travel) / e.drag : dt * dt * 0.5f;
        x_[i] += vx_[i] * travel + e.gravity.x * accel;
        y_[i] += vy_[i] * travel + e.gravity.y * accel;
        vx_[i] = vx_[i] * decay + e.gravity.x * travel;
        vy_[i] = vy_[i] * decay + e.gravity.y * travel;
        rotation_[i] += spin_[i] * dt;
        ++i;
    }
    for (auto& e : emitters_)
    {
        if (!e.running) continue;
        const double active = e.effect.duration < 0 ? deltaSeconds : std::min(deltaSeconds, std::max(0.0, e.effect.duration - e.age));
        const double previousCarry = e.carry;
        e.carry += active * e.effect.rate;
        const double owed = std::floor(e.carry);
        const size_t count = static_cast<size_t>(std::min(owed, static_cast<double>(capacity_ - x_.size())));
        for (size_t n = 0; n < count; ++n)
        {
            const double birth = (n + 1.0 - previousCarry) / e.effect.rate;
            spawn(e.effect, e.position, e.style, e.rng, static_cast<float>(deltaSeconds - birth));
        }
        e.carry -= owed; // drop overflow instead of building an emission backlog
        e.age += deltaSeconds;
        if (e.effect.duration >= 0 && e.age >= e.effect.duration) e.running = false;
    }
    std::erase_if(emitters_, [](const Emitter& e) { return !e.running; });
    if (x_.empty() && emitters_.empty()) styles_.clear();
}

void ParticleSystem::draw(Renderer& renderer)
{
    if (x_.empty()) return;
    if (!haveDot_)
    {
        constexpr int side = 32;
        unsigned char pixels[side * side * 4];
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x)
            {
                const float dx = (x + 0.5f - side / 2.0f) / (side / 2.0f);
                const float dy = (y + 0.5f - side / 2.0f) / (side / 2.0f);
                const float a = std::max(0.0f, 1 - std::sqrt(dx * dx + dy * dy));
                auto* p = pixels + (y * side + x) * 4;
                p[0] = p[1] = p[2] = 255;
                p[3] = static_cast<uint8_t>(a * a * 255);
            }
        dot_ = renderer.createTexture(side, side, pixels, true);
        releaseDot_ = renderer.textureRelease(dot_);
        haveDot_ = true;
    }
    const BlendMode previous = renderer.blendMode();
    for (size_t i = 0; i < x_.size(); ++i)
    {
        const auto& e = styles_[style_[i]];
        const float t = age_[i] / life_[i];
        const float size = size_[i] * (1 + (e.endSize - 1) * t);
        if (!renderer.visible({x_[i] - size / 2, y_[i] - size / 2, size, size})) continue;
        renderer.setBlendMode(e.blend);
        const TextureId texture = !e.texture.empty() && textureLookup ? textureLookup(e.texture) : dot_;
        renderer.drawSpriteRotated(texture, {x_[i], y_[i]}, {size, size}, rotation_[i], mix(e.startColor, e.endColor, t));
    }
    renderer.setBlendMode(previous);
}

}
