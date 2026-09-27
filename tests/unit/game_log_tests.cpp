#include "../common/test_environment.h"
#include "../in_game/log_reader.h"

namespace {
std::string valid_log() {
    std::string log = "QERE|START\r\n";
    for (const auto& name : game_test::expected_checks()) log += "QERE|PASS|" + name + "\r\n";
    return log + "QERE|DONE|passed=5|failed=0\r\n";
}
TEST(GameLog, AcceptsExactlyTheExpectedChecks) {
    const auto results = game_test::parse_results(valid_log());
    ASSERT_EQ(results.size(), 5u);
    for (const auto& [name, passed] : results) EXPECT_TRUE(passed) << name;
}
TEST(GameLog, RejectsMissingStartOrCompletion) {
    auto log = valid_log();
    EXPECT_THROW(game_test::parse_results(log.substr(log.find('\n') + 1)), std::runtime_error);
    EXPECT_THROW(game_test::parse_results(log.substr(0, log.find("QERE|DONE"))), std::runtime_error);
}
TEST(GameLog, RejectsDuplicatesEvenWithPassingSummary) {
    auto log = valid_log();
    log.insert(log.find("QERE|DONE"), "QERE|PASS|quest_reset\n");
    EXPECT_THROW(game_test::parse_results(log), std::runtime_error);
}
TEST(GameLog, RejectsUnknownOrMissingCheck) {
    auto log = valid_log();
    log.replace(log.find("quest_reset"), std::string("quest_reset").size(), "unknown");
    EXPECT_THROW(game_test::parse_results(log), std::runtime_error);
    EXPECT_THROW(game_test::parse_results("QERE|START\nQERE|DONE|passed=0|failed=0\n"), std::runtime_error);
}
TEST(GameLog, RejectsInconsistentSummaryAndTrailingRecords) {
    auto log = valid_log();
    log.replace(log.find("passed=5"), 8, "passed=6");
    EXPECT_THROW(game_test::parse_results(log), std::runtime_error);
    EXPECT_THROW(game_test::parse_results(valid_log() + "QERE|PASS|quest_reset\n"), std::runtime_error);
}
TEST(GameLog, RetainsExplicitFailureInsteadOfTreatingCompletionAsPass) {
    auto log = valid_log();
    log.replace(log.find("PASS|quest_reset"), 4, "FAIL");
    log.replace(log.find("passed=5|failed=0"), 17, "passed=4|failed=1");
    const auto results = game_test::parse_results(log);
    EXPECT_FALSE(results.at("quest_reset"));
    EXPECT_TRUE(results.at("levels_loaded"));
}
}
