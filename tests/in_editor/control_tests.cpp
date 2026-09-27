#include "control_fixture.h"

namespace editor_test {

TEST_F(Controls, ShowsClassicLayoutAndMenus) {
    ASSERT_TRUE((!IsWindowVisible(GetDlgItem(window,120)) && !IsWindowVisible(GetDlgItem(window,110)))) <<
        "classic layout hides search and properties by default";
    int visible_buttons = 0;
    for (HWND control = GetWindow(window,GW_CHILD); control; control = GetWindow(control,GW_HWNDNEXT)) {
        wchar_t type[32]{}; GetClassNameW(control,type,32);
        if (std::wstring(type) == L"Button" && IsWindowVisible(control)) ++visible_buttons;
    }
    ASSERT_TRUE((visible_buttons == 2)) <<"classic main view has only OK and Cancel buttons";
    ASSERT_TRUE((IsWindowVisible(GetDlgItem(window,134)))) <<"classic map tabs visible";
    ASSERT_TRUE((GetMenuItemCount(GetMenu(window)) == 4)) <<"menu has File Edit Help Language";
    HMENU edit_menu = GetSubMenu(GetMenu(window),1);
    ASSERT_TRUE((GetMenuItemCount(edit_menu) == 5)) <<"Edit only contains Undo Redo separator Properties Delete";
    ASSERT_TRUE((!GetDlgItem(window,120) && !GetDlgItem(window,121))) <<"removed View features do not create hidden search controls";
}

TEST_F(Controls, ChangesLanguageAndRejectsLockedConfig) {
    SendMessageW(window,WM_COMMAND,150,0);
    std::cout << "I18N English menu selected\n" << std::flush;
    GetMenuStringW(GetMenu(window),3,caption,128,MF_BYPOSITION);
    ASSERT_TRUE((std::wstring(caption) == L"Language")) <<"language menu appears to the right of Help and refreshes immediately";
    GetWindowTextW(GetDlgItem(window,133),caption,128);
    ASSERT_TRUE((std::wstring(caption) == L"Cancel")) <<"main buttons refresh to English";
    ASSERT_TRUE(((GetMenuState(GetSubMenu(GetMenu(window),3),150,MF_BYCOMMAND) & MF_CHECKED) != 0)) <<
        "selected language checked";
    ASSERT_TRUE((quest::read_file(directory/L"QuestEditor.cfg") == "language = 1\r\n")) <<"UI language choice persisted beside DLL";
    std::cout << "I18N recreating window\n" << std::flush;
    destroy();
    std::cout << "I18N previous window destroyed\n" << std::flush;
    window = create(GetModuleHandleW(nullptr),nullptr,GetCommandLineA(),0,nullptr);
    std::cout << "I18N window recreated\n" << std::flush;
    graph = FindWindowExW(window,nullptr,L"QuestEditorRE.Graph.v1",nullptr);
    pump();
    GetMenuStringW(GetMenu(window),0,caption,128,MF_BYPOSITION);
    ASSERT_TRUE((std::wstring(caption) == L"File")) <<"recreated editor restores saved English choice";
    property_mode = 2;
    UINT_PTR language_timer = SetTimer(nullptr,0,20,edit_own_properties);
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(375,87));
    KillTimer(nullptr,language_timer);
    ASSERT_TRUE((observed_property_tab == L"Begin")) <<"properties use active English language";
    std::cout << "I18N English properties checked\n" << std::flush;
    SendMessageW(window,WM_COMMAND,151,0);
    GetMenuStringW(GetMenu(window),3,caption,128,MF_BYPOSITION);
    ASSERT_TRUE((std::wstring(caption) == L"语言")) <<"main menu switches back to Chinese";
    language_timer = SetTimer(nullptr,0,20,edit_own_properties);
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(375,87));
    KillTimer(nullptr,language_timer);
    ASSERT_TRUE((observed_property_tab == L"开始")) <<"properties use active Chinese language";
    std::cout << "I18N Chinese properties checked\n" << std::flush;
    property_mode = 0;
    HANDLE language_lock = CreateFileW((directory/L"QuestEditor.cfg").c_str(),GENERIC_READ,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,0,nullptr);
    ASSERT_TRUE((language_lock != INVALID_HANDLE_VALUE)) <<"UI config lock created";
    dialog_answer = IDOK;
    const int language_errors_before = dialogs_created;
    language_timer = SetTimer(nullptr,0,20,answer_own_dialog);
    SendMessageW(window,WM_COMMAND,150,0);
    KillTimer(nullptr,language_timer);
    CloseHandle(language_lock);
    std::cout << "I18N locked config handled\n" << std::flush;
    GetMenuStringW(GetMenu(window),3,caption,128,MF_BYPOSITION);
    ASSERT_TRUE((std::wstring(caption) == L"语言")) <<"failed config save keeps UI in previous language";
    ASSERT_TRUE((dialogs_created == language_errors_before+1)) <<"failed language save displays error";
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == fixture && quest::read_file(directory/L"mission.txt") == original_mission)) <<
        "language switches preserve game files byte for byte";
}

