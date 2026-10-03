#include "Checks.h"
#include "CutsceneTests.h"

#include <yorehold/framework/animation/Cutscene.h>

#include <cmath>
#include <string>
#include <vector>

namespace regression
{

void cutscenes()
{
    const char* script = R"({"steps": [
        {"bars": true},
        {"event": "music"},
        {"camera": [2000, 1000], "zoom": 2, "seconds": 2, "wait": false},
        {"caption": "The goblins are gone.", "seconds": 1},
        {"event": "halfway"},
        {"pause": 1},
        {"title": "The End", "seconds": 2},
        {"fade": [0, 0, 0, 255], "seconds": 1},
        {"event": "done"}
    ]})";
    auto cutscene = yh::Cutscene::fromJson(script);
    CHECK(cutscene && cutscene->steps.size() == 9);
    if (!cutscene) return;

    yh::Camera camera;
    camera.setBounds({0, 0, 4000, 4000});
    camera.setViewport({800, 600});
    camera.jumpTo({1000, 1000}, 1);
    std::vector<std::string> events;
    auto record = [&](std::string_view name) { events.emplace_back(name); };

    cutscene->start();
    CHECK(cutscene->running());
    cutscene->update(0, camera, record);
    // Bars and the first event are instant; the camera move doesn't block the caption.
    CHECK(events == std::vector<std::string>{"music"});
    cutscene->update(0.5, camera, record);
    CHECK(camera.position().x > 1000 && camera.position().x < 2000);
    cutscene->update(0.6, camera, record); // caption (1 s) ends, "halfway" fires while the camera still moves
    CHECK(events.size() == 2 && events[1] == "halfway");
    CHECK(camera.position().x < 2000);
    for (int i = 0; i < 100 && cutscene->running(); i++)
        cutscene->update(0.1, camera, record);
    CHECK(!cutscene->running());
    CHECK(std::abs(camera.position().x - 2000) < 0.01f && std::abs(camera.zoom() - 2) < 0.001f);
    CHECK(events == (std::vector<std::string>{"music", "halfway", "done"}));

    // Skipping lands on the end state and still fires every event that hadn't happened.
    events.clear();
    camera.jumpTo({1000, 1000}, 1);
    cutscene->start();
    cutscene->update(0.1, camera, record);
    cutscene->skip(camera, record);
    CHECK(!cutscene->running() && events == (std::vector<std::string>{"music", "halfway", "done"}));
    CHECK(std::abs(camera.position().x - 2000) < 0.01f);

    CHECK(!yh::Cutscene::fromJson(R"({"steps": [{"dance": true}]})"));
    CHECK(!yh::Cutscene::fromJson(R"({"steps": [{"pause": -1}]})"));
    CHECK(!yh::Cutscene::fromJson(R"({"steps": [{"fade": [0, 0, 0]}]})"));
    CHECK(!yh::Cutscene::fromJson(R"({"steps": [{"camera": [1]}]})"));
    CHECK(!yh::Cutscene::fromJson(R"({"steps": [{"event": ""}]})"));
    auto empty = yh::Cutscene::fromJson(R"({"steps": []})");
    CHECK(empty);
    if (empty)
    {
        empty->start();
        CHECK(!empty->running());
    }
}

}
