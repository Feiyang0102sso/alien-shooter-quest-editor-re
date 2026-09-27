#include "../common/test_environment.h"

namespace {
using Mission = test_support::Files;

TEST_F(Mission, ReadsMultilineTextAndFirstDuplicate) {
    const std::string mission = "<1_1_1>\r\n<Font=17>Hello\r\nworld\r\n[heading]\r\nTitle\r\n";
    ASSERT_TRUE((quest::mission_text(mission, "1_1_1") == "<Font=17>Hello\r\nworld\r\n")) << "mission multiline tags";
    const auto indexed_text = quest::mission_texts(mission+"<1_1_1>\r\nDuplicate\r\n<empty>\r\n<last>\r\nLast");
    ASSERT_TRUE((indexed_text.at("1_1_1") == quest::mission_text(mission,"1_1_1")
        && indexed_text.at("heading") == "Title\r\n" && indexed_text.at("empty").empty()
        && indexed_text.at("last") == "Last")) <<"mission index preserves first duplicate, font tags, empty block and unterminated text";
}

TEST_F(Mission, EditsOneEntryWithoutChangingOthers) {
    const std::string mission = "<1_1_1>\r\n<Font=17>Hello\r\nworld\r\n[heading]\r\nTitle\r\n";
    ASSERT_TRUE((quest::set_mission_text(mission, "heading", "New") == "<1_1_1>\r\n<Font=17>Hello\r\nworld\r\n[heading]\r\nNew\r\n")) << "mission unrelated entries";
    ASSERT_TRUE((quest::mission_text(quest::set_mission_text(mission, "added", "text"), "added") == "text\r\n")) << "new mission key";
}


TEST_F(Mission, RejectsDuplicateTargetAndInvalidKey) {
    const std::string original = "<task>\nfirst\n<task>\nsecond\n";
    EXPECT_THROW(quest::set_mission_text(original, "task", "new"), std::runtime_error);
    EXPECT_THROW(quest::set_mission_text(original, "bad\nkey", "new"), std::runtime_error);
    EXPECT_EQ(original, "<task>\nfirst\n<task>\nsecond\n");
}

}
