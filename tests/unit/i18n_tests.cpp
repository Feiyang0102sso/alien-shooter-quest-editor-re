#include "../common/test_environment.h"

namespace {
using I18n = test_support::Files;

TEST_F(I18n, DefaultsToChineseAndPersistsEnglish) {
    using namespace quest::i18n;
    const auto directory = workspace/L"language-fixture";
    std::filesystem::create_directories(directory/L"i18n");
    const auto config = directory/L"QuestEditor.cfg";
    initialize(directory);
    ASSERT_TRUE((language() == 2 && std::wstring(wide("menu.file")) == L"文件")) <<"missing language config defaults to Chinese";
    set_language(1);
    ASSERT_TRUE((language() == 1 && std::wstring(wide("menu.file")) == L"File")) <<"English language selected";
    ASSERT_TRUE((quest::read_file(config) == "language = 1\r\n")) <<"language config created beside module";
    initialize(directory);
    ASSERT_TRUE((language() == 1)) <<"saved language survives reload";
}

TEST_F(I18n, PreservesBomCommentsAndUnknownKeys) {
    using namespace quest::i18n;
    const auto directory = workspace/L"language-fixture";
    std::filesystem::create_directories(directory/L"i18n");
    const auto config = directory/L"QuestEditor.cfg";
    initialize(directory);
    set_language(1);
    const std::string original = "\xEF\xBB\xBF; keep\r\nother = 19\r\nlanguage = 1  ; selection\r\n";
    write(config,original);
    set_language(2);
    ASSERT_TRUE((quest::read_file(config) == "\xEF\xBB\xBF; keep\r\nother = 19\r\nlanguage = 2  ; selection\r\n")) <<
        "language save preserves BOM comments unknown keys and spacing";
}

TEST_F(I18n, FallsBackFromInvalidLanguage) {
    using namespace quest::i18n;
    const auto directory = workspace/L"language-fixture";
    std::filesystem::create_directories(directory/L"i18n");
    const auto config = directory/L"QuestEditor.cfg";
    initialize(directory);
    write(config,"language = 99\n");
    initialize(directory);
    ASSERT_TRUE((language() == 2)) <<"invalid language falls back to Chinese";
}

TEST_F(I18n, LoadsExternalTranslationsWithFallback) {
    using namespace quest::i18n;
    const auto directory = workspace/L"language-fixture";
    std::filesystem::create_directories(directory/L"i18n");
    const auto config = directory/L"QuestEditor.cfg";
    initialize(directory);
    write(directory/L"i18n"/L"en.ini","menu.file = \"Custom 文件\"\nmenu.edit = \"bad\\q\"\ncommon.cancel = \"Exit\\tEsc\"\n");
    set_language(1);
    ASSERT_TRUE((std::wstring(wide("menu.file")) == L"Custom 文件")) <<"external UTF8 translation overrides embedded entry";
    ASSERT_TRUE((std::wstring(wide("menu.edit")) == L"Edit" && std::wstring(wide("menu.help")) == L"Help")) <<
        "malformed and missing translations retain embedded English";
    ASSERT_TRUE((std::wstring(wide("common.cancel")) == L"Exit\tEsc")) <<"translation escape sequences decoded";
    const wchar_t* filter = wide("editor.open_filter");
    const wchar_t* pattern = filter+wcslen(filter)+1;
    ASSERT_TRUE((std::wstring(pattern) == L"*.cfg;*.db")) <<"embedded NUL filter separators preserved";
}

TEST_F(I18n, FailedSavePreservesActiveLanguage) {
    using namespace quest::i18n;
    const auto directory = workspace/L"language-fixture";
    std::filesystem::create_directories(directory/L"i18n");
    const auto config = directory/L"QuestEditor.cfg";
    initialize(directory);
    set_language(1);
    const auto before = quest::read_file(config);
    HANDLE locked = CreateFileW(config.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    ASSERT_TRUE((locked != INVALID_HANDLE_VALUE)) <<"lock language config fixture";
    bool rejected = false;
    try { set_language(2); } catch (const std::exception&) { rejected = true; }
    CloseHandle(locked);
    ASSERT_TRUE((rejected && language() == 1 && quest::read_file(config) == before)) <<
        "failed language save retains active language and original config";
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        ASSERT_TRUE((!entry.path().filename().wstring().starts_with(L"QuestEditor.cfg.tmp."))) <<"failed language save removes temporary file";
    }
}

TEST_F(I18n, AppendsMissingLanguageKey) {
    using namespace quest::i18n;
    const auto directory = workspace/L"language-fixture";
    std::filesystem::create_directories(directory/L"i18n");
    const auto config = directory/L"QuestEditor.cfg";
    initialize(directory);
    set_language(1);
    write(config,"; no language key");
    set_language(2);
    ASSERT_TRUE((quest::read_file(config) == "; no language key\r\nlanguage = 2\r\n")) <<"missing key appended without losing contents";
}

}
