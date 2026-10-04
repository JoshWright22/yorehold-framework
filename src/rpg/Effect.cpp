// Running an effect: its steps in order, against whatever the host says is there.

#include "yorehold/framework/rpg/Effect.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>

namespace yh
{

namespace
{

// What the rolls made so far in this effect say about one creature.
struct Outcome
{
    std::optional<bool> hit;
    bool critical = false;
    std::optional<bool> saved;
    std::optional<bool> passed;
};

bool isEvent(std::string_view when)
{
    return std::any_of(std::begin(conditionEvents), std::end(conditionEvents), [&](const char* known) { return when == known; });
}

class Run
{
public:
    Run(const Effect& effect, EffectHost& host, const EffectContext& context, EffectResult& result)
        : effect_(effect), host_(host), context_(context), rules_(*context.rules), random_(*context.random), result_(result)
    {
    }

    void steps(const std::vector<EffectStep>& list, const std::vector<EffectActor>& subjects, bool top)
    {
        for (const EffectStep& step : list)
        {
            // Something done now leaves the steps that wait for an event alone; an event runs only those.
            if (isEvent(step.when) ? step.when != context_.event : top && !context_.event.empty())
                continue;
            one(step, subjects);
        }
    }

private:
    using Kind = EffectStep::Kind;

    std::vector<EffectActor> aimedAt(const EffectStep& step, const std::vector<EffectActor>& subjects)
    {
        if (step.target == "self")
            return {context_.self};
        if (step.target == "target")
            return subjects;
        return host_.group(step.target, context_);
    }

    // The save that goes with the whole effect, made once by each creature a step asks it of.
    void saveIfAsked(EffectActor who, Outcome& outcome)
    {
        Character* sheet = host_.sheet(who);
        if (outcome.saved || effect_.save.ability.empty() || !sheet)
            return;
        const int dc = effect_.save.casterDc ? context_.dc : effect_.save.dc;
        const RollResult roll = sheet->rollSave(rules_, effect_.save.ability, Advantage::None, random_);
        outcome.saved = roll.total >= dc;
        EffectEvent event{EffectEvent::Kind::Save, who, context_.self, roll};
        event.dc = dc;
        event.success = *outcome.saved;
        event.id = effect_.save.ability;
        result_.events.push_back(std::move(event));
    }

    // How an attack or check went, as far as `when` asks. Empty if none was made about them.
    std::optional<bool> rolled(const std::string& when, EffectActor who) const
    {
        const auto found = outcomes_.find(who);
        if (found == outcomes_.end())
            return std::nullopt;
        const Outcome& outcome = found->second;
        if (when == "success" || when == "failure")
            return outcome.passed ? std::optional(*outcome.passed == (when == "success")) : std::nullopt;
        if (!outcome.hit)
            return std::nullopt;
        return when == "hit" ? *outcome.hit : when == "miss" ? !*outcome.hit : outcome.critical;
    }

    // Whether the step happens to `who`. A save is the business of whoever the step lands on. An
    // attack or a check is about whoever it was rolled against, so a step for someone else (the
    // doer healing on a hit) follows the roll made about the creatures in `subjects`.
    bool holds(const EffectStep& step, EffectActor who, const std::vector<EffectActor>& subjects)
    {
        if (!step.ifFlag.empty())
        {
            const Character* subject = host_.sheet(who);
            if (!subject || !subject->hasFlag(rules_, step.ifFlag))
                return false;
        }
        Outcome& outcome = outcomes_[who];
        const std::string& when = step.when;
        if (when == "saveFailed" || when == "saveSucceeded" || step.onSave == EffectStep::OnSave::None)
            saveIfAsked(who, outcome);
        if (step.onSave == EffectStep::OnSave::None && outcome.saved.value_or(false))
            return false;
        if (when.empty() || isEvent(when)) return true;
        if (when == "saveFailed") return outcome.saved && !*outcome.saved;
        if (when == "saveSucceeded") return outcome.saved.value_or(false);
        if (const std::optional<bool> own = rolled(when, who))
            return *own;
        return std::any_of(subjects.begin(), subjects.end(), [&](EffectActor subject) { return rolled(when, subject).value_or(false); });
    }

