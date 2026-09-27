#include "../common/test_environment.h"
#include "../common/process.h"
#include "../common/game_environment.h"
#include "log_reader.h"

namespace {
std::map<std::string, bool> run_game() {
    const auto& project = test_support::project;
    const auto& game = test_support::game;
    const auto& workspace = test_support::output;
    const auto config = quest::game_configuration_path(game);
    const auto document = quest::QuestDocument::parse(quest::read_file(config));
    ensure(document.find("Quest", "1_1_3").has_value(), "AS2R smoke test requires quest 1_1_3");
    const auto script_path = game / L"Maps" / L"Level_01.lgc";
    std::string script = quest::read_file(script_path);
    const auto init = script.find("InitGame();");
    ensure(init != std::string::npos && script.find("QeSmokeTest") == std::string::npos,
        "Expected unmodified Level_01 InitGame hook");
    const auto recovery = project / "out/test/recovery";
    TemporaryFile original_script(script_path, recovery);
    TemporaryFile test_script(game / L"Maps" / L"qere-smoke.lgc", recovery);
    TemporaryFile test_config(game / L"AlienShooter.cfg", recovery);
    write(game / L"Maps" / L"qere-smoke.lgc", quest::read_file(project / L"tests/assets/in_game/smoke.lgc"));
    script.insert(init + std::string("InitGame();").size(), "\r\n    QeSmokeTest();\r\n");
    const auto entry = script.find("main()");
    ensure(entry != std::string::npos, "Level_01 main hook missing");
    script.insert(entry, "#include \"maps\\qere-smoke.lgc\"\r\n\r\n");
    write(script_path, script);
    const auto settings = game / L"AlienShooter.cfg";
    TestGameSettings game_settings;
    game_settings.configure(project / L"tests/assets/config/test.cfg", settings);
    const auto result = game / L"qere-test.log";
    // Only the named test output is replaced, preventing a previous PASS from being reused.
    std::filesystem::remove(result);
    {
        TestProcess process(game / L"AlienShooter.exe", L"");
        hidden_test_window(process, L"AlienShooter");
        process.wait(120000);
    }
    ensure(std::filesystem::exists(result), "LGC did not produce qere-test.log; inspect game Logs/error.log");
    const auto log = quest::read_file(result);
    write(workspace / L"in_game.log", log);
    original_script.restore();
    test_script.restore();
    test_config.restore();

    return game_test::parse_results(log);
}
class Game : public testing::Test {
protected:
    static inline bool attempted = false;
    static inline std::string launch_error;
    static inline std::map<std::string, bool> results;
    void SetUp() override {
        // One engine launch supplies independent named observations for this suite.
        if (!attempted) {
            attempted = true;
            try { results = run_game(); }
            catch (const std::exception& error) { launch_error = error.what(); }
        }
        ASSERT_TRUE(launch_error.empty()) << launch_error;
    }
};
TEST_F(Game, LoadsQuestConfiguration) { EXPECT_TRUE(results.at("levels_loaded")); }
TEST_F(Game, LoadsFirstQuestGroup) { EXPECT_TRUE(results.at("first_group_loaded")); }
TEST_F(Game, ResetsQuestState) { EXPECT_TRUE(results.at("quest_reset")); }
TEST_F(Game, BeginCommandChangesQuestState) { EXPECT_TRUE(results.at("quest_begin_command")); }
TEST_F(Game, LoadsMissionText) { EXPECT_TRUE(results.at("mission_text_loaded")); }
}
