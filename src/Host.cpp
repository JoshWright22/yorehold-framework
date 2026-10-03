#include "yorehold/framework/Host.h"

#include "yorehold/framework/debug/Profiler.h"
#include "yorehold/framework/graphics/Renderer.h"

#include <SDL3/SDL.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace yh
{

namespace
{

constexpr Color clearColor{10, 10, 14, 255};
constexpr Color panelColor{0, 0, 0, 190};
constexpr Color textColor{255, 255, 255, 255};

// fileSafe gives 20261002-140311, otherwise 2026-10-02 14:03:11.
std::string timestamp(bool fileSafe)
{
    SDL_Time now = 0;
    SDL_DateTime t{};
    SDL_GetCurrentTime(&now);
    SDL_TimeToDateTime(now, &t, true);

    char buffer[32];
    if (fileSafe)
        std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d-%02d%02d%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
    else
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
    return buffer;
}

// Frame times for the F3 overlay and the --frames report.
class FrameStats
{
public:
    void add(double seconds)
    {
        recent_[next_] = seconds;
        next_ = (next_ + 1) % recent_.size();
        count_ = std::min(count_ + 1, recent_.size());
        if (keepAll_)
            all_.push_back(seconds);
    }

    void keepAll(bool keep) { keepAll_ = keep; }

    // Recent frame times in seconds, oldest first.
    size_t recentCount() const { return count_; }
    double recent(size_t i) const { return recent_[(next_ + recent_.size() - count_ + i) % recent_.size()]; }

    double recentAverageMs() const
    {
        double total = 0;
        for (size_t i = 0; i < count_; i++)
            total += recent_[i];
        return count_ ? total / static_cast<double>(count_) * 1000.0 : 0.0;
    }

    double recentWorstMs() const
    {
        double worst = 0;
        for (size_t i = 0; i < count_; i++)
            worst = std::max(worst, recent_[i]);
        return worst * 1000.0;
    }

    void logReport() const
    {
        if (all_.empty())
            return;

        std::vector<double> sorted = all_;
        std::sort(sorted.begin(), sorted.end());
        double total = 0;
        for (double s : sorted)
            total += s;

        const double average = total / static_cast<double>(sorted.size()) * 1000.0;
        const double p99 = sorted[sorted.size() * 99 / 100] * 1000.0;
        std::printf("frames=%zu avg_ms=%.3f p99_ms=%.3f worst_ms=%.3f fps=%.1f\n",
            sorted.size(), average, p99, sorted.back() * 1000.0, 1000.0 / average);
        std::fflush(stdout);
    }

private:
    std::array<double, 120> recent_{};
    size_t next_ = 0;
    size_t count_ = 0;
    bool keepAll_ = false;
    std::vector<double> all_;
};

class Host
{
public:
    Host(Game& game, const HostSettings& settings)
        : game_(game), settings_(settings)
    {
    }

    int run()
    {
        // Text boxes draw input-method composition inline; the OS still shows its candidate list.
        SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            return 1;
        }

        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        flags |= SDL_WINDOW_METAL;
#endif
        if (settings_.hidden)
            flags |= SDL_WINDOW_HIDDEN;

        window_ = SDL_CreateWindow(settings_.title.c_str(), settings_.width, settings_.height, flags);
        if (!window_ || !renderer_.init(window_, settings_.vsync))
        {
            SDL_Log("Window or GPU setup failed: %s", SDL_GetError());
            renderer_.shutdown();
            if (window_) SDL_DestroyWindow(window_);
            SDL_Quit();
            return 1;
        }
        stats_.keepAll(settings_.exitAfterFrames > 0);

        if (!settings_.hidden)
            restoreWindowPlacement();
        if (settings_.raise)
        {
            SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
            SDL_RaiseWindow(window_);
        }

        loadScript();
        game_.load();
        loop();

        if (noteOpen_)
            closeNote(false);
        stats_.logReport();
        game_.unload();
        renderer_.shutdown();

        SDL_DestroyWindow(window_);
        SDL_Quit();
        return 0;
    }

private:
    void loop()
    {
        Uint64 last = SDL_GetPerformanceCounter();
        const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
        int frame = 0;

        while (running_)
        {
            playScript(frame);
            SDL_Event event;
            while (SDL_PollEvent(&event))
                handleEvent(event);

            const Uint64 now = SDL_GetPerformanceCounter();
            const double delta = static_cast<double>(now - last) / frequency;
            last = now;
            stats_.add(delta);

            game_.update(settings_.fixedDeltaSeconds > 0 ? settings_.fixedDeltaSeconds : std::min(delta, settings_.maxDeltaSeconds));
            renderer_.beginFrame(clearColor);
            game_.draw(renderer_);
            renderer_.flush();
            frame++;

            if (captureRequested_)
            {
                captureRequested_ = false;
                captureForFeedback();
            }

            const bool lastFrame = settings_.exitAfterFrames > 0 && frame >= settings_.exitAfterFrames;
            if (lastFrame)
            {
                if (!settings_.screenshotOnExit.empty() && renderer_.saveScreenshot(settings_.screenshotOnExit))
                    std::printf("screenshot=%s\n", settings_.screenshotOnExit.c_str());
                running_ = false;
            }

            drawOverlays();
            // Scripted shots include the F3 overlay so automated checks can read it.
            if (!pendingScriptShot_.empty())
            {
                renderer_.flush();
                renderer_.saveScreenshot(pendingScriptShot_);
                std::printf("screenshot=%s\n", pendingScriptShot_.c_str());
                pendingScriptShot_.clear();
            }
            debug::endFrame();
            renderer_.endFrame();
#if defined(__EMSCRIPTEN__)
            emscripten_sleep(settings_.vsync ? 8 : 0);
#endif
        }
    }

    void handleEvent(const SDL_Event& event)
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            running_ = false;
            return;
        }

        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            renderer_.resize(event.window.data1, event.window.data2);

        if ((event.type == SDL_EVENT_WINDOW_MOVED || event.type == SDL_EVENT_WINDOW_RESIZED) && !settings_.hidden)
            saveWindowPlacement();

        if (noteOpen_ && handleNoteEvent(event))
            return;

        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            if (event.key.key == SDLK_F3)
            {
                showStats_ = !showStats_;
                return;
            }
            if (event.key.key == SDLK_F12)
            {
                captureRequested_ = true;
                return;
            }
        }

        SDL_Event scaled = event;
        int logicalWidth = 0, logicalHeight = 0;
        SDL_GetWindowSize(window_, &logicalWidth, &logicalHeight);
        const Vec2 pixels = renderer_.outputSize();
        const float sx = pixels.x / std::max(1, logicalWidth), sy = pixels.y / std::max(1, logicalHeight);
        if (scaled.type == SDL_EVENT_MOUSE_MOTION)
        {
            scaled.motion.x *= sx; scaled.motion.y *= sy;
            scaled.motion.xrel *= sx; scaled.motion.yrel *= sy;
        }
        else if (scaled.type == SDL_EVENT_MOUSE_BUTTON_DOWN || scaled.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            scaled.button.x *= sx; scaled.button.y *= sy;
        }
        game_.handleEvent(scaled);
    }

    struct ScriptStep
    {
        int frame;
        std::string command;
        std::string a;
        std::string b;
    };

    void loadScript()
    {
        if (settings_.inputScript.empty())
            return;
        std::ifstream file(settings_.inputScript);
        std::string line;
        while (std::getline(file, line))
        {
            std::istringstream words(line);
            ScriptStep step{};
            if (words >> step.frame >> step.command)
            {
                if (step.command == "text") std::getline(words >> std::ws, step.a);
                else words >> step.a >> step.b;
                script_.push_back(step);
            }
        }
    }

    // Pushes this frame's scripted events into SDL's queue, as if the player did them.
    void playScript(int frame)
    {
        for (const ScriptStep& step : script_)
        {
            if (step.frame != frame)
                continue;
            SDL_Event e{};
            const Uint8 button = step.a == "right" ? SDL_BUTTON_RIGHT : step.a == "middle" ? SDL_BUTTON_MIDDLE : SDL_BUTTON_LEFT;
            if (step.command == "move")
            {
                e.type = SDL_EVENT_MOUSE_MOTION;
                e.motion.x = std::stof(step.a);
                e.motion.y = std::stof(step.b);
                e.motion.xrel = e.motion.x - scriptMouse_.x;
                e.motion.yrel = e.motion.y - scriptMouse_.y;
                scriptMouse_ = {e.motion.x, e.motion.y};
            }
            else if (step.command == "down" || step.command == "up")
            {
                e.type = step.command == "down" ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                e.button.button = button;
                e.button.down = step.command == "down";
                e.button.x = scriptMouse_.x;
                e.button.y = scriptMouse_.y;
            }
            else if (step.command == "key" || step.command == "keyup")
            {
                e.type = step.command == "key" ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                // Underscores stand in for spaces in key names: "key Left_Ctrl".
                std::string name = step.a;
                std::replace(name.begin(), name.end(), '_', ' ');
                e.key.key = SDL_GetKeyFromName(name.c_str());
                e.key.down = step.command == "key";
            }
            else if (step.command == "text")
            {
                e.type = SDL_EVENT_TEXT_INPUT;
                e.text.text = step.a.c_str(); // script storage stays alive through the event loop
            }
            else if (step.command == "wheel")
            {
                e.type = SDL_EVENT_MOUSE_WHEEL;
                e.wheel.y = std::stof(step.a);
                e.wheel.mouse_x = scriptMouse_.x;
                e.wheel.mouse_y = scriptMouse_.y;
            }
            else if (step.command == "shot")
            {
                pendingScriptShot_ = step.a;
                continue;
            }
            else
            {
                continue;
            }
            SDL_PushEvent(&e);
        }
    }

    // Saved on every move/resize rather than on exit, because the dev watcher kills the process.
    std::string placementFile() const
    {
        return settings_.stateDir.empty() ? std::string() : settings_.stateDir + "/window-" + settings_.title + ".txt";
    }

    void saveWindowPlacement()
    {
        const std::string file = placementFile();
        if (file.empty())
            return;

        int x = 0, y = 0, w = 0, h = 0;
        SDL_GetWindowPosition(window_, &x, &y);
        SDL_GetWindowSize(window_, &w, &h);
        std::error_code error;
        std::filesystem::create_directories(settings_.stateDir, error);
        std::ofstream(file) << x << ' ' << y << ' ' << w << ' ' << h << '\n';
    }

    void restoreWindowPlacement()
    {
        const std::string file = placementFile();
        if (file.empty())
            return;

        int x = 0, y = 0, w = 0, h = 0;
        if (std::ifstream(file) >> x >> y >> w >> h && w > 0 && h > 0)
        {
            SDL_SetWindowSize(window_, w, h);
            SDL_SetWindowPosition(window_, x, y);
        }
    }

    // The note prompt swallows all keyboard input while it's open.
    bool handleNoteEvent(const SDL_Event& event)
    {
        switch (event.type)
        {
        case SDL_EVENT_TEXT_INPUT:
            note_ += event.text.text;
            return true;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
                closeNote(true);
            else if (event.key.key == SDLK_ESCAPE)
                closeNote(false);
            else if (event.key.key == SDLK_BACKSPACE && !note_.empty())
            {
                // Drop one UTF-8 character, not one byte.
                size_t end = note_.size() - 1;
                while (end > 0 && (static_cast<unsigned char>(note_[end]) & 0xC0) == 0x80)
                    end--;
                note_.erase(end);
            }
            return true;
        case SDL_EVENT_KEY_UP:
            return true;
        default:
            return false;
        }
    }

    void captureForFeedback()
    {
        if (settings_.feedbackDir.empty())
        {
            SDL_Log("F12: no feedback folder set");
            return;
        }

        std::error_code error;
        std::filesystem::create_directories(settings_.feedbackDir, error);

        pendingShot_ = timestamp(true) + ".png";
        if (!renderer_.saveScreenshot(settings_.feedbackDir + "/" + pendingShot_))
        {
            pendingShot_.clear();
            return;
        }

        pendingTime_ = timestamp(false);
        pendingContext_ = game_.describe();
        pendingFps_ = 1000.0 / std::max(stats_.recentAverageMs(), 0.001);
        note_.clear();
        noteOpen_ = true;
        SDL_StartTextInput(window_);
    }

    void closeNote(bool keepNote)
    {
        noteOpen_ = false;
        SDL_StopTextInput(window_);

        std::ofstream log(settings_.feedbackDir + "/log.md", std::ios::app);
        log << "- " << pendingTime_ << " | " << settings_.title;
        if (!pendingContext_.empty())
            log << " | " << pendingContext_;
        char fps[32];
        std::snprintf(fps, sizeof(fps), "%.0f fps", pendingFps_);
        log << " | " << fps << " | [" << pendingShot_ << "](" << pendingShot_ << ")";
        if (keepNote && !note_.empty())
            log << ": " << note_;
        log << "\n";

        flashText_ = "Saved feedback " + pendingShot_;
        flashUntil_ = SDL_GetTicks() + 2000;
    }

    void drawOverlays()
    {
        const Vec2 size = renderer_.outputSize();
        const float line = Renderer::lineHeight() + 4;

        if (showStats_)
            drawStats(size);

        if (noteOpen_)
        {
            const float top = size.y - line * 3 - 12;
            renderer_.fillRect({0, top, size.x, line * 3 + 12}, panelColor);
            renderer_.drawText({12, top + 10}, "Screenshot saved. Type a note, Enter to save, Esc to skip:", textColor);
            renderer_.drawText({12, top + 10 + line * 1.5f}, "> " + note_ + ((SDL_GetTicks() / 500) % 2 ? "_" : " "), textColor);
        }
        else if (SDL_GetTicks() < flashUntil_)
        {
            renderer_.fillRect({0, size.y - line - 12, size.x, line + 12}, panelColor);
            renderer_.drawText({12, size.y - line - 4}, flashText_, textColor);
        }
    }
    // F3: frame time, a graph of recent frames, GPU work, and game-reported debug values.
    void drawStats(Vec2 size)
    {
        const RenderStats gpu = renderer_.stats();
        std::vector<std::string> lines;
        char text[128];
        std::snprintf(text, sizeof(text), "%.1f ms (worst %.1f)  %.0f fps",
            stats_.recentAverageMs(), stats_.recentWorstMs(), 1000.0 / std::max(stats_.recentAverageMs(), 0.001));
        lines.emplace_back(text);
        std::snprintf(text, sizeof(text), "%u draws  %u passes  %u verts", gpu.drawCalls, gpu.passes, gpu.vertices);
        lines.emplace_back(text);
        std::snprintf(text, sizeof(text), "%u textures  %.1f MB", gpu.textures, static_cast<double>(gpu.textureBytes) / (1024.0 * 1024.0));
        lines.emplace_back(text);
        const auto& reported = debug::entries();
        const size_t firstReported = lines.size();
        for (const debug::Entry& entry : reported)
        {
            if (entry.timer) std::snprintf(text, sizeof(text), "%s %.2f ms", entry.name.c_str(), entry.smoothed);
            else std::snprintf(text, sizeof(text), "%s %g", entry.name.c_str(), entry.current);
            lines.emplace_back(text);
        }

        const float scale = 1.5f;
        const float line = Renderer::lineHeight(scale) + 2;
        float width = 240;
        for (const std::string& l : lines) width = std::max(width, Renderer::textWidth(l, scale));
        const float graphHeight = 48;
        const float left = size.x - width - 16;
        const float height = line * static_cast<float>(lines.size()) + graphHeight + 24;
        renderer_.fillRect({left, 0, width + 16, height}, panelColor);
        float y = 8;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i == firstReported) y += graphHeight + 8;
            renderer_.drawText({left + 8, y}, lines[i], i < 3 ? textColor : Color{255, 210, 120, 255}, scale);
            y += line;
        }

        // Bars scale to 33 ms; the line marks 60 fps. Slow frames turn red.
        const Rect graph{left + 8, 8 + line * 3 + 4, width, graphHeight};
        renderer_.fillRect(graph, {255, 255, 255, 20});
        const size_t count = stats_.recentCount();
        const float barWidth = graph.w / 120.0f;
        for (size_t i = 0; i < count; ++i)
        {
            const double ms = stats_.recent(i) * 1000.0;
            const float h = std::min(1.0f, static_cast<float>(ms / 33.3)) * graph.h;
            const Color color = ms > 33.4 ? Color{230, 80, 70, 255} : ms > 16.8 ? Color{240, 190, 70, 255} : Color{110, 200, 120, 255};
            renderer_.fillRect({graph.x + barWidth * static_cast<float>(i + 120 - count), graph.y + graph.h - h, std::max(1.0f, barWidth - 1), h}, color);
        }
        renderer_.fillRect({graph.x, graph.y + graph.h * 0.5f, graph.w, 1}, {255, 255, 255, 90});
    }

    Game& game_;
    std::vector<ScriptStep> script_;
    Vec2 scriptMouse_;
    std::string pendingScriptShot_;
    HostSettings settings_;
    SDL_Window* window_ = nullptr;
    Renderer renderer_;
    bool running_ = true;
    FrameStats stats_;
    bool showStats_ = false;

    bool captureRequested_ = false;
    bool noteOpen_ = false;
    std::string note_;
    std::string pendingShot_;
    std::string pendingTime_;
    std::string pendingContext_;
    double pendingFps_ = 0;
    std::string flashText_;
    Uint64 flashUntil_ = 0;
};

}