    int scaleSteps(const EffectScale& scale) const
    {
        if (scale.by == EffectScale::By::None)
            return 0;
        const Character* self = host_.sheet(context_.self);
        const int level = scale.by == EffectScale::By::Slot ? context_.slot : context_.level > 0 ? context_.level : self ? self->level : 1;
        return level > scale.from ? (level - scale.from) / std::max(1, scale.every) : 0;
    }

    // The step's amount, rolled: its dice (twice over for a critical hit) plus whatever scaling adds.
    RollResult rollAmount(const EffectStep& step, bool doubled)
    {
        const Character* self = host_.sheet(context_.self);
        std::string text = step.amount;
        if (text == "weapon")
            text = self ? self->damageDice(rules_) : "0";
        else if (text == "speed")
            text = std::to_string(self ? self->speedSquares(rules_) : 0);
        DiceExpression dice = DiceExpression::parse(text).value_or(DiceExpression{});
        if (doubled)
            for (DiceTerm& term : dice.terms)
                if (term.sides > 0)
                    term.count *= 2;
        if (const int times = scaleSteps(step.scale); times > 0)
        {
            if (const std::optional<DiceExpression> more = DiceExpression::parse(step.scale.dice); more && !step.scale.dice.empty())
                for (DiceTerm term : more->terms)
                {
                    term.count *= times;
                    dice.terms.push_back(term);
                }
            if (step.scale.value != 0)
            {
                DiceTerm flat;
                flat.count = std::abs(step.scale.value) * times;
                flat.sign = step.scale.value < 0 ? -1 : 1;
                dice.terms.push_back(flat);
            }
        }
        return roll(dice, random_);
    }

    void ended(EffectActor who, const std::vector<std::string>& ids)
    {
        for (const std::string& id : ids)
        {
            EffectEvent event{EffectEvent::Kind::ConditionEnded, who, context_.self};
            event.id = id;
            result_.events.push_back(std::move(event));
        }
    }

    void note(EffectEvent::Kind kind, EffectActor who, std::string id, int amount = 0, RollResult roll = {})
    {
        EffectEvent event{kind, who, context_.self, std::move(roll)};
        event.amount = amount;
        event.id = std::move(id);
        result_.events.push_back(std::move(event));
    }

