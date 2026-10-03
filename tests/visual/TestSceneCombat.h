#pragma once

#include "RpgSamples.h"

#include <yorehold/framework/graphics/Renderer.h>
#include <yorehold/framework/input/Input.h>
#include <yorehold/framework/rpg/Combat.h>
#include <yorehold/framework/testing/TestScene.h>
#include <yorehold/framework/ui/Ui.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

#include <cstdio>
#include <memory>

// M6: turn-based combat. Party of four vs three goblins: initiative, turns, action economy, attacks.
// The goblins take their own turns. Same seed = same fight, so fights can be replayed exactly.
class TestSceneCombat : public yh::TestScene
{
public:
    TestSceneCombat() { reset(SDL_GetTicks()); }

    bool handleEvent(const SDL_Event& event) override
    {
        input_.handle(event);
        return true;
    }

    void update(double deltaSeconds) override
    {
        // Goblins act on their own after a short pause so you can follow along.
        if (!encounter_->started() || encounter_->finished() || encounter_->current().team != 1)
            return;
        enemyTimer_ += deltaSeconds;
        if (enemyTimer_ < 0.6)
            return;
        enemyTimer_ = 0;
        std::vector<size_t> targets;
        for (size_t i = 0; i < encounter_->order().size(); i++)
        {
            const yh::Combatant& c = encounter_->order()[i];
            if (c.team == 0 && !c.character->down())
                targets.push_back(i);
        }
        if (!targets.empty())
            encounter_->attack(targets[encounter_->random().range(0, static_cast<int>(targets.size()) - 1)]);
        encounter_->nextTurn();
    }

    void draw(yh::Renderer& renderer) override
    {
        renderer.clear({12, 14, 22, 255});
        ui_.begin(renderer, input_);
        const yh::Rect bounds = renderer.bounds();
        char text[160];

        float x = 10, y = 10;
        std::snprintf(text, sizeof(text), "Seed %llu   Round %d", static_cast<unsigned long long>(seed_), encounter_->round());
        ui_.label({x, y}, text, ui_.theme.accent);
        y += 30;

        // Initiative order. Click an enemy row to target it.
        for (size_t i = 0; i < encounter_->order().size(); i++)
        {
            const yh::Combatant& c = encounter_->order()[i];
            const yh::Rect rowRect{x, y, 470, 30};
            const bool isCurrent = encounter_->started() && i == encounter_->currentIndex();
            if (ui_.hovered(rowRect) && input_.buttonClicked(yh::MouseButton::Left) && c.team == 1)
                target_ = i;
            renderer.fillRect(rowRect, isCurrent ? yh::Color{58, 52, 92, 255} : yh::Color{22, 24, 34, 255});
            if (target_ && *target_ == i)
                renderer.drawRect(rowRect, ui_.theme.bad, 2);
            std::snprintf(text, sizeof(text), "%s%2d %-8s HP %2d/%-2d AC %d", isCurrent ? ">" : " ", c.initiative,
                c.character->name.substr(0, 8).c_str(), c.character->hp, c.character->maxHp(), c.character->armorClass(rules_));
            const yh::Color color = c.character->down() ? ui_.theme.textDim : c.team == 0 ? ui_.theme.good : ui_.theme.bad;
            ui_.label({x + 8, y + 7}, text, color);
            y += 34;
        }

        y += 10;
        if (!encounter_->started())
        {
            if (ui_.button({x, y, 280, 32}, "Roll initiative"))
                encounter_->start();
        }
        else if (encounter_->finished())
        {
            ui_.label({x, y + 6}, encounter_->winningTeam() == 0 ? "Victory!" : "The party falls...", ui_.theme.accent);
        }
        else
        {
            yh::Combatant& current = encounter_->current();
            const bool myTurn = current.team == 0;
            std::snprintf(text, sizeof(text), "%s: action %s, move %d sq", current.character->name.c_str(),
                current.budget.action ? "ready" : "used", current.budget.movementLeft);
            ui_.label({x, y}, text);
            y += 30;
            const bool targetAlive = target_ && !encounter_->order()[*target_].character->down();
            if (ui_.button({x, y, 150, 32}, "Attack", myTurn && current.budget.action && targetAlive))
                encounter_->attack(*target_);
            if (ui_.button({x + 160, y, 100, 32}, "Dash", myTurn && current.budget.action))
                encounter_->dash();
            if (ui_.button({x + 270, y, 100, 32}, "Move 1", myTurn && current.budget.movementLeft > 0))
                encounter_->spendMovement(1);
            if (ui_.button({x + 380, y, 90, 32}, "End", myTurn))
                encounter_->nextTurn();
            y += 40;
            if (myTurn && !targetAlive)
                ui_.label({x, y}, "Click a goblin to target it", ui_.theme.textDim);
        }
        if (ui_.button({x, bounds.h - 44, 180, 32}, "New fight"))
            reset(SDL_GetTicks());
        if (ui_.button({x + 190, bounds.h - 44, 280, 32}, "Replay same seed"))
            reset(seed_);

        const float logX = 500;
        ui_.log({logX, 10, std::max(200.0f, bounds.w - logX - 10), bounds.h - 20}, encounter_->log());
        input_.endFrame();
    }

    const char* help() const override { return "M6: initiative, turns, attacks; goblins fight back"; }

private:
    void reset(uint64_t seed)
    {
        seed_ = seed;
        yh::Random random(seed);
        party_.clear();
        const char* names[] = {"Astra", "Brom", "Cel", "Dax"};
        for (const char* name : names)
            party_.push_back(rpgsamples::hero(rules_, name, "Fighter", random));
        party_.push_back(rpgsamples::goblin(rules_, "Gob", random));
        party_.push_back(rpgsamples::goblin(rules_, "Snik", random));
        party_.push_back(rpgsamples::goblin(rules_, "Rattle", random));

        encounter_ = std::make_unique<yh::Encounter>(rules_, seed);
        for (size_t i = 0; i < party_.size(); i++)
            encounter_->add(party_[i], i < 4 ? 0 : 1);
        target_.reset();
        enemyTimer_ = 0;
    }

    yh::Input input_;
    yh::Ui ui_;
    yh::Ruleset rules_ = yh::Ruleset::modern();
    std::vector<yh::Character> party_; // never resized after reset(), so the encounter's pointers stay valid
    std::unique_ptr<yh::Encounter> encounter_;
    std::optional<size_t> target_;
    uint64_t seed_ = 0;
    double enemyTimer_ = 0;
};
