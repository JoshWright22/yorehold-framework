#include "FormTests.h"
#include "Checks.h"

#include <yorehold/framework/editor/Form.h>

namespace regression
{
namespace
{

const char* gear = R"form({
  "id": "gear",
  "label": "Gear",
  "folder": "gear",
  "editorOnly": "kept for the caller",
  "fields": [
    {"key": "name", "required": true, "default": "New gear"},
    {"key": "weight", "type": "number", "min": 0},
    {"key": "value", "type": "integer", "min": 0, "max": 1000, "label": "Value (cp)"},
    {"key": "magic", "type": "flag"},
    {"key": "slot", "type": "choice", "options": ["", "hand", "body"]},
    {"key": "ability", "type": "choice", "optionsFrom": "abilities"},
    {"key": "tags", "type": "list", "optionsFrom": "tags"},
    {"key": "use", "type": "json"}
  ]
})form";

yh::FormSchema schema()
{
    std::string error;
    std::optional<yh::FormSchema> read = yh::FormSchema::fromJson(gear, &error);
    CHECK(read && error.empty());
    return read ? *read : yh::FormSchema{};
}

void reading()
{
    const yh::FormSchema form = schema();
    CHECK(form.id == "gear" && form.folder == "gear" && form.idKey == "id" && form.fields.size() == 8);
    CHECK(form.field("value") && form.field("value")->title() == "Value (cp)" && form.field("weight")->title() == "weight");
    CHECK(form.field("value")->min == 0.0 && form.field("value")->max == 1000.0 && !form.field("missing"));

    std::string error;
    CHECK(!yh::FormSchema::fromJson(R"({"id": "x", "fields": [{"key": "a", "type": "colour"}]})", &error) && error.find("colour") != std::string::npos);
    CHECK(!yh::FormSchema::fromJson(R"({"id": "x", "fields": [{"key": "a", "colour": 1}]})", &error) && error.find("colour") != std::string::npos);
    CHECK(!yh::FormSchema::fromJson(R"({"id": "x", "fields": [{"key": "a"}, {"key": "a"}]})", &error) && error.find("twice") != std::string::npos);
    CHECK(!yh::FormSchema::fromJson(R"({"id": "x", "fields": [{"key": "a", "type": "integer", "default": "ten"}]})", &error));
    CHECK(!yh::FormSchema::fromJson(R"({"id": "x", "fields": [{"key": "a", "type": "integer", "min": 5, "max": 1}]})", &error));
    CHECK(!yh::FormSchema::fromJson(R"({"fields": []})", &error) && !yh::FormSchema::fromJson("[", &error));
    // No id key: the file name is the id.
    const auto kit = yh::FormSchema::fromJson(R"({"id": "kit", "idKey": "", "fields": [{"key": "name"}]})");
    CHECK(kit && kit->blank("door") == yh::FormJson::object());
}

// The parsed value, or a string no field makes, so a failed parse never matches.
yh::FormJson parsed(const yh::FormField& field, std::string_view text)
{
    const std::optional<yh::FormJson> value = field.parse(text);
    return value ? *value : yh::FormJson("<refused>");
}

void typing()
{
    const yh::FormSchema form = schema();
    const yh::FormField& weight = *form.field("weight");
    const yh::FormField& value = *form.field("value");
    std::string error;
    CHECK(parsed(weight, "2.5") == yh::FormJson(2.5) && parsed(weight, " 3 ").is_number_integer() && parsed(weight, "3") == yh::FormJson(3));
    CHECK(!weight.parse("-1", &error) && error == "weight is at least 0");
    CHECK(parsed(weight, "").is_null()); // blank leaves it out
    CHECK(parsed(value, "40") == yh::FormJson(40) && !value.parse("4.5", &error) && error == "Value (cp) is a whole number" && !value.parse("2000"));
    CHECK(parsed(*form.field("magic"), "true") == yh::FormJson(true) && parsed(*form.field("magic"), "no") == yh::FormJson(false) && !form.field("magic")->parse("maybe"));
    CHECK(parsed(*form.field("tags"), "a, b,, c ") == yh::FormJson::array({"a", "b", "c"}) && parsed(*form.field("tags"), " , ").is_null());
    CHECK(parsed(*form.field("use"), R"({"cost": 1})") == yh::FormJson::object({{"cost", 1}}) && !form.field("use")->parse("{cost", &error) && error.find("JSON") != std::string::npos);
    CHECK(parsed(*form.field("name"), "") == yh::FormJson("")); // required: kept, and reported as empty
    CHECK(parsed(*form.field("slot"), "hand") == yh::FormJson("hand"));

    const yh::FormJson item = yh::FormJson::parse(R"({"id": "rope", "name": "Rope", "weight": 10, "tags": ["tool", "camp"], "use": {"cost": 1}})");
    CHECK(form.field("name")->text(item) == "Rope" && weight.text(item) == "10" && form.field("tags")->text(item) == "tool, camp");
    CHECK(form.field("use")->text(item) == R"({"cost":1})" && value.text(item).empty());
}

void checking()
{
    const yh::FormSchema form = schema();
    const yh::FormOptions lists{{"abilities", {"str", "dex"}}, {"tags", {"tool"}}};
    CHECK(form.field("ability")->choices(lists) == std::vector<std::string>{"str", "dex"});
    CHECK(form.field("slot")->choices(lists).size() == 3);

    yh::FormJson made = form.blank("rope");
    CHECK(made.dump() == R"({"id":"rope","name":"New gear"})" && form.problems(made, lists).empty());

    yh::FormJson wrong = yh::FormJson::parse(R"({"id": "x", "name": "", "weight": "heavy", "value": 2.5, "magic": 1, "slot": 3, "tags": [1], "extra": true})");
    std::vector<std::string> other;
    const std::vector<std::string> found = form.problems(wrong, lists, &other);
    CHECK(found.size() == 5 && found[0] == "name: is empty" && found[1] == "weight: is a number");
    // A choice written another way (an object, a number) is left to the file's reader.
    CHECK(other == std::vector<std::string>{"slot: names something that isn't offered here"});
    CHECK(form.unlisted(wrong) == std::vector<std::string>{"extra"});

    std::vector<std::string> warnings;
    const yh::FormJson odd = yh::FormJson::parse(R"({"name": "Odd", "ability": "luck", "tags": ["tool", "junk"], "slot": "hand"})");
    CHECK(form.problems(odd, lists, &warnings).empty() && warnings.size() == 2);
    // Without a list to check against, anything goes.
    warnings.clear();
    CHECK(form.problems(odd, {}, &warnings).empty() && warnings.empty());
    CHECK(form.problems(yh::FormJson::array(), lists).size() == 1);
}

}

void forms()
{
    reading();
    typing();
    checking();
}

}
