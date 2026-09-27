#include "test_environment.h"

namespace test_support {
std::filesystem::path project;
std::filesystem::path game;
std::filesystem::path output;
void ensure(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ensure(bool(stream), "Cannot write test file: " + path.string());
}
std::string asset(const std::filesystem::path& relative) {
    return quest::read_file(project / "tests/assets" / relative);
}
void Files::SetUp() {
    original_directory = std::filesystem::current_path();
    const auto* test = testing::UnitTest::GetInstance()->current_test_info();
    workspace = output / "work" / test->test_suite_name() / test->name();
    std::filesystem::create_directories(workspace);
    // A run-specific parent prevents old data from making a new case pass.
    fixture = asset("unit/levels.cfg");
    dll = project / "bin/Win32/Release/QuestEditor.dll";
}
void Files::TearDown() {
    std::filesystem::current_path(original_directory);
}
void create_samples(const std::filesystem::path& samples) {
    const auto fixture = asset("unit/levels.cfg");
    std::string chinese = fixture;
    chinese.insert(chinese.find("NextQuests="), "Comment=" + quest::encode(L"调查基地，消灭敌人并保护附近的队友，完成任务。", 936) + "\r\n");
    for (const auto& name : {L"AS2R full/AlienShooter2 Reloaded", L"AS2 pre release demo", L"AS2C", L"AS2R"}) {
        const auto directory = samples / name;
        std::filesystem::create_directories(directory / L"Text");
        std::string config = fixture;
        std::string mission = quest::encode(L"Привет, необходимо уничтожить противника и защитить товарищей.", 1251);
        if (std::wstring(name) == L"AS2R full/AlienShooter2 Reloaded") {
            config = chinese;
            mission = quest::encode(L"调查基地，消灭敌人并保护附近的队友，完成任务。", 936);
        } else {
            config.insert(config.find("NextQuests="), "Comment=" + mission + "\r\n");
        }
        if (std::wstring(name) == L"AS2 pre release demo") {
            mission = quest::encode(L"调查基地，消灭敌人并保护附近的队友，完成任务。", 936);
        }
        write(directory / L"levels.cfg", config);
        write(directory / L"Text" / L"mission.txt", "<1_1_1>\r\n" + mission + "\r\n");
        write(directory / L"Survive.cfg", fixture);
    }
}
}
