#include "test_environment.h"
#include "process.h"
#include "report.h"
#include <tlhelp32.h>
#include <iostream>
#include <sstream>

namespace {
class TestDesktop {
    HDESK handle_ = nullptr;
public:
    std::wstring name = L"QuestEditorTests-" + std::to_wstring(GetCurrentProcessId());
    TestDesktop() {
        handle_ = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        ensure(handle_ != nullptr, "Cannot create a private test desktop");
    }
    ~TestDesktop() { CloseDesktop(handle_); }
};

// Refuse deployment while the actual target executable is in use.
void check_stopped(const std::filesystem::path& game) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    ensure(snapshot != INVALID_HANDLE_VALUE, "Cannot enumerate processes");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"AlienShooter.exe") != 0
                && _wcsicmp(entry.szExeFile, L"MapEditor.exe") != 0
                && _wcsicmp(entry.szExeFile, L"QuestEditor.exe") != 0) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) continue;
            wchar_t path[32768]{};
            DWORD length = 32768;
            if (QueryFullProcessImageNameW(process, 0, path, &length)) {
                if (std::filesystem::equivalent(std::filesystem::path(path).parent_path(), game)) running = true;
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    ensure(!running, "Close the game, MapEditor and QuestEditor in the test directory before running tests");
}
void deploy(const std::filesystem::path& project, const std::filesystem::path& game) {
    const auto binary = project / L"bin/Win32/Release";
    for (const auto* file : {L"QuestEditor.dll", L"QuestEditor.exe"}) {
        std::filesystem::copy_file(binary / file, game / file, std::filesystem::copy_options::overwrite_existing);
    }
    std::filesystem::create_directories(game / L"i18n");
    for (const auto* file : {L"cn.ini", L"en.ini"}) {
        std::filesystem::copy_file(binary / L"i18n" / file, game / L"i18n" / file,
            std::filesystem::copy_options::overwrite_existing);
    }
    if (!std::filesystem::exists(game / L"QuestEditor.cfg")) {
        std::filesystem::copy_file(binary / L"QuestEditor.cfg", game / L"QuestEditor.cfg");
    }
    std::cout << "[DEPLOY] QuestEditor updated\n";
}
// A small glob for the BAT's optional Suite.Case selector.
bool matches(const char* pattern, const char* name) {
    if (*pattern == 0) return *name == 0;
    if (*pattern == '*') {
        return matches(pattern + 1, name) || (*name && matches(pattern, name + 1));
    }
    if (*name && (*pattern == '?' || *pattern == *name)) return matches(pattern + 1, name + 1);
    return false;
}
std::string group_of(const std::string& suite) {
    if (suite == "Game") return "in_game";
    if (suite == "Host") return "host";
    if (suite == "Controls" || suite == "Abi") return "controls";
    if (suite == "Manual") return "manual";
    return "unit";
}
bool selected(const std::string& mode, const std::string& group) {
    if (mode == "all" || mode == "list") return true;
    if (mode == "in_editor") return group == "controls" || group == "host" || group == "manual";
    return mode == group;
}
std::string case_filter(const std::string& group, const std::string& pattern) {
    std::string filter;
    const auto* unit = testing::UnitTest::GetInstance();
    for (int suite_index = 0; suite_index < unit->total_test_suite_count(); ++suite_index) {
        const auto* suite = unit->GetTestSuite(suite_index);
        if (group_of(suite->name()) != group) continue;
        for (int case_index = 0; case_index < suite->total_test_count(); ++case_index) {
            const auto* test = suite->GetTestInfo(case_index);
            const auto name = std::string(suite->name()) + "." + test->name();
            if (!matches(pattern.c_str(), name.c_str())) continue;
            if (!filter.empty()) filter += ":";
            filter += name;
        }
    }
    return filter;
}
void report_unfinished(const std::string& filter, const std::string& reason,
    int& passed, int& failed, int& manual) {
    const auto log = quest::read_file(test_support::project / "out/test/run.log");
    std::istringstream names(filter);
    std::string name;
    while (std::getline(names, name, ':')) {
        bool found = false;
        std::istringstream lines(log);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.starts_with("[PASS] " + name + " (")) { ++passed; found = true; break; }
            if (line.starts_with("[FAIL] " + name + " (")) { ++failed; found = true; break; }
            if (line.starts_with("[NEED MANUAL] " + name + " (")) { ++manual; found = true; break; }
        }
        if (!found) {
            ++failed;
            test_support::status("FAIL", name + " (not completed: " + reason + ")");
        }
    }
}
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::cout << std::unitbuf;
    HANDLE run_lock = nullptr;
    try {
        testing::InitGoogleTest(&argc, argv);
        ensure(argc >= 4, "Usage: tests <all|unit|in_game|in_editor|list> <project> <game> [Suite.Case]");
        const auto mode = quest::encode(argv[1], CP_UTF8);
        test_support::project = std::filesystem::canonical(argv[2]);
        test_support::game = std::filesystem::absolute(argv[3]);
        std::string pattern = "*";
        if (argc >= 5) pattern = quest::encode(argv[4], CP_UTF8);
        if (mode.starts_with("worker_")) {
            ensure(argc == 6, "Worker requires its run directory");
            const auto group = mode.substr(7);
            test_support::output = argv[5];
            GTEST_FLAG_SET(filter, pattern);
            GTEST_FLAG_SET(output, "xml:" + (test_support::output / (group + ".xml")).string());
            test_support::install_report(group);
            return RUN_ALL_TESTS();
        }
        ensure(mode == "all" || mode == "unit" || mode == "in_game" || mode == "in_editor" || mode == "list",
            "Unknown test group");
        if (mode == "list") {
            const auto* unit = testing::UnitTest::GetInstance();
            int count = 0;
            for (int i = 0; i < unit->total_test_suite_count(); ++i) {
                const auto* suite = unit->GetTestSuite(i);
                for (int j = 0; j < suite->total_test_count(); ++j) {
                    const auto* test = suite->GetTestInfo(j);
                    const auto name = std::string(suite->name()) + "." + test->name();
                    if (matches(pattern.c_str(), name.c_str())) { std::cout << name << '\n'; ++count; }
                }
            }
            std::cout << "Listed: " << count << '\n';
            return count == 0;
        }
        run_lock = CreateMutexW(nullptr, FALSE, L"Local\\QuestEditorRE.TestRun");
        ensure(run_lock != nullptr && GetLastError() != ERROR_ALREADY_EXISTS, "Another test run is active");
        const auto log_directory = test_support::project / "out/test";
        std::filesystem::create_directories(log_directory);
        test_support::write(log_directory / "run.log", "");
        test_support::output = log_directory / ("run-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(test_support::output);
        test_support::write(log_directory / "latest.txt", test_support::output.string());
        wchar_t executable[32768]{};
        ensure(GetModuleFileNameW(nullptr, executable, 32768), "Cannot resolve runner path");
        int passed = 0, failed = 0, manual = 0, infrastructure_failed = 0;
        bool deployed = false;
        for (const std::string group : {"unit", "in_game", "controls", "host", "manual"}) {
            if (!selected(mode, group)) continue;
            const auto filter = case_filter(group, pattern);
            if (filter.empty()) continue;
            try {
                if ((group == "in_game" || group == "host") && !deployed) {
                    ensure(std::filesystem::is_directory(test_support::game), "Game directory missing");
                    const auto recovery = test_support::project / "out/test/recovery";
                    if (std::filesystem::exists(recovery)) {
                        for (const auto& entry : std::filesystem::directory_iterator(recovery)) {
                            ensure(entry.path().extension() != ".original", "Unrestored test files exist in out/test/recovery");
                        }
                    }
                    check_stopped(test_support::game);
                    deploy(test_support::project, test_support::game);
                    deployed = true;
                }
                std::unique_ptr<TestDesktop> desktop;
                if (group == "controls") desktop = std::make_unique<TestDesktop>();
                std::wstring desktop_name;
                WORD show = SW_HIDE;
                if (desktop) { desktop_name = desktop->name; show = SW_SHOWNORMAL; }
                const auto arguments = L"worker_" + quest::decode(group, CP_UTF8) + L" \""
                    + test_support::project.wstring() + L"\" \"" + test_support::game.wstring()
                    + L"\" \"" + quest::decode(filter, CP_UTF8) + L"\" \"" + test_support::output.wstring() + L"\"";
                TestProcess child(executable, arguments, desktop_name, show);
                // Assertion failures still produce a summary; crashes and timeouts do not.
                const DWORD code = child.exit_code(240000);
                std::ifstream summary(test_support::output / (group + ".summary"));
                int good = 0, bad = 0, skipped = 0;
                ensure(bool(summary >> good >> bad >> skipped), "Worker did not finish; inspect last RUN entry");
                if (code != 0 && bad == 0) throw std::runtime_error("Worker exited abnormally");
                passed += good;
                failed += bad;
                if (group == "manual") manual += skipped;
                else if (skipped) { failed += skipped; test_support::status("FAIL", "Automatic cases were skipped"); }
            } catch (const std::exception& error) {
                ++infrastructure_failed;
                test_support::status("FAIL", group + ": " + error.what());
                report_unfinished(filter, error.what(), passed, failed, manual);
            }
        }
        if (passed + failed + manual == 0 && infrastructure_failed == 0) {
            ++infrastructure_failed;
            test_support::status("FAIL", "No cases match the requested filter");
        }
        test_support::status("SUMMARY", "Automatic: " + std::to_string(passed) + " passed, "
            + std::to_string(failed) + " failed; manual: " + std::to_string(manual)
            + " pending; infrastructure: " + std::to_string(infrastructure_failed) + " failed");
        CloseHandle(run_lock);
        return failed != 0 || infrastructure_failed != 0;
    } catch (const std::exception& error) {
        test_support::status("FAIL", error.what());
        if (run_lock) CloseHandle(run_lock);
        return 1;
    }
}