TEST_F(Controls, SelectsDisplayEncodingsWithoutTranscoding) {
    file_menu = GetSubMenu(GetMenu(window),0);
    ASSERT_TRUE(((GetMenuState(file_menu,142,MF_BYCOMMAND) & MF_CHECKED) != 0)) <<
        "CFG third ANSI item is checked for ASCII input";
    ASSERT_TRUE(((GetMenuState(file_menu,145,MF_BYCOMMAND) & MF_CHECKED) != 0)) <<
        "mission third ANSI item is checked for ASCII input";
    wchar_t encoding_caption[128]{};
    GetMenuStringW(file_menu,142,encoding_caption,128,MF_BYCOMMAND);
    ASSERT_TRUE((std::wstring(encoding_caption).starts_with(L"ANSI"))) <<"third encoding is ANSI rather than UTF8";
    SendMessageW(window,WM_COMMAND,141,0);
    ASSERT_TRUE(((GetMenuState(file_menu,141,MF_BYCOMMAND) & MF_CHECKED) != 0
        && (GetMenuState(file_menu,142,MF_BYCOMMAND) & MF_CHECKED) == 0)) <<"CFG encoding check follows selection exclusively";
    SendMessageW(window,WM_COMMAND,143,0);
    ASSERT_TRUE(((GetMenuState(file_menu,143,MF_BYCOMMAND) & MF_CHECKED) != 0
        && (GetMenuState(file_menu,144,MF_BYCOMMAND) & MF_CHECKED) == 0
        && (GetMenuState(file_menu,141,MF_BYCOMMAND) & MF_CHECKED) != 0)) <<"mission selection stays independent from CFG";
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == fixture && quest::read_file(directory/L"mission.txt") == original_mission)) <<
        "switching display encodings and saving preserves both files";
    SendMessageW(window,WM_COMMAND,140,0);
    SendMessageW(window,WM_COMMAND,144,0);
    SendMessageW(window,WM_COMMAND,142,0);
    SendMessageW(window,WM_COMMAND,145,0);
    ASSERT_TRUE(((GetMenuState(file_menu,142,MF_BYCOMMAND) & MF_CHECKED) != 0
        && (GetMenuState(file_menu,145,MF_BYCOMMAND) & MF_CHECKED) != 0
        && (GetMenuState(file_menu,141,MF_BYCOMMAND) & MF_CHECKED) == 0
        && (GetMenuState(file_menu,144,MF_BYCOMMAND) & MF_CHECKED) == 0)) <<
        "ANSI selection stays distinct even when system codepage equals GBK";
    SendMessageW(window,WM_COMMAND,140,0);
    SendMessageW(window,WM_COMMAND,144,0);
}

TEST_F(Controls, PansWithoutEditingData) {
    pan();
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == fixture && !std::filesystem::exists(config.wstring()+L".qere-layout"))) <<"pan does not dirty or save data";
    SendMessageW(graph,WM_RBUTTONUP,0,MAKELPARAM(630,87));
    SendMessageW(graph,WM_RBUTTONUP,0,MAKELPARAM(173,87));
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == fixture)) <<"right click cell or initial boundary is inert without menu";
}

