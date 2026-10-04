#include "yorehold/framework/assets/FileSystem.h"

#include <miniz.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

namespace yh
{

namespace fs = std::filesystem;

namespace
{

int64_t modifiedStamp(const fs::path& file)
{
    std::error_code error;
    const auto time = fs::last_write_time(file, error);
    return error ? -1 : static_cast<int64_t>(time.time_since_epoch().count());
}

}

struct FileSystem::Mount
{
    std::string name;
    bool isZip = false;
    fs::path folder;
    mz_zip_archive zip{};
    std::vector<std::string> only; // path prefixes it may supply; empty = everything

    ~Mount()
    {
        if (isZip)
            mz_zip_reader_end(&zip);
    }

    fs::path diskPath(std::string_view path) const
    {
        std::error_code error;
        const fs::path root = fs::weakly_canonical(folder, error);
        if (error) return {};
        const fs::path file = fs::weakly_canonical(root / fs::path(std::u8string(path.begin(), path.end())), error);
        if (error) return {};
        const fs::path relative = file.lexically_relative(root);
        if (relative.empty() || relative.is_absolute()) return {};
        for (const auto& component : relative)
            if (component == "..") return {};
        return file;
    }

    bool allows(std::string_view path) const
    {
        return only.empty() || std::any_of(only.begin(), only.end(), [&](const std::string& prefix) { return path.starts_with(prefix); });
    }

    bool has(std::string_view path) const
    {
        if (!allows(path))
            return false;
        if (isZip)
            return mz_zip_reader_locate_file(const_cast<mz_zip_archive*>(&zip), std::string(path).c_str(), nullptr, 0) >= 0;
        std::error_code error;
        return fs::is_regular_file(diskPath(path), error);
    }
};

FileSystem::FileSystem() = default;
FileSystem::~FileSystem() = default;

std::string FileSystem::normalize(std::string_view path)
{
    if (path.find('\0') != std::string_view::npos || path.find(':') != std::string_view::npos
        || (!path.empty() && (path.front() == '/' || path.front() == '\\'))) return {};
    std::string clean(path);
    std::replace(clean.begin(), clean.end(), '\\', '/');
    std::string out;
    for (size_t first = 0; first < clean.size();)
    {
        const size_t end = clean.find('/', first);
        const auto segment = std::string_view(clean).substr(first, end == std::string::npos ? clean.size() - first : end - first);
        if (segment == "..") return {};
        if (!segment.empty() && segment != ".")
        {
            if (!out.empty()) out += '/';
            out += segment;
        }
        if (end == std::string::npos) break;
        first = end + 1;
    }
    return out;
}

bool FileSystem::mountFolder(const std::string& folder, std::string name)
{
    std::error_code error;
    if (!fs::is_directory(folder, error))
        return false;
    auto mount = std::make_unique<Mount>();
    mount->name = name.empty() ? folder : std::move(name);
    mount->folder = folder;
    mounts_.push_back(std::move(mount));
    return true;
}

bool FileSystem::mountZip(const std::string& zipFile, std::string name)
{
    auto mount = std::make_unique<Mount>();
    mount->isZip = true;
    if (!mz_zip_reader_init_file(&mount->zip, zipFile.c_str(), 0))
    {
        mount->isZip = false;
        return false;
    }
    mount->name = name.empty() ? zipFile : std::move(name);
    mounts_.push_back(std::move(mount));
    return true;
}

void FileSystem::restrict(std::string_view name, std::vector<std::string> folders)
{
    for (std::string& folder : folders)
    {
        folder = normalize(folder);
        if (!folder.empty() && folder.back() != '/')
            folder += '/';
    }
    std::erase_if(folders, [](const std::string& folder) { return folder.empty(); });
    for (const auto& mount : mounts_)
        if (mount->name == name)
            mount->only = folders;
}

void FileSystem::unmount(std::string_view name)
{
    std::erase_if(mounts_, [&](const std::unique_ptr<Mount>& m) { return m->name == name; });
}

std::vector<std::string> FileSystem::mounts() const
{
    std::vector<std::string> names;
    for (const auto& m : mounts_)
        names.push_back(m->name);
    return names;
}

bool FileSystem::exists(std::string_view path) const
{
    return source(path).has_value();
}

std::optional<std::string> FileSystem::source(std::string_view path) const
{
    const std::string clean = normalize(path);
    if (clean.empty()) return std::nullopt;
    for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it)
    {
        if ((*it)->has(clean))
            return (*it)->name;
    }
    return std::nullopt;
}

