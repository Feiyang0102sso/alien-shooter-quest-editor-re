#include "control_fixture.h"

namespace editor_test {

TEST_F(Controls, CloseCanCancelOrDiscardChanges) {
    dialogs_answered = 0;
    create_test_node(window);
    dialog_answer = IDCANCEL;
    timer = SetTimer(nullptr,0,20,answer_own_dialog);
    SendMessageW(window,WM_CLOSE,0,0);
    KillTimer(nullptr,timer);
    ASSERT_TRUE((IsWindowVisible(window))) <<"cancel close preserves unsaved editor";
    dialog_answer = IDNO;
    timer = SetTimer(nullptr,0,20,answer_own_dialog);
    SendMessageW(window,WM_CLOSE,0,0);
    KillTimer(nullptr,timer);
    std::cout << "UI CLOSE window=" << IsWindow(window) << " visible=" << IsWindowVisible(window)
        << " owner=" << GetWindow(window,GW_OWNER) << " dialogs=" << dialogs_answered << "\n";
    ASSERT_TRUE((!IsWindow(window))) <<"discard closes standalone editor";
    ASSERT_TRUE((dialogs_answered == 2)) <<"unsaved prompts exercised";
    check.open(config);
    ASSERT_TRUE((check.document.sections().size() == 4)) <<"discarded new node never reaches disk";
}

TEST_F(Controls, CancelButtonConfirmsDiscard) {
    const auto before_navigation = quest::read_file(config);
    create_test_node(window);
    dialog_answer = IDCANCEL;
    timer = SetTimer(nullptr,0,20,answer_own_dialog);
    SendMessageW(window,WM_COMMAND,133,0);
    KillTimer(nullptr,timer);
    ASSERT_TRUE((IsWindowVisible(window))) <<"Cancel button can cancel its discard confirmation";
    dialog_answer = IDOK;
    timer = SetTimer(nullptr,0,20,answer_own_dialog);
    SendMessageW(window,WM_COMMAND,133,0);
    KillTimer(nullptr,timer);
    ASSERT_TRUE((!IsWindow(window) && quest::read_file(config) == before_navigation)) <<"confirmed Cancel discards without saving";
}

TEST_F(Controls, OkSavesNewTask) {
    create_test_node(window);
    SendMessageW(window,WM_COMMAND,126,0);
    check.open(config);
    ASSERT_TRUE((!IsWindow(window) && check.document.find("Quest","1_1_3"))) <<"OK saves new task and closes";
}

TEST_F(Controls, MixedMissionCannotBeLossilyReencoded) {
    destroy();
    // Exercise the full-text window to ensure replacement characters cannot trigger lossy re-encoding.
    const std::string mixed_mission = "<1_1_1>\r\n"+quest::encode(L"Привет",1251)
        +"\r\n<other>\r\n"+quest::encode(L"中文",936)+"\r\n<broken>\r\n\x81\r\n";
    write(directory/L"mission.txt",mixed_mission);
    window = create(GetModuleHandleW(nullptr),nullptr,GetCommandLineA(),0,nullptr);
    file_menu = GetSubMenu(GetMenu(window),0);
    int mission_checked = 0;
    for (UINT id = 143; id <= 145; ++id) {
        if (GetMenuState(file_menu,id,MF_BYCOMMAND) & MF_CHECKED) ++mission_checked;
    }
    ASSERT_TRUE(((GetMenuState(file_menu,142,MF_BYCOMMAND) & MF_CHECKED) != 0
        && mission_checked == 1)) <<"reopened mixed file shows exactly one current mission encoding";
    SendMessageW(window,WM_COMMAND,143,0);
    SendMessageW(window,WM_COMMAND,144,0);
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(directory/L"mission.txt") == mixed_mission)) <<
        "changing display encoding does not transcode actual mixed mission bytes";
    preserve_text = true;
    timer = SetTimer(nullptr,0,20,edit_own_text);
    SendMessageW(window,WM_COMMAND,125,0);
    KillTimer(nullptr,timer);
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(directory/L"mission.txt") == mixed_mission)) <<
        "full mission apply without edits preserves mixed bytes in actual UI";
    append_text = true;
    dialog_answer = IDOK;
    const int errors_before = dialogs_created;
    UINT_PTR error_timer = SetTimer(nullptr,0,20,answer_own_dialog);
    timer = SetTimer(nullptr,0,20,edit_own_text);
    SendMessageW(window,WM_COMMAND,125,0);
    KillTimer(nullptr,timer);
    KillTimer(nullptr,error_timer);
    ASSERT_TRUE((dialogs_created == errors_before+1)) <<"lossy full mission edit reports an error";
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(directory/L"mission.txt") == mixed_mission)) <<
        "failed full mission edit leaves disk and session bytes unchanged";
    preserve_text = false;
    append_text = false;
}

