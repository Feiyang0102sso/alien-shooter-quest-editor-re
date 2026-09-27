#pragma once
#include <map>
#include <string>
#include <vector>

namespace game_test {
const std::vector<std::string>& expected_checks();
// Reject incomplete, duplicate and unexpected results instead of accepting a stray PASS.
std::map<std::string, bool> parse_results(const std::string& log);
}
