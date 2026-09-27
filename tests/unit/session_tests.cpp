#include "../common/test_environment.h"

namespace {
using Session = test_support::Files;

TEST_F(Session, OpensAsciiWithoutEdits) {
    const std::string mission = "<1_1_1>\r\n<Font=17>Hello\r\nworld\r\n[heading]\r\nTitle\r\n";
    const auto temporary = workspace / L"test-output";
    std::filesystem::create_directories(temporary);
    const auto config = temporary / L"levels.cfg";
    write(config, fixture);
    write(temporary / L"mission.txt", mission);
    quest::EditSession session;
    session.open(config);
    ASSERT_TRUE((!session.dirty())) << "opened session clean";
    ASSERT_TRUE((session.codepage == quest::default_game_codepage() && session.mission_codepage == quest::default_game_codepage()
        && session.cfg_ansi && session.mission_ansi)) <<"ASCII game files default to system ANSI in both independent selectors";
}

TEST_F(Session, RejectsUnsupportedBomWithoutLosingSession) {
    const std::string mission = "<1_1_1>\r\n<Font=17>Hello\r\nworld\r\n[heading]\r\nTitle\r\n";
    const auto temporary = workspace / L"test-output";
    std::filesystem::create_directories(temporary);
    const auto config = temporary / L"levels.cfg";
    write(config, fixture);
    write(temporary / L"mission.txt", mission);
    quest::EditSession session;
    session.open(config);
    const auto unicode_config = temporary/L"unicode.cfg";
    write(unicode_config,"\xEF\xBB\xBF"+fixture);
    EXPECT_THROW(([&] { session.open(unicode_config); })(), std::exception) <<"game file with UTF8 BOM is rejected rather than automatically selected";
    ASSERT_TRUE((session.path == config && session.document.bytes() == fixture)) <<"unsupported encoding preserves existing session";
    write(temporary/L"mission.txt","\xEF\xBB\xBF<text>\r\nhello\r\n");
    EXPECT_THROW(([&] { session.open(config); })(), std::exception) <<"mission with UTF8 BOM is rejected without conversion";
    ASSERT_TRUE((session.mission == mission)) <<"unsupported mission preserves existing session text";
    write(temporary/L"mission.txt",mission);
}

TEST_F(Session, NoOpSaveLeavesTimestampUntouched) {
    const std::string mission = "<1_1_1>\r\n<Font=17>Hello\r\nworld\r\n[heading]\r\nTitle\r\n";
    const auto temporary = workspace / L"test-output";
    std::filesystem::create_directories(temporary);
    const auto config = temporary / L"levels.cfg";
    write(config, fixture);
    write(temporary / L"mission.txt", mission);
    quest::EditSession session;
    session.open(config);
    const auto original_time = std::filesystem::last_write_time(config);
    session.save();
    ASSERT_TRUE((std::filesystem::last_write_time(config) == original_time)) << "no-op save untouched";
}

TEST_F(Session, SavesLayoutWithoutChangingGameData) {
    const std::string mission = "<1_1_1>\r\n<Font=17>Hello\r\nworld\r\n[heading]\r\nTitle\r\n";
    const auto temporary = workspace / L"test-output";
    std::filesystem::create_directories(temporary);
    const auto config = temporary / L"levels.cfg";
    write(config, fixture);
    write(temporary / L"mission.txt", mission);
    quest::EditSession session;
    session.open(config);
    session.checkpoint();
    session.layout["1_1_1"] = {333, 444};
    session.save();
    ASSERT_TRUE((quest::read_file(config) == fixture)) << "layout never changes game data";
    quest::EditSession reopen;
    reopen.open(config);
    ASSERT_TRUE((reopen.position(*reopen.document.find("Quest", "1_1_1")) == quest::Position{333, 444})) << "layout reload";
}

TEST_F(Session, CreatesConnectsDeletesAndRestoresHistory) {
    const std::string mission = "<1_1_1>\r\n<Font=17>Hello\r\nworld\r\n[heading]\r\nTitle\r\n";
    const auto temporary = workspace / L"test-output";
    std::filesystem::create_directories(temporary);
    const auto config = temporary / L"levels.cfg";
    write(config, fixture);
    write(temporary / L"mission.txt", mission);
    quest::EditSession session;
    session.open(config);
    const auto before = session.snapshot();
    const auto name = session.create(1, 3);
    ASSERT_TRUE((name == "1_1_3")) << "new stable task ID";
    session.connect("1_1_2", name, false);
    ASSERT_TRUE((session.document.find("Quest", "1_1_2")->number("NextQuests") == 1003)) << "connect packed ID";
    session.undo(); session.undo();
    ASSERT_TRUE((session.document.bytes() == before.configuration)) << "create and connect undo";
    session.redo(); session.redo();
    ASSERT_TRUE((session.document.find("Quest", name).has_value())) << "redo restores task";
    session.erase(name);
    ASSERT_TRUE((!session.document.find("Quest", name))) << "delete task";
    ASSERT_TRUE((!session.document.validate().empty())) << "dangling references visible";
    session.undo();
    session.disconnect("1_1_2", false);
    ASSERT_TRUE((session.document.find("Quest", "1_1_2")->value("NextQuests").empty())) << "disconnect";
    EXPECT_THROW(([&] { session.erase("1_1_0"); })(), std::exception) << "protect row header";
}

TEST_F(Session, TogglesBoundariesWithoutLosingMetadata) {
    quest::EditSession boundaries;
    boundaries.path = workspace/L"boundary.cfg";
    boundaries.document = quest::QuestDocument::parse(fixture+"Map=Level004\r\nTimeLine=0\r\nAddMoneyMAIN=137\r\n"
        "Map=custom\r\nTimeLine=5 ; boundary note\r\nExtra=kept\r\nMap=duplicate\r\nTimeLine=5\r\n");
    const auto before_boundaries = boundaries.document.bytes();
    ASSERT_TRUE((!boundaries.toggle_boundary(0) && !boundaries.can_undo())) <<"initial boundary is immutable without undo entry";
    boundaries.toggle_boundary(3);
    ASSERT_TRUE((boundaries.document.find("Map","Level004_2")->number("TimeLine") == 3)) <<"boundary uses original naming with collision protection";
    ASSERT_TRUE((boundaries.document.find("Map","Level004")->number("AddMoneyMAIN") == 137)) <<"boundary creation preserves existing reward record";
    const auto boundary_count = boundaries.document.sections().size();
    boundaries.toggle_boundary(3);
    boundaries.toggle_boundary(3);
    ASSERT_TRUE((boundaries.document.sections().size() == boundary_count)) <<"repeated boundary toggles reuse plain marker without accumulating records";
    boundaries.undo(); boundaries.undo();
    boundaries.undo();
    ASSERT_TRUE((boundaries.document.bytes() == before_boundaries)) <<"boundary creation undo is byte exact";
    boundaries.toggle_boundary(5);
    ASSERT_TRUE((boundaries.document.find("Map","custom")->number("TimeLine") == 0
        && boundaries.document.find("Map","duplicate")->number("TimeLine") == 0)) <<"cancel boundary disables duplicate timeline entries together";
    ASSERT_TRUE((boundaries.document.find("Map","custom")->value("Extra") == "kept"
        && boundaries.document.bytes().find("; boundary note") != std::string::npos)) <<"cancel boundary preserves extensions and comments";
    boundaries.undo();
    ASSERT_TRUE((boundaries.document.bytes() == before_boundaries)) <<"boundary cancellation undo is byte exact";
}

TEST_F(Session, PlacesNodesWithoutOverlappingRows) {
    const auto directory = workspace / L"边界测试 空格";
    std::filesystem::create_directories(directory);
    const auto path = directory / L"levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    ASSERT_TRUE((!session.dirty())) << "empty session clean";
    session.open(path);
    const auto original = session.snapshot();
    const auto initial_positions = session.default_positions();
    session.document.append("Quest=1_1_3\nTimeLine=1\nQuest=1_2_0\nTimeLine=0\nQuest=1_2_1\nTimeLine=1\n");
    const auto expanded_positions = session.default_positions();
    ASSERT_TRUE((expanded_positions.at("1_1_3").y >= expanded_positions.at("1_1_1").y + 124)) <<
        "same timeline nodes have separate default slots";
    ASSERT_TRUE((expanded_positions.at("1_2_0").y >= expanded_positions.at("1_1_3").y + 124)) <<
        "expanded row leaves space before next header";
    ASSERT_TRUE((expanded_positions.at("1_1_1") == initial_positions.at("1_1_1"))) <<
        "first timeline slot stays stable";
    session.document = quest::QuestDocument::parse(original.configuration);
}

TEST_F(Session, InsertsBetweenNeighborsAsOneUndo) {
    const auto directory = workspace / L"边界测试 空格";
    std::filesystem::create_directories(directory);
    const auto path = directory / L"levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    ASSERT_TRUE((!session.dirty())) << "empty session clean";
    session.open(path);
    const auto original = session.snapshot();
    session.document.set("Quest","1_1_2","TimeLine","3");
    const auto before_insert = session.snapshot();
    const auto inserted = session.create_between(1,2,{492,90},"1_1_1","1_1_2");
    ASSERT_TRUE((session.document.connection_target(*session.document.find("Quest","1_1_1"),false) == 1003
        && session.document.connection_target(*session.document.find("Quest",inserted),false) == 1002)) <<
        "insert into row reconnects both neighbors";
    session.undo();
    ASSERT_TRUE((session.document.bytes() == before_insert.configuration && session.layout == before_insert.layout)) <<
        "automatic insertion is one undo step";
}

TEST_F(Session, RejectsInvalidCompoundEditWithoutHistory) {
    const auto directory = workspace / L"边界测试 空格";
    std::filesystem::create_directories(directory);
    const auto path = directory / L"levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    ASSERT_TRUE((!session.dirty())) << "empty session clean";
    session.open(path);
    const auto original = session.snapshot();
    EXPECT_THROW(([&] { session.create_linked("1_1_2", 1, 1, {10,20}, false); })(), std::exception) << "invalid compound link rejects";
    ASSERT_TRUE((session.document.bytes() == original.configuration && !session.can_undo())) << "compound failure leaves no node or history";
}

TEST_F(Session, DiscardsBackToSavedState) {
    const auto directory = workspace / L"边界测试 空格";
    std::filesystem::create_directories(directory);
    const auto path = directory / L"levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    ASSERT_TRUE((!session.dirty())) << "empty session clean";
    session.open(path);
    const auto original = session.snapshot();
    session.create(1,3);
    session.discard_changes();
    ASSERT_TRUE((session.document.bytes() == original.configuration && !session.can_undo() && !session.dirty())) <<
        "discard restores last save without rereading files or retaining history";
}

TEST_F(Session, UndoesCompoundCreationAndBatchDeletion) {
    const auto directory = workspace / L"边界测试 空格";
    std::filesystem::create_directories(directory);
    const auto path = directory / L"levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    ASSERT_TRUE((!session.dirty())) << "empty session clean";
    session.open(path);
    const auto original = session.snapshot();
    const auto created = session.create_linked("1_1_2", 1, 3, {10,20}, false);
    ASSERT_TRUE((session.document.connection_target(*session.document.find("Quest","1_1_2"),false) == 1003)) << "compound link complete";
    session.undo();
    ASSERT_TRUE((session.document.bytes() == original.configuration && session.layout.empty())) << "compound gesture single undo";
    session.redo();
    ASSERT_TRUE((session.layout.at(created) == quest::Position{10,20})) << "compound redo includes layout";
    session.erase_many({"1_1_0","1_1_1","1_1_2",created});
    ASSERT_TRUE((session.document.find("Quest","1_1_0").has_value())) << "batch delete protects row header";
    session.undo();
    ASSERT_TRUE((session.document.find("Quest","1_1_1") && session.document.find("Quest",created))) << "batch delete single undo";
}

TEST_F(Session, EnforcesLinkDirection) {
    const auto directory = workspace / L"边界测试 空格";
    std::filesystem::create_directories(directory);
    const auto path = directory / L"levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    ASSERT_TRUE((!session.dirty())) << "empty session clean";
    session.open(path);
    const auto original = session.snapshot();
    EXPECT_THROW(([&] { session.connect("1_1_2","1_1_1",false); })(), std::exception) << "backwards main link rejects";
    EXPECT_THROW(([&] { session.connect("1_1_1","1_1_2",true); })(), std::exception) << "forward branch rejects";
    session.connect("1_1_2","1_1_1",true);
    ASSERT_TRUE((session.document.connection_target(*session.document.find("Quest","1_1_2"),true) == 1001)) << "backwards branch encoded";
}

TEST_F(Session, ValidatesGroupsAndCreatesIntermediateHeaders) {
    const auto directory = workspace / L"边界测试 空格";
    std::filesystem::create_directories(directory);
    const auto path = directory / L"levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    ASSERT_TRUE((!session.dirty())) << "empty session clean";
    session.open(path);
    const auto original = session.snapshot();
    EXPECT_THROW(([&] { session.create(50,1); })(), std::exception) << "row 50 cannot be created";
    EXPECT_THROW(([&] { session.create(0,1); })(), std::exception) << "row zero cannot be created";
    EXPECT_THROW(([&] { session.create(1,-1); })(), std::exception) << "negative timeline cannot be created";
    session.create(4,1);
    ASSERT_TRUE((session.document.find("Quest","1_2_0") && session.document.find("Quest","1_3_0")
        && session.document.find("Quest","1_4_0"))) << "new row fills required headers";
}

TEST_F(Session, RejectsPackedIndexOverflow) {
    const auto directory = workspace / L"边界测试 空格";
    std::filesystem::create_directories(directory);
    const auto path = directory / L"levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    ASSERT_TRUE((!session.dirty())) << "empty session clean";
    session.open(path);
    const auto original = session.snapshot();
    write(path,"FileVer=2\nQuest=1_1_0\nTimeLine=0\nQuest=1_1_998\nTimeLine=1\n");
    session.open(path);
    ASSERT_TRUE((session.create(1,2) == "1_1_999")) << "sparse index 999 allowed";
    const auto at_limit = session.document.bytes();
    EXPECT_THROW(([&] { session.create(1,3); })(), std::exception) << "index 1000 rejects";
    ASSERT_TRUE((session.document.bytes() == at_limit)) << "index overflow preserves document";
}

TEST_F(Session, EnforcesRowCapacity) {
    const auto directory = workspace / L"边界测试 空格";
    std::filesystem::create_directories(directory);
    const auto path = directory / L"levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    ASSERT_TRUE((!session.dirty())) << "empty session clean";
    session.open(path);
    const auto original = session.snapshot();
    std::string capacity = "FileVer=2\n";
    for (int index = 0; index < 298; ++index) {
        capacity += "Quest=1_1_" + std::to_string(index) + "\nTimeLine=" + std::to_string(index) + "\n";
    }
    write(path,capacity);
    session.open(path);
    ASSERT_TRUE((session.create(1,299) == "1_1_298")) << "299 objects including header allowed";
    EXPECT_THROW(([&] { session.create(1,300); })(), std::exception) << "300th row object rejects";
}

TEST_F(Session, RetainsLegacyLinkEncoding) {
    const auto directory = workspace / L"边界测试 空格";
    std::filesystem::create_directories(directory);
    const auto path = directory / L"levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    ASSERT_TRUE((!session.dirty())) << "empty session clean";
    session.open(path);
    const auto original = session.snapshot();
    const std::string legacy = "Quest=1_2_0\nTimeLine=0\nQuest=1_2_1\nTimeLine=1\nNextQuests=2\nComent2=Old inline heading\nQuest=1_2_2\nTimeLine=2\n";
    auto document = quest::QuestDocument::parse(legacy);
    ASSERT_TRUE((document.version() == 0)) << "missing version is zero";
    ASSERT_TRUE((document.connection_target(*document.find("Quest","1_2_1"),false) == 2002)) << "legacy local link resolved";
    ASSERT_TRUE((document.validate().empty())) << "legacy links do not falsely dangle";
    write(path,legacy);
    session.open(path);
    session.connect("1_2_1","1_2_2",false);
    ASSERT_TRUE((session.document.find("Quest","1_2_1")->value("NextQuests") == "2")) << "legacy connection preserves encoding";
}

TEST_F(Session, PreservesLayoutFormattingOnNoOpSave) {
    const auto directory = workspace / L"边界测试 空格";
    std::filesystem::create_directories(directory);
    const auto path = directory / L"levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    ASSERT_TRUE((!session.dirty())) << "empty session clean";
    session.open(path);
    const auto original = session.snapshot();
    write(path,fixture);
    write(path.wstring() + L".qere-layout","# user note\r\n1_1_1   123.0000 456.000\r\n");
    session.open(path);
    ASSERT_TRUE((!session.dirty())) << "layout whitespace is not an edit";
    session.save();
    ASSERT_TRUE((quest::read_file(path.wstring() + L".qere-layout") == "# user note\r\n1_1_1   123.0000 456.000\r\n")) << "no-op layout preserves original bytes";
    session.document.set("Quest","1_1_1","Comment","unrelated");
    session.save();
    ASSERT_TRUE((quest::read_file(path.wstring() + L".qere-layout").starts_with("# user note"))) << "configuration edit preserves layout formatting";
}


TEST_F(Session, FailedOpenPreservesUnsavedChangesAndUndoHistory) {
    const auto path = workspace / "levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    session.open(path);
    const auto created = session.create(1, 3);
    const auto before = session.document.bytes();
    ASSERT_TRUE(session.can_undo());
    EXPECT_THROW(session.open(workspace / "missing.cfg"), std::runtime_error);
    EXPECT_EQ(session.path, path);
    EXPECT_EQ(session.document.bytes(), before);
    EXPECT_TRUE(session.dirty());
    EXPECT_TRUE(session.undo());
    EXPECT_FALSE(session.document.find("Quest", created).has_value());
}
TEST_F(Session, NewEditAfterUndoClearsRedo) {
    const auto path = workspace / "levels.cfg";
    write(path, fixture);
    quest::EditSession session;
    session.open(path);
    session.create(1, 3);
    ASSERT_TRUE(session.undo());
    ASSERT_TRUE(session.can_redo());
    session.create(1, 4);
    EXPECT_FALSE(session.can_redo());
    EXPECT_FALSE(session.redo());
}

}
