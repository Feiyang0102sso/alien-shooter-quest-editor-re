#pragma once
#include <filesystem>

// Editor strings use UTF-8 en.ini/cn.ini files, independently of game file encodings.
// Preserve menu \t, message \n and file-filter \0 escapes.
// CFG fields, state constants, window classes, filenames and DLL exports remain in compatibility code.
// Task names, comments and bodies come from CFG/mission.txt, not this catalog.
namespace quest::i18n {
// Use the current DLL/EXE directory by default; tests may supply an isolated directory.
void initialize(const std::filesystem::path& directory = {});
int language();
void set_language(int value);
const wchar_t* wide(const char* key);
const char* narrow(const char* key);
}
