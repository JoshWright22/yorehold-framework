#pragma once

#include <cstdint>
#include <stdexcept>

namespace yh
{

// Deterministic random numbers (PCG32). The same seed gives the same rolls on every platform and
// compiler, which std::mt19937 + std::uniform_int_distribution doesn't guarantee. That's what lets
// the server pick a seed and every client replay the exact same dice.
class Random
{
public:
    explicit Random(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL)
    {
        increment_ = (stream << 1u) | 1u;
        next();
        state_ += seed;
        next();
    }

    uint32_t next()
    {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + increment_;
        const uint32_t shifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rotation = static_cast<uint32_t>(old >> 59u);
        return (shifted >> rotation) | (shifted << ((~rotation + 1u) & 31u));
    }

    // Uniform integer in [low, high], without modulo bias.
    int range(int low, int high)
    {
        if (low > high) throw std::invalid_argument("Random range is reversed");
        const uint64_t wide = static_cast<uint64_t>(static_cast<int64_t>(high) - low) + 1;
        if (wide == (uint64_t(1) << 32)) return static_cast<int>(static_cast<int64_t>(low) + next());
        const uint32_t span = static_cast<uint32_t>(wide);
        const uint32_t threshold = (~span + 1u) % span;
        uint32_t value;
        do
            value = next();
        while (value < threshold);
        return static_cast<int>(static_cast<int64_t>(low) + value % span);
    }

private:
    uint64_t state_ = 0;
    uint64_t increment_ = 0;
};

}
