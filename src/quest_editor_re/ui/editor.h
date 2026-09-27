#pragma once
#include <windows.h>
#include <filesystem>

namespace quest {
HWND create_editor(HINSTANCE module, HWND owner, const std::filesystem::path& initial_path);
void destroy_editor();
void set_pick_mode();
void selected_data(int* group, int* index);
void topmost(bool enabled);
}
