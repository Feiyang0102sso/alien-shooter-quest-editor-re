#include "../common/test_environment.h"
#include "../common/process.h"
#include "../common/game_environment.h"

namespace {
constexpr DWORD WINDOW_TIMEOUT = 30000;
constexpr UINT QUEST_COMMAND = 0xA09B;
constexpr UINT STACK_COMMAND = 0xA0AB;
constexpr int ACTION_COMBO = 1057;
constexpr int SELECT_QUEST = 1113;

struct WindowQuery {
    DWORD process;
    const wchar_t* class_name;
    HWND result = nullptr;
};
BOOL CALLBACK find_window(HWND window, LPARAM parameter) {
    auto& query = *reinterpret_cast<WindowQuery*>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    wchar_t type[128]{};
    GetClassNameW(window, type, 128);
    if (process == query.process && IsWindowVisible(window) && std::wstring(type) == query.class_name) {
        query.result = window;
        return FALSE;
    }
    return TRUE;
}
HWND wait_window(DWORD process, const wchar_t* type) {
    std::wcout << L"[WAIT] " << type << L"\n" << std::flush;
    const auto deadline = GetTickCount64() + WINDOW_TIMEOUT;
    while (GetTickCount64() < deadline) {
        WindowQuery query{process, type};
        EnumWindows(find_window, reinterpret_cast<LPARAM>(&query));
        if (query.result) {
            SetWindowPos(query.result, HWND_BOTTOM, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            require_offscreen(query.result);
            return query.result;
        }
        Sleep(50);
    }
    std::wcerr << L"[TIMEOUT] " << type << L" pid=" << process << L"\n";
    throw std::runtime_error("Expected host window did not appear");
}
LRESULT message(HWND window, UINT id, WPARAM first = 0, LPARAM second = 0) {
    DWORD_PTR result = 0;
    ensure(SendMessageTimeoutW(window, id, first, second, SMTO_ABORTIFHUNG | SMTO_BLOCK,
        3000, &result) != 0, "Host control stopped responding");
    return static_cast<LRESULT>(result);
}
void wait_closed(HWND window) {
    const auto deadline = GetTickCount64() + WINDOW_TIMEOUT;
    while (IsWindow(window) && IsWindowVisible(window) && GetTickCount64() < deadline) Sleep(50);
    ensure(!IsWindow(window) || !IsWindowVisible(window), "Editor did not close");
}
int parameter(HWND dialog, int control) {
    wchar_t text[64]{};
    message(GetDlgItem(dialog, control), WM_GETTEXT, 64, reinterpret_cast<LPARAM>(text));
    ensure(text[0] != 0, "Host parameter is empty");
    return std::stoi(text);
}
void wait_parameters(HWND dialog, int group, int quest) {
    // The editor hides before its synchronous notification updates the host controls.
    const auto deadline = GetTickCount64() + WINDOW_TIMEOUT;
    int actual_group = -1;
    int actual_quest = -1;
    int actual_state = -1;
    while (GetTickCount64() < deadline) {
        actual_group = parameter(dialog, 1007);
        actual_quest = parameter(dialog, 1008);
        actual_state = parameter(dialog, 1009);
        if (actual_group == group && actual_quest == quest && actual_state == 0) return;
        Sleep(50);
    }
    std::cerr << "[FAIL] Quest parameters=" << actual_group << ',' << actual_quest << ',' << actual_state << "\n";
    throw std::runtime_error("Quest parameters did not reach the expected values");
}
void select_action(HWND dialog, const wchar_t* action) {
    HWND combo = GetDlgItem(dialog, ACTION_COMBO);
    ensure(combo != nullptr, "StackLine action control missing");
    const auto index = message(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
        reinterpret_cast<LPARAM>(action));
    ensure(index != CB_ERR, "Quest action missing from host list");
    message(combo, CB_SETCURSEL, index);
    PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(ACTION_COMBO, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
}
void check_action(HWND dialog, const wchar_t* action) {
    wchar_t text[128]{};
    message(GetDlgItem(dialog, ACTION_COMBO), WM_GETTEXT, 128, reinterpret_cast<LPARAM>(text));
    ensure(std::wstring(text) == action, "Quest selection changed action type");
}
class Host : public testing::Test {
protected:
    std::filesystem::path config_path, mission_path;
    std::string before, mission_before;
    std::unique_ptr<TemporaryFile> settings;
    std::unique_ptr<TestGameSettings> registry;
    std::unique_ptr<TestProcess> process;
    HWND host = nullptr;
    quest::Position position{};
    void SetUp() override {
        const auto& game = test_support::game;
        config_path = quest::game_configuration_path(game);
        before = quest::read_file(config_path);
        quest::EditSession session;
        session.open(config_path);
        mission_path = session.mission_path;
        mission_before = quest::read_file(mission_path);
        ASSERT_TRUE(session.document.find("Quest", "1_1_1").has_value());
        position = session.default_positions().at("1_1_1");
        settings = std::make_unique<TemporaryFile>(game / "MapEditor.cfg", test_support::project / "out/test/recovery");
        registry = std::make_unique<TestGameSettings>();
        const auto path = game / "MapEditor.cfg";
        registry->configure(test_support::project / "tests/assets/config/test.cfg", path);
        ASSERT_TRUE(WritePrivateProfileStringW(L"game", L"StartMap", L"new.map", path.c_str()));
        process = std::make_unique<TestProcess>(game / "MapEditor.exe", L"");
        host = hidden_test_window(*process, L"MapEditor");
        ShowWindowAsync(host, SW_SHOWNOACTIVATE);
        ASSERT_EQ(WaitForInputIdle(process->info.hProcess, WINDOW_TIMEOUT), 0u);
        message(host, WM_ACTIVATEAPP, TRUE, 0);
        message(host, WM_ACTIVATE, WA_ACTIVE, 0);
        // Input-idle can occur while the engine still loads resources and its DLL.
        // Opening and closing the public editor entry is a readiness handshake.
        const auto deadline = GetTickCount64() + WINDOW_TIMEOUT;
        HWND ready_editor = nullptr;
        while (GetTickCount64() < deadline) {
            WindowQuery query{process->info.dwProcessId, L"QuestEditorRE.Main.v1"};
            EnumWindows(find_window, reinterpret_cast<LPARAM>(&query));
            if (query.result) { ready_editor = query.result; break; }
            ensure(WaitForSingleObject(process->info.hProcess, 0) == WAIT_TIMEOUT,
                "MapEditor exited during initialization");
            PostMessageW(host, WM_COMMAND, QUEST_COMMAND, 0);
            Sleep(100);
        }
        ASSERT_NE(ready_editor, nullptr) << "MapEditor did not finish loading the quest editor";
        SetWindowPos(ready_editor, HWND_BOTTOM, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        require_offscreen(ready_editor);
        PostMessageW(ready_editor, WM_COMMAND, 133, 0);
        wait_closed(ready_editor);
    }
    void TearDown() override {
        if (process) {
            if (IsWindow(host)) PostMessageW(host, WM_CLOSE, 0, 0);
            // Report exit failures but still restore files and registry settings.
            try { process->wait(15000); }
            catch (const std::exception& error) { ADD_FAILURE() << error.what(); }
            process.reset();
        }
        if (settings) {
            try { settings->restore(); }
            catch (const std::exception& error) { ADD_FAILURE() << error.what(); }
            settings.reset();
        }
        registry.reset();
        if (!config_path.empty()) EXPECT_EQ(quest::read_file(config_path), before);
        if (!mission_path.empty()) EXPECT_EQ(quest::read_file(mission_path), mission_before);
    }
    void exercise_action(const wchar_t* action, bool cancel) {
        HWND editor = nullptr;
            // Both commands use the original StackLine parameter dialog.
            PostMessageW(host, WM_COMMAND, STACK_COMMAND, 0);
            HWND dialog = wait_window(process->info.dwProcessId, L"#32770");
            ensure(GetDlgItem(dialog, SELECT_QUEST) != nullptr, "Expected StackLine dialog");
            select_action(dialog, action);
            PostMessageW(dialog, WM_COMMAND, SELECT_QUEST, reinterpret_cast<LPARAM>(GetDlgItem(dialog, SELECT_QUEST)));
            editor = wait_window(process->info.dwProcessId, L"QuestEditorRE.Main.v1");
            HWND graph = FindWindowExW(editor, nullptr, L"QuestEditorRE.Graph.v1", nullptr);
            ensure(graph != nullptr, "Quest selection graph missing");
            const float dpi_scale = GetDpiForWindow(graph) / 96.0f;
            const int x = static_cast<int>((94 + 0.65f * (position.x + 85)) * dpi_scale);
            const int y = static_cast<int>((-24 + 0.65f * (position.y + 50)) * dpi_scale);
            PostMessageW(graph, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            PostMessageW(graph, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            wait_closed(editor);
            wait_parameters(dialog, 1, 1);
            check_action(dialog, action);

        if (cancel) {
            // Reopen and cancel: the original protocol explicitly returns (0,0,0).
            PostMessageW(dialog, WM_COMMAND, SELECT_QUEST, reinterpret_cast<LPARAM>(GetDlgItem(dialog, SELECT_QUEST)));
            editor = wait_window(process->info.dwProcessId, L"QuestEditorRE.Main.v1");
            PostMessageW(editor, WM_COMMAND, 133, 0);
            wait_closed(editor);
            wait_parameters(dialog, 0, 0);
            check_action(dialog, action);

        }
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        wait_closed(dialog);
    }
};
TEST_F(Host, QOpensQuestEditor) {
    PostMessageW(host, WM_KEYDOWN, 'Q', 1 | (MapVirtualKeyW('Q', MAPVK_VK_TO_VSC) << 16));
    PostMessageW(host, WM_KEYUP, 'Q', 1 | (MapVirtualKeyW('Q', MAPVK_VK_TO_VSC) << 16) | 0xC0000000);
    HWND editor = wait_window(process->info.dwProcessId, L"QuestEditorRE.Main.v1");
    EXPECT_NE(FindWindowExW(editor, nullptr, L"QuestEditorRE.Graph.v1", nullptr), nullptr);
    PostMessageW(editor, WM_COMMAND, 133, 0);
    wait_closed(editor);
}
TEST_F(Host, SetQuestReturnsSelectedParameters) { ASSERT_NO_THROW(exercise_action(L"ACT_SET_QUEST", false)); }
TEST_F(Host, SetQuestCancelReturnsZeroParameters) { ASSERT_NO_THROW(exercise_action(L"ACT_SET_QUEST", true)); }
TEST_F(Host, WhileNotQuestReturnsSelectedParameters) { ASSERT_NO_THROW(exercise_action(L"ACT_WHILE_NOT_QUEST", false)); }
TEST_F(Host, WhileNotQuestCancelReturnsZeroParameters) { ASSERT_NO_THROW(exercise_action(L"ACT_WHILE_NOT_QUEST", true)); }
TEST_F(Host, CommandCanReopenQuestEditor) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        message(host, WM_COMMAND, QUEST_COMMAND, 0);
        HWND editor = wait_window(process->info.dwProcessId, L"QuestEditorRE.Main.v1");
        EXPECT_NE(FindWindowExW(editor, nullptr, L"QuestEditorRE.Graph.v1", nullptr), nullptr);
        PostMessageW(editor, WM_COMMAND, 133, 0);
        wait_closed(editor);
    }
}
}
