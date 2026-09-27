#include "../common/test_environment.h"
namespace {
int notifications = 0;
WPARAM notification_value = 0;
LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_COMMAND) { ++notifications; notification_value = wparam; return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
}

class Abi : public test_support::Files {
protected:
    HWND host = nullptr;
    HMODULE library = nullptr;
    void TearDown() override {
        if (library) {
            using Destroy = void(__cdecl*)();
            auto destroy = reinterpret_cast<Destroy>(GetProcAddress(library, "Delete_zQuestEditorMainWnd"));
            if (destroy) destroy();
            FreeLibrary(library);
        }
        if (host) DestroyWindow(host);
        UnregisterClassW(L"QuestEditorRE.TestHost", GetModuleHandleW(nullptr));
        Files::TearDown();
    }
};
TEST_F(Abi, SupportsRepeatedCreatePickCancelAndDestroy) {
    const auto fixture_dir = workspace / L"abi-fixture";
    std::filesystem::create_directories(fixture_dir);
    write(fixture_dir / L"levels.cfg", fixture);
    write(fixture_dir / L"mission.txt", "<1_1_1>\r\nTest\r\n");
    std::filesystem::current_path(fixture_dir);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = host_proc; type.hInstance = instance; type.lpszClassName = L"QuestEditorRE.TestHost";
    RegisterClassW(&type);
    host = CreateWindowW(type.lpszClassName, L"ABI test host", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, instance, nullptr);
    for (int iteration = 0; iteration < 4; ++iteration) {
        library = LoadLibraryW(dll.c_str());
        ASSERT_TRUE((library != nullptr)) << "LoadLibrary DLL";
        using Create = HWND(__cdecl*)(HINSTANCE, HINSTANCE, LPSTR, int, HWND);
        using Destroy = void(__cdecl*)();
        using Pick = int(__cdecl*)();
        using Get = int(__cdecl*)(int*, int*);
        using Top = char(__cdecl*)(char);
        auto create = reinterpret_cast<Create>(GetProcAddress(library, "CreateNew_zQuestEditorMainWnd"));
        auto destroy = reinterpret_cast<Destroy>(GetProcAddress(library, "Delete_zQuestEditorMainWnd"));
        auto pick = reinterpret_cast<Pick>(GetProcAddress(library, "SetGetQuestMode"));
        auto get = reinterpret_cast<Get>(GetProcAddress(library, "GetCustomData"));
        auto top = reinterpret_cast<Top>(GetProcAddress(library, "SetTopMost"));
        ASSERT_TRUE((create && destroy && pick && get && top)) << "all fixed exports";
        ASSERT_TRUE((GetProcAddress(library, MAKEINTRESOURCEA(1)) == reinterpret_cast<FARPROC>(create))) << "ordinal 1";
        get(nullptr, nullptr); destroy();
        top(1);
        HWND window = create(instance, nullptr, GetCommandLineA(), 0, host);
        ASSERT_TRUE((IsWindow(window))) << "create owned editor";
        pump();
        ASSERT_TRUE((top(1) == 1)) << "topmost ABI";
        top(0);
        int group = 99, index = 99;
        get(&group, &index);
        ASSERT_TRUE((group == 0 && index == 0)) << "initial custom data";
        pick();
        HWND graph = FindWindowExW(window, nullptr, L"QuestEditorRE.Graph.v1", nullptr);
        ASSERT_TRUE((graph != nullptr)) << "canvas exists";
        const int previous = notifications;
        // Send native mouse messages to our own host to exercise the real window handlers.
        SendMessageW(graph, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(335, 67));
        get(&group, &index);
        if (group != 1 || index != 1) {
            RECT bounds{};
            GetClientRect(graph,&bounds);
            std::cout << "PICK mismatch iteration=" << iteration << " group=" << group << " index=" << index
                << " canvas=" << bounds.right << 'x' << bounds.bottom << " dpi=" << GetDpiForWindow(graph)
                << " space=" << GetKeyState(VK_SPACE) << " ctrl=" << GetKeyState(VK_CONTROL) << '\n';
        }
        ASSERT_TRUE((group == 1 && index == 1)) << "picked task data";
        ASSERT_TRUE((notifications == previous + 1 && notification_value == 0x029A029A)) << "exact synchronous notification";
        ASSERT_TRUE((!IsWindowVisible(window))) << "picker hides";
        ASSERT_TRUE((quest::read_file(fixture_dir / L"levels.cfg") == fixture)) << "selection never saves";
        top(1);
        window = create(instance, nullptr, GetCommandLineA(), 0, host);
        ASSERT_TRUE((IsWindow(window))) << "repeated create";
        pick(); get(&group, &index);
        ASSERT_TRUE((group == 0 && index == 0)) << "selection resets";
        SendMessageW(window, WM_CLOSE, 0, 0);
        ASSERT_TRUE((notifications == previous + 2)) << "cancel notifies";
        destroy(); destroy();
        ASSERT_TRUE((!IsWindow(window))) << "idempotent delete";
        FreeLibrary(library); library = nullptr; pump();
    }
    DestroyWindow(host); host = nullptr;
    UnregisterClassW(type.lpszClassName, instance);
}
}