TEST_F(Controls, RawPropertiesRejectLossyEdit) {
    destroy();
    auto mixed_config = fixture;
    mixed_config.insert(mixed_config.find("NextQuests="),"Comment=\x81\r\n");
    write(config,mixed_config);
    window = create(GetModuleHandleW(nullptr),nullptr,GetCommandLineA(),0,nullptr);
    SendMessageW(window,WM_COMMAND,141,0);
    graph = FindWindowExW(window,nullptr,L"QuestEditorRE.Graph.v1",nullptr);
    property_mode = 3;
    const int raw_errors_before = dialogs_created;
    timer = SetTimer(nullptr,0,20,edit_own_properties);
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(335,67));
    KillTimer(nullptr,timer);
    ASSERT_TRUE((property_observed && dialogs_created == raw_errors_before+1)) <<
        "raw CFG lossy edit is rejected and properties remain open for correction";
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == mixed_config)) <<"rejected raw CFG edit preserves original bytes";
    destroy();
}

TEST_F(Controls, ChineseSampleDisplaysAndPreservesBytes) {
    destroy();
    // Use isolated mock samples to verify real menus and property windows without changing source assets.
    const auto chinese_directory = samples/L"AS2R full"/L"AlienShooter2 Reloaded";
    const auto chinese_config = quest::read_file(quest::game_configuration_path(chinese_directory));
    const auto chinese_mission = quest::read_file(chinese_directory/L"Text"/L"mission.txt");
    write(config,chinese_config);
    write(directory/L"mission.txt",chinese_mission);
    window = create(GetModuleHandleW(nullptr),nullptr,GetCommandLineA(),0,nullptr);
    file_menu = GetSubMenu(GetMenu(window),0);
    ASSERT_TRUE(((GetMenuState(file_menu,141,MF_BYCOMMAND) & MF_CHECKED) != 0
        && (GetMenuState(file_menu,144,MF_BYCOMMAND) & MF_CHECKED) != 0)) <<"reported sample UI checks GBK independently for CFG and mission";
    graph = FindWindowExW(window,nullptr,L"QuestEditorRE.Graph.v1",nullptr);
    check.open(config);
    const auto first_position = check.default_positions().at("1_1_1");
    const int first_x = static_cast<int>(94+0.65f*(first_position.x+85));
    const int first_y = static_cast<int>(-24+0.65f*(first_position.y+50));
    property_mode = 2;
    timer = SetTimer(nullptr,0,20,edit_own_properties);
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(first_x,first_y));
    KillTimer(nullptr,timer);
    ASSERT_TRUE((observed_comment == L"调查基地，消灭敌人并保护附近的队友，完成任务。")) <<"reported sample properties show correctly decoded Chinese";
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((quest::read_file(config) == chinese_config && quest::read_file(directory/L"mission.txt") == chinese_mission)) <<
        "auto detected sample no-op properties and save are byte exact";
    destroy();
}

TEST_F(Controls, HostPrefersCfgAndFallsBackToDb) {
    destroy();
    // Verify automatic DB discovery through the fixed DLL entry, not only Session::open.
    const auto db_path = directory/L"levels.db";
    auto db_document = quest::QuestDocument::parse(fixture);
    db_document.set("Quest","1_1_1","Comment","DB source");
    write(db_path,db_document.bytes());
    write(config,fixture);
    write(directory/L"mission.txt",original_mission);
    window = create(GetModuleHandleW(nullptr),nullptr,GetCommandLineA(),0,nullptr);
    graph = FindWindowExW(window,nullptr,L"QuestEditorRE.Graph.v1",nullptr);
    property_mode = 2;
    timer = SetTimer(nullptr,0,20,edit_own_properties);
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(335,67));
    KillTimer(nullptr,timer);
    ASSERT_TRUE((observed_comment.empty())) <<"host DLL uses CFG before neighboring DB";
    destroy();
    std::filesystem::remove(config);
    window = create(GetModuleHandleW(nullptr),nullptr,GetCommandLineA(),0,nullptr);
    graph = FindWindowExW(window,nullptr,L"QuestEditorRE.Graph.v1",nullptr);
    timer = SetTimer(nullptr,0,20,edit_own_properties);
    SendMessageW(graph,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(335,67));
    KillTimer(nullptr,timer);
    ASSERT_TRUE((observed_comment == L"DB source")) <<"host DLL automatically opens DB when CFG is absent";
    SendMessageW(window,WM_COMMAND,101,0);
    ASSERT_TRUE((!std::filesystem::exists(config) && quest::read_file(db_path) == db_document.bytes())) <<"host DB save keeps DB extension and original bytes";
    destroy();
}

}
