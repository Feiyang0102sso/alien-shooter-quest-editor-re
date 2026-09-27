#include "../i18n/i18n.h"
#include "../ui/editor.h"

namespace {
HINSTANCE module_instance = nullptr;
}

// Preserve the fixed x86 cdecl ABI; the host command line is not a levels.cfg path.
extern "C" HWND __cdecl CreateNew_zQuestEditorMainWnd(HINSTANCE, HINSTANCE, LPSTR, int, HWND parent) {
    // Handle filesystem errors inside the ABI; exceptions must not cross the legacy host's C stack.
    std::error_code error;
    const auto directory = std::filesystem::current_path(error);
    if (error) {
        MessageBoxW(parent, quest::i18n::wide("launcher.host_directory_failed"), quest::i18n::wide("common.app_title"), MB_ICONERROR);
        return nullptr;
    }
    return quest::create_editor(module_instance, parent, directory / L"levels.cfg");
}
extern "C" void __cdecl Delete_zQuestEditorMainWnd() { quest::destroy_editor(); }
extern "C" int __cdecl GetCustomData(int* group, int* index) {
    quest::selected_data(group, index);
    return 0;
}
extern "C" int __cdecl SetGetQuestMode() {
    quest::set_pick_mode();
    return 1;
}
extern "C" char __cdecl SetTopMost(char enabled) {
    quest::topmost(enabled != 0);
    return enabled;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        module_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
