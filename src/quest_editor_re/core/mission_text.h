#pragma once
#include <map>
#include <string>
namespace quest {
// Preserve mission text blocks and their original byte encoding.
std::string mission_text(const std::string& bytes, const std::string& key);
std::map<std::string,std::string> mission_texts(const std::string& bytes);
std::string set_mission_text(const std::string& bytes, const std::string& key, const std::string& text);

}
