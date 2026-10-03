#pragma once

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace yh
{

// Bumped whenever a framework save format (TileMap, Objects, Regions, FogOfWar...) changes shape.
// Envelopes record it so old saves can be upgraded by the framework's own migrations.
inline constexpr int frameworkSaveVersion = 1;

// Edits a saved document in place, upgrading it by exactly one version. Throw to reject the save.
using Migration = std::function<void(nlohmann::json& data)>;

// A versioned save envelope: {"format": name, "version": N, "framework": F, "data": {...}}.
// Bump the version whenever the client's save layout changes and register a step from the old
// version; read() runs every step from the saved version up to the current one. Reads are
// all-or-nothing: a failing step leaves nothing half-upgraded.
class SaveFormat
{
public:
    SaveFormat(std::string name, int version);

    // Upgrades data saved at `from` to `from + 1`.
    void migrate(int from, Migration step);
    // Upgrades framework-owned data inside the client data saved with framework version `from`.
    // Clients rarely need this; the framework registers its own steps.
    void migrateFramework(int from, Migration step);

    const std::string& name() const { return name_; }
    int version() const { return version_; }

    // `data` must be JSON (any value). Returns the envelope.
    std::string write(std::string_view data) const;
    // Returns the data upgraded to the current version.
    std::optional<std::string> read(std::string_view envelope, std::string* error = nullptr, int* savedVersion = nullptr) const;

    // Writes through a temporary file and keeps the previous save as `path.bak`, so a crash or full
    // disk mid-write never destroys the last good save.
    bool writeFile(const std::string& path, std::string_view data, std::string* error = nullptr) const;
    // Falls back to `path.bak` if the main file is missing or unreadable.
    std::optional<std::string> readFile(const std::string& path, std::string* error = nullptr, int* savedVersion = nullptr) const;

private:
    std::string name_;
    int version_;
    std::map<int, Migration> steps_;
    std::map<int, Migration> frameworkSteps_;
};

// Atomic text write: temp file, flush, then replace. Keeps the old file as `path.bak` when asked.
bool writeFileAtomically(const std::string& path, std::string_view text, bool keepBackup = true, std::string* error = nullptr);
std::optional<std::string> readTextFile(const std::string& path);

}
