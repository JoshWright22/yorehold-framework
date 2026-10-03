#include "yorehold/framework/ui/TextEdit.h"

namespace yh
{

namespace
{
bool continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
// Letters, digits and anything non-ASCII (accented and non-Latin letters) count as word characters.
bool wordByte(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x80 || u == '_' || (u >= '0' && u <= '9') || ((u | 0x20) >= 'a' && (u | 0x20) <= 'z');
}
}

size_t TextEdit::previous(std::string_view text, size_t i)
{
    i = std::min(i, text.size());
    if (i == 0) return 0;
    --i;
    while (i > 0 && continuation(text[i])) --i;
    return i;
}

size_t TextEdit::next(std::string_view text, size_t i)
{
    if (i >= text.size()) return text.size();
    ++i;
    while (i < text.size() && continuation(text[i])) ++i;
    return i;
}

size_t TextEdit::previousWord(std::string_view text, size_t i)
{
    i = std::min(i, text.size());
    while (i > 0 && !wordByte(text[previous(text, i)])) i = previous(text, i);
    while (i > 0 && wordByte(text[previous(text, i)])) i = previous(text, i);
    return i;
}

size_t TextEdit::nextWord(std::string_view text, size_t i)
{
    while (i < text.size() && wordByte(text[i])) i = next(text, i);
    while (i < text.size() && !wordByte(text[i])) i = next(text, i);
    return std::min(i, text.size());
}

std::string_view TextEdit::selected(std::string_view text) const
{
    const auto [from, to] = selection();
    return from >= text.size() ? std::string_view() : text.substr(from, std::min(to, text.size()) - from);
}

void TextEdit::place(size_t position, bool extend)
{
    caret = position;
    if (!extend) anchor = caret;
}

void TextEdit::left(std::string_view text, bool extend, bool word)
{
    if (hasSelection() && !extend) { place(selection().first, false); return; }
    place(word ? previousWord(text, caret) : previous(text, caret), extend);
}

void TextEdit::right(std::string_view text, bool extend, bool word)
{
    if (hasSelection() && !extend) { place(selection().second, false); return; }
    place(word ? nextWord(text, caret) : next(text, caret), extend);
}

void TextEdit::selectWord(std::string_view text, size_t position)
{
    position = std::min(position, text.size());
    while (position > 0 && continuation(text[position])) --position;
    size_t from = position, to = position;
    const bool inWord = position < text.size() && wordByte(text[position]);
    while (from > 0 && wordByte(text[previous(text, from)]) == inWord) from = previous(text, from);
    while (to < text.size() && wordByte(text[to]) == inWord) to = next(text, to);
    anchor = from;
    caret = to;
}

void TextEdit::clamp(std::string_view text)
{
    auto fix = [&](size_t& i) {
        i = std::min(i, text.size());
        while (i > 0 && i < text.size() && continuation(text[i])) --i;
    };
    fix(caret);
    fix(anchor);
}

bool TextEdit::erase(std::string& text, bool forward, bool word)
{
    clamp(text);
    if (!hasSelection())
    {
        if (forward) caret = word ? nextWord(text, caret) : next(text, caret);
        else caret = word ? previousWord(text, caret) : previous(text, caret);
    }
    const auto [from, to] = selection();
    place(from, false);
    if (from == to) return false;
    text.erase(from, to - from);
    return true;
}

bool TextEdit::insert(std::string& text, std::string_view input, size_t maxBytes)
{
    clamp(text);
    const auto [from, to] = selection();
    const size_t kept = text.size() - (to - from);
    const size_t room = kept >= maxBytes ? 0 : maxBytes - kept;
    size_t take = std::min(input.size(), room);
    while (take > 0 && take < input.size() && continuation(input[take])) --take;
    if (take == 0 && from == to) return false;
    std::string clean(input.substr(0, take));
    for (char& c : clean)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) c = ' ';
    text.replace(from, to - from, clean);
    place(from + clean.size(), false);
    return true;
}

}
