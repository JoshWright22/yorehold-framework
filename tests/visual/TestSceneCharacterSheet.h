#pragma once

#include "RpgSamples.h"

#include <yorehold/framework/graphics/Renderer.h>
#include <yorehold/framework/input/Input.h>
#include <yorehold/framework/rpg/Character.h>
#include <yorehold/framework/testing/TestScene.h>
#include <yorehold/framework/ui/Ui.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

// M5: a working character sheet. Click anything rollable; every roll goes to the log on the right.
class TestSceneCharacterSheet : public yh::TestScene
{
public:
    TestSceneCharacterSheet()
    {
        ui_.theme.textScale = 1.5f; // a sheet has a lot on it
        newCharacter();
    }

    bool handleEvent(const SDL_Event& event) override
    {
        input_.handle(event);
        return true;
    }

    void draw(yh::Renderer& renderer) override
    {
        renderer.clear({12, 14, 22, 255});
        ui_.begin(renderer, input_);
        const yh::Ruleset& rules = modern_ ? modernRules_ : classicRules_;
        yh::Character& c = character_;
        const float row = 32;
        char text[160];

        // Column 1: identity, rules, abilities, skills.
        float x = 10, y = 10;
        std::snprintf(text, sizeof(text), "%s, level %d %s", c.name.c_str(), c.level, c.characterClass.c_str());
        ui_.label({x, y}, text, ui_.theme.accent);
        std::snprintf(text, sizeof(text), "XP %d (next level at %d)", c.xp,
            c.level - 1 < static_cast<int>(rules.xpForLevel.size()) ? rules.xpForLevel[c.level - 1] : 0);
        ui_.label({x, y += 24}, text, ui_.theme.textDim);
        y += 30;
        if (ui_.toggle({x, y, 150, 28}, "Classic", !modern_))
            modern_ = false;
        if (ui_.toggle({x + 160, y, 150, 28}, "Modern", modern_))
            modern_ = true;
        y += 36;
        const char* advantageNames[3] = {"Normal", "Adv", "Disadv"};
        for (int i = 0; i < 3; i++)
        {
            if (ui_.toggle({x + i * 104.0f, y, 98, 28}, advantageNames[i], static_cast<int>(advantage_) == i))
                advantage_ = static_cast<yh::Advantage>(i);
        }

        y += 44;
        ui_.label({x, y}, "Abilities", ui_.theme.accent);
        y += 26;
        for (const yh::AbilityDefinition& ability : rules.abilities)
        {
            const int score = c.abilityScore(ability.id);
            const int mod = c.abilityModifier(rules, ability.id);
            std::snprintf(text, sizeof(text), "%-3s %2d %+d", upper(ability.id).c_str(), score, mod);
            ui_.label({x, y + 7}, text);
            if (ui_.button({x + 150, y, 80, 28}, "Check"))
                addRoll(ability.name + " check", c.rollCheck(rules, ability.id, advantage_, random_));
            if (ui_.button({x + 236, y, 80, 28}, "Save"))
                addRoll(ability.name + " save", c.rollSave(rules, ability.id, advantage_, random_));
            y += row;
        }

        if (!rules.skills.empty())
        {
            y += 8;
            ui_.label({x, y}, "Skills (prof +" + std::to_string(rules.proficiencyBonus(c.level)) + ")", ui_.theme.accent);
            y += 26;
            for (size_t i = 0; i < rules.skills.size(); i++)
            {
                const yh::SkillDefinition& skill = rules.skills[i];
                std::snprintf(text, sizeof(text), "%s %+d", skill.name.substr(0, 8).c_str(), c.checkModifier(rules, skill.id));
                const yh::Rect cell{x + (i % 2) * 160.0f, y + (i / 2) * 30.0f, 154, 26};
                if (ui_.button(cell, text))
                    addRoll(skill.name, c.rollCheck(rules, skill.id, advantage_, random_));
            }
        }

        // Column 2: health, combat numbers, inventory, conditions, dice.
        x = 350;
        y = 10;
        std::snprintf(text, sizeof(text), "HP %d / %d%s", c.hp, c.maxHp(), c.tempHp ? ("  +" + std::to_string(c.tempHp) + " temp").c_str() : "");
        ui_.label({x, y}, text, c.down() ? ui_.theme.bad : ui_.theme.text);
        ui_.bar({x, y + 24, 320, 14}, c.maxHp() ? static_cast<float>(c.hp) / c.maxHp() : 0, c.hp * 3 < c.maxHp() ? ui_.theme.bad : ui_.theme.good);
        y += 46;
        const int amounts[4] = {-5, -1, 1, 5};
        for (int i = 0; i < 4; i++)
        {
            std::snprintf(text, sizeof(text), "%+d", amounts[i]);
            if (ui_.button({x + i * 64.0f, y, 58, 28}, text))
                amounts[i] < 0 ? (void)c.takeDamage(-amounts[i]) : c.heal(amounts[i]);
        }
        if (ui_.button({x + 256, y, 64, 28}, "T+5"))
            c.tempHp += 5;

        y += 40;
        std::snprintf(text, sizeof(text), "AC %d  Speed %d ft (%d sq)", c.armorClass(rules), c.speedFeet(), c.speedSquares(rules));
        ui_.label({x, y}, text);
        std::snprintf(text, sizeof(text), "Init %+d  Atk %+d  Dmg %s", c.initiativeModifier(rules), c.attackModifier(rules), c.damageDice(rules).c_str());
        ui_.label({x, y += 24}, text);
        y += 28;
        if (ui_.button({x, y, 100, 28}, "Attack"))
            addRoll("Attack", yh::rollD20(c.attackModifier(rules), advantage_, random_));
        if (ui_.button({x + 106, y, 100, 28}, "Damage"))
            addRoll("Damage", yh::roll(c.damageDice(rules), random_));
        if (ui_.button({x + 212, y, 108, 28}, "Initiative"))
            addRoll("Initiative", yh::rollD20(c.initiativeModifier(rules), yh::Advantage::None, random_));

        y += 44;
        std::snprintf(text, sizeof(text), "Inventory  %.0f / %.0f lb", c.carriedWeight(), c.carryCapacity(rules));
        ui_.label({x, y}, text, c.carriedWeight() > c.carryCapacity(rules) ? ui_.theme.bad : ui_.theme.accent);
        y += 26;
        for (size_t i = 0; i < c.inventory.size(); i++)
        {
            const yh::Item& item = c.inventory[i];
            std::snprintf(text, sizeof(text), "%s%s%s", item.equipped ? "* " : "  ", item.name.substr(0, 13).c_str(),
                item.quantity > 1 ? (" x" + std::to_string(item.quantity)).c_str() : "");
            ui_.label({x, y + 7}, text, item.equipped ? ui_.theme.good : ui_.theme.text);
            if (!item.slot.empty() && ui_.button({x + 236, y, 84, 28}, item.equipped ? "Remove" : "Equip"))
                item.equipped ? c.unequip(i) : (void)c.equip(i);
            y += 30;
        }

        y += 8;
        ui_.label({x, y}, "Conditions (3 rounds)", ui_.theme.accent);
        y += 26;
        for (size_t i = 0; i < rules.conditions.size(); i++)
        {
            const yh::ConditionDefinition& def = rules.conditions[i];
            const yh::Rect cell{x + (i % 2) * 164.0f, y + (i / 2) * 32.0f, 156, 28};
            if (ui_.toggle(cell, def.name, c.hasCondition(def.id)))
                c.hasCondition(def.id) ? c.removeCondition(def.id) : c.addCondition(rules, def.id, 3);
        }
        y += ((rules.conditions.size() + 1) / 2) * 32.0f + 4;
        if (ui_.button({x, y, 156, 28}, "End round"))
        {
            c.endRound();
            log_.push_back("-- round ends, conditions tick down --");
        }

        // Column 3: dice tray, sheet actions, log.
        x = 690;
        y = 10;
        const yh::Rect bounds = renderer.bounds();
        const float width = std::max(200.0f, bounds.w - x - 10);
        const char* dice[] = {"d4", "d6", "d8", "d10", "d12", "d20", "d100", "2d6+3", "4d6kh3"};
        for (int i = 0; i < 9; i++)
        {
            const float w = (width - 12) / 3;
            if (ui_.button({x + (i % 3) * (w + 6), y + (i / 3) * 32.0f, w, 28}, dice[i]))
                addRoll(dice[i], yh::roll(dice[i], random_));
        }
        y += 104;
        const float third = (width - 12) / 3;
        if (ui_.button({x, y, third, 28}, "New"))
            newCharacter();
        if (ui_.button({x + third + 6, y, third, 28}, "Save"))
            save();
        if (ui_.button({x + (third + 6) * 2, y, third, 28}, "Load"))
            load();
        y += 32;
        if (ui_.button({x, y, third, 28}, "+100 XP"))
        {
            const int before = c.level;
            c.addXp(rules, 100);
            log_.push_back(c.level > before ? "Level up! Now level " + std::to_string(c.level) : "+100 XP");
        }
        y += 36;
        ui_.log({x, y, width, bounds.h - y - 10}, log_);

        input_.endFrame();
    }

