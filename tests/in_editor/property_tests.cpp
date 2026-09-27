#include "control_fixture.h"

namespace editor_test {

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
