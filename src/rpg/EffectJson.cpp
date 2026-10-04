// Effects from JSON, and the checks made on them before anything runs.

#include "yorehold/framework/rpg/Effect.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

constexpr int deepest = 6; // steps under steps under steps...

constexpr std::pair<EffectStep::Kind, const char*> kinds[] = {
    {EffectStep::Kind::Damage, "damage"}, {EffectStep::Kind::Heal, "heal"}, {EffectStep::Kind::TempHp, "tempHp"},
    {EffectStep::Kind::Condition, "condition"}, {EffectStep::Kind::Modifier, "modifier"}, {EffectStep::Kind::Move, "move"},
    {EffectStep::Kind::Resource, "resource"}, {EffectStep::Kind::Summon, "summon"}, {EffectStep::Kind::Light, "light"},
    {EffectStep::Kind::Surface, "surface"}, {EffectStep::Kind::Flag, "flag"}, {EffectStep::Kind::Roll, "roll"},
    {EffectStep::Kind::Repeat, "repeat"}, {EffectStep::Kind::Choose, "choose"},
};

[[noreturn]] void fail(const std::string& path, const std::string& what)
{
    throw std::invalid_argument(path + ": " + what);
}

bool oneOf(std::string_view text, std::initializer_list<const char*> names)
{
    return std::any_of(names.begin(), names.end(), [&](const char* name) { return text == name; });
}

std::string text(const json& j, const std::string& path, const char* key, const char* fallback = nullptr)
{
    if (!j.contains(key))
    {
        if (!fallback) fail(path + "." + key, "is needed");
        return fallback;
    }
    if (!j.at(key).is_string() || j.at(key).get<std::string>().empty() || j.at(key).get<std::string>().size() > 64)
        fail(path + "." + key, "is a name of 1 to 64 characters");
    return j.at(key).get<std::string>();
}

int whole(const json& j, const std::string& path, const char* key, int fallback, int low, int high)
{
    if (!j.contains(key))
        return fallback;
    if (!j.at(key).is_number_integer() || j.at(key).get<long long>() < low || j.at(key).get<long long>() > high)
        fail(path + "." + key, "is a whole number from " + std::to_string(low) + " to " + std::to_string(high));
    return j.at(key).get<int>();
}

bool truth(const json& j, const std::string& path, const char* key, bool fallback)
{
    if (!j.contains(key))
        return fallback;
    if (!j.at(key).is_boolean()) fail(path + "." + key, "is true or false");
    return j.at(key).get<bool>();
}

float number(const json& j, const std::string& path, const char* key, float low, float high)
{
    if (!j.contains(key)) fail(path + "." + key, "is needed");
    if (!j.at(key).is_number() || !std::isfinite(j.at(key).get<float>()) || j.at(key).get<float>() < low || j.at(key).get<float>() > high)
        fail(path + "." + key, "is a number from " + std::to_string(static_cast<int>(low)) + " to " + std::to_string(static_cast<int>(high)));
    return j.at(key).get<float>();
}

// Rounds: left out is `fallback`, -1 is "until something ends it".
int rounds(const json& j, const std::string& path, int fallback)
{
    const int value = whole(j, path, "duration", fallback, -1, 100000);
    if (j.contains("duration") && value == 0) fail(path + ".duration", "is a number of rounds, or -1 for until something ends it");
    return value;
}

// An amount: a whole number, dice, or one of the `names`.
std::string amount(const json& j, const std::string& path, const char* key, std::initializer_list<const char*> names, const char* fallback = nullptr)
{
    if (!j.contains(key))
    {
        if (!fallback) fail(path + "." + key, "is needed");
        return fallback;
    }
    const json& value = j.at(key);
    if (value.is_number_integer() && value.get<long long>() >= 0 && value.get<long long>() <= 100000)
        return std::to_string(value.get<int>());
    if (value.is_string() && (oneOf(value.get<std::string>(), names) || DiceExpression::parse(value.get<std::string>())))
        return value.get<std::string>();
    std::string allowed = "is a whole number or dice like \"2d6+1\"";
    for (const char* name : names)
        allowed += std::string(", or \"") + name + "\"";
    fail(path + "." + key, allowed);
}

bool knownEvent(std::string_view name)
{
    return std::any_of(std::begin(conditionEvents), std::end(conditionEvents), [&](const char* known) { return name == known; });
}

