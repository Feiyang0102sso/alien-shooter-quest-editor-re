#pragma once
#include <string>

namespace quest {
// Game files use legacy encodings; a system UTF-8 ACP falls back to CP1252.
unsigned default_game_codepage();
std::wstring decode(const std::string& bytes, unsigned codepage);
std::string encode(const std::wstring& text, unsigned codepage);
// Detect CP1251/GBK heuristically; keep the caller's fallback for ASCII or weak evidence without changing bytes.
unsigned detect_game_encoding(const std::string& bytes, unsigned fallback);
// Require lossless original bytes before whole-block edits to protect unchanged malformed text.
std::string encode_edit(const std::string& original, const std::wstring& edited, unsigned codepage);

struct GameEncoding {
    unsigned codepage;
    bool system_ansi;
};
// Validate the legacy game format and independently choose each file's display encoding.
GameEncoding inspect_game_encoding(const std::string& bytes);
}
