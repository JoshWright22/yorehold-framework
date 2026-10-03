#include "yorehold/framework/save/SaveFile.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

std::filesystem::path utf8Path(const std::string& path) { return std::u8string(path.begin(), path.end()); }

void runSteps(json& data, int from, int to, const std::map<int, Migration>& steps, const char* what)
{
    for (int v = from; v < to; ++v)
    {
        const auto step = steps.find(v);
        if (step == steps.end()) throw std::invalid_argument(std::string("No ") + what + " migration from version " + std::to_string(v));
        step->second(data);
    }
}

}

SaveFormat::SaveFormat(std::string name, int version) : name_(std::move(name)), version_(version)
{
    if (name_.empty() || version_ < 1) throw std::invalid_argument("Save formats need a name and a version of at least 1");
}

void SaveFormat::migrate(int from, Migration step)
{
    if (from < 1 || from >= version_ || !step) throw std::invalid_argument("Migration must upgrade an older version");
    steps_[from] = std::move(step);
}

void SaveFormat::migrateFramework(int from, Migration step)
{
    if (from < 1 || from >= frameworkSaveVersion || !step) throw std::invalid_argument("Framework migration must upgrade an older version");
    frameworkSteps_[from] = std::move(step);
}

std::string SaveFormat::write(std::string_view data) const
{
    return json{{"format", name_}, {"version", version_}, {"framework", frameworkSaveVersion}, {"data", json::parse(data)}}.dump();
}

std::optional<std::string> SaveFormat::read(std::string_view envelope, std::string* error, int* savedVersion) const
{
    if (error) error->clear();
    try
    {
        json j = json::parse(envelope);
        if (!j.is_object() || j.value("format", "") != name_) throw std::invalid_argument("Not a " + name_ + " save");
        const int version = j.at("version").get<int>();
        const int framework = j.value("framework", 1);
        if (savedVersion) *savedVersion = version;
        if (version > version_ || framework > frameworkSaveVersion) throw std::invalid_argument("Saved by a newer version");
        if (version < 1 || framework < 1) throw std::invalid_argument("Invalid save version");
        json data = std::move(j.at("data"));
        // Framework data is upgraded first so client steps always see current framework shapes.
        runSteps(data, framework, frameworkSaveVersion, frameworkSteps_, "framework");
        runSteps(data, version, version_, steps_, name_.c_str());
        return data.dump();
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return std::nullopt; }
}

bool SaveFormat::writeFile(const std::string& path, std::string_view data, std::string* error) const
{
    try { return writeFileAtomically(path, write(data), true, error); }
    catch (const std::exception& e) { if (error) *error = e.what(); return false; }
}

std::optional<std::string> SaveFormat::readFile(const std::string& path, std::string* error, int* savedVersion) const
{
    std::string why;
    if (const auto text = readTextFile(path))
        if (auto data = read(*text, &why, savedVersion)) { if (error) error->clear(); return data; }
    if (const auto backup = readTextFile(path + ".bak"))
        if (auto data = read(*backup, nullptr, savedVersion)) { if (error) error->clear(); return data; }
    if (error) *error = why.empty() ? "Save file not found" : why;
    return std::nullopt;
}

bool writeFileAtomically(const std::string& path, std::string_view text, bool keepBackup, std::string* error)
{
    namespace fs = std::filesystem;
    const fs::path target = utf8Path(path);
    const fs::path temp = utf8Path(path + ".tmp");
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) { if (error) *error = "Could not write " + path + ".tmp"; fs::remove(temp, ec); return false; }
    }
    // Between these renames the main file may briefly be missing; readers fall back to the backup.
    if (keepBackup && fs::exists(target, ec)) fs::rename(target, utf8Path(path + ".bak"), ec);
    ec.clear();
    fs::rename(temp, target, ec);
    if (ec) { if (error) *error = ec.message(); return false; }
    return true;
}

std::optional<std::string> readTextFile(const std::string& path)
{
    std::ifstream in(utf8Path(path), std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

}
