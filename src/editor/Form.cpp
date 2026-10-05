#include "yorehold/framework/editor/Form.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>

namespace yh
{

namespace
{

constexpr std::pair<FormField::Type, std::string_view> typeNames[] = {
    {FormField::Type::Text, "text"},     {FormField::Type::Integer, "integer"}, {FormField::Type::Number, "number"},
    {FormField::Type::Flag, "flag"},     {FormField::Type::Choice, "choice"},   {FormField::Type::List, "list"},
    {FormField::Type::Json, "json"},
};

std::string trim(std::string_view text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

// "a, b,, c" -> a, b, c
std::vector<std::string> split(std::string_view text)
{
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= text.size())
    {
        size_t comma = text.find(',', at);
        if (comma == std::string_view::npos)
            comma = text.size();
        if (std::string item = trim(text.substr(at, comma - at)); !item.empty())
            out.push_back(std::move(item));
        at = comma + 1;
    }
    return out;
}

std::string numberText(double value)
{
    if (value == std::floor(value) && std::abs(value) < 9e15)
        return std::to_string(static_cast<long long>(value));
    return FormJson(value).dump();
}

// A range problem, or "".
std::string outOfRange(const FormField& field, double value)
{
    if (field.min && value < *field.min)
        return "is at least " + numberText(*field.min);
    if (field.max && value > *field.max)
        return "is at most " + numberText(*field.max);
    return {};
}

// The empty value a required field starts with.
FormJson emptyOf(FormField::Type type)
{
    switch (type)
    {
    case FormField::Type::Integer:
    case FormField::Type::Number: return 0;
    case FormField::Type::Flag: return false;
    case FormField::Type::List: return FormJson::array();
    case FormField::Type::Json: return FormJson::object();
    default: return "";
    }
}

}

std::optional<FormField::Type> FormField::typeFromName(std::string_view name)
{
    for (const auto& [type, text] : typeNames)
        if (text == name)
            return type;
    return std::nullopt;
}

std::vector<std::string> FormField::choices(const FormOptions& lists) const
{
    std::vector<std::string> out;
    std::set<std::string, std::less<>> seen;
    auto add = [&](const std::string& value) {
        if (seen.insert(value).second)
            out.push_back(value);
    };
    for (const std::string& value : options)
        add(value);
    if (!optionsFrom.empty())
        if (const auto found = lists.find(optionsFrom); found != lists.end())
            for (const std::string& value : found->second)
                add(value);
    return out;
}

std::string FormField::text(const FormJson& object) const
{
    if (!object.is_object() || !object.contains(key))
        return {};
    const FormJson& value = object.at(key);
    if (value.is_string())
        return value.get<std::string>();
    if (type == Type::List && value.is_array() && std::all_of(value.begin(), value.end(), [](const FormJson& v) { return v.is_string(); }))
    {
        std::string out;
        for (const FormJson& item : value)
            out += (out.empty() ? "" : ", ") + item.get<std::string>();
        return out;
    }
    return value.dump();
}

std::optional<FormJson> FormField::parse(std::string_view typed, std::string* error) const
{
    auto fail = [&](std::string why) -> std::optional<FormJson> {
        if (error)
            *error = title() + " " + why;
        return std::nullopt;
    };
    const std::string text = type == Type::Text ? std::string(typed) : trim(typed);
    if (text.empty() && type != Type::Text)
        return required ? emptyOf(type) : FormJson();
    switch (type)
    {
    case Type::Text:
        if (text.empty())
            return required ? FormJson("") : FormJson();
        return FormJson(text);
    case Type::Integer:
    {
        long long value = 0;
        const auto [end, problem] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (problem != std::errc() || end != text.data() + text.size())
            return fail("is a whole number");
        if (std::string range = outOfRange(*this, static_cast<double>(value)); !range.empty())
            return fail(range);
        return FormJson(value);
    }
    case Type::Number:
    {
        double value = 0;
        const auto [end, problem] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (problem != std::errc() || end != text.data() + text.size() || !std::isfinite(value))
            return fail("is a number");
        if (std::string range = outOfRange(*this, value); !range.empty())
            return fail(range);
        // Written as typed: "3" stays a whole number, "3.5" doesn't.
        if (text.find_first_of(".eE") == std::string::npos && std::abs(value) < 9e15)
            return FormJson(static_cast<long long>(value));
        return FormJson(value);
    }
    case Type::Flag:
        if (text == "true" || text == "yes" || text == "1")
            return FormJson(true);
        if (text == "false" || text == "no" || text == "0")
            return FormJson(false);
        return fail("is true or false");
    case Type::Choice:
        return FormJson(text);
    case Type::List:
    {
        FormJson list = FormJson::array();
        for (std::string& item : split(text))
            list.push_back(std::move(item));
        if (list.empty() && !required)
            return FormJson();
        return list;
    }
    case Type::Json:
    {
        FormJson value = FormJson::parse(text, nullptr, false);
        if (value.is_discarded())
            return fail("isn't valid JSON");
        return value;
    }
    }
    return fail("has an unknown type");
}

std::string FormField::problem(const FormJson& object, const FormOptions& lists, bool* unknown) const
{
    if (unknown)
        *unknown = false;
    if (!object.is_object() || !object.contains(key))
        return required ? "is missing" : std::string();
    const FormJson& value = object.at(key);
    const std::vector<std::string> allowed = choices(lists);
    auto known = [&](const std::string& item) {
        return allowed.empty() || std::find(allowed.begin(), allowed.end(), item) != allowed.end();
    };
    switch (type)
    {
    case Type::Text:
        if (!value.is_string())
            return "is text";
        if (required && value.get<std::string>().empty())
            return "is empty";
        return {};
    case Type::Integer:
        if (!value.is_number_integer())
            return "is a whole number";
        return outOfRange(*this, value.get<double>());
    case Type::Number:
        if (!value.is_number())
            return "is a number";
        return outOfRange(*this, value.get<double>());
    case Type::Flag:
        return value.is_boolean() ? std::string() : std::string("is true or false");
    case Type::Choice:
    {
        // Something other than a name (a file's long form, like an object of changes) is left
        // to the reader of the file; the form can only replace it with a name.
        if (!value.is_string())
        {
            if (unknown)
                *unknown = true;
            return {};
        }
        const std::string& name = value.get_ref<const std::string&>();
        if (required && name.empty())
            return "is empty";
        if (!name.empty() && !known(name) && unknown)
            *unknown = true;
        return {};
    }
    case Type::List:
        if (!value.is_array() || !std::all_of(value.begin(), value.end(), [](const FormJson& v) { return v.is_string(); }))
            return "is a list of names";
        if (unknown)
            *unknown = !std::all_of(value.begin(), value.end(), [&](const FormJson& v) { return known(v.get<std::string>()); });
        return {};
    case Type::Json:
        return {};
    }
    return {};
}

const FormField* FormSchema::field(std::string_view key) const
{
    const auto found = std::find_if(fields.begin(), fields.end(), [&](const FormField& f) { return f.key == key; });
    return found == fields.end() ? nullptr : &*found;
}

FormJson FormSchema::blank(const std::string& entryId) const
{
    FormJson out = FormJson::object();
    if (!idKey.empty())
        out[idKey] = entryId;
    for (const FormField& f : fields)
    {
        if (f.key == idKey)
            continue;
        if (!f.fallback.is_null())
            out[f.key] = f.fallback;
        else if (f.required)
            out[f.key] = emptyOf(f.type);
    }
    return out;
}

std::vector<std::string> FormSchema::problems(const FormJson& object, const FormOptions& lists, std::vector<std::string>* warnings) const
{
    std::vector<std::string> out;
    if (!object.is_object())
        return {"the file isn't a JSON object"};
    for (const FormField& f : fields)
    {
        bool unknown = false;
        if (std::string why = f.problem(object, lists, &unknown); !why.empty())
            out.push_back(f.title() + ": " + why);
        else if (unknown && warnings)
            warnings->push_back(f.title() + ": names something that isn't offered here");
    }
    return out;
}

std::vector<std::string> FormSchema::unlisted(const FormJson& object) const
{
    std::vector<std::string> out;
    if (!object.is_object())
        return out;
    for (const auto& [key, value] : object.items())
        if (key != idKey && !field(key))
            out.push_back(key);
    return out;
}

std::optional<FormSchema> FormSchema::fromJson(std::string_view text, std::string* error)
{
    const FormJson j = FormJson::parse(text, nullptr, false);
    if (j.is_discarded())
    {
        if (error)
            *error = "the form isn't valid JSON";
        return std::nullopt;
    }
    return fromObject(j, error);
}

std::optional<FormSchema> FormSchema::fromObject(const FormJson& j, std::string* error)
{
    auto fail = [&](std::string why) -> std::optional<FormSchema> {
        if (error)
            *error = std::move(why);
        return std::nullopt;
    };
    auto text = [](const FormJson& from, const char* key, std::string& into) {
        if (!from.contains(key))
            return true;
        if (!from.at(key).is_string())
            return false;
        into = from.at(key).get<std::string>();
        return true;
    };
    if (!j.is_object())
        return fail("a form is an object");
    FormSchema schema;
    if (!text(j, "id", schema.id) || schema.id.empty())
        return fail("a form needs an id");
    const std::string at = schema.id + ": ";
    if (!text(j, "label", schema.label) || !text(j, "folder", schema.folder) || !text(j, "idKey", schema.idKey))
        return fail(at + "label, folder and idKey are text");
    if (!j.contains("fields") || !j.at("fields").is_array())
        return fail(at + "fields is a list");

    static const std::set<std::string, std::less<>> known{"key", "type", "label", "help", "required", "min", "max", "options", "optionsFrom", "default"};
    for (const FormJson& f : j.at("fields"))
    {
        FormField field;
        if (!f.is_object() || !text(f, "key", field.key) || field.key.empty())
            return fail(at + "each field is an object with a key");
        const std::string where = at + field.key + ": ";
        for (const auto& [key, value] : f.items())
            if (!known.contains(key))
                return fail(where + "unknown field \"" + key + "\"");
        if (schema.field(field.key) || field.key == schema.idKey)
            return fail(where + "is listed twice");
        std::string typeName = "text";
        if (!text(f, "type", typeName))
            return fail(where + "type is text");
        const std::optional<FormField::Type> type = FormField::typeFromName(typeName);
        if (!type)
            return fail(where + "unknown type \"" + typeName + "\"");
        field.type = *type;
        if (!text(f, "label", field.label) || !text(f, "help", field.help) || !text(f, "optionsFrom", field.optionsFrom))
            return fail(where + "label, help and optionsFrom are text");
        if (f.contains("required"))
        {
            if (!f.at("required").is_boolean())
                return fail(where + "required is true or false");
            field.required = f.at("required").get<bool>();
        }
        for (const char* bound : {"min", "max"})
            if (f.contains(bound))
            {
                if (!f.at(bound).is_number())
                    return fail(where + bound + " is a number");
                (std::string_view(bound) == "min" ? field.min : field.max) = f.at(bound).get<double>();
            }
        if (field.min && field.max && *field.min > *field.max)
            return fail(where + "min is more than max");
        if (f.contains("options"))
        {
            if (!f.at("options").is_array())
                return fail(where + "options is a list of names");
            for (const FormJson& option : f.at("options"))
            {
                if (!option.is_string())
                    return fail(where + "options is a list of names");
                field.options.push_back(option.get<std::string>());
            }
        }
        if (f.contains("default"))
        {
            field.fallback = f.at("default");
            FormJson probe = FormJson::object();
            probe[field.key] = field.fallback;
            if (std::string why = field.problem(probe, {}); !why.empty())
                return fail(where + "default " + why);
        }
        schema.fields.push_back(std::move(field));
    }
    return schema;
}

}
