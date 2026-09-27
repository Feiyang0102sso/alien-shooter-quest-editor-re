#include "control_fixture.h"

namespace editor_test {
namespace {
int bonus_step = 0;
bool bonus_visited = false;
int keyboard_step = 0;

// Keep a real rendered dialog alongside the test report for layout inspection.
void capture_bonus(HWND dialog) {
    RECT bounds{};
    GetWindowRect(dialog, &bounds);
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = bounds.right - bounds.left;
    bitmap.bmiHeader.biHeight = -(bounds.bottom - bounds.top);
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    const DWORD size = bitmap.bmiHeader.biWidth * (-bitmap.bmiHeader.biHeight) * 4;
    void* pixels = nullptr;
    HDC context = CreateCompatibleDC(nullptr);
    HBITMAP image = CreateDIBSection(context, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
    const auto previous = SelectObject(context, image);
    EXPECT_TRUE(PrintWindow(dialog, context, 0));
    GdiFlush();
    BITMAPFILEHEADER header{};
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + size;
    std::ofstream output(test_support::output / "bonus-items.bmp", std::ios::binary);
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(&bitmap.bmiHeader), sizeof(BITMAPINFOHEADER));
    output.write(static_cast<const char*>(pixels), size);
    SelectObject(context, previous);
    DeleteObject(image);
    DeleteDC(context);
}

std::wstring text_of(HWND control) {
    std::wstring text(GetWindowTextLengthW(control) + 1, L'\0');
    GetWindowTextW(control, text.data(), static_cast<int>(text.size()));
    text.resize(wcslen(text.c_str()));
    return text;
}

void bonus_page(HWND dialog, int page) {
    HWND tabs = GetDlgItem(dialog, 200);
    TabCtrl_SetCurSel(tabs, page);
    NMHDR change{tabs, 200, TCN_SELCHANGE};
    SendMessageW(dialog, WM_NOTIFY, 200, reinterpret_cast<LPARAM>(&change));
}

// Click a native dropdown row, including the combo's own selection/close notifications.
void select_bonus_item(HWND combo, const wchar_t* item_id) {
    const auto index = SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
        reinterpret_cast<LPARAM>(item_id));
    ASSERT_NE(index, CB_ERR);
    SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
    COMBOBOXINFO info{sizeof(info)};
    ASSERT_TRUE(GetComboBoxInfo(combo, &info));
    RECT row{};
    ASSERT_NE(SendMessageW(info.hwndList, LB_GETITEMRECT, index, reinterpret_cast<LPARAM>(&row)), LB_ERR);
    const auto point = MAKELPARAM(row.left + 5, (row.top + row.bottom) / 2);
    SendMessageW(info.hwndList, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(info.hwndList, WM_LBUTTONUP, 0, point);
    pump();
}

// Post keys through the modal message loop: Enter adds an item, not an accidental OK.
void CALLBACK bonus_keyboard(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = FindWindowW(L"QuestEditorRE.Properties.v1", nullptr);
    if (!dialog) return;
    HWND combo = GetDlgItem(dialog, 234);
    HWND give = GetDlgItem(dialog, 232);
    COMBOBOXINFO info{sizeof(info)};
    GetComboBoxInfo(combo, &info);
    if (keyboard_step == 0) {
        bonus_page(dialog, 2);
        SetFocus(info.hwndItem);
        SetWindowTextW(combo, L"HEALTH");
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(234, CBN_EDITCHANGE), reinterpret_cast<LPARAM>(combo));
        PostMessageW(info.hwndItem, WM_KEYDOWN, VK_ESCAPE, 0);
    } else if (keyboard_step == 1) {
        EXPECT_FALSE(SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0));
        PostMessageW(info.hwndItem, WM_KEYDOWN, VK_RETURN, 0);
    } else if (keyboard_step == 2) {
        EXPECT_EQ(text_of(give), L"Equip_health");
        SetFocus(give);
        SendMessageW(give, EM_SETSEL, static_cast<WPARAM>(-1), -1);
        PostMessageW(give, WM_KEYDOWN, VK_RETURN, 0);
    } else {
        EXPECT_EQ(text_of(give), L"Equip_health\r\n");
        KillTimer(nullptr, timer);
        bonus_visited = true;
        SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    }
    ++keyboard_step;
}

