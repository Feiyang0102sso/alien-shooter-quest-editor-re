#pragma once
// Shared paths and isolated mock files. Assets are never modified by tests.
#include "../../src/quest_editor_re/core/edit_session.h"
#include "../../src/quest_editor_re/game_files/path_selection.h"
#include "../../src/quest_editor_re/i18n/i18n.h"
#include "../../src/quest_editor_re/game_files/file_io.h"
#include "../../src/quest_editor_re/backup/file_backup.h"
#include <gtest/gtest.h>
#include <windows.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

namespace test_support {
extern std::filesystem::path project;
extern std::filesystem::path game;
extern std::filesystem::path output;
void ensure(bool condition, const std::string& message);
void write(const std::filesystem::path& path, const std::string& bytes);
std::string asset(const std::filesystem::path& relative);
void create_samples(const std::filesystem::path& samples);

// Every case owns a fresh directory, even when cases run in a different order.
class Files : public testing::Test {
protected:
    std::filesystem::path workspace;
    std::filesystem::path original_directory;
    std::filesystem::path dll;
    std::string fixture;
    void SetUp() override;
    void TearDown() override;
};
}
using test_support::write;
using test_support::ensure;
