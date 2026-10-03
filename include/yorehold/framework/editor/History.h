#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

// Undo/redo for editors. Each edit is a pair of callbacks that apply and revert it; they must
// capture everything they need (old and new values), not read live state when they run.
//
// Groups combine several edits into one undo step ("Paste 40 tiles"). A merge key combines
// consecutive edits with the same key, so one brush stroke or slider drag undoes as a whole;
// call breakMerge() when the stroke ends (mouse up).
class History
{
public:
    using Action = std::function<void()>;

    explicit History(size_t limit = 200);

    // Applies `redo` now and records it.
    void perform(std::string_view label, Action redo, Action undo, std::string_view mergeKey = {});
    // Records an edit that has already been applied.
    void record(std::string_view label, Action redo, Action undo, std::string_view mergeKey = {});
    void beginGroup(std::string_view label);
    void endGroup();
    void breakMerge() { mergeKey_.clear(); }

    bool undo();
    bool redo();
    bool canUndo() const { return position_ > 0 && groupDepth_ == 0; }
    bool canRedo() const { return position_ < entries_.size() && groupDepth_ == 0; }
    // For menu text: "Undo Paint tiles".
    std::string_view undoLabel() const { return canUndo() ? std::string_view(entries_[position_ - 1].label) : std::string_view(); }
    std::string_view redoLabel() const { return canRedo() ? std::string_view(entries_[position_].label) : std::string_view(); }
    size_t size() const { return entries_.size(); }

    // Unsaved-changes tracking: true when the document differs from the last markSaved().
    bool dirty() const { return savedPosition_ != position_; }
    void markSaved() { savedPosition_ = position_; }
    void clear();

private:
    struct Step { Action redo, undo; };
    struct Entry
    {
        std::string label;
        std::vector<Step> steps;
    };

    void add(std::string_view label, Step step, std::string_view mergeKey);
    void push(Entry entry, std::string_view mergeKey);

    std::vector<Entry> entries_;
    size_t position_ = 0;
    size_t limit_;
    // npos when the saved state was discarded (undone past, then branched or trimmed).
    size_t savedPosition_ = 0;
    std::string mergeKey_;
    Entry group_;
    int groupDepth_ = 0;
    bool running_ = false;
};

}