// Exercise the real dropdown, clipboard and modal commit path in the isolated desktop.
void CALLBACK edit_bonus(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = FindWindowW(L"QuestEditorRE.Properties.v1", nullptr);
    if (!dialog) return;
    KillTimer(nullptr, timer);
    bonus_visited = true;
    bonus_page(dialog, 2);
    HWND give = GetDlgItem(dialog, 232);
    HWND remove = GetDlgItem(dialog, 233);
    HWND filter = GetDlgItem(dialog, 234);
    EXPECT_NE(filter, nullptr);
    EXPECT_EQ(GetDlgItem(dialog, 236), nullptr);
    EXPECT_EQ(GetDlgItem(dialog, 237), nullptr);
    EXPECT_TRUE(GetWindowLongW(give, GWL_STYLE) & ES_MULTILINE);
    RECT left{}, right{};
    GetWindowRect(give, &left);
    GetWindowRect(remove, &right);
    EXPECT_LT(left.left, right.left);
    EXPECT_EQ(left.top, right.top);
    if (!filter) {
        SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        return;
    }
    if (bonus_step == 1) {
        EXPECT_EQ(text_of(give), L"Ammo_test\r\nEquip_health\r\nEquip_health\r\nObj_key");
        EXPECT_EQ(text_of(remove), L"Armor_test\r\nWeapon_test");
        capture_bonus(dialog);
    } else if (bonus_step == 2) {
        EXPECT_FALSE(IsWindowEnabled(filter));
        SetWindowTextW(give, L"custom_mod_item\r\ncustom_mod_item");
    } else {
        EXPECT_EQ(SendMessageW(filter, CB_GETCOUNT, 0, 0), 6);
        SetWindowTextW(filter, L"no_such_item");
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(234, CBN_EDITCHANGE), reinterpret_cast<LPARAM>(filter));
        EXPECT_EQ(SendMessageW(filter, CB_GETCOUNT, 0, 0), 0);
        SendMessageW(filter, CB_SHOWDROPDOWN, FALSE, 0);
        pump();
        EXPECT_TRUE(text_of(give).empty());
        SetWindowTextW(filter, L"HEALTH");
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(234, CBN_EDITCHANGE), reinterpret_cast<LPARAM>(filter));
        EXPECT_EQ(SendMessageW(filter, CB_GETCOUNT, 0, 0), 1);
        EXPECT_EQ(text_of(filter), L"HEALTH");
        select_bonus_item(filter, L"Equip_health");
        EXPECT_EQ(text_of(give), L"Equip_health");
        EXPECT_TRUE(text_of(filter).empty());
        select_bonus_item(filter, L"Equip_health");
        EXPECT_EQ(text_of(give), L"Equip_health\r\nEquip_health");
        HWND remove_filter = GetDlgItem(dialog, 235);
        select_bonus_item(remove_filter, L"Armor_test");
        EXPECT_EQ(text_of(remove), L"Armor_test");
        // The edit accepts native copy/paste; mixed delimiters become separate rows immediately.
        const wchar_t pasted[] = L"Ammo_test ^Equip_health\r\nEquip_health\tObj_key";
        EXPECT_TRUE(OpenClipboard(dialog));
        EmptyClipboard();
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(pasted));
        void* destination = GlobalLock(memory);
        memcpy(destination, pasted, sizeof(pasted));
        GlobalUnlock(memory);
        EXPECT_NE(SetClipboardData(CF_UNICODETEXT, memory), nullptr);
        CloseClipboard();
        SetFocus(give);
        SendMessageW(give, EM_SETSEL, 0, -1);
        SendMessageW(give, WM_PASTE, 0, 0);
        EXPECT_EQ(text_of(give), L"Ammo_test\r\nEquip_health\r\nEquip_health\r\nObj_key");
        SetWindowTextW(remove, L"Armor_test ^ Weapon_test");
        bonus_page(dialog, 3);
        EXPECT_NE(text_of(GetDlgItem(dialog, 201)).find(L"AddItemMAIN=Ammo_test Equip_health Equip_health Obj_key"), std::wstring::npos);
        bonus_page(dialog, 2);
        EXPECT_EQ(text_of(remove), L"Armor_test\r\nWeapon_test");
    }
    if (bonus_step == 3) SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    else SendMessageW(dialog, WM_COMMAND, IDOK, 0);
}
}

