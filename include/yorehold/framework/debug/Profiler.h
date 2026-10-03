#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace yh::debug
{

// Numbers and timings shown in the F3 overlay. Game code reports what it cares about:
//   yh::debug::value("tokens", tokens.size());
//   { YH_PROFILE("pathfinding"); nav.path(a, b); }
// Timers add up every scope with the same name during a frame. Everything runs on the main thread.
void value(std::string_view name, double amount);

class Scope
{
public:
    explicit Scope(const char* name);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    const char* name_;
    uint64_t start_;
};

struct Entry
{
    std::string name;
    double current = 0;  // this frame (ms for timers)
    double smoothed = 0; // eased over recent frames, readable while it flickers
    bool timer = false;
    bool reported = false;
};

// Called by the Host once per frame, after drawing.
void endFrame();
const std::vector<Entry>& entries();
void clear();

}

#define YH_PROFILE_JOIN2(a, b) a##b
#define YH_PROFILE_JOIN(a, b) YH_PROFILE_JOIN2(a, b)
#define YH_PROFILE(name) ::yh::debug::Scope YH_PROFILE_JOIN(yhProfileScope, __LINE__)(name)