bool knownResult(std::string_view name)
{
    return std::any_of(std::begin(effectResults), std::end(effectResults), [&](const char* known) { return name == known; });
}

std::vector<EffectStep> parseSteps(const json& list, const std::string& path, int depth);

EffectStep parseStep(const json& j, const std::string& path, int depth)
{
    if (!j.is_object()) fail(path, "a step is an object with a \"do\"");
    EffectStep step;
    const std::string kind = text(j, path, "do");
    const auto found = std::find_if(std::begin(kinds), std::end(kinds), [&](const auto& k) { return kind == k.second; });
    if (found == std::end(kinds)) fail(path + ".do", "unknown step \"" + kind + "\"");
    step.kind = found->first;

    // Fields every step takes, then the ones of its kind; anything else is a slip of the pen.
    std::vector<const char*> fields{"do", "target", "when", "ifFlag", "scale", "onSave"};
    using Kind = EffectStep::Kind;
    switch (step.kind)
    {
    case Kind::Damage:
        fields.insert(fields.end(), {"dice", "type", "crit", "minimum"});
        step.amount = amount(j, path, "dice", {"weapon"});
        step.type = text(j, path, "type", "untyped");
        step.minimum = whole(j, path, "minimum", 0, 0, 100000);
        if (const std::string crit = text(j, path, "crit", "double"); crit == "double" || crit == "normal")
            step.critDoubles = crit == "double";
        else
            fail(path + ".crit", "is \"double\" or \"normal\"");
        break;
    case Kind::Heal:
    case Kind::TempHp:
        fields.push_back("dice");
        step.amount = amount(j, path, "dice", {});
        break;
    case Kind::Condition:
        fields.insert(fields.end(), {"id", "remove", "duration", "value"});
        step.id = text(j, path, "id");
        step.remove = truth(j, path, "remove", false);
        step.duration = rounds(j, path, Character::definedDuration);
        step.value = whole(j, path, "value", 1, 1, 1000);
        break;
    case Kind::Modifier:
    {
        fields.insert(fields.end(), {"id", "stat", "op", "value", "duration"});
        step.id = text(j, path, "id", "");
        step.modifier.stat = text(j, path, "stat");
        step.modifier.value = number(j, path, "value", -100000, 100000);
        const std::string op = text(j, path, "op", "add");
        if (!oneOf(op, {"add", "multiply", "override"})) fail(path + ".op", "is \"add\", \"multiply\" or \"override\"");
        step.modifier.op = op == "add" ? Modifier::Op::Add : op == "multiply" ? Modifier::Op::Multiply : Modifier::Op::Override;
        step.duration = rounds(j, path, -1);
        break;
    }
    case Kind::Move:
        fields.insert(fields.end(), {"how", "distance"});
        step.how = text(j, path, "how");
        if (!oneOf(step.how, {"push", "pull", "teleport"})) fail(path + ".how", "is \"push\", \"pull\" or \"teleport\"");
        step.amount = amount(j, path, "distance", {"speed"}, "1");
        break;
    case Kind::Resource:
        fields.insert(fields.end(), {"id", "op", "amount"});
        step.id = text(j, path, "id");
        step.how = text(j, path, "op", "spend");
        if (!oneOf(step.how, {"spend", "restore"})) fail(path + ".op", "is \"spend\" or \"restore\"");
        step.amount = amount(j, path, "amount", {"speed"}, "1");
        break;
    case Kind::Summon:
        fields.insert(fields.end(), {"id", "count", "duration"});
        step.id = text(j, path, "id");
        step.amount = amount(j, path, "count", {}, "1");
        step.duration = rounds(j, path, -1);
        break;
    case Kind::Light:
        fields.insert(fields.end(), {"radius", "duration"});
        step.size = number(j, path, "radius", 0, 1000);
        step.duration = rounds(j, path, -1);
        break;
    case Kind::Surface:
        fields.insert(fields.end(), {"id", "size", "duration"});
        step.id = text(j, path, "id");
        step.size = j.contains("size") ? number(j, path, "size", 0, 1000) : 1.0f;
        step.duration = rounds(j, path, -1);
        break;
    case Kind::Flag:
        fields.insert(fields.end(), {"id", "remove"});
        step.id = text(j, path, "id");
        step.remove = truth(j, path, "remove", false);
        break;
    case Kind::Roll:
        fields.insert(fields.end(), {"kind", "ability", "dc", "against", "steps"});
        step.how = text(j, path, "kind");
        if (!oneOf(step.how, {"attack", "check", "save"})) fail(path + ".kind", "is \"attack\", \"check\" or \"save\"");
        if (step.how == "attack")
        {
            if (j.contains("ability") || j.contains("dc") || j.contains("against"))
                fail(path, "an attack is rolled with the weapon against armour class and takes no ability, dc or against");
        }
        else
        {
            step.ability = text(j, path, "ability");
            if (j.contains("dc") && j.at("dc") == "caster")
                step.casterDc = true;
            else
                step.dc = whole(j, path, "dc", 10, -1000, 1000);
            step.against = text(j, path, "against", "");
            if (!step.against.empty() && (step.how == "save" || j.contains("dc")))
                fail(path + ".against", "is for a check, in place of dc");
        }
        if (!j.contains("steps")) fail(path + ".steps", "is needed");
        break;
    case Kind::Repeat:
        fields.insert(fields.end(), {"times", "steps"});
        step.amount = amount(j, path, "times", {});
        if (!j.contains("steps")) fail(path + ".steps", "is needed");
        break;
    case Kind::Choose:
    {
        fields.push_back("options");
        if (!j.contains("options") || !j.at("options").is_array() || j.at("options").empty() || j.at("options").size() > 32)
            fail(path + ".options", "is a list of 1 to 32 options");
        size_t index = 0;
        for (const json& option : j.at("options"))
        {
            const std::string at = path + ".options[" + std::to_string(index++) + "]";
            if (!option.is_object()) fail(at, "an option is an object with a name and steps");
            for (auto it = option.begin(); it != option.end(); ++it)
                if (it.key() != "name" && it.key() != "steps")
                    fail(at + "." + it.key(), "unknown field");
            step.optionNames.push_back(text(option, at, "name"));
            if (!option.contains("steps")) fail(at + ".steps", "is needed");
            step.options.push_back(parseSteps(option.at("steps"), at + ".steps", depth + 1));
        }
        break;
    }
    }
    for (auto it = j.begin(); it != j.end(); ++it)
        if (std::none_of(fields.begin(), fields.end(), [&](const char* field) { return it.key() == field; }))
            fail(path + "." + it.key(), "unknown field for a \"" + kind + "\" step");

    // A check against nobody in particular is the doer's own; everything else is aimed.
    const bool ownCheck = step.kind == Kind::Roll && step.how == "check" && step.against.empty();
    step.target = text(j, path, "target", ownCheck ? "self" : "target");
    if (!oneOf(step.target, {"self", "target", "area", "allies", "enemies"}))
        fail(path + ".target", "is \"self\", \"target\", \"area\", \"allies\" or \"enemies\"");
    step.when = text(j, path, "when", "");
    step.ifFlag = text(j, path, "ifFlag", "");
    if (!step.when.empty() && !knownResult(step.when) && !knownEvent(step.when))
        fail(path + ".when", "unknown \"" + step.when + "\"");
    const std::string onSave = text(j, path, "onSave", "full");
    if (!oneOf(onSave, {"full", "half", "none"})) fail(path + ".onSave", "is \"full\", \"half\" or \"none\"");
    step.onSave = onSave == "half" ? EffectStep::OnSave::Half : onSave == "none" ? EffectStep::OnSave::None : EffectStep::OnSave::Full;
    if (step.onSave == EffectStep::OnSave::Half && step.kind != Kind::Damage)
        fail(path + ".onSave", "only damage can be halved");

    if (j.contains("scale"))
    {
        const json& scale = j.at("scale");
        const std::string at = path + ".scale";
        if (!scale.is_object()) fail(at, "is an object with \"by\"");
        for (auto it = scale.begin(); it != scale.end(); ++it)
            if (!oneOf(it.key(), {"by", "from", "every", "dice", "value"}))
                fail(at + "." + it.key(), "unknown field");
        const std::string by = text(scale, at, "by");
        if (by != "level" && by != "slot") fail(at + ".by", "is \"level\" or \"slot\"");
        step.scale.by = by == "level" ? EffectScale::By::Level : EffectScale::By::Slot;
        step.scale.from = whole(scale, at, "from", 1, 0, 1000);
        step.scale.every = whole(scale, at, "every", 1, 1, 1000);
        step.scale.dice = amount(scale, at, "dice", {}, "");
        step.scale.value = whole(scale, at, "value", 0, -100000, 100000);
        const bool rolled = oneOf(kind, {"damage", "heal", "tempHp", "repeat", "summon", "resource", "move"});
        if (!step.scale.dice.empty() && !rolled) fail(at + ".dice", "this step has no amount to add dice to");
        if (!rolled && step.kind != Kind::Condition) fail(at, "this step has nothing to scale");
        if (step.scale.dice.empty() && step.scale.value == 0) fail(at, "needs \"dice\" or \"value\"");
    }

    if (j.contains("steps"))
        step.steps = parseSteps(j.at("steps"), path + ".steps", depth + 1);
    return step;
}