TEST_F(Controls, TogglesBoundaryAndRestoresWithUndo) {
    pan();
    SendMessageW(graph,WM_RBUTTONUP,0,MAKELPARAM(710,87));
    SendMessageW(window,WM_COMMAND,150,0);
    SendMessageW(window,WM_COMMAND,151,0);
    GetWindowTextW(window,caption,128);
    ASSERT_TRUE((std::wstring(caption).ends_with(L" *"))) <<"language switches preserve unsaved changes";
    SendMessageW(window,WM_COMMAND,101,0);
    auto boundary_document = quest::QuestDocument::parse(quest::read_file(config));
    ASSERT_TRUE((boundary_document.find("Map","Level005")->number("TimeLine") == 4
        && TabCtrl_GetItemCount(GetDlgItem(window,134)) == 2)) <<"right click gap creates boundary and level tab after pan";
    SendMessageW(graph,WM_RBUTTONUP,0,MAKELPARAM(710,87));
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((TabCtrl_GetItemCount(GetDlgItem(window,134)) == 1)) <<"right click same gap cancels boundary";
    SendMessageW(window,WM_COMMAND,102,0);
    ASSERT_TRUE((TabCtrl_GetItemCount(GetDlgItem(window,134)) == 2)) <<"undo restores boundary and level tab";
    SendMessageW(window,WM_COMMAND,102,0);
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == fixture)) <<"boundary edits undo without changing quests";
}

TEST_F(Controls, DeletesOnlySelectedConnection) {
    pan();
    // Select the black connection directly and press Delete without creating a temporary node.
    SendMessageW(graph,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(442,87));
    SendMessageW(graph,WM_LBUTTONUP,0,MAKELPARAM(442,87));
    SendMessageW(graph,WM_KEYDOWN,VK_DELETE,0);
    SendMessageW(window,WM_COMMAND,101,0);
    auto line_check = quest::QuestDocument::parse(quest::read_file(config));
    ASSERT_TRUE((line_check.find("Quest","1_1_1")->value("NextQuests").empty())) <<"click line and Delete disconnects without deleting a task";
    ASSERT_TRUE((line_check.sections().size() == 4)) <<"disconnect keeps all nodes";
    SendMessageW(window,WM_COMMAND,102,0);
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == fixture)) <<"line deletion undo is byte exact";
}

TEST_F(Controls, BodyDragDoesNotMoveNodes) {
    pan();
    // Dragging the node body creates links; releasing inside the source must not move it.
    SendMessageW(graph,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(375,87));
    SendMessageW(graph,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(415,117));
    SendMessageW(graph,WM_LBUTTONUP,0,MAKELPARAM(415,117));
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == fixture)) <<"UI node drag preserves TimeLine and CFG";
    ASSERT_TRUE((!std::filesystem::exists(config.wstring()+L".qere-layout"))) <<"body drag does not create layout sidecar";
    check.open(config);
    ASSERT_TRUE((check.layout.empty())) <<"body drag keeps fixed grid positions";
    SendMessageW(window,WM_COMMAND,102,0);
    SendMessageW(window,WM_COMMAND,101,0);
    check.open(config);
    ASSERT_TRUE((check.layout.empty())) <<"UI undo restores layout";
}

TEST_F(Controls, CreatesAtGridOriginAndIgnoresGaps) {
    pan();
    // Gaps do not create nodes; the right side of a gray cell still belongs to its column.
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(710,87));
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == fixture)) <<"double click in grid gap does not create";
    // Double-clicking inside a gray cell must place the card at the cell origin.
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(630,87));
    SendMessageW(window,WM_COMMAND,101,0);
    check.open(config);
    ASSERT_TRUE((check.position(*check.document.find("Quest","1_1_3")) == quest::Position{698,90})) <<
        "double click creates at grey cell origin, not cursor";
    ASSERT_TRUE((check.document.connection_target(*check.document.find("Quest","1_1_2"),false) == 1003)) <<"double click automatically connects previous task";
    SendMessageW(window,WM_COMMAND,102,0);
    SendMessageW(window,WM_COMMAND,101,0);
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(682,87));
    SendMessageW(window,WM_COMMAND,101,0);
    check.open(config);
    ASSERT_TRUE((check.document.find("Quest","1_1_3")->number("TimeLine") == 3
        && check.position(*check.document.find("Quest","1_1_3")) == quest::Position{698,90})) <<
        "right side of grey cell remains in same timeline";
    SendMessageW(window,WM_COMMAND,102,0);
    SendMessageW(window,WM_COMMAND,101,0);
}

}
