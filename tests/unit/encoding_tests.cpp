#include "../common/test_environment.h"

namespace {
using Encoding = test_support::Files;

TEST_F(Encoding, RoundTripsSupportedLanguages) {
    const std::wstring russian = L"Привет мир";
    const std::wstring chinese = L"任务编辑器";
    ASSERT_TRUE((quest::decode(quest::encode(russian, 1251), 1251) == russian)) << "CP1251 roundtrip";
    ASSERT_TRUE((quest::decode(quest::encode(chinese, 936), 936) == chinese)) << "GBK roundtrip";
    ASSERT_TRUE((quest::decode(quest::encode(chinese + russian, 65001), 65001) == chinese + russian)) << "UTF8 roundtrip";
    EXPECT_THROW(([&] { quest::encode(chinese, 1251); })(), std::exception) << "unrepresentable text rejected";
    ASSERT_TRUE((quest::decode(quest::encode(chinese+russian,936),936) == chinese+russian)) <<
        "one GBK encoding represents both Chinese and common Russian letters";
}

TEST_F(Encoding, KeepsAsciiFallback) {
    ASSERT_TRUE((quest::detect_game_encoding(fixture,1251) == 1251
        && quest::detect_game_encoding(fixture,936) == 936)) <<"ASCII has no encoding evidence and keeps caller fallback";
}

TEST_F(Encoding, DetectsGbkAndCyrillicIndependently) {
    for (const auto& text : {L"调查基地 获取权限 找到控制组的电脑",L"打开大门，完成任务！",L"Начало, пустышка",L"НАЙТИ ПУЛЬТ УПРАВЛЕНИЯ ВОРОТ."}) {
        ASSERT_TRUE((quest::detect_game_encoding(quest::encode(text,936),1251) == 936)) <<
            "detect GBK Chinese or GBK Cyrillic regardless of CFG fallback";
    }
    for (const auto& text : {L"Начало, пустышка",L"НАЙТИ ПУЛЬТ УПРАВЛЕНИЯ ВОРОТ.",L"Открыть ворота",L"тест",L"Go to control point №13"}) {
        ASSERT_TRUE((quest::detect_game_encoding(quest::encode(text,1251),936) == 1251)) <<
            "detect CP1251 regardless of mission fallback";
    }
}

TEST_F(Encoding, PreservesUnchangedMixedBytesAndRejectsLossyEdits) {
    const std::wstring russian = L"Привет мир";
    const std::wstring chinese = L"任务编辑器";
    const std::string mixed_bytes = "<first>\r\n"+quest::encode(russian,1251)
        +"\r\n<second>\r\n"+quest::encode(chinese,936)+"\r\n<broken>\r\n\x81\r\n";
    const auto mixed_display = quest::decode(mixed_bytes,936);
    ASSERT_TRUE((quest::encode_edit(mixed_bytes,mixed_display,936) == mixed_bytes)) <<
        "unchanged mixed or malformed text keeps original bytes";
    EXPECT_THROW(([&] { quest::encode_edit(mixed_bytes,mixed_display+L"edited",936); })(), std::exception) <<
        "whole block edit rejects lossy original decoding";
    const auto gbk_original = quest::encode(chinese+russian,936);
    ASSERT_TRUE((quest::encode_edit(gbk_original,chinese+russian+L"!",936) == gbk_original+"!")) <<
        "lossless whole block edit preserves original multilingual bytes";
    EXPECT_THROW(([&] { quest::encode_edit("original",chinese,1251); })(), std::exception) <<
        "whole block edit rejects unrepresentable new input";
}

TEST_F(Encoding, DetectsIndependentFileEncodings) {
    const auto samples = workspace / "samples";
    test_support::create_samples(samples);
    quest::EditSession chinese_sample;
    chinese_sample.open(samples/L"AS2R full"/L"AlienShooter2 Reloaded"/L"levels.cfg");
    ASSERT_TRUE((chinese_sample.codepage == 936)) <<"reported Chinese AS2R full CFG opens as GBK";
    ASSERT_TRUE((chinese_sample.mission_codepage == 936 && !chinese_sample.dirty())) <<"reported Chinese mission is independently detected without edits";
    ASSERT_TRUE((quest::decode(chinese_sample.document.find("Quest","1_1_1")->value("Comment"),chinese_sample.codepage) == L"调查基地，消灭敌人并保护附近的队友，完成任务。")) <<
        "reported first task displays real Chinese rather than mojibake";
    for (const auto& name : {L"AS2 pre release demo",L"AS2C",L"AS2R"}) {
        quest::EditSession russian_sample;
        russian_sample.open(samples/name/L"levels.cfg");
        ASSERT_TRUE((russian_sample.codepage == 1251)) <<"existing Russian CFG is not mistaken for GBK";
        ASSERT_TRUE((russian_sample.document.bytes() == quest::read_file(samples/name/L"levels.cfg") && !russian_sample.dirty())) <<
            "automatic display detection preserves original sample bytes";
        std::wcout << L"DETECT " << name << L" cfg=" << russian_sample.codepage << L" mission=" << russian_sample.mission_codepage << L'\n';
        if (std::wstring(name) == L"AS2 pre release demo") ASSERT_TRUE((russian_sample.mission_codepage == 936)) <<"GBK Russian mission differs from its CP1251 CFG";
        if (std::wstring(name) == L"AS2C") ASSERT_TRUE((russian_sample.mission_codepage == 1251)) <<"CP1251 mission is no longer forced to GBK";
    }
}

TEST_F(Encoding, RoundTripsGeneratedCorpus) {
    const auto samples = workspace / "samples";
    test_support::create_samples(samples);
    int count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(samples)) {
        if (!entry.is_regular_file()) continue;
        const auto name = entry.path().filename().wstring();
        if (name != L"levels.cfg" && name != L"mission.txt" && name != L"Survive.cfg") continue;
        const auto bytes = quest::read_file(entry.path());
        const auto document = quest::QuestDocument::parse(bytes);
        ASSERT_TRUE((document.bytes() == bytes)) << "sample byte roundtrip";
        if (name == L"levels.cfg") {
            int errors = 0, warnings = 0, quests = 0;
            for (const auto& issue : document.validate()) { if (issue.error) ++errors; else ++warnings; }
            for (const auto& section : document.sections()) if (section.kind == "Quest") ++quests;
            std::wcout << L"CORPUS " << entry.path().wstring() << L" quests=" << quests << L" errors=" << errors << L" warnings=" << warnings << L"\n";
            ASSERT_TRUE((errors == 0)) << "sample blocking diagnostics";
        }
        ++count;
    }
    ASSERT_TRUE((count >= 10)) << "full sample corpus enumerated";
    std::cout << "CORPUS files=" << count << "\n";
}


TEST_F(Encoding, InspectionKeepsAsciiAsSystemAnsi) {
    const auto encoding = quest::inspect_game_encoding("Quest=1_1_1\n");
    EXPECT_TRUE(encoding.system_ansi);
    EXPECT_EQ(encoding.codepage, quest::default_game_codepage());
}
TEST_F(Encoding, InspectionRejectsBinaryAndUtf8Bom) {
    EXPECT_THROW(quest::inspect_game_encoding(std::string("a\0b", 3)), std::runtime_error);
    EXPECT_THROW(quest::inspect_game_encoding("\xEF\xBB\xBFQuest=1_1_1"), std::runtime_error);
}
TEST_F(Encoding, InvalidUtf8AndUnpairedSurrogateAreRejected) {
    EXPECT_THROW(quest::decode("\xC0\xAF", CP_UTF8), std::runtime_error);
    EXPECT_THROW(quest::encode(std::wstring(1, wchar_t(0xD800)), CP_UTF8), std::runtime_error);
}

}
