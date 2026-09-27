#include "control_fixture.h"
namespace editor_test {
std::string raw_fixture;
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
}

int dialog_answer = IDNO;
int dialogs_answered = 0;
int dialogs_created = 0;
HWND observed_dialog = nullptr;

// Mock only the OS message-box boundary in the isolated test DLL.
// Error reporting and the resulting editor state remain part of the assertions.
int WINAPI test_message_box(HWND, LPCWSTR, LPCWSTR, UINT type) {
    if ((type & MB_TYPEMASK) == MB_OK) {
        EXPECT_EQ(dialog_answer, IDOK) << "Unexpected error message box";
    }
    ++dialogs_created;
    ++dialogs_answered;
    std::cout << "[EXPECTED] MessageBox response=" << dialog_answer << "\n";
    return dialog_answer;
}
void mock_message_boxes(HMODULE library) {
    auto* base = reinterpret_cast<BYTE*>(library);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* image = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base
        + image->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* imported = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::string(reinterpret_cast<char*>(imported->Name)) != "MessageBoxW") continue;
            DWORD previous = 0;
            ensure(VirtualProtect(&addresses->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &previous),
                "make test message-box import writable");
            addresses->u1.Function = reinterpret_cast<ULONG_PTR>(test_message_box);
            DWORD ignored = 0;
            ensure(VirtualProtect(&addresses->u1.Function, sizeof(ULONG_PTR), previous, &ignored),
                "restore test import protection");
            return;
        }
    }
    ensure(false, "test DLL imports MessageBoxW");
}
// Record dialog creation so an answered prompt is not mistaken for a missing prompt.
LRESULT CALLBACK observe_dialog(int code, WPARAM window, LPARAM details) {
    if (code == HCBT_CREATEWND) {
        wchar_t type[64]{};
        GetClassNameW(reinterpret_cast<HWND>(window),type,64);
        if (std::wstring(type) == L"#32770") {
            ++dialogs_created;
            observed_dialog = reinterpret_cast<HWND>(window);
        }
    }
    if (code == HCBT_DESTROYWND && observed_dialog == reinterpret_cast<HWND>(window)) observed_dialog = nullptr;
    return CallNextHookEx(nullptr,code,window,details);
}
BOOL CALLBACK find_own_dialog(HWND window, LPARAM result) {
    DWORD process = 0;
    GetWindowThreadProcessId(window,&process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(window)) return TRUE;
    wchar_t name[64]{};
    GetClassNameW(window,name,64);
    if (std::wstring(name) != L"#32770") return TRUE;
    *reinterpret_cast<HWND*>(result) = window;
    return FALSE;
}
void CALLBACK answer_own_dialog(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = observed_dialog;
    if (!IsWindow(dialog)) EnumThreadWindows(GetCurrentThreadId(),find_own_dialog,reinterpret_cast<LPARAM>(&dialog));
    if (!dialog || !IsWindowVisible(dialog)) return;
    DWORD process = 0;
    GetWindowThreadProcessId(dialog,&process);
    if (process != GetCurrentProcessId()) return;
    // MessageBox dispatches timers during creation; wait until the button is ready.
    const HWND button = GetDlgItem(dialog,dialog_answer);
    if (!button || !IsWindowEnabled(button)) return;
    KillTimer(nullptr,timer);
    ++dialogs_answered;
    std::cout << "UI DIALOG answer=" << dialog_answer << "\n" << std::flush;
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(dialog_answer,BN_CLICKED),reinterpret_cast<LPARAM>(button));
}

std::wstring observed_text;
bool preserve_text = false;
bool append_text = false;
void CALLBACK edit_own_text(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = FindWindowW(L"QuestEditorRE.Text.v1",nullptr);
    DWORD process = 0;
    if (!dialog) return;
    GetWindowThreadProcessId(dialog,&process);
    if (process != GetCurrentProcessId()) return;
    KillTimer(nullptr,timer);
    wchar_t text[256]{};
    GetWindowTextW(GetDlgItem(dialog,10),text,256);
    observed_text = text;
    if (append_text) SetWindowTextW(GetDlgItem(dialog,10),(observed_text+L"edited").c_str());
    else if (!preserve_text) SetWindowTextW(GetDlgItem(dialog,10),L"Updated heading");
    SendMessageW(dialog,WM_COMMAND,IDOK,0);
}

