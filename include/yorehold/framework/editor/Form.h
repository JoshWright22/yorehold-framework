#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

// Forms for editing JSON definition files (one object per file) without writing a screen for each
// kind. A FormSchema lists a kind's fields; each field turns typed text into the value it stands
// for and says what is wrong with the value a file has. Ordered JSON, so a file keeps the order
// it was written in. Keys a form has no field for are left alone.
using FormJson = nlohmann::ordered_json;
// Named lists a choice or list field can offer ("items", "skills"), filled in by the caller.
using FormOptions = std::map<std::string, std::vector<std::string>, std::less<>>;

struct FormField
{
    enum class Type
    {
        Text,    // a string
        Integer, // a whole number
        Number,  // any number
        Flag,    // true or false
        Choice,  // one string out of `options` and the list `optionsFrom` names
        List,    // strings, typed "a, b, c"; checked against the same lists when they are given
        Json,    // anything, typed as JSON (nested parts the form has no fields for)
    };

    std::string key;   // in the object
    std::string label; // shown; empty = the key
    std::string help;  // one line about it
    Type type = Type::Text;
    bool required = false;
    std::optional<double> min, max; // Integer and Number
    std::vector<std::string> options;
    std::string optionsFrom;
    FormJson fallback; // what a new entry starts with; null = left out (required fields get an empty value)

    const std::string& title() const { return label.empty() ? key : label; }
    // `options` then the named list, without repeats.
    std::vector<std::string> choices(const FormOptions& lists) const;
    // As a text box shows it: "" when the key is missing, "a, b" for a list, compact JSON for the rest.
    std::string text(const FormJson& object) const;
    // What typed text stands for. Null means leave the key out (blank text on a field that isn't
    // required). nullopt, with `error` filled, when it can't be this field's value.
    std::optional<FormJson> parse(std::string_view text, std::string* error = nullptr) const;
    // "" when the object's value fits, else why ("is missing", "is a whole number"...). An entry
    // that isn't among the offered choices sets `unknown` instead of being refused, since the
    // lists may not know everything the game will.
    std::string problem(const FormJson& object, const FormOptions& lists, bool* unknown = nullptr) const;

    static std::optional<Type> typeFromName(std::string_view name);
};

struct FormSchema
{
    std::string id;           // "item"
    std::string label;        // "Items"; empty = the id
    std::string folder;       // where its files go: "items"
    std::string idKey = "id"; // the key the file name follows; empty = the file name is the only id
    std::vector<FormField> fields;

    const FormField* field(std::string_view key) const;
    // A new entry: the id (under idKey) and every field's fallback, in field order.
    FormJson blank(const std::string& id) const;
    // "name: is missing" for each field that doesn't fit; `warnings` (if given) gets the unknown choices.
    std::vector<std::string> problems(const FormJson& object, const FormOptions& lists, std::vector<std::string>* warnings = nullptr) const;
    // Keys the object has that no field (or idKey) covers.
    std::vector<std::string> unlisted(const FormJson& object) const;

    // {"id", "label", "folder", "idKey", "fields": [{"key", "type", "label", "help", "required",
    // "min", "max", "options", "optionsFrom", "default"}]}. Other keys on the schema itself are
    // the caller's and are ignored; unknown keys on a field are refused.
    static std::optional<FormSchema> fromJson(std::string_view json, std::string* error = nullptr);
    // The same, from an object already parsed (one entry of a list of forms).
    static std::optional<FormSchema> fromObject(const FormJson& json, std::string* error = nullptr);
};

}
