#pragma once
#include "test_environment.h"
namespace test_support {
void status(const std::string& state, const std::string& message);
void install_report(const std::string& group);
}
