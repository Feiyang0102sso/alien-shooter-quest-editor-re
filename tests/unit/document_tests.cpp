#include "../common/test_environment.h"

namespace {
using Document = test_support::Files;

TEST_F(Document, PreservesOriginalBytes) {
    auto document = quest::QuestDocument::parse(fixture);
    ASSERT_TRUE((document.bytes() == fixture)) << "fixture byte roundtrip";
    ASSERT_TRUE((document.sections().size() == 4)) << "section count";
    ASSERT_TRUE((document.find("Quest", "1_1_1")->fields.size() == 6)) << "duplicate and unknown fields retained";
    auto section = document.find("Quest", "1_1_1");
    EXPECT_THROW(([&] { document.replace_block(*section, "Quest=1_9_9\n"); })(), std::exception) << "block cannot silently renumber";
    ASSERT_TRUE((document.validate().empty())) << "valid fixture diagnostics";
}

TEST_F(Document, ParsesPackedQuestIds) {
    ASSERT_TRUE((quest::QuestId::parse("1_7_2")->packed() == 7002)) << "packed ID";
    ASSERT_TRUE((!quest::QuestId::parse("1_7_1000"))) << "packed index range";
    ASSERT_TRUE((!quest::QuestId::parse("x_7_2"))) << "invalid ID";
}

TEST_F(Document, ChangesOnlySelectedField) {
    auto document = quest::QuestDocument::parse(fixture);
    document.set("Quest", "1_1_2", "Comment", "new");
    auto expected = fixture;
    expected.replace(expected.find("old"), 3, "new");
    ASSERT_TRUE((document.bytes() == expected)) << "single field preserves all other bytes";
    document.set("Quest", "1_1_2", "WinOnBegin", "1");
    ASSERT_TRUE((document.find("Quest", "1_1_2")->number("WinOnBegin") == 1)) << "new field";
}

TEST_F(Document, EditsConditionWithoutLosingNeighbors) {
    const std::string condition_source = "Quest=1_1_1\r\nChangeStateItem = QS_BEGUN CSI_TAKE key | QS_FAILED CSI_DEATH monster ; keep\r\nUnknown=abc\r\n";
    auto conditions = quest::QuestDocument::parse(condition_source);
    conditions.set_condition("1_1_1","ChangeStateItem","QS_BEGUN CSI_TAKE","other");
    ASSERT_TRUE((conditions.bytes() == "Quest=1_1_1\r\nChangeStateItem = QS_BEGUN CSI_TAKE other | QS_FAILED CSI_DEATH monster ; keep\r\nUnknown=abc\r\n")) <<
        "condition edit preserves pipe neighbor, spacing, comment and unknown field";
    conditions.set_condition("1_1_1","ChangeStateItem","QS_BEGUN CSI_TAKE","");
    ASSERT_TRUE((conditions.bytes().find("ChangeStateItem = QS_FAILED CSI_DEATH monster ; keep") != std::string::npos)) <<
        "clear first condition preserves adjacent failure slot";
    conditions.set_condition("1_1_1","ChangeStateItem","QS_FAILED CSI_DEATH","");
    ASSERT_TRUE((conditions.bytes().find("; keep\r\nUnknown=abc") != std::string::npos)) <<
        "clear final condition retains user comment";
    conditions.set_condition("1_1_1","SetFlagmanCoord","QS_FAILED","1 2 3 4");
    ASSERT_TRUE((conditions.bytes().find("SetFlagmanCoord=QS_FAILED 1 2 3 4") != std::string::npos)) <<"insert independent coordinate condition";
    conditions = quest::QuestDocument::parse(condition_source);
    conditions.set_condition("1_1_1","ChangeStateItem","QS_FAILED CSI_DEATH","");
    ASSERT_TRUE((conditions.bytes().find("QS_BEGUN CSI_TAKE key ; keep") != std::string::npos)) <<"clear last pipe slot keeps previous condition";
}

TEST_F(Document, RejectsAmbiguousConditionSlot) {
    auto conditions = quest::QuestDocument{};
    conditions = quest::QuestDocument::parse("Quest=1_1_1\nChangeStateItem=QS_BEGUN CSI_TAKE a|QS_BEGUN CSI_TAKE b\n");
    const auto duplicate_conditions = conditions.bytes();
    EXPECT_THROW(([&] { conditions.set_condition("1_1_1","ChangeStateItem","QS_BEGUN CSI_TAKE","c"); })(), std::exception) <<"duplicate slot edit requires raw CFG";
    ASSERT_TRUE((conditions.bytes() == duplicate_conditions)) <<"ambiguous edit preserves both original conditions";
}

TEST_F(Document, ReportsMalformedFields) {
    auto invalid = quest::QuestDocument::parse("Quest=bad\nTimeLine=-1\nChangeStateItem=QS_OOPS CSI_TAKE key\n");
    ASSERT_TRUE((invalid.validate().size() >= 3)) << "invalid fields diagnosed";
}

TEST_F(Document, HandlesBomAndMissingFinalNewline) {
    auto no_newline = quest::QuestDocument::parse("Quest=1_1_1\nTimeLine=1");
    no_newline.set("Quest", "1_1_1", "Comment", "x");
    ASSERT_TRUE((no_newline.bytes() == "Quest=1_1_1\nTimeLine=1\nComment=x\n")) << "append field without final newline";
    ASSERT_TRUE((quest::QuestDocument::parse("\xEF\xBB\xBFQuest=1_1_0\r\n").sections().size() == 1)) << "UTF8 BOM";
}

TEST_F(Document, RandomBytesRoundTrip) {
    std::mt19937 random(42);
    for (int iteration = 0; iteration < 1500; ++iteration) {
        SCOPED_TRACE("seed=42 iteration=" + std::to_string(iteration));
        std::string data;
        const int length = static_cast<int>(random() % 2048);
        for (int index = 0; index < length; ++index) data.push_back(static_cast<char>(random() % 256));
        ASSERT_TRUE((quest::QuestDocument::parse(data).bytes() == data)) << "random byte roundtrip";
    }
}

TEST_F(Document, WarnsAboutLongLinesAndMissingHeaders) {
    auto long_line = quest::QuestDocument::parse(";" + std::string(999,'x') + "\n");
    ASSERT_TRUE((long_line.validate().size() == 1 && !long_line.validate().front().error)) <<
        "original physical line limit produces a non-destructive warning";
    auto no_header = quest::QuestDocument::parse("Quest=1_1_1\nTimeLine=0\n");
    ASSERT_TRUE((no_header.validate().size() == 1)) <<"missing row header diagnosed";
}

TEST_F(Document, PreservesPipeSegmentSemantics) {
    auto document = quest::QuestDocument::parse("FileVer=2\nQuest=1_1_1\nTimeLine=1\nNextQuests=1002 | 1003\nStartingQuests=2 3|4 5\nChangeStateItem=QS_BEGUN CSI_TAKE key|QS_COMPLETED CSI_DEATH gate\n");
    const auto multi = document.find("Quest","1_1_1");
    ASSERT_TRUE((multi->value("NextQuests") == "1003" && multi->value("StartingQuests") == "4 5")) << "pipe scalar and array last segment";
    ASSERT_TRUE((multi->fields.size() == 7)) << "pipe conditions kept in order";
    for (const auto& diagnostic : document.validate()) ASSERT_TRUE((!diagnostic.error)) << "pipe format has no blocking diagnostics";
}

}
