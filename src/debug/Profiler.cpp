#include "yorehold/framework/debug/Profiler.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>

namespace yh::debug
{

namespace
{

std::vector<Entry>& table()
{
    static std::vector<Entry> entries;
    return entries;
}

Entry& find(std::string_view name, bool timer)
{
    auto& entries = table();
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.name == name; });
    if (it != entries.end()) return *it;
    entries.push_back({std::string(name), 0, 0, timer, false});
    return entries.back();
}

}

void value(std::string_view name, double amount)
{
    Entry& entry = find(name, false);
    entry.current = amount;
    entry.reported = true;
}

Scope::Scope(const char* name) : name_(name), start_(SDL_GetPerformanceCounter()) {}

Scope::~Scope()
{
    const double ms = static_cast<double>(SDL_GetPerformanceCounter() - start_) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    Entry& entry = find(name_, true);
    entry.current += ms;
    entry.reported = true;
}

void endFrame()
{
    for (Entry& entry : table())
    {
        // Values hold until reported again; timers count zero for frames they didn't run.
        if (!entry.reported && !entry.timer) continue;
        entry.smoothed = entry.smoothed == 0 ? entry.current : entry.smoothed * 0.9 + entry.current * 0.1;
        if (entry.timer) entry.current = 0;
        entry.reported = false;
    }
}

const std::vector<Entry>& entries()
{
    return table();
}

void clear()
{
    table().clear();
}

}