TEST_F(Controls, BonusItemsFilterPasteSaveReloadAndUndo) {
    write(directory / "Weapon.cfg",
        "; Equip=Ignore_comment\r\nAmmo=Ammo_test\r\nCost=1\r\n"
        "Weapon=Weapon_test\r\nArmor=Armor_test\r\nImplant=Implant_test\r\n"
        " Equip = Equip_health ; comment\r\nObjects=Obj_key\r\nEquip=Equip_health\r\n");
    pan();
    for (bonus_step = 0; bonus_step < 2; ++bonus_step) {
        bonus_visited = false;
        const auto bonus_timer = SetTimer(nullptr, 0, 20, edit_bonus);
        SendMessageW(graph, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(375, 87));
        KillTimer(nullptr, bonus_timer);
        ASSERT_TRUE(bonus_visited);
        SendMessageW(window, WM_COMMAND, 101, 0);
        check.open(config);
        const auto section = check.document.find("Quest", "1_1_1");
        ASSERT_EQ(section->value("AddItemMAIN"), "Ammo_test Equip_health Equip_health Obj_key");
        ASSERT_EQ(section->value("RemItem"), "Armor_test Weapon_test");
    }
    SendMessageW(window, WM_COMMAND, 102, 0);
    SendMessageW(window, WM_COMMAND, 101, 0);
    EXPECT_EQ(quest::read_file(config), fixture);
}

TEST_F(Controls, BonusItemsRemainEditableWithoutWeaponFile) {
    pan();
    bonus_step = 2;
    bonus_visited = false;
    const auto bonus_timer = SetTimer(nullptr, 0, 20, edit_bonus);
    SendMessageW(graph, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(375, 87));
    KillTimer(nullptr, bonus_timer);
    ASSERT_TRUE(bonus_visited);
    SendMessageW(window, WM_COMMAND, 101, 0);
    check.open(config);
    EXPECT_EQ(check.document.find("Quest", "1_1_1")->value("AddItemMAIN"), "custom_mod_item custom_mod_item");
}

TEST_F(Controls, BonusItemsCancelDiscardsBothListsAndRawChanges) {
    write(directory / "Weapon.cfg", "Ammo=Ammo_test\nWeapon=Weapon_test\nArmor=Armor_test\n"
        "Implant=Implant_test\nEquip=Equip_health\nObjects=Obj_key\n");
    pan();
    bonus_step = 3;
    bonus_visited = false;
    const auto bonus_timer = SetTimer(nullptr, 0, 20, edit_bonus);
    SendMessageW(graph, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(375, 87));
    KillTimer(nullptr, bonus_timer);
    ASSERT_TRUE(bonus_visited);
    SendMessageW(window, WM_COMMAND, 101, 0);
    EXPECT_EQ(quest::read_file(config), fixture);
}

TEST_F(Controls, BonusItemsKeyboardAddsAndKeepsMultilineEnter) {
    write(directory / "Weapon.cfg", "Equip=Equip_health\nAmmo=Ammo_test\n");
    pan();
    keyboard_step = 0;
    bonus_visited = false;
    const auto bonus_timer = SetTimer(nullptr, 0, 30, bonus_keyboard);
    SendMessageW(graph, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(375, 87));
    KillTimer(nullptr, bonus_timer);
    EXPECT_TRUE(bonus_visited);
    SendMessageW(window, WM_COMMAND, 101, 0);
    EXPECT_EQ(quest::read_file(config), fixture);
}

TEST_F(Controls, AcceptsUnchangedPropertiesWithoutEdits) {
    pan();
    RECT graph_before{}; GetWindowRect(graph,&graph_before);
    property_mode = 2;
    UINT_PTR noop_timer = SetTimer(nullptr,0,20,edit_own_properties);
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(375,87));
    KillTimer(nullptr,noop_timer);
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == fixture)) <<"opening and accepting unchanged properties is byte exact";
}

