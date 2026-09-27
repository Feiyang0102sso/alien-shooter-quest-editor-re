#pragma once
#include <filesystem>

namespace quest {
// Directories and same-name DB files prefer CFG; unrelated paths are unchanged.
std::filesystem::path game_configuration_path(std::filesystem::path requested);
// Prefer Text/mission.txt, then the task configuration's neighboring mission.txt.
std::filesystem::path mission_text_path(const std::filesystem::path& configuration);
}
