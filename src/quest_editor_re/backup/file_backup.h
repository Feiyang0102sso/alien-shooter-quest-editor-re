#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace quest {
namespace backup {
// Number of pre-save copies retained independently for each game file.
inline constexpr int max_copies = 2;
static_assert(max_copies >= 1, "At least one backup copy is required");
}
// Check original bytes before saving; keep backups and temporary files beside the target.
struct FileChange {
    std::filesystem::path path;
    std::string before;
    std::string after;
    bool existed = true;
    int backup_limit = 0; // Zero preserves transaction backups; the edit session supplies limits for game files.
};
// False means content was saved but backup rotation/cleanup was incomplete; write failures throw.
bool save_files(const std::vector<FileChange>& changes);

}