    const char* help() const override { return "M5: sheet - click to roll, equip, damage, conditions, save/load"; }

private:
    static std::string upper(std::string s)
    {
        for (char& ch : s)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return s;
    }

    std::string savePath() const { return std::string(YH_DEV_STATE_DIR) + "/sheet-test.json"; }

    void newCharacter()
    {
        const char* names[] = {"Astra", "Brom", "Cel", "Dax", "Eira", "Fenn"};
        character_ = rpgsamples::hero(modernRules_, names[random_.range(0, 5)], "Fighter", random_);
        log_.push_back("Rolled a new character: " + character_.name);
    }

    void addRoll(const std::string& what, const yh::RollResult& result)
    {
        std::string line = what + " " + result.describe();
        if (result.natural20())
            line += "  NAT 20!";
        else if (result.natural1())
            line += "  nat 1";
        log_.push_back(line);
    }

    void save()
    {
        std::error_code error;
        std::filesystem::create_directories(YH_DEV_STATE_DIR, error);
        std::ofstream(savePath()) << character_.toJson();
        log_.push_back("Saved to .dev/sheet-test.json");
    }

    void load()
    {
        std::ifstream file(savePath());
        std::stringstream text;
        text << file.rdbuf();
        std::string error;
        if (std::optional<yh::Character> loaded = yh::Character::fromJson(text.str(), &error))
        {
            character_ = std::move(*loaded);
            log_.push_back("Loaded " + character_.name);
        }
        else
        {
            log_.push_back("Load failed: " + error);
        }
    }

    yh::Input input_;
    yh::Ui ui_;
    yh::Ruleset classicRules_ = yh::Ruleset::classic();
    yh::Ruleset modernRules_ = yh::Ruleset::modern();
    bool modern_ = true;
    yh::Advantage advantage_ = yh::Advantage::None;
    yh::Random random_{SDL_GetTicks()};
    yh::Character character_;
    std::vector<std::string> log_;
};