    void one(const EffectStep& step, const std::vector<EffectActor>& subjects)
    {
        std::vector<EffectActor> who;
        for (const EffectActor actor : aimedAt(step, subjects))
            if (holds(step, actor, subjects))
                who.push_back(actor);
        // A step waiting on a result needs someone the result is true of.
        const bool gated = !step.when.empty() && !isEvent(step.when);

        switch (step.kind)
        {
        case Kind::Damage:
        {
            std::optional<RollResult> shared; // one roll for everyone it lands on, as an area spell is rolled
            for (const EffectActor actor : who)
            {
                Character* sheet = host_.sheet(actor);
                if (!sheet)
                    continue;
                Outcome& outcome = outcomes_[actor];
                const bool doubled = step.critDoubles && outcome.critical;
                if (!doubled && !shared)
                    shared = rollAmount(step, false);
                const RollResult rolled = doubled ? rollAmount(step, true) : *shared;
                int amount = std::max(step.minimum, rolled.total);
                if (step.onSave == EffectStep::OnSave::Half)
                {
                    saveIfAsked(actor, outcome);
                    if (outcome.saved.value_or(false))
                        amount /= 2;
                }
                const bool wasUp = !sheet->down();
                EffectEvent event{EffectEvent::Kind::Damage, actor, context_.self, rolled};
                event.amount = host_.damage(actor, std::max(0, amount), step.type, context_);
                event.critical = doubled;
                event.dropped = wasUp && sheet->down();
                event.id = step.type;
                result_.events.push_back(std::move(event));
                ended(actor, sheet->conditionEvent(rules_, "damage"));
            }
            break;
        }
        case Kind::Heal:
        case Kind::TempHp:
        {
            std::optional<RollResult> shared;
            for (const EffectActor actor : who)
            {
                Character* sheet = host_.sheet(actor);
                if (!sheet)
                    continue;
                if (!shared)
                    shared = rollAmount(step, false);
                const int amount = std::max(0, shared->total);
                if (step.kind == Kind::TempHp)
                {
                    sheet->tempHp = std::max(sheet->tempHp, amount); // temporary HP never adds up
                    note(EffectEvent::Kind::TempHp, actor, {}, amount, *shared);
                    continue;
                }
                const bool wasDown = sheet->down();
                const int before = sheet->hp;
                sheet->heal(amount);
                note(EffectEvent::Kind::Heal, actor, {}, sheet->hp - before, *shared);
                if (wasDown && !sheet->down())
                    ended(actor, sheet->conditionEvent(rules_, "healed"));
            }
            break;
        }
        case Kind::Condition:
            for (const EffectActor actor : who)
            {
                Character* sheet = host_.sheet(actor);
                if (!sheet)
                    continue;
                if (step.remove)
                {
                    if (!sheet->hasCondition(step.id))
                        continue;
                    sheet->removeCondition(step.id);
                    note(EffectEvent::Kind::ConditionRemoved, actor, step.id);
                    continue;
                }
                const int value = std::max(1, step.value + step.scale.value * scaleSteps(step.scale));
                sheet->addCondition(rules_, step.id, step.duration, value);
                note(EffectEvent::Kind::ConditionAdded, actor, step.id, sheet->conditionValue(step.id));
            }
            break;
        case Kind::Modifier:
            for (const EffectActor actor : who)
            {
                Character* sheet = host_.sheet(actor);
                if (!sheet)
                    continue;
                // Tracked apart from the ruleset's conditions. Doing the same thing again replaces
                // what it left last time instead of piling up.
                const std::string id = "effect:" + (!step.id.empty() ? step.id : !context_.source.empty() ? context_.source : std::string("modifier"));
                if (modified_.insert({actor, id}).second)
                    sheet->removeCondition(id);
                sheet->addModifier(id, step.modifier, step.duration);
                note(EffectEvent::Kind::Modifier, actor, step.modifier.stat, static_cast<int>(step.modifier.value));
            }
            break;
        case Kind::Move:
        case Kind::Resource:
        {
            std::optional<RollResult> shared;
            for (const EffectActor actor : who)
            {
                if (!shared)
                    shared = rollAmount(step, false);
                const int amount = std::max(0, shared->total);
                if (step.kind == Kind::Move)
                {
                    if (host_.move(actor, step.how, amount, context_))
                        note(EffectEvent::Kind::Move, actor, step.how, amount);
                    continue;
                }
                const int change = step.how == "spend" ? -amount : amount;
                if (host_.resource(actor, step.id, change, context_))
                    note(EffectEvent::Kind::Resource, actor, step.id, change);
            }
            break;
        }
        case Kind::Light:
            for (const EffectActor actor : who)
                if (host_.light(actor, step.size, step.duration, context_))
                    note(EffectEvent::Kind::Light, actor, {}, static_cast<int>(step.size));
            break;
        case Kind::Summon:
        {
            if (gated && who.empty())
                break;
            const int count = std::max(0, rollAmount(step, false).total);
            if (count > 0 && host_.summon(step.id, count, step.duration, context_))
                note(EffectEvent::Kind::Summon, context_.self, step.id, count);
            break;
        }
        case Kind::Surface:
            if (!(gated && who.empty()) && host_.surface(step.id, step.size, step.duration, context_))
                note(EffectEvent::Kind::Surface, context_.self, step.id, static_cast<int>(step.size));
            break;
        case Kind::Flag:
            if (!(gated && who.empty()) && host_.flag(step.id, !step.remove, context_))
                note(EffectEvent::Kind::Flag, context_.self, step.id, step.remove ? 0 : 1);
            break;
        case Kind::Roll:
            for (const EffectActor actor : who)
            {
                if (rollFor(step, actor))
                    steps(step.steps, {actor}, false);
            }
            break;
        case Kind::Repeat:
        {
            if (gated && who.empty())
                break;
            const int times = std::clamp(rollAmount(step, false).total, 0, 100);
            for (int i = 0; i < times; i++)
                steps(step.steps, gated ? who : subjects, false);
            break;
        }
        case Kind::Choose:
        {
            if (gated && who.empty())
                break;
            const size_t choice = std::min(host_.choose(step.optionNames, context_), step.options.size() - 1);
            note(EffectEvent::Kind::Choice, context_.self, step.optionNames[choice], static_cast<int>(choice));
            steps(step.options[choice], gated ? who : subjects, false);
            break;
        }
        }
    }

