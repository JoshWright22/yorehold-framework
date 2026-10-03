#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

// One virtual file tree built from folders and zip archives (.yoreskin, .yore chapters...).
// Later mounts override earlier ones, so a skin mounted on top of the defaults only needs the files
// it changes. Paths always use forward slashes, e.g. "textures/token.png".
//
// The same code runs on desktop (real folders) and in the browser (a virtual folder in browser storage).
class FileSystem
{
public:
    FileSystem();
    ~FileSystem();
    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;

    // `name` identifies the mount for unmount(); defaults to the path.
    bool mountFolder(const std::string& folder, std::string name = {});
    bool mountZip(const std::string& zipFile, std::string name = {});
    void unmount(std::string_view name);
    std::vector<std::string> mounts() const;

    bool exists(std::string_view path) const;
    std::optional<std::vector<unsigned char>> read(std::string_view path) const;
    std::optional<std::string> readText(std::string_view path) const;
    // Files directly inside `directory` (all mounts combined, no duplicates).
    std::vector<std::string> list(std::string_view directory) const;
    // Which mount a path currently comes from (for debugging skins).
    std::optional<std::string> source(std::string_view path) const;

    // Paths of files that changed on disk (in folder mounts) since the last call. Only files that
    // have been read are watched, so this stays cheap. Drives hot reload.
    std::vector<std::string> pollChanges();

    static std::string normalize(std::string_view path);
    // Zips a folder (e.g. a finished skin) into a .yoreskin / .yore file.
    static bool packFolder(const std::string& folder, const std::string& zipFile);

private:
    struct Mount;
    std::vector<std::unique_ptr<Mount>> mounts_;
    struct Watched
    {
        std::string path;
        int64_t stamp;
    };
    mutable std::vector<Watched> watched_;
};

}
