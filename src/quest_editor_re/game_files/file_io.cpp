#include "file_io.h"
#include "../i18n/i18n.h"
#include <fstream>
#include <stdexcept>

namespace quest {
std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error(quest::i18n::narrow("errors.open_file_prefix") + path.filename().string());
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

}
