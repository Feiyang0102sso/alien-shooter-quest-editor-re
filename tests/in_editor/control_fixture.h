#pragma once
#include "../common/test_environment.h"
#include <algorithm>
namespace editor_test {
extern int dialog_answer, dialogs_answered, dialogs_created, property_mode;
extern bool preserve_text, append_text, property_observed;
extern std::wstring observed_property_tab, observed_comment;
void pump();
void CALLBACK answer_own_dialog(HWND, UINT, UINT_PTR, DWORD);
void CALLBACK edit_own_text(HWND, UINT, UINT_PTR, DWORD);
void CALLBACK edit_own_properties(HWND, UINT, UINT_PTR, DWORD);
void create_test_node(HWND window);
std::map<DWORD,int> graph_colors(HWND graph);
// Every UI case owns a fresh DLL, settings file and mock game directory.
class Controls : public test_support::Files {
protected:
    using Create = HWND(__cdecl*)(HINSTANCE,HINSTANCE,LPSTR,int,HWND);
    using Destroy = void(__cdecl*)();
    HHOOK dialog_hook = nullptr;
    HMODULE library = nullptr;
    Create create = nullptr;
    Destroy destroy = nullptr;
    HWND window = nullptr, graph = nullptr;
    HMENU file_menu = nullptr;
    std::filesystem::path directory, config, samples;
    const std::string original_mission = "<1_1_1>\r\nFind the key\r\n<heading>\r\nOriginal title\r\n";
    quest::EditSession check;
    wchar_t caption[128]{};
    UINT_PTR timer = 0;
    void SetUp() override;
    void TearDown() override;
    void pan();
};
}
