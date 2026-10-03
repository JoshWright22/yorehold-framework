#include "yorehold/framework/input/Input.h"

#include <SDL3/SDL_gamepad.h>
#include <nlohmann/json.hpp>

#include <stdexcept>

namespace yh
{

std::string InputMap::toJson() const
{
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [name, bindings] : actions_)
    {
        auto& entries = j[name] = nlohmann::json::array();
        for (const Binding& b : bindings)
            switch (b.type)
            {
            case Binding::Type::Key: entries.push_back({{"type", "key"}, {"key", b.key}}); break;
            case Binding::Type::Mouse: entries.push_back({{"type", "mouse"}, {"button", static_cast<int>(b.button)}}); break;
            // SDL's names ("a", "dpup", "leftx") keep saved bindings readable and stable across SDL versions.
            case Binding::Type::GamepadButton:
                entries.push_back({{"type", "gamepadButton"}, {"button", SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(b.pad))}});
                break;
            case Binding::Type::GamepadAxis:
                entries.push_back({{"type", "gamepadAxis"}, {"axis", SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(b.pad))}, {"direction", b.direction}});
                break;
            }
    }
    return j.dump();
}

std::optional<InputMap> InputMap::fromJson(std::string_view json, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = nlohmann::json::parse(json);
        if (!j.is_object()) throw std::invalid_argument("Input map must be an object");
        InputMap map;
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (it.key().empty() || !it.value().is_array()) throw std::invalid_argument("Invalid action bindings");
            map.actions_.try_emplace(it.key());
            for (const auto& b : it.value())
            {
                const std::string type = b.at("type").get<std::string>();
                if (type == "key")
                {
                    const int64_t key = b.at("key").get<int64_t>();
                    if (key < 0 || key > UINT32_MAX) throw std::invalid_argument("Invalid key code");
                    map.bind(it.key(), Binding::keyboard(static_cast<uint32_t>(key)));
                }
                else if (type == "mouse")
                {
                    const int button = b.at("button").get<int>();
                    if (button < 1 || button > 5) throw std::invalid_argument("Invalid mouse button");
                    map.bind(it.key(), Binding::mouse(static_cast<MouseButton>(button)));
                }
                else if (type == "gamepadButton")
                {
                    const auto button = SDL_GetGamepadButtonFromString(b.at("button").get<std::string>().c_str());
                    if (button == SDL_GAMEPAD_BUTTON_INVALID) throw std::invalid_argument("Unknown gamepad button");
                    map.bind(it.key(), Binding::gamepadButton(static_cast<uint8_t>(button)));
                }
                else if (type == "gamepadAxis")
                {
                    const auto axis = SDL_GetGamepadAxisFromString(b.at("axis").get<std::string>().c_str());
                    const int direction = b.value("direction", 1);
                    if (axis == SDL_GAMEPAD_AXIS_INVALID || (direction != 1 && direction != -1)) throw std::invalid_argument("Invalid gamepad axis");
                    map.bind(it.key(), Binding::gamepadAxis(static_cast<uint8_t>(axis), static_cast<int8_t>(direction)));
                }
                else throw std::invalid_argument("Unknown binding type");
            }
        }
        return map;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return std::nullopt; }
}

}
