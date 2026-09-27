#include "report.h"
#include <iostream>
namespace test_support {
void status(const std::string& state, const std::string& message) {
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO previous{};
    const bool colored = GetConsoleScreenBufferInfo(console, &previous) != FALSE;
    WORD color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    if (state == "PASS") color = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    if (state == "FAIL") color = FOREGROUND_RED | FOREGROUND_INTENSITY;
    if (state == "NEED MANUAL" || state == "SKIP") color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    if (colored) SetConsoleTextAttribute(console, color);
    const auto line = "[" + state + "] " + message;
    std::cout << line << std::endl;
    if (colored) SetConsoleTextAttribute(console, previous.wAttributes);
    std::ofstream log(project / "out/test/run.log", std::ios::app);
    log << line << '\n';
}
class Report : public testing::EmptyTestEventListener {
    std::string group_;
public:
    explicit Report(std::string group) : group_(std::move(group)) {}
    void OnTestStart(const testing::TestInfo& test) override {
        status("RUN", std::string(test.test_suite_name()) + "." + test.name());
    }
    void OnTestPartResult(const testing::TestPartResult& part) override {
        if (!part.failed() && !part.skipped()) return;
        std::string message;
        if (part.file_name()) message = std::string(part.file_name()) + ":" + std::to_string(part.line_number()) + "\n";
        message += part.message();
        status("DETAIL", message);
    }
    void OnTestEnd(const testing::TestInfo& test) override {
        std::string state = "PASS";
        if (test.result()->Failed()) state = "FAIL";
        else if (test.result()->Skipped()) {
            state = "FAIL";
            if (std::string(test.test_suite_name()) == "Manual") state = "NEED MANUAL";
        }
        status(state, std::string(test.test_suite_name()) + "." + test.name()
            + " (" + std::to_string(test.result()->elapsed_time()) + " ms)");
    }
    void OnTestProgramEnd(const testing::UnitTest& unit) override {
        std::ofstream summary(output / (group_ + ".summary"));
        summary << unit.successful_test_count() << ' ' << unit.failed_test_count()
                << ' ' << unit.skipped_test_count() << '\n';
    }
};
void install_report(const std::string& group) {
    auto& listeners = testing::UnitTest::GetInstance()->listeners();
    delete listeners.Release(listeners.default_result_printer());
    listeners.Append(new Report(group));
}
}
