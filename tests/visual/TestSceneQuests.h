#pragma once

#include <yorehold/framework/assets/Assets.h>
#include <yorehold/framework/assets/FileSystem.h>
#include <yorehold/framework/input/Input.h>
#include <yorehold/framework/rpg/QuestJournal.h>
#include <yorehold/framework/testing/TestScene.h>
#include <yorehold/framework/ui/Ui.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>

// F10: journal entries reveal and update as chapter events set the same flags used by dialogue.
class TestSceneQuests : public yh::TestScene
{
public:
    TestSceneQuests()
    {
        files_.mountFolder(YH_FRAMEWORK_ASSETS, "framework");
        const auto text = files_.readText("quests/keep.json");
        std::string error;
        const auto journal = text ? yh::QuestJournal::fromJson(*text, &error) : std::nullopt;
        if (!journal) throw std::runtime_error("Could not load quest journal: " + error);
        journal_ = *journal;
    }

    bool handleEvent(const SDL_Event& event) override
    {
        input_.handle(event);
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            if (event.key.key == SDLK_R) { flags_.clear(); scroll_ = 0; }
            else if (event.key.key >= SDLK_1 && event.key.key <= SDLK_5)
                storyEvent(static_cast<size_t>(event.key.key - SDLK_1));
        }
        return true;
    }

    void draw(yh::Renderer& renderer) override
    {
        if (!assets_) assets_ = std::make_unique<yh::Assets>(files_, renderer);
        ui_.theme.font = assets_->font("fonts/AtkinsonHyperlegible-Regular.ttf", 20);
        ui_.begin(renderer, input_);
        renderer.clear({14, 18, 28, 255});
        ui_.label({24, 20}, "The chapter journal", ui_.theme.accent);
        ui_.label({24, 52}, "1-5: story events | R: reset | Scroll: journal", ui_.theme.textDim);

        const std::array<const char*, 5> events{"Talk to Mara", "Accept the rescue", "Rescue prisoners", "Defeat the chief", "Abandon the rescue"};
        float y = 104;
        for (size_t i = 0; i < events.size(); ++i)
        {
            if (ui_.button({24, y, 210, 44}, std::to_string(i + 1) + ". " + events[i])) storyEvent(i);
            y += 54;
        }
        if (ui_.button({24, y + 20, 210, 44}, "Reset chapter")) { flags_.clear(); scroll_ = 0; }

        const float width = std::max(200.0f, renderer.bounds().w - 296);
        const auto entries = journal_.entries(flags_);
        float contentHeight = 0;
        for (const auto& entry : entries) contentHeight += cardHeight(*entry.quest, width) + 12;
        ui_.beginScroll({260, 104, width + 12, renderer.bounds().h - 128}, contentHeight, scroll_);
        y = 0;
        if (entries.empty()) ui_.label({8, y}, "No quests discovered yet.", ui_.theme.textDim);
        for (const auto& entry : entries)
        {
            drawQuest(entry, width, y);
            y += cardHeight(*entry.quest, width) + 12;
        }
        ui_.endScroll();
        input_.endFrame();
    }

    const char* help() const override { return "Authored quests, objectives, discovery, completion and failure"; }

private:
    std::vector<std::string> descriptionLines(const yh::Quest& quest, float width)
    {
        return ui_.theme.font ? ui_.theme.font->wrap(quest.description, width - 24) : std::vector<std::string>{quest.description};
    }

    float cardHeight(const yh::Quest& quest, float width)
    {
        return 100 + static_cast<float>(descriptionLines(quest, width).size() + quest.objectives.size()) * ui_.lineHeight();
    }

    void drawQuest(const yh::QuestEntry& entry, float width, float top)
    {
        const auto& quest = *entry.quest;
        const auto status = entry.progress.status;
        const auto color = status == yh::QuestStatus::Failed ? ui_.theme.bad : status == yh::QuestStatus::Completed ? ui_.theme.good : ui_.theme.accent;
        const char* label = status == yh::QuestStatus::Failed ? "Failed" : status == yh::QuestStatus::Completed ? "Completed" : "Active";
        ui_.panel({0, top, width, cardHeight(quest, width)});
        float y = top + 12;
        ui_.label({12, y}, quest.title, ui_.theme.accent);
        y += ui_.lineHeight();
        ui_.label({12, y}, label, color);
        y += ui_.lineHeight() + 8;
        for (const auto& line : descriptionLines(quest, width))
        {
            ui_.label({12, y}, line, ui_.theme.textDim);
            y += ui_.lineHeight();
        }
        y += 10;
        for (size_t i = 0; i < quest.objectives.size(); ++i)
        {
            const bool complete = entry.progress.objectiveComplete[i];
            ui_.label({12, y}, std::string(complete ? "[x] " : "[ ] ") + quest.objectives[i].text,
                complete ? ui_.theme.good : ui_.theme.text);
            y += ui_.lineHeight();
        }
        ui_.bar({12, top + cardHeight(quest, width) - 20, width - 24, 8}, entry.progress.fraction(), color);
    }

    void storyEvent(size_t event)
    {
        switch (event)
        {
        case 0: flags_.insert("met_mara"); break;
        case 1: flags_.insert("keep_gate_open"); flags_.insert("accepted_rescue"); break;
        case 2: flags_.insert("prisoners_safe"); break;
        case 3: flags_.insert("chief_defeated"); break;
        case 4: flags_.insert("abandoned_rescue"); break;
        default: break;
        }
    }

    yh::FileSystem files_;
    std::unique_ptr<yh::Assets> assets_;
    yh::Input input_;
    yh::Ui ui_;
    yh::QuestJournal journal_;
    std::set<std::string> flags_;
    float scroll_ = 0;
};
