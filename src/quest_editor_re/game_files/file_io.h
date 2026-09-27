#pragma once
#include <filesystem>
#include <string>
namespace quest {
// Read raw bytes; decoding belongs to the encoding module.
std::string read_file(const std::filesystem::path& path);
}