int property_mode = 0;
std::wstring observed_property_tab;
bool property_observed = false;
std::wstring observed_comment;
void property_page(HWND window, int page) {
    HWND tabs = GetDlgItem(window,200);
    TabCtrl_SetCurSel(tabs,page);
    NMHDR change{tabs,200,TCN_SELCHANGE};
    SendMessageW(window,WM_NOTIFY,200,reinterpret_cast<LPARAM>(&change));
}
void create_test_node(HWND window) {
    HWND tabs = GetDlgItem(window,134);
    TabCtrl_SetCurSel(tabs,0);
    NMHDR change{tabs,134,TCN_SELCHANGE};
    SendMessageW(window,WM_NOTIFY,134,reinterpret_cast<LPARAM>(&change));
    HWND graph = FindWindowExW(window,nullptr,L"QuestEditorRE.Graph.v1",nullptr);
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(500,67));
}

void CALLBACK edit_own_properties(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = FindWindowW(L"QuestEditorRE.Properties.v1",nullptr);
    DWORD process = 0;
    if (!dialog) return;
    GetWindowThreadProcessId(dialog,&process);
    if (process != GetCurrentProcessId()) return;
    KillTimer(nullptr,timer);
    wchar_t tab_text[128]{};
    TCITEMW item{};
    item.mask = TCIF_TEXT; item.pszText = tab_text; item.cchTextMax = 128;
    TabCtrl_GetItem(GetDlgItem(dialog,200),0,&item);
    observed_property_tab = tab_text;
    property_observed = !IsWindowEnabled(GetWindow(dialog,GW_OWNER))
        && TabCtrl_GetItemCount(GetDlgItem(dialog,200)) == 4
        && IsWindowVisible(GetDlgItem(dialog,211)) && !IsWindowVisible(GetDlgItem(dialog,221));
    if (property_mode == 2) {
        wchar_t comment[256]{};
        GetWindowTextW(GetDlgItem(dialog,242),comment,256);
        observed_comment = comment;
        SendMessageW(dialog,WM_COMMAND,IDOK,0);
        return;
    }
    if (property_mode == 3) {
        property_page(dialog,3);
        wchar_t content[2048]{};
        GetWindowTextW(GetDlgItem(dialog,201),content,2048);
        SetWindowTextW(GetDlgItem(dialog,201),(std::wstring(content)+L"; edited\r\n").c_str());
        dialog_answer = IDOK;
        const UINT_PTR error_timer = SetTimer(nullptr,0,20,answer_own_dialog);
        SendMessageW(dialog,WM_COMMAND,IDOK,0);
        KillTimer(nullptr,error_timer);
        property_observed = IsWindow(dialog);
        SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);
        return;
    }
    property_page(dialog,3);
    auto raw = quest::decode(raw_fixture,CP_UTF8);
    raw = raw.substr(raw.find(L"Quest=1_1_1"));
    raw = raw.substr(0,raw.find(L"Quest=1_1_2"));
    raw += L"Coment2=heading ignored\r\n";
    SetWindowTextW(GetDlgItem(dialog,201),raw.c_str());
    property_page(dialog,0);
    for (int id = 250; id <= 254; ++id) SendMessageW(GetDlgItem(dialog,id),BM_SETCHECK,BST_CHECKED,0);
    SetWindowTextW(GetDlgItem(dialog,260),L"10");
    SetWindowTextW(GetDlgItem(dialog,261),L"20");
    SetWindowTextW(GetDlgItem(dialog,262),L"0");
    SetWindowTextW(GetDlgItem(dialog,263),L"150");
    SetWindowTextW(GetDlgItem(dialog,211),L"changed_key");
    SetWindowTextW(GetDlgItem(dialog,241),L"Updated heading");
    SetPropW(GetDlgItem(dialog,211),L"QERE.KeepControls",reinterpret_cast<HANDLE>(1));
    property_page(dialog,1);
    property_observed = property_observed && IsWindowVisible(GetDlgItem(dialog,221))
        && !IsWindowVisible(GetDlgItem(dialog,211)) && GetPropW(GetDlgItem(dialog,211),L"QERE.KeepControls");
    SetWindowTextW(GetDlgItem(dialog,222),L"failure_target");
    property_page(dialog,2);
    property_observed = property_observed && GetPropW(GetDlgItem(dialog,211),L"QERE.KeepControls");
    RemovePropW(GetDlgItem(dialog,211),L"QERE.KeepControls");
    SetWindowTextW(GetDlgItem(dialog,230),L"137");
    if (property_mode) SendMessageW(dialog,WM_COMMAND,IDOK,0);
    else SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);
}

