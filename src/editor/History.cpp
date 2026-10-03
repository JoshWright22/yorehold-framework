#include "yorehold/framework/editor/History.h"

#include <stdexcept>

namespace yh
{

namespace
{
constexpr size_t discarded = static_cast<size_t>(-1);
}

History::History(size_t limit) : limit_(limit)
{
    if (limit_ == 0) throw std::invalid_argument("History needs room for at least one edit");
}

void History::perform(std::string_view label, Action redo, Action undo, std::string_view mergeKey)
{
    if (!redo || !undo) throw std::invalid_argument("History edits need redo and undo");
    if (running_) throw std::logic_error("Cannot record edits while undoing or redoing");
    redo();
    add(label, {std::move(redo), std::move(undo)}, mergeKey);
}

void History::record(std::string_view label, Action redo, Action undo, std::string_view mergeKey)
{
    if (!redo || !undo) throw std::invalid_argument("History edits need redo and undo");
    add(label, {std::move(redo), std::move(undo)}, mergeKey);
}

void History::beginGroup(std::string_view label)
{
    if (running_) throw std::logic_error("Cannot record edits while undoing or redoing");
    if (groupDepth_++ == 0) group_ = {std::string(label), {}};
}

void History::endGroup()
{
    if (groupDepth_ == 0) throw std::logic_error("endGroup without beginGroup");
    if (--groupDepth_ > 0 || group_.steps.empty()) return;
    Entry entry = std::move(group_);
    group_ = {};
    push(std::move(entry), {});
}

void History::add(std::string_view label, Step step, std::string_view mergeKey)
{
    if (running_) throw std::logic_error("Cannot record edits while undoing or redoing");
    if (groupDepth_ > 0) { group_.steps.push_back(std::move(step)); return; }
    Entry entry{std::string(label), {}};
    entry.steps.push_back(std::move(step));
    push(std::move(entry), mergeKey);
}

void History::push(Entry entry, std::string_view mergeKey)
{
    if (position_ < entries_.size())
    {
        // A new edit after undoing discards the redo branch, and with it any saved state there.
        if (savedPosition_ != discarded && savedPosition_ > position_) savedPosition_ = discarded;
        entries_.resize(position_);
        mergeKey_.clear();
    }
    // Never merge into the saved entry, or dirty() would miss the change.
    if (!mergeKey.empty() && mergeKey == mergeKey_ && position_ > 0 && savedPosition_ != position_)
    {
        for (Step& step : entry.steps) entries_.back().steps.push_back(std::move(step));
        return;
    }
    entries_.push_back(std::move(entry));
    ++position_;
    mergeKey_ = mergeKey;
    if (entries_.size() > limit_)
    {
        entries_.erase(entries_.begin());
        --position_;
        if (savedPosition_ != discarded) savedPosition_ = savedPosition_ == 0 ? discarded : savedPosition_ - 1;
    }
}

bool History::undo()
{
    if (!canUndo() || running_) return false;
    running_ = true;
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard{running_};
    const Entry& entry = entries_[position_ - 1];
    for (auto step = entry.steps.rbegin(); step != entry.steps.rend(); ++step) step->undo();
    --position_;
    mergeKey_.clear();
    return true;
}

bool History::redo()
{
    if (!canRedo() || running_) return false;
    running_ = true;
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard{running_};
    for (const Step& step : entries_[position_].steps) step.redo();
    ++position_;
    mergeKey_.clear();
    return true;
}

void History::clear()
{
    if (running_) throw std::logic_error("Cannot clear history while undoing or redoing");
    entries_.clear();
    group_ = {};
    groupDepth_ = 0;
    mergeKey_.clear();
    savedPosition_ = position_ == savedPosition_ ? 0 : discarded;
    position_ = 0;
}

}
