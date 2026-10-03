#pragma once

#include <string>
#include <string_view>

union SDL_Event;

namespace yh
{

class Renderer;

class Game
{
public:
    virtual ~Game() = default;

    virtual void load() {}
    // Called before the renderer/window shuts down; release device-backed game resources here.
    virtual void unload() {}
    virtual void update(double deltaSeconds) { (void)deltaSeconds; }
    virtual void draw(Renderer& renderer) { (void)renderer; }

    // Return true if the event was used, so nothing else reacts to it.
    virtual bool handleEvent(const SDL_Event& event) { (void)event; return false; }

    // Short label saved with feedback screenshots, e.g. the current test scene.
    virtual std::string describe() const { return {}; }
};

struct HostSettings
{
    std::string title = "Yorehold";
    int width = 1280;
    int height = 720;
    bool vsync = true;
    bool hidden = false;

    // Stop after this many frames (0 = run until the window closes) and log frame-time stats.
    int exitAfterFrames = 0;
    // Save the last frame here before exiting. Used with exitAfterFrames for automated screenshots.
    std::string screenshotOnExit;
    // F12 saves screenshots and notes here.
    std::string feedbackDir;
    // Dev builds remember window position and the open test scene here, so relaunches pick up where you were.
    std::string stateDir;
    // Pull the window in front of everything on start (the dev watcher relaunches from the background).
    bool raise = false;
    // Replays scripted input for automated checks. One command per line, "<frame> <command>":
    //   10 move 400 300 | 11 down left | 14 up left | 20 key F6 | 30 text Café | 40 shot path.png
    std::string inputScript;
    // Deterministic simulation for scripted validation/replays; 0 uses the measured frame time.
    double fixedDeltaSeconds = 0;
    double maxDeltaSeconds = 0.1;
};

// Reads --frames N, --screenshot PATH, --hidden, --no-vsync, --size WxH, --raise,
// --input SCRIPT and --fixed-dt SECONDS. Unknown arguments are ignored.
HostSettings parseHostArgs(int argc, char** argv, HostSettings settings = {});

// Opens the window and runs the game loop until the window closes. Returns the process exit code.
// Built-in keys: F3 shows frame times, F12 saves a screenshot and asks for a feedback note.
int run(Game& game, const HostSettings& settings = {});

}
