#include "yorehold/framework/text/Strings.h"

#include "yorehold/framework/assets/FileSystem.h"

#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_stdinc.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace yh
{

namespace
{

using Table = std::map<std::string, std::string, std::less<>>;
using ArgList = std::vector<std::pair<std::string_view, std::string_view>>;

void flatten(const nlohmann::json& j, const std::string& prefix, Table& out)
{
    if (j.is_string()) { out[prefix] = j.get<std::string>(); return; }
    if (!j.is_object()) throw std::invalid_argument("Translation \"" + prefix + "\" must be text or an object");
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        if (it.key().empty() || it.key().find('.') != std::string::npos) throw std::invalid_argument("Translation keys can't be empty or contain dots");
        flatten(it.value(), prefix.empty() ? it.key() : prefix + "." + it.key(), out);
    }
}

std::string normalizeLocale(std::string_view locale)
{
    std::string result(locale);
    std::replace(result.begin(), result.end(), '_', '-');
    return result;
}

std::string language(std::string_view locale)
{
    std::string lang(locale.substr(0, locale.find('-')));
    for (char& c : lang) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lang;
}

std::string formatWith(std::string_view text, const ArgList& args)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) { out += c; ++i; continue; }
        if (c == '{')
        {
            const size_t close = text.find('}', i);
            if (close != std::string_view::npos)
            {
                const auto name = text.substr(i + 1, close - i - 1);
                const auto arg = std::find_if(args.begin(), args.end(), [&](const auto& a) { return a.first == name; });
                if (arg != args.end()) { out += arg->second; i = close; continue; }
            }
        }
        out += c; // unknown placeholders stay visible so they get noticed
    }
    return out;
}

}

bool Strings::add(std::string_view locale, std::string_view json, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = nlohmann::json::parse(json);
        if (!j.is_object()) throw std::invalid_argument("A language file must be an object");
        Table entries;
        flatten(j, {}, entries);
        Table& table = tables_[normalizeLocale(locale)];
        for (auto& [key, text] : entries) table[key] = std::move(text);
        return true;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return false; }
}

bool Strings::load(const FileSystem& files, std::string_view locale, std::string_view fallback, std::string* error)
{
    if (error) error->clear();
    const std::string wanted = normalizeLocale(locale);
    bool found = false;
    std::string why;
    std::vector<std::string> names;
    for (const std::string& name : {normalizeLocale(fallback), language(wanted), wanted})
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    for (const std::string& name : names)
    {
        const auto text = files.readText("lang/" + name + ".json");
        if (!text) continue;
        tables_.erase(name);
        if (!add(name, *text, &why)) { if (error) *error = "lang/" + name + ".json: " + why; return false; }
        found |= name == wanted || name == language(wanted);
    }
    use(wanted, fallback);
    if (!found && error) *error = "No translation for " + wanted;
    return found;
}

void Strings::use(std::string_view locale, std::string_view fallback)
{
    locale_ = normalizeLocale(locale);
    chain_.clear();
    for (const std::string& name : {locale_, language(locale_), normalizeLocale(fallback)})
        if (!name.empty() && std::find(chain_.begin(), chain_.end(), name) == chain_.end()) chain_.push_back(name);
    missing_.clear();
}

std::optional<std::string_view> Strings::find(std::string_view key) const
{
    for (const std::string& name : chain_)
    {
        const auto table = tables_.find(name);
        if (table == tables_.end()) continue;
        const auto entry = table->second.find(key);
        if (entry != table->second.end()) return std::string_view(entry->second);
    }
    return std::nullopt;
}

bool Strings::has(std::string_view key) const
{
    return find(key).has_value();
}

std::string Strings::get(std::string_view key, Args args) const
{
    const auto text = find(key);
    if (!text) { missing_.emplace(key); return std::string(key); }
    return formatWith(*text, ArgList(args));
}

std::string Strings::plural(std::string_view key, long long count, Args args) const
{
    const std::string category = pluralCategory(locale_, count);
    auto text = find(std::string(key) + "." + category);
    if (!text) text = find(std::string(key) + ".other");
    if (!text) { missing_.emplace(key); return std::string(key); }
    const std::string number = std::to_string(count);
    ArgList all(args);
    all.emplace_back("count", number);
    return formatWith(*text, all);
}

std::string Strings::format(std::string_view text, Args args)
{
    return formatWith(text, ArgList(args));
}

// Integer plural rules for common languages, simplified from Unicode CLDR.
std::string Strings::pluralCategory(std::string_view locale, long long count)
{
    const std::string lang = language(locale);
    const long long n = count < 0 ? -count : count;
    const long long n10 = n % 10, n100 = n % 100;
    for (const char* none : {"ja", "zh", "ko", "th", "vi", "id", "ms", "lo", "my"})
        if (lang == none) return "other";
    if (lang == "fr" || lang == "pt") return n <= 1 ? "one" : "other";
    if (lang == "ru" || lang == "uk" || lang == "be")
        return n10 == 1 && n100 != 11 ? "one" : n10 >= 2 && n10 <= 4 && (n100 < 12 || n100 > 14) ? "few" : "many";
    if (lang == "pl")
        return n == 1 ? "one" : n10 >= 2 && n10 <= 4 && (n100 < 12 || n100 > 14) ? "few" : "many";
    if (lang == "cs" || lang == "sk") return n == 1 ? "one" : n >= 2 && n <= 4 ? "few" : "other";
    if (lang == "ar")
        return n == 0 ? "zero" : n == 1 ? "one" : n == 2 ? "two" : n100 >= 3 && n100 <= 10 ? "few" : n100 >= 11 ? "many" : "other";
    return n == 1 ? "one" : "other";
}

std::vector<std::string> Strings::available(const FileSystem& files)
{
    std::vector<std::string> locales;
    for (const std::string& path : files.list("lang"))
    {
        std::string name = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
        if (name.size() > 5 && name.ends_with(".json")) locales.push_back(name.substr(0, name.size() - 5));
    }
    return locales;
}

std::vector<std::string> Strings::preferredLocales()
{
    std::vector<std::string> result;
    int count = 0;
    if (SDL_Locale** locales = SDL_GetPreferredLocales(&count))
    {
        for (int i = 0; i < count; ++i)
        {
            if (!locales[i] || !locales[i]->language) continue;
            std::string name = locales[i]->language;
            if (locales[i]->country) name += std::string("-") + locales[i]->country;
            result.push_back(std::move(name));
        }
        SDL_free(locales);
    }
    return result;
}

}
