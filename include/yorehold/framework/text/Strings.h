#pragma once

#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yh
{

class FileSystem;

// Translated UI text. Each language is a JSON file at "lang/<locale>.json" in the FileSystem,
// so skins and chapters can add or override languages:
//   {"menu": {"start": "Start"}, "hit": "{name} hits for {damage}",
//    "coins": {"one": "{count} coin", "other": "{count} coins"}}
// Nested objects become dotted keys ("menu.start"). Missing keys fall back to the fallback
// language, then to the key itself, so untranslated text is visible rather than blank.
class Strings
{
public:
    using Args = std::initializer_list<std::pair<std::string_view, std::string_view>>;

    // Loads the fallback language, then the requested one over it ("pt-BR" also loads "pt" in between).
    bool load(const FileSystem& files, std::string_view locale, std::string_view fallback = "en", std::string* error = nullptr);
    // Adds or overrides entries for one locale from JSON text.
    bool add(std::string_view locale, std::string_view json, std::string* error = nullptr);
    // Chooses which loaded locales lookups use (most specific first).
    void use(std::string_view locale, std::string_view fallback = "en");
    const std::string& locale() const { return locale_; }

    bool has(std::string_view key) const;
    // {name} placeholders are replaced from args; {{ and }} are literal braces.
    std::string get(std::string_view key, Args args = {}) const;
    // Picks the plural form ("zero", "one", "two", "few", "many", "other") for `count` using the
    // language's rules; {count} is available as a placeholder.
    std::string plural(std::string_view key, long long count, Args args = {}) const;

    // Keys looked up but missing in the current language: a translator's to-do list.
    const std::set<std::string, std::less<>>& missing() const { return missing_; }
    // Locales with a file in "lang/".
    static std::vector<std::string> available(const FileSystem& files);
    // The OS's preferred languages, best first, e.g. {"pt-BR", "en"}.
    static std::vector<std::string> preferredLocales();
    static std::string pluralCategory(std::string_view locale, long long count);
    static std::string format(std::string_view text, Args args);

private:
    std::optional<std::string_view> find(std::string_view key) const;

    std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> tables_;
    std::vector<std::string> chain_;
    std::string locale_;
    mutable std::set<std::string, std::less<>> missing_;
};

}
