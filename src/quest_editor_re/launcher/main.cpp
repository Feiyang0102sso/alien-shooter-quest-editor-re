#include "../i18n/i18n.h"
#include "../game_files/path_selection.h"
#include <windows.h>
#include <shellapi.h>
#include <filesystem>

// The standalone launcher uses the public DLL ABI shared with legacy hosts.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto directory = std::filesystem::path(executable).parent_path();
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (count > 1) {
        std::error_code error;
        auto requested = std::filesystem::absolute(arguments[1],error);
        if (error || !std::filesystem::exists(requested,error)) {
            LocalFree(arguments);
            MessageBoxW(nullptr,quest::i18n::wide("launcher.missing_path"),quest::i18n::wide("common.app_title"),MB_ICONERROR);
            return 1;
        }
        if (!std::filesystem::is_directory(requested,error)) {
            if (_wcsicmp(requested.filename().c_str(),L"levels.cfg") != 0
                && _wcsicmp(requested.filename().c_str(),L"levels.db") != 0) {
                LocalFree(arguments);
                MessageBoxW(nullptr,quest::i18n::wide("launcher.invalid_argument"),quest::i18n::wide("common.app_title"),MB_ICONERROR);
                return 1;
            }
            requested = requested.parent_path();
        }
        if (!SetCurrentDirectoryW(requested.c_str())) {
            LocalFree(arguments);
            MessageBoxW(nullptr,quest::i18n::wide("launcher.change_directory_failed"),quest::i18n::wide("common.app_title"),MB_ICONERROR);
            return 1;
        }
    } else if (!std::filesystem::exists(quest::game_configuration_path(std::filesystem::current_path()))
        && std::filesystem::exists(quest::game_configuration_path(directory))) {
        SetCurrentDirectoryW(directory.c_str());
    }
    LocalFree(arguments);
    HMODULE library = LoadLibraryW((directory / L"QuestEditor.dll").c_str());
    if (!library) {
        MessageBoxW(nullptr, quest::i18n::wide("launcher.load_dll_failed"), quest::i18n::wide("common.app_title"), MB_ICONERROR);
        return 1;
    }
    using Create = HWND(__cdecl*)(HINSTANCE, HINSTANCE, LPSTR, int, HWND);
    using Destroy = void(__cdecl*)();
    using Top = void(__cdecl*)(unsigned char);
    const auto create = reinterpret_cast<Create>(GetProcAddress(library, "CreateNew_zQuestEditorMainWnd"));
    const auto destroy = reinterpret_cast<Destroy>(GetProcAddress(library, "Delete_zQuestEditorMainWnd"));
    const auto top = reinterpret_cast<Top>(GetProcAddress(library, "SetTopMost"));
    if (!create || !destroy) { FreeLibrary(library); return 2; }
    if (top) top(0);
    HWND window = create(instance, nullptr, GetCommandLineA(), SW_SHOW, nullptr);
    if (!window) { FreeLibrary(library); return 3; }
    ShowWindow(window, SW_SHOW);
    MSG message{};
    while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    destroy();
    FreeLibrary(library);
    return 0;
}
