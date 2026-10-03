#pragma once

#include <yorehold/framework/assets/Assets.h>
#include <yorehold/framework/assets/FileSystem.h>
#include <yorehold/framework/input/Input.h>
#include <yorehold/framework/rpg/Character.h>
#include <yorehold/framework/rpg/Dialogue.h>
#include <yorehold/framework/testing/TestScene.h>
#include <yorehold/framework/ui/Ui.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <memory>
#include <stdexcept>

// F9: an authored conversation, flag-gated replies and checks using the existing character sheet.
class TestSceneDialogue : public yh::TestScene
{
public:
    TestSceneDialogue()
    {
        files_.mountFolder(YH_FRAMEWORK_ASSETS, "framework");
        const auto text = files_.readText("dialogue/gatekeeper.json");
        std::string error;
        const auto dialogue = text ? yh::Dialogue::fromJson(*text, &error) : std::nullopt;
        if (!dialogue) throw std::runtime_error("Could not load gatekeeper dialogue: " + error);
        dialogue_ = *dialogue;
        hero_.stats.setBase("cha", 16);
        hero_.level = 1;
        hero_.proficiencies.insert("persuasion");
        reset();
    }

    bool handleEvent(const SDL_Event& event) override
    {
        input_.handle(event);
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            if (event.key.key == SDLK_R) reset();
            else if (event.key.key == SDLK_S) saveCheckpoint();
            else if (event.key.key == SDLK_L) restoreCheckpoint();
            else if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9)
                choose(static_cast<size_t>(event.key.key - SDLK_1));
        }
        return true;
    }

    void draw(yh::Renderer& renderer) override
    {
        if (!assets_) assets_ = std::make_unique<yh::Assets>(files_, renderer);
        ui_.theme.font = assets_->font("fonts/AtkinsonHyperlegible-Regular.ttf", 20);
        ui_.begin(renderer, input_);
        renderer.clear({14, 18, 28, 255});

        const auto bounds = renderer.bounds();
        const float leftWidth = std::max(240.0f, bounds.w * 0.64f - 32);
        const float right = leftWidth + 48;
        ui_.label({24, 20}, "A conversation at the keep", ui_.theme.accent);
        ui_.label({24, 52}, "1-9: reply | R: replay | S: save | L: restore", ui_.theme.textDim);
        float y = 104;
        if (const auto* node = session_->current())
        {
            ui_.label({24, y}, node->speaker, ui_.theme.accent);
            y += 36;
            if (ui_.theme.font)
            {
                for (const auto& line : ui_.theme.font->wrap(node->text, leftWidth))
                {
                    ui_.label({24, y}, line);
                    y += ui_.lineHeight();
                }
            }
            else { ui_.label({24, y}, node->text); y += 60; }
            y += 24;
            const auto choices = session_->choices();
            for (size_t i = 0; i < choices.size(); ++i)
            {
                const std::string label = std::to_string(i + 1) + ". " + choices[i]->text;
                if (replyButton(label, leftWidth, y)) { choose(i); break; }
            }
            if (session_->finished()) ui_.label({24, y}, "Conversation complete", ui_.theme.good);
        }
        else ui_.label({24, y}, "You leave the gatekeeper.", ui_.theme.textDim);

        ui_.label({right, 104}, "Story flags", ui_.theme.accent);
        float flagY = 142;
        for (const auto& flag : session_->flags())
        {
            ui_.label({right, flagY}, flag, ui_.theme.good);
            flagY += ui_.lineHeight();
        }
        ui_.log({right, std::max(300.0f, flagY + 20), std::max(100.0f, bounds.w - right - 24),
            std::max(100.0f, bounds.h - std::max(300.0f, flagY + 20) - 80)}, log_);

        if (ui_.button({24, bounds.h - 60, 180, 40}, "Replay conversation")) reset();
        if (ui_.toggle({214, bounds.h - 60, 140, 40}, "Advantage", advantage_)) advantage_ = !advantage_;
        if (ui_.button({364, bounds.h - 60, 160, 40}, "Save checkpoint")) saveCheckpoint();
        if (ui_.button({534, bounds.h - 60, 140, 40}, "Restore", !checkpoint_.empty())) restoreCheckpoint();
        input_.endFrame();
    }

    const char* help() const override { return "Branching dialogue, story flags and seeded character checks"; }

private:
    bool replyButton(const std::string& text, float width, float& y)
    {
        const auto lines = ui_.theme.font ? ui_.theme.font->wrap(text, width - 20) : std::vector<std::string>{text};
        const float height = std::max(44.0f, static_cast<float>(lines.size()) * ui_.lineHeight() + 12);
        const bool chosen = ui_.button({24, y, width, height}, "");
        float lineY = y + 6;
        for (const auto& line : lines)
        {
            ui_.label({34, lineY}, line);
            lineY += ui_.lineHeight();
        }
        y += height + 10;
        return chosen;
    }

    void reset()
    {
        session_ = std::make_unique<yh::DialogueSession>(dialogue_);
        random_ = yh::Random(73);
        log_.clear();
        advantage_ = false;
    }

    void saveCheckpoint()
    {
        checkpoint_ = session_->snapshot();
        checkpointRandom_ = random_;
        checkpointAdvantage_ = advantage_;
        log_.push_back("Checkpoint saved");
    }

    void restoreCheckpoint()
    {
        if (checkpoint_.empty()) return;
        std::string error;
        if (!session_->restore(checkpoint_, &error)) throw std::runtime_error(error);
        random_ = checkpointRandom_;
        advantage_ = checkpointAdvantage_;
        log_ = {"Checkpoint restored"};
    }

    void choose(size_t index)
    {
        const auto choices = session_->choices();
        if (index >= choices.size()) return;
        const std::string text = choices[index]->text;
        const auto result = session_->choose(choices[index]->id, [&](std::string_view skill) {
            return hero_.rollCheck(rules_, skill, advantage_ ? yh::Advantage::Advantage : yh::Advantage::None, random_);
        });
        if (!result) return;
        log_.push_back(text);
        if (result->roll)
        {
            log_.push_back(result->roll->describe());
            log_.push_back(result->passed ? "Check passed" : "Check failed");
        }
    }

    yh::FileSystem files_;
    std::unique_ptr<yh::Assets> assets_;
    yh::Input input_;
    yh::Ui ui_;
    yh::Dialogue dialogue_;
    std::unique_ptr<yh::DialogueSession> session_;
    yh::Ruleset rules_ = yh::Ruleset::modern();
    yh::Character hero_;
    yh::Random random_{73};
    yh::Random checkpointRandom_{73};
    std::string checkpoint_;
    std::vector<std::string> log_;
    bool advantage_ = false;
    bool checkpointAdvantage_ = false;
};
