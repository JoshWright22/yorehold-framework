#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace yh
{

// Caret and selection editing for one line of UTF-8 text. Ui::textBox drives it from keys,
// mouse and clipboard; it has no rendering or SDL dependency so editors can reuse it.
// Positions are byte offsets that always sit on code point boundaries.
struct TextEdit
{
    size_t caret = 0;
    size_t anchor = 0; // other end of the selection; equals caret when nothing is selected

    bool hasSelection() const { return caret != anchor; }
    std::pair<size_t, size_t> selection() const { return {std::min(caret, anchor), std::max(caret, anchor)}; }
    std::string_view selected(std::string_view text) const;

    // Moves the caret; `extend` keeps the anchor (Shift), `word` jumps words (Ctrl).
    void left(std::string_view text, bool extend, bool word);
    void right(std::string_view text, bool extend, bool word);
    void home(bool extend) { place(0, extend); }
    void end(std::string_view text, bool extend) { place(text.size(), extend); }
    void place(size_t position, bool extend);
    void selectAll(std::string_view text) { anchor = 0; caret = text.size(); }
    void selectWord(std::string_view text, size_t position);
    // Keeps positions valid after the text changed elsewhere.
    void clamp(std::string_view text);

    // Backspace (`forward` false) or Delete; removes the selection if there is one.
    bool erase(std::string& text, bool forward, bool word);
    // Replaces the selection. Line breaks and control characters become spaces; input that
    // doesn't fit in `maxBytes` is cut at a code point boundary.
    bool insert(std::string& text, std::string_view input, size_t maxBytes);

    static size_t previous(std::string_view text, size_t i);
    static size_t next(std::string_view text, size_t i);
    static size_t previousWord(std::string_view text, size_t i);
    static size_t nextWord(std::string_view text, size_t i);
};

}
