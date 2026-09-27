#include "../common/test_environment.h"

namespace {
using GamePaths = test_support::Files;

TEST_F(GamePaths, PrefersCfgAndSavesDbFallbackToOriginalPath) {
    const auto directory = workspace/L"db-and-backups";
    std::filesystem::create_directories(directory);
    const auto cfg = directory/L"levels.cfg";
    const auto db = directory/L"levels.db";
    const auto mission = directory/L"mission.txt";
    write(db,fixture);
    write(mission,"<first>\r\ninitial\r\n");
    quest::EditSession session;
    session.open(directory);
    ASSERT_TRUE((session.path == db)) <<"directory falls back to levels.db";
    session.document.set("Quest","1_1_1","Comment","DB source");
    ASSERT_TRUE((session.save())) <<"DB saves with backup rotation";
    const auto db_bytes = quest::read_file(db);
    ASSERT_TRUE((!std::filesystem::exists(cfg) && db_bytes.find("DB source") != std::string::npos)) <<
        "DB edit saves back to DB without creating CFG";
    write(cfg,fixture);
    session.open(directory);
    ASSERT_TRUE((session.path == cfg && session.document.bytes() == fixture)) <<"directory prefers CFG when both exist";
    session.open(db);
    ASSERT_TRUE((session.path == cfg)) <<"explicit same-name DB also respects CFG priority";
}


TEST_F(GamePaths, KeepsUnrelatedPathsAndReportsExpectedMissingCfg) {
    const auto arbitrary = workspace / "custom.txt";
    EXPECT_EQ(quest::game_configuration_path(arbitrary), arbitrary);
    EXPECT_EQ(quest::game_configuration_path(workspace), workspace / "levels.cfg");
}
TEST_F(GamePaths, ResolvesMixedCaseDbExtensionWithCfgPriority) {
    const auto cfg = workspace / "levels.cfg";
    const auto db = workspace / "levels.DB";
    write(cfg, fixture);
    write(db, "different data");
    EXPECT_EQ(quest::game_configuration_path(db), cfg);
}
TEST_F(GamePaths, PrefersNestedMissionAndFallsBackBesideConfiguration) {
    const auto configuration = workspace / "levels.cfg";
    const auto beside = workspace / "mission.txt";
    EXPECT_EQ(quest::mission_text_path(configuration), beside);
    write(beside, "root");
    std::filesystem::create_directories(workspace / "Text");
    const auto nested = workspace / "Text/mission.txt";
    write(nested, "nested");
    EXPECT_EQ(quest::mission_text_path(configuration), nested);
    std::filesystem::remove(nested);
    EXPECT_EQ(quest::mission_text_path(configuration), beside);
}

}