std::optional<std::vector<unsigned char>> FileSystem::read(std::string_view path) const
{
    const std::string clean = normalize(path);
    if (clean.empty()) return std::nullopt;
    for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it)
    {
        Mount& mount = **it;
        if (!mount.has(clean))
            continue;

        if (mount.isZip)
        {
            size_t size = 0;
            void* data = mz_zip_reader_extract_file_to_heap(&mount.zip, clean.c_str(), &size, 0);
            if (!data)
                return std::nullopt;
            std::vector<unsigned char> bytes(static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + size);
            mz_free(data);
            return bytes;
        }

        const fs::path file = mount.diskPath(clean);
        std::ifstream in(file, std::ios::binary);
        if (!in) return std::nullopt;
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto known = std::find_if(watched_.begin(), watched_.end(), [&](const Watched& w) { return w.path == clean; });
        if (known == watched_.end())
            watched_.push_back({clean, modifiedStamp(file)});
        else
            known->stamp = modifiedStamp(file);
        return bytes;
    }
    return std::nullopt;
}

std::optional<std::string> FileSystem::readText(std::string_view path) const
{
    std::optional<std::vector<unsigned char>> bytes = read(path);
    if (!bytes)
        return std::nullopt;
    return std::string(bytes->begin(), bytes->end());
}

std::vector<std::string> FileSystem::list(std::string_view directory) const
{
    std::string prefix = normalize(directory);
    if (!directory.empty() && prefix.empty() && directory != ".") return {};
    if (!prefix.empty() && prefix.back() != '/')
        prefix += '/';

    std::set<std::string> names;
    for (const auto& mount : mounts_)
    {
        if (mount->isZip)
        {
            const mz_uint count = mz_zip_reader_get_num_files(&mount->zip);
            for (mz_uint i = 0; i < count; i++)
            {
                char name[512];
                mz_zip_reader_get_filename(&mount->zip, i, name, sizeof(name));
                const std::string entry = name;
                if (entry.starts_with(prefix) && entry.find('/', prefix.size()) == std::string::npos && entry.size() > prefix.size() && mount->allows(entry))
                    names.insert(entry);
            }
            continue;
        }
        std::error_code error;
        const fs::path location = prefix.empty() ? mount->folder : mount->diskPath(prefix);
        for (const auto& entry : fs::directory_iterator(location, error))
        {
            if (entry.is_regular_file(error) && mount->has(prefix + entry.path().filename().string()))
                names.insert(prefix + entry.path().filename().string());
        }
    }
    return {names.begin(), names.end()};
}

std::vector<std::string> FileSystem::pollChanges()
{
    std::vector<std::string> changed;
    for (Watched& w : watched_)
    {
        int64_t current = -1;
        for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it)
        {
            if (!(*it)->has(w.path)) continue;
            current = (*it)->isZip ? -2 : modifiedStamp((*it)->diskPath(w.path));
            break;
        }
        if (current != w.stamp) { w.stamp = current; changed.push_back(w.path); }
    }
    return changed;
}

bool FileSystem::packFolder(const std::string& folder, const std::string& zipFile)
{
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, zipFile.c_str(), 0))
        return false;
    bool ok = true;
    std::error_code error;
    for (const auto& entry : fs::recursive_directory_iterator(folder, error))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string name = normalize(fs::relative(entry.path(), folder).generic_string());
        ok &= mz_zip_writer_add_file(&zip, name.c_str(), entry.path().string().c_str(), nullptr, 0, MZ_BEST_COMPRESSION) != 0;
    }
    ok &= mz_zip_writer_finalize_archive(&zip) != 0;
    mz_zip_writer_end(&zip);
    return ok && !error;
}

}