std::map<DWORD,int> graph_colors(HWND graph) {
    RECT rectangle{}; GetClientRect(graph,&rectangle);
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = rectangle.right;
    bitmap.bmiHeader.biHeight = -rectangle.bottom;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HDC context = CreateCompatibleDC(nullptr);
    HBITMAP image = CreateDIBSection(context,&bitmap,DIB_RGB_COLORS,&pixels,nullptr,0);
    const HGDIOBJ previous = SelectObject(context,image);
    SendMessageW(graph,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(context),PRF_CLIENT);
    GdiFlush();
    std::map<DWORD,int> colors;
    for (int index = 0; index < rectangle.right*rectangle.bottom; ++index) {
        ++colors[static_cast<DWORD*>(pixels)[index] & 0xFFFFFF];
    }
    SelectObject(context,previous); DeleteObject(image); DeleteDC(context);
    return colors;
}


void Controls::SetUp() {
    Files::SetUp();
    raw_fixture = fixture;
    dialog_answer = IDNO;
    dialogs_answered = dialogs_created = 0;
    observed_dialog = nullptr;
    preserve_text = append_text = property_observed = false;
    property_mode = 0;
    observed_property_tab.clear();
    observed_comment.clear();
    observed_text.clear();
    directory = workspace / "ui-fixture";
    std::filesystem::create_directories(directory);
    config = directory / "levels.cfg";
    write(config, fixture);
    write(directory / "mission.txt", original_mission);
    samples = workspace / "samples";
    test_support::create_samples(samples);
    std::filesystem::current_path(directory);
    // The DLL directory owns language settings; never reuse the user's configuration.
    const auto isolated_dll = directory / "QuestEditor.dll";
    std::filesystem::copy_file(dll, isolated_dll);
    dialog_hook = SetWindowsHookExW(WH_CBT, observe_dialog, nullptr, GetCurrentThreadId());
    ASSERT_NE(dialog_hook, nullptr);
    library = LoadLibraryW(isolated_dll.c_str());
    ASSERT_NE(library, nullptr);
    mock_message_boxes(library);
    create = reinterpret_cast<Create>(GetProcAddress(library, "CreateNew_zQuestEditorMainWnd"));
    destroy = reinterpret_cast<Destroy>(GetProcAddress(library, "Delete_zQuestEditorMainWnd"));
    ASSERT_NE(create, nullptr);
    ASSERT_NE(destroy, nullptr);
    window = create(GetModuleHandleW(nullptr), nullptr, GetCommandLineA(), 0, nullptr);
    ASSERT_TRUE(IsWindow(window));
    graph = FindWindowExW(window, nullptr, L"QuestEditorRE.Graph.v1", nullptr);
    ASSERT_NE(graph, nullptr);
    file_menu = GetSubMenu(GetMenu(window), 0);
    pump();
}
void Controls::TearDown() {
    if (destroy) destroy();
    if (library) FreeLibrary(library);
    if (dialog_hook) UnhookWindowsHookEx(dialog_hook);
    pump();
    Files::TearDown();
}
void Controls::pan() {
    SendMessageW(graph,WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(350,300));
    SendMessageW(graph,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(390,320));
    SendMessageW(graph,WM_MBUTTONUP,0,MAKELPARAM(390,320));
}
}
