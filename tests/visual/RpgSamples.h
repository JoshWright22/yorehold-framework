#pragma once

#include <yorehold/framework/rpg/Character.h>

// Sample gear and characters for the RPG test scenes.
namespace rpgsamples
{

inline yh::Item longsword()
{
    yh::Item i{"longsword", "Longsword", "mainHand", "1d8"};
    i.weight = 3;
    i.value = 1500;
    return i;
}

inline yh::Item dagger()
{
    yh::Item i{"dagger", "Dagger", "mainHand", "1d4", "dex"};
    i.weight = 1;
    i.value = 200;
    return i;
}

inline yh::Item chainShirt()
{
    yh::Item i{"chainShirt", "Chain shirt", "armor"};
    i.weight = 20;
    i.value = 5000;
    i.modifiers = {{"ac", yh::Modifier::Op::Override, 13, ""}};
    return i;
}

inline yh::Item shield()
{
    yh::Item i{"shield", "Shield", "offHand"};
    i.weight = 6;
    i.value = 1000;
    i.modifiers = {{"ac", yh::Modifier::Op::Add, 2, ""}};
    return i;
}

inline yh::Item ringOfStrength()
{
    yh::Item i{"ringStr", "Ring of might", "ring"};
    i.value = 50000;
    i.modifiers = {{"str", yh::Modifier::Op::Add, 2, ""}};
    return i;
}

inline yh::Item rations()
{
    yh::Item i{"rations", "Rations"};
    i.weight = 2;
    i.quantity = 5;
    return i;
}

inline yh::Character hero(const yh::Ruleset& rules, std::string name, std::string characterClass, yh::Random& random, bool equip = true)
{
    yh::Character c = yh::makeRandomCharacter(rules, std::move(name), std::move(characterClass), random);
    c.hitDie = "1d10";
    c.stats.setBase("maxHp", static_cast<float>(10 + c.abilityModifier(rules, "con") + 6));
    c.hp = c.maxHp();
    c.proficiencies = {"weapons", "athletics", "perception", "str", "con"};
    c.inventory = {longsword(), dagger(), chainShirt(), shield(), ringOfStrength(), rations()};
    c.resources["healingSurge"] = {2, 2};
    if (equip)
    {
        c.equip(0);
        c.equip(2);
        c.equip(3);
    }
    return c;
}

inline yh::Character goblin(const yh::Ruleset& rules, std::string name, yh::Random& random)
{
    yh::Character c = yh::makeRandomCharacter(rules, std::move(name), "Goblin", random);
    c.stats.setBase("maxHp", 7);
    c.hp = 7;
    c.stats.setBase("ac", 13);
    c.inventory = {dagger()};
    c.equip(0);
    return c;
}

}