TEST_F(Controls, CancelsAllPropertyPagesWithoutWriting) {
    pan();
    RECT graph_before{}; GetWindowRect(graph,&graph_before);
    property_mode = 0;
    property_observed = false;
    UINT_PTR property_timer = SetTimer(nullptr,0,20,edit_own_properties);
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(375,87));
    KillTimer(nullptr,property_timer);
    ASSERT_TRUE((property_observed)) <<"double click opens owned modal Begin/Fail/BonusMain properties";
    RECT graph_after{}; GetWindowRect(graph,&graph_after);
    ASSERT_TRUE((EqualRect(&graph_before,&graph_after))) <<"properties window does not shrink canvas into a sidebar";
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == fixture && quest::read_file(directory/L"mission.txt") == original_mission)) <<
        "properties Cancel discards all pages without writing";
}

TEST_F(Controls, SavesAllPropertyPagesAsOneUndo) {
    pan();
    UINT_PTR property_timer = 0;
    property_mode = 1;
    property_timer = SetTimer(nullptr,0,20,edit_own_properties);
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(375,87));
    KillTimer(nullptr,property_timer);
    ASSERT_TRUE((quest::read_file(config) == fixture)) <<"properties OK only commits to session";
    SendMessageW(window,WM_COMMAND,101,0);
    check.open(config);
    const auto edited = check.document.find("Quest","1_1_1");
    ASSERT_TRUE((edited->number("AddMoneyMAIN") == 137)) <<"BonusMain edits money";
    auto colors = graph_colors(graph);
    for (const DWORD color : {0xFF0000u,0x0000FFu,0x00FF00u,0x00FFFFu,0x008000u}) {
        ASSERT_TRUE((colors[color] >= 4)) <<"checked flag draws its original colored marker on canvas";
    }
    ASSERT_TRUE((edited->value("SetFlagmanCoord") == "QS_BEGUN 10 20 0 150")) <<"four coordinate controls save one Begin trigger";
    ASSERT_TRUE((check.document.bytes().find("QS_BEGUN CSI_TAKE changed_key") != std::string::npos)) <<
        "Begin condition edited";
    ASSERT_TRUE((check.document.bytes().find("QS_FAILED CSI_DEATH failure_target") != std::string::npos)) <<
        "Fail condition added independently";
    ASSERT_TRUE((check.document.bytes().find("QS_BEGUN CSI_DEATH monster") != std::string::npos)) <<
        "editing conditions preserves other slots";
    ASSERT_TRUE((quest::mission_text(check.mission,"heading") == "Updated heading\r\n")) <<"properties edit heading text key";
    ASSERT_TRUE((edited->value("Coment2") == "heading ignored")) <<"heading edit preserves reference tail";
    ASSERT_TRUE((check.document.bytes().find("UnknownExtension = hello  ; original comment") != std::string::npos)) <<
        "properties preserve unknown bytes";
    SendMessageW(window,WM_COMMAND,102,0);
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == fixture && quest::read_file(directory/L"mission.txt") == original_mission)) <<
        "all property pages and text undo in one step";
    colors = graph_colors(graph);
    for (const DWORD color : {0xFF0000u,0x0000FFu,0x00FF00u,0x00FFFFu,0x008000u}) {
        ASSERT_TRUE((colors[color] == 0)) <<"undo clears task flag markers";
    }
}