std::vector<EffectStep> parseSteps(const json& list, const std::string& path, int depth)
{
    if (!list.is_array() || list.size() > 64) fail(path, "is a list of up to 64 steps");
    if (depth > deepest) fail(path, "steps are nested too deep");
    std::vector<EffectStep> steps;
    for (size_t i = 0; i < list.size(); i++)
        steps.push_back(parseStep(list[i], path + "[" + std::to_string(i) + "]", depth));
    return steps;
}

void checkSteps(const std::vector<EffectStep>& steps, const std::string& path, const Ruleset& rules, bool hasSave)
{
    auto measurable = [&](const std::string& name) { return rules.ability(name) || rules.skill(name); };
    for (size_t i = 0; i < steps.size(); i++)
    {
        const EffectStep& step = steps[i];
        const std::string at = path + "[" + std::to_string(i) + "]";
        if (step.kind == EffectStep::Kind::Condition && !rules.condition(step.id))
            fail(at + ".id", "unknown condition \"" + step.id + "\"");
        if (step.kind == EffectStep::Kind::Roll && step.how == "save" && !rules.ability(step.ability))
            fail(at + ".ability", "unknown ability \"" + step.ability + "\"");
        if (step.kind == EffectStep::Kind::Roll && step.how == "check" && !measurable(step.ability))
            fail(at + ".ability", "unknown ability or skill \"" + step.ability + "\"");
        if (!step.against.empty() && !measurable(step.against))
            fail(at + ".against", "unknown ability or skill \"" + step.against + "\"");
        if (step.onSave != EffectStep::OnSave::Full && !hasSave)
            fail(at + ".onSave", "there is no save to make: give the effect a \"save\" or put the step under a save roll");
        const bool underSave = hasSave || (step.kind == EffectStep::Kind::Roll && step.how == "save");
        checkSteps(step.steps, at + ".steps", rules, underSave);
        for (size_t option = 0; option < step.options.size(); option++)
            checkSteps(step.options[option], at + ".options[" + std::to_string(option) + "].steps", rules, hasSave);
    }
}

}

