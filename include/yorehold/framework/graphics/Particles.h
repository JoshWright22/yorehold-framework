#pragma once

#include "yorehold/framework/graphics/Renderer.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

struct Range
{
    float min = 0;
    float max = 0;
    bool operator==(const Range&) const = default;
};

// What an effect looks like, as data. Skins ship these as JSON, so players can restyle every spell,
// footstep and dice burst without code.
struct ParticleEffect
{
    std::string name;
    std::string texture;        // asset path; empty = built-in soft dot
    float rate = 40;            // particles per second while running
    int burst = 0;              // particles emitted at once on start
    float duration = -1;        // seconds the emitter runs; -1 = until stopped
    Range life{0.6f, 1.2f};     // seconds
    Range speed{40, 120};       // world units per second
    Range angle{0, 360};        // degrees; 270 = up
    Range size{10, 18};
    float endSize = 0.3f;       // size multiplier at end of life
    Range spin{0, 0};           // degrees per second
    Vec2 gravity{0, 0};
    float drag = 0;             // fraction of speed lost per second
    float spawnRadius = 0;
    Color startColor{255, 200, 120, 255};
    Color endColor{255, 60, 20, 0};
    BlendMode blend = BlendMode::Additive;
    bool operator==(const ParticleEffect&) const = default;

    static std::optional<ParticleEffect> fromJson(std::string_view json, std::string* error = nullptr);
};

// All particles in one pool, stored as parallel arrays so updating thousands is a tight loop.
class ParticleSystem
{
public:
    using EmitterId = uint32_t;
    explicit ParticleSystem(size_t capacity = 20000);
    ~ParticleSystem() { if (releaseDot_) releaseDot_(); }
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;
    // Turns an effect's texture path into a TextureId (e.g. via Assets). Unset = soft dot for everything.
    std::function<TextureId(std::string_view)> textureLookup;

    EmitterId start(const ParticleEffect& effect, Vec2 position, uint64_t seed = 0);
    void burst(const ParticleEffect& effect, Vec2 position, int count = -1);
    void move(EmitterId emitter, Vec2 position);
    // Stops emitting; particles already out finish their lives.
    void stop(EmitterId emitter);
    void clear();
    // CPU snapshots are useful for replay checks and editor inspection.
    Vec2 position(size_t particle) const { return {x_.at(particle), y_.at(particle)}; }

    void update(double deltaSeconds);
    void draw(Renderer& renderer);
    size_t liveParticles() const { return x_.size(); }
    size_t liveEmitters() const;

private:
    struct Emitter
    {
        EmitterId id;
        ParticleEffect effect;
        Vec2 position;
        double age = 0;
        double carry = 0; // fractional particles owed
        bool running = true;
        uint64_t rng = 1;
        uint16_t style = 0;
    };

    void spawn(const ParticleEffect& effect, Vec2 position, uint16_t style, uint64_t& rng, float age = 0);
    uint16_t styleFor(const ParticleEffect& effect);
    static float random01(uint64_t& rng);

    std::vector<Emitter> emitters_;
    std::vector<ParticleEffect> styles_; // looks shared by particles (index = style)
    EmitterId nextId_ = 1;
    uint64_t rng_ = 0x9E3779B97F4A7C15ULL;
    TextureId dot_ = 0;
    bool haveDot_ = false;
    size_t capacity_;
    std::function<void()> releaseDot_;

    std::vector<float> x_, y_, vx_, vy_, age_, life_, size_, rotation_, spin_;
    std::vector<uint16_t> style_;
};

}