    // Makes the step's roll about `actor` and records how it went. False if it could not be made.
    bool rollFor(const EffectStep& step, EffectActor actor)
    {
        Character* self = host_.sheet(context_.self);
        Character* subject = host_.sheet(actor);
        if (!subject)
            return false;
        Outcome& outcome = outcomes_[actor];
        if (step.how == "save")
        {
            const int dc = step.casterDc ? context_.dc : step.dc;
            const RollResult roll = subject->rollSave(rules_, step.ability, Advantage::None, random_);
            outcome.saved = roll.total >= dc;
            EffectEvent event{EffectEvent::Kind::Save, actor, context_.self, roll};
            event.dc = dc;
            event.success = *outcome.saved;
            event.id = step.ability;
            result_.events.push_back(std::move(event));
            return true;
        }
        if (!self)
            return false;
        if (step.how == "check")
        {
            const int dc = !step.against.empty() ? rules_.passiveBase + subject->checkModifier(rules_, step.against)
                : step.casterDc ? context_.dc : step.dc;
            const RollResult roll = self->rollCheck(rules_, step.ability, Advantage::None, random_);
            outcome.passed = roll.total >= dc;
            EffectEvent event{EffectEvent::Kind::Check, actor, context_.self, roll};
            event.dc = dc;
            event.success = *outcome.passed;
            event.id = step.ability;
            result_.events.push_back(std::move(event));
            return true;
        }
        // An attack with whatever the doer holds, against armour class. A natural 1 misses, a
        // natural 20 hits and is a critical hit.
        const RollResult roll = rollD20(self->attackModifier(rules_), self->attackAdvantage(rules_), random_);
        const std::vector<std::string> afterAttack = self->conditionEvent(rules_, "attack"); // they still counted for this roll
        const int ac = subject->armorClass(rules_);
        outcome.critical = roll.natural20();
        outcome.hit = !roll.natural1() && (outcome.critical || roll.total >= ac);
        EffectEvent event{EffectEvent::Kind::Attack, actor, context_.self, roll};
        event.dc = ac;
        event.success = *outcome.hit;
        event.critical = outcome.critical;
        result_.events.push_back(std::move(event));
        ended(context_.self, afterAttack);
        return true;
    }

    const Effect& effect_;
    EffectHost& host_;
    const EffectContext& context_;
    const Ruleset& rules_;
    Random& random_;
    EffectResult& result_;
    std::map<EffectActor, Outcome> outcomes_;
    std::set<std::pair<EffectActor, std::string>> modified_; // modifiers this run has already started afresh
};

}

int EffectHost::damage(EffectActor who, int amount, std::string_view, const EffectContext&)
{
    if (Character* subject = sheet(who))
        subject->takeDamage(amount);
    return amount;
}

bool EffectHost::resource(EffectActor who, std::string_view id, int change, const EffectContext&)
{
    Character* subject = sheet(who);
    if (!subject)
        return false;
    const auto found = subject->resources.find(std::string(id));
    if (found == subject->resources.end())
        return false;
    found->second.current = std::clamp(found->second.current + change, 0, found->second.max);
    return true;
}

EffectResult Effect::run(EffectHost& host, const EffectContext& context) const
{
    EffectResult result;
    if (!context.rules || !context.random)
        return result;
    Run(*this, host, context, result).steps(steps, context.targets, true);
    return result;
}

}