std::optional<Effect> Effect::fromJson(std::string_view source, std::string* error)
{
    if (error) error->clear();
    try
    {
        const json j = json::parse(source);
        Effect effect;
        if (j.is_array())
        {
            effect.steps = parseSteps(j, "effects", 1);
            return effect;
        }
        if (!j.is_object()) fail("effects", "is a list of steps");
        for (auto it = j.begin(); it != j.end(); ++it)
            if (it.key() != "effects" && it.key() != "save")
                fail(it.key(), "unknown field beside \"effects\" and \"save\"");
        if (j.contains("save"))
        {
            const json& save = j.at("save");
            if (!save.is_object()) fail("save", "is an object with an ability and a dc");
            for (auto it = save.begin(); it != save.end(); ++it)
                if (it.key() != "ability" && it.key() != "dc")
                    fail("save." + it.key(), "unknown field");
            effect.save.ability = text(save, "save", "ability");
            if (save.contains("dc") && save.at("dc") == "caster")
                effect.save.casterDc = true;
            else
                effect.save.dc = whole(save, "save", "dc", 10, -1000, 1000);
        }
        effect.steps = parseSteps(j.value("effects", json::array()), "effects", 1);
        return effect;
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

bool Effect::check(const Ruleset& rules, std::string* error) const
{
    if (error) error->clear();
    try
    {
        if (!save.ability.empty() && !rules.ability(save.ability))
            fail("save.ability", "unknown ability \"" + save.ability + "\"");
        checkSteps(steps, "effects", rules, !save.ability.empty());
        return true;
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return false;
    }
}

}
