#include "mission_text.h"
#include "quest_document.h"
#include "../i18n/i18n.h"
#include <stdexcept>

namespace quest {
static std::string text_key(const Line& line) {
    std::string clean = trim(line.text);
    if (clean.starts_with("\xEF\xBB\xBF")) clean.erase(0, 3);
    if (clean.size() < 3 || clean.find('=') != std::string::npos) return {};
    if ((clean.front() == '<' && clean.back() == '>') || (clean.front() == '[' && clean.back() == ']')) {
        return clean.substr(1, clean.size() - 2);
    }
    return {};
}

std::string mission_text(const std::string& bytes, const std::string& key) {
    bool active = false;
    std::string result;
    const auto document = QuestDocument::parse(bytes);
    for (const auto& line : document.lines()) {
        const auto header = text_key(line);
        if (!header.empty()) {
            if (active) break;
            active = header == key;
        } else if (active) result += line.text + line.ending;
    }
    return result;
}

std::map<std::string,std::string> mission_texts(const std::string& bytes) {
    std::map<std::string,std::string> result;
    std::string* active = nullptr;
    const auto document = QuestDocument::parse(bytes);
    for (const auto& line : document.lines()) {
        const auto header = text_key(line);
        if (!header.empty()) {
            const auto [entry,inserted] = result.try_emplace(header);
            active = nullptr;
            // Match single-key reads: preview only the first block for duplicate text keys.
            if (inserted) active = &entry->second;
        } else if (active) *active += line.text+line.ending;
    }
    return result;
}

std::string set_mission_text(const std::string& bytes, const std::string& key, const std::string& text) {
    if (key.empty() || key.find_first_of("<>[]=\r\n") != std::string::npos) throw std::runtime_error(quest::i18n::narrow("errors.invalid_mission_key"));
    const auto document = QuestDocument::parse(bytes);
    std::string result;
    bool active = false;
    bool found = false;
    for (const auto& line : document.lines()) {
        const auto header = text_key(line);
        if (!header.empty()) {
            active = header == key;
            result += line.text + line.ending;
            if (active) {
                if (found) throw std::runtime_error(quest::i18n::narrow("errors.duplicate_mission_key"));
                found = true;
                if (line.ending.empty()) result += document.newline();
                result += text;
                if (!text.empty() && text.back() != '\n' && text.back() != '\r') result += document.newline();
            }
        } else if (!active) result += line.text + line.ending;
    }
    if (!found) {
        if (!result.empty() && result.back() != '\n' && result.back() != '\r') result += document.newline();
        result += "<" + key + ">" + document.newline() + text;
        if (!text.empty() && text.back() != '\n' && text.back() != '\r') result += document.newline();
    }
    return result;
}

}