TEST_F(Controls, SelectsOverlappingConnectionsIndependently) {
    pan();
    // Dragging left from the node body creates a reverse link without a port or Shift.
    SendMessageW(graph,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(509,87));
    SendMessageW(graph,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(375,87));
    SendMessageW(graph,WM_LBUTTONUP,0,MAKELPARAM(375,87));
    SendMessageW(window,WM_COMMAND,101,0);
    check.open(config);
    ASSERT_TRUE((check.document.connection_target(*check.document.find("Quest","1_1_2"),true) == 1001)) <<
        "body drag left automatically creates branch connection";
    ASSERT_TRUE((check.layout.empty())) <<"body connection never moves either endpoint";
    const auto both_lines = quest::read_file(config);
    SendMessageW(graph,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(442,87));
    SendMessageW(graph,WM_LBUTTONUP,0,MAKELPARAM(442,87));
    SendMessageW(graph,WM_KEYDOWN,VK_DELETE,0);
    SendMessageW(window,WM_COMMAND,101,0);
    check.open(config);
    ASSERT_TRUE((check.document.find("Quest","1_1_2")->value("LineQuests").empty()
        && check.document.connection_target(*check.document.find("Quest","1_1_1"),false) == 1002)) <<
        "disconnect top red line preserves overlapping black line";
    SendMessageW(window,WM_COMMAND,102,0);
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == both_lines)) <<"undo restores both connection types";
    for (int click = 0; click < 2; ++click) {
        SendMessageW(graph,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(442,87));
        SendMessageW(graph,WM_LBUTTONUP,0,MAKELPARAM(442,87));
    }
    SendMessageW(window,WM_COMMAND,136,0);
    SendMessageW(window,WM_COMMAND,101,0);
    check.open(config);
    ASSERT_TRUE((check.document.find("Quest","1_1_1")->value("NextQuests").empty()
        && check.document.connection_target(*check.document.find("Quest","1_1_2"),true) == 1001)) <<
        "repeated click cycles overlapping lines and disconnects only selected black line";
    ASSERT_TRUE((check.document.sections().size() == 4)) <<"disconnecting either line never deletes nodes";
    SendMessageW(window,WM_COMMAND,102,0);
    SendMessageW(window,WM_COMMAND,101,0);
    SendMessageW(window,WM_COMMAND,102,0);
    SendMessageW(window,WM_COMMAND,101,0);
}

TEST_F(Controls, CreatesLinkedNodeAsOneUndo) {
    pan();
    // Dragging a real port to an empty cell creates a node and main link as one undo step.
    SendMessageW(graph,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(425,87));
    SendMessageW(graph,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(600,87));
    SendMessageW(graph,WM_LBUTTONUP,0,MAKELPARAM(600,87));
    SendMessageW(window,WM_COMMAND,101,0);
    check.open(config);
    ASSERT_TRUE((check.document.find("Quest","1_1_3").has_value())) <<"UI link to blank creates task";
    ASSERT_TRUE((check.position(*check.document.find("Quest","1_1_3")) == quest::Position{698,90})) <<
        "link to grey cell also snaps to cell origin";
    ASSERT_TRUE((check.document.connection_target(*check.document.find("Quest","1_1_1"),false) == 1003)) <<"UI link gesture updates source";
    SendMessageW(window,WM_COMMAND,102,0);
    SendMessageW(window,WM_COMMAND,101,0);
    check.open(config);
    ASSERT_TRUE((!check.document.find("Quest","1_1_3") && check.document.connection_target(*check.document.find("Quest","1_1_1"),false) == 1002)) <<
        "UI compound undo restores node and link";
}

TEST_F(Controls, DeletesTaskButKeepsRowHeader) {
    pan();
    SendMessageW(graph,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(375,87));
    SendMessageW(graph,WM_LBUTTONUP,0,MAKELPARAM(375,87));
    dialog_answer = IDYES;
    UINT_PTR delete_timer = SetTimer(nullptr,0,20,answer_own_dialog);
    SendMessageW(window,WM_COMMAND,112,0);
    KillTimer(nullptr,delete_timer);
    SendMessageW(window,WM_COMMAND,101,0);
    check.open(config);
    ASSERT_TRUE((!check.document.find("Quest","1_1_1") && check.document.find("Quest","1_1_0"))) <<"UI delete keeps row header";
    SendMessageW(window,WM_COMMAND,102,0);
    SendMessageW(window,WM_COMMAND,101,0);
    check.open(config);
    ASSERT_TRUE((check.document.find("Quest","1_1_1").has_value())) <<"UI delete undo restores task";
}

TEST_F(Controls, NavigationDoesNotChangeConfiguration) {
    pan();
    const auto before_navigation = quest::read_file(config);
    HWND map_tabs = GetDlgItem(window,134);
    TabCtrl_SetCurSel(map_tabs,0);
    NMHDR map_change{map_tabs,134,TCN_SELCHANGE};
    SendMessageW(window,WM_NOTIFY,134,reinterpret_cast<LPARAM>(&map_change));
    SendMessageW(graph,WM_HSCROLL,SB_LINERIGHT,0);
    SendMessageW(graph,WM_VSCROLL,SB_LINEDOWN,0);
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == before_navigation)) <<"map tabs and native scrollbars do not change CFG";
}

}
