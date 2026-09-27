#include "path_selection.h"
#include <cwchar>

namespace quest {
std::filesystem::path game_configuration_path(std::filesystem::path requested) {
    if (std::filesystem::is_directory(requested)) requested /= L"levels.cfg";
    const auto extension = requested.extension().wstring();
    if (_wcsicmp(extension.c_str(),L".cfg") != 0 && _wcsicmp(extension.c_str(),L".db") != 0) return requested;
    auto cfg = requested;
    cfg.replace_extension(L".cfg");
    if (std::filesystem::exists(cfg)) return cfg;
    auto db = requested;
    db.replace_extension(L".db");
    if (std::filesystem::exists(db)) return db;
    return requested;
}
std::filesystem::path mission_text_path(const std::filesystem::path& configuration) {
    const auto nested = configuration.parent_path() / "Text" / "mission.txt";
    if (std::filesystem::exists(nested)) return nested;
    return configuration.parent_path() / "mission.txt";
}
}