HostSettings parseHostArgs(int argc, char** argv, HostSettings settings)
{
    for (int i = 1; i < argc; i++)
    {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;

        if (arg == "--frames" && hasValue)
            settings.exitAfterFrames = std::atoi(argv[++i]);
        else if (arg == "--screenshot" && hasValue)
            settings.screenshotOnExit = argv[++i];
        else if (arg == "--hidden")
            settings.hidden = true;
        else if (arg == "--raise")
            settings.raise = true;
        else if (arg == "--input" && hasValue)
            settings.inputScript = argv[++i];
        else if (arg == "--no-vsync")
            settings.vsync = false;
        else if (arg == "--fixed-dt" && hasValue)
            settings.fixedDeltaSeconds = std::clamp(std::strtod(argv[++i], nullptr), 0.0, 1.0);
        else if (arg == "--size" && hasValue)
        {
            // WxH, e.g. 1600x900
            char* end = nullptr;
            settings.width = static_cast<int>(std::strtol(argv[++i], &end, 10));
            if (end && *end == 'x')
                settings.height = static_cast<int>(std::strtol(end + 1, nullptr, 10));
        }
    }

    // A screenshot run with no frame count stops after a few frames so animations have started.
    if (!settings.screenshotOnExit.empty() && settings.exitAfterFrames == 0)
        settings.exitAfterFrames = 30;

    return settings;
}

int run(Game& game, const HostSettings& settings)
{
    Host host(game, settings);
    return host.run();
}

}
