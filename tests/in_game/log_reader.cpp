#include "log_reader.h"
#include <sstream>
#include <stdexcept>

namespace game_test {
const std::vector<std::string>& expected_checks() {
    static const std::vector<std::string> names{
        "levels_loaded", "first_group_loaded", "quest_reset",
        "quest_begin_command", "mission_text_loaded", "quest_gives_item", "quest_removes_item"
    };
    return names;
}
std::map<std::string, bool> parse_results(const std::string& log) {
    std::map<std::string, bool> results;
    std::istringstream stream(log);
    std::string line;
    bool started = false;
    bool finished = false;
    int passed = 0;
    int failed = 0;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (finished) throw std::runtime_error("Unexpected data after LGC completion");
        if (!started) {
            if (line != "QERE|START") throw std::runtime_error("Missing LGC start marker");
            started = true;
            continue;
        }
        if (line.starts_with("QERE|DONE|")) {
            const auto expected = "QERE|DONE|passed=" + std::to_string(passed)
                + "|failed=" + std::to_string(failed);
            if (line != expected) throw std::runtime_error("LGC totals do not match results");
            finished = true;
            continue;
        }
        bool success = line.starts_with("QERE|PASS|");
        if (!success && !line.starts_with("QERE|FAIL|"))
            throw std::runtime_error("Unknown LGC result record: " + line);
        const auto name = line.substr(10);
        bool known = false;
        for (const auto& expected : expected_checks()) {
            if (name == expected) known = true;
        }
        if (!known) throw std::runtime_error("Unknown LGC check: " + name);
        if (!results.emplace(name, success).second)
            throw std::runtime_error("Duplicate LGC check: " + name);
        if (success) ++passed;
        else ++failed;
    }
    if (!started || !finished || results.size() != expected_checks().size())
        throw std::runtime_error("Incomplete LGC result set");
    return results;
}
}
