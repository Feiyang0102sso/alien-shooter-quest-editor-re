#include "../i18n/i18n.h"
#include "quest_document.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace quest {

std::string trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::optional<int> integer(const std::string& text) {
    const std::string clean = trim(text);
    int value = 0;
    const auto result = std::from_chars(clean.data(), clean.data() + clean.size(), value);
    if (result.ec != std::errc() || result.ptr != clean.data() + clean.size()) return {};
    return value;
}

std::optional<Field> parse_field(const Line& line, size_t index) {
    std::string text = line.text;
    if (index == 0 && text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
    const size_t comment = text.find(';');
    if (comment != std::string::npos) text.resize(comment);
    const size_t equal = text.find('=');
    if (equal == std::string::npos) return {};
    Field field{trim(text.substr(0, equal)), trim(text.substr(equal + 1)), index};
    if (field.name.empty()) return {};
    return field;
}

std::string Section::value(const std::string& key) const {
    for (auto item = fields.rbegin(); item != fields.rend(); ++item) {
        if (item->name == key) return item->value;
    }
    return {};
}

int Section::number(const std::string& key, int fallback) const {
    const auto content = value(key);
    if (content.empty()) return fallback;
    // The engine uses atoi; accept trailing text but keep validation warnings.
    return std::atoi(content.c_str());
}

std::optional<QuestId> QuestId::parse(const std::string& text) {
    const size_t first = text.find('_');
    if (first == std::string::npos) return {};
    const size_t second = text.find('_', first + 1);
    if (second == std::string::npos) return {};
    const auto level = integer(text.substr(0, first));
    const auto group = integer(text.substr(first + 1, second - first - 1));
    const auto index = integer(text.substr(second + 1));
    if (!level || !group || !index) return {};
    if (*level < 0 || *group < 1 || *group > 999 || *index < 0 || *index > 999) return {};
    return QuestId{*level, *group, *index};
}

int QuestId::packed() const { return group * 1000 + index; }
std::string QuestId::text() const {
    return std::to_string(level) + "_" + std::to_string(group) + "_" + std::to_string(index);
}

QuestDocument QuestDocument::parse(const std::string& bytes) {
    QuestDocument document;
    size_t start = 0;
    while (start < bytes.size()) {
        size_t end = bytes.find_first_of("\r\n", start);
        if (end == std::string::npos) {
            document.lines_.push_back({bytes.substr(start), ""});
            break;
        }
        size_t next = end + 1;
        if (bytes[end] == '\r' && next < bytes.size() && bytes[next] == '\n') ++next;
        document.lines_.push_back({bytes.substr(start, end - start), bytes.substr(end, next - end)});
        start = next;
    }
    return document;
}

std::string QuestDocument::bytes() const {
    std::string result;
    for (const auto& line : lines_) result += line.text + line.ending;
    return result;
}

std::vector<Section> QuestDocument::sections() const {
    return indexed_sections();
}

const std::vector<Section>& QuestDocument::indexed_sections() const {
    if (section_index_) return *section_index_;
    std::vector<Section> result;
    for (size_t index = 0; index < lines_.size(); ++index) {
        const auto field = parse_field(lines_[index], index);
        if (!field) continue;
        if (field->name == "Quest" || field->name == "Map") {
            if (!result.empty()) result.back().end = index;
            result.push_back({field->name, field->value, index, lines_.size(), {}});
        } else if (!result.empty()) {
            // The original parser feeds each pipe-delimited segment to the same field handler in order.
            size_t start = 0;
            for (;;) {
                const size_t separator = field->value.find('|', start);
                result.back().fields.push_back({field->name,
                    trim(field->value.substr(start, separator - start)), index});
                if (separator == std::string::npos) break;
                start = separator + 1;
            }
        }
    }
    section_index_ = std::move(result);
    return *section_index_;
}

std::optional<Section> QuestDocument::find(const std::string& kind, const std::string& name) const {
    for (const auto& section : indexed_sections()) {
        if (section.kind == kind && section.name == name) return section;
    }
    return {};
}

std::string QuestDocument::block(const Section& section) const {
    std::string result;
    for (size_t index = section.begin; index < section.end; ++index) {
        result += lines_.at(index).text + lines_.at(index).ending;
    }
    return result;
}

std::string QuestDocument::newline() const {
    for (const auto& line : lines_) {
        if (!line.ending.empty()) return line.ending;
    }
    return "\r\n";
}

int QuestDocument::version() const {
    int result = 0;
    for (size_t index = 0; index < lines_.size(); ++index) {
        const auto field = parse_field(lines_[index], index);
        if (!field) continue;
        if (field->name == "Quest" || field->name == "Map") break;
        if (field->name == "FileVer") result = integer(field->value).value_or(0);
    }
    return result;
}

int QuestDocument::connection_target(const Section& section, bool branch) const {
    std::string key = "NextQuests";
    if (branch) key = "LineQuests";
    std::istringstream input(section.value(key));
    int target = 0;
    if (branch) {
        int type = 0;
        if (!(input >> type) || type != 1) return 0;
    }
    if (!(input >> target)) return 0;
    if (!branch && version() == 0) {
        const auto owner = QuestId::parse(section.name);
        if (owner) target += owner->group * 1000;
    }
    return target;
}

void QuestDocument::set(const std::string& kind, const std::string& name,
                   const std::string& key, const std::string& value) {
    if (key == "Quest" || key == "Map" || key.find_first_of("=;\r\n") != std::string::npos
        || key.empty() || value.find_first_of("\r\n") != std::string::npos) {
        throw std::runtime_error(quest::i18n::narrow("errors.invalid_field"));
    }
    const auto section = find(kind, name);
    if (!section) throw std::runtime_error(quest::i18n::narrow("errors.section_not_found"));
    for (auto field = section->fields.rbegin(); field != section->fields.rend(); ++field) {
        if (field->name != key) continue;
        section_index_.reset();
        auto& line = lines_.at(field->line);
        const size_t equal = line.text.find('=');
        size_t start = equal + 1;
        while (start < line.text.size() && (line.text[start] == ' ' || line.text[start] == '\t')) ++start;
        size_t end = line.text.find(';', start);
        if (end == std::string::npos) end = line.text.size();
        while (end > start && (line.text[end - 1] == ' ' || line.text[end - 1] == '\t')) --end;
        line.text.replace(start, end - start, value);
        return;
    }
    size_t insertion = section->end;
    // Leave trailing separator comments with the next block so new fields stay above them.
    while (insertion > section->begin + 1) {
        const auto clean = trim(lines_[insertion - 1].text);
        if (!clean.empty() && clean.front() != ';') break;
        --insertion;
    }
    const std::string ending = newline();
    section_index_.reset();
    if (insertion > 0 && lines_[insertion - 1].ending.empty()) lines_[insertion - 1].ending = ending;
    lines_.insert(lines_.begin() + insertion, Line{key + "=" + value, ending});
}

std::optional<std::string> condition_tail(const std::string& value, const std::string& prefix) {
    std::istringstream input(value), expected(prefix);
    std::string token, actual;
    while (expected >> token) {
        if (!(input >> actual) || actual != token) return {};
    }
    std::string tail;
    std::getline(input, tail);
    return trim(tail);
}

void QuestDocument::set_condition(const std::string& name, const std::string& key,
                             const std::string& prefix, const std::string& value) {
    if ((key != "ChangeStateItem" && key != "SetFlagmanCoord") || prefix.empty()
        || value.find_first_of(";|\r\n") != std::string::npos) {
        throw std::runtime_error(quest::i18n::narrow("errors.invalid_condition"));
    }
    const auto section = find("Quest", name);
    if (!section) throw std::runtime_error(quest::i18n::narrow("errors.quest_not_found"));
    size_t matched_line = 0, matched_start = 0, matched_end = 0, count = 0;
    for (size_t index = section->begin + 1; index < section->end; ++index) {
        const auto field = parse_field(lines_[index], index);
        if (!field || field->name != key) continue;
        const auto& text = lines_[index].text;
        size_t end = text.find(';');
        if (end == std::string::npos) end = text.size();
        size_t start = text.find('=') + 1;
        while (start <= end) {
            size_t stop = text.find('|', start);
            if (stop == std::string::npos || stop > end) stop = end;
            if (condition_tail(text.substr(start, stop-start), prefix)) {
                ++count; matched_line = index; matched_start = start; matched_end = stop;
            }
            if (stop == end) break;
            start = stop + 1;
        }
    }
    if (count > 1) throw std::runtime_error(quest::i18n::narrow("errors.multiple_conditions"));
    if (!count) {
        if (value.empty()) return;
        section_index_.reset();
        // Do not use set: it would overwrite other state/action slots of this field.
        size_t insertion = section->begin + 1;
        const auto ending = newline();
        if (lines_[section->begin].ending.empty()) lines_[section->begin].ending = ending;
        lines_.insert(lines_.begin()+insertion, Line{key+"="+prefix+" "+value, ending});
        return;
    }
    section_index_.reset();
    auto& text = lines_[matched_line].text;
    if (!value.empty()) {
        while (matched_start < matched_end && (text[matched_start] == ' ' || text[matched_start] == '\t')) ++matched_start;
        while (matched_end > matched_start && (text[matched_end-1] == ' ' || text[matched_end-1] == '\t')) --matched_end;
        text.replace(matched_start, matched_end-matched_start, prefix+" "+value);
    } else if (matched_end < text.size() && text[matched_end] == '|') {
        text.erase(matched_start, matched_end-matched_start+1);
    } else if (matched_start > 0 && text[matched_start-1] == '|') {
        text.erase(matched_start-1, matched_end-matched_start+1);
    } else {
        const auto comment = text.find(';');
        if (comment != std::string::npos) text.erase(0, comment);
        else lines_.erase(lines_.begin()+matched_line);
    }
}

void QuestDocument::replace_block(const Section& section, const std::string& bytes) {
    auto replacement = parse(bytes);
    const auto blocks = replacement.sections();
    if (blocks.size() != 1 || blocks.front().kind != section.kind || blocks.front().name != section.name) {
        throw std::runtime_error(quest::i18n::narrow("errors.preserve_section_header"));
    }
    if (!replacement.lines_.empty() && replacement.lines_.back().ending.empty() && section.end < lines_.size()) {
        replacement.lines_.back().ending = newline();
    }
    section_index_.reset();
    lines_.erase(lines_.begin() + section.begin, lines_.begin() + section.end);
    lines_.insert(lines_.begin() + section.begin, replacement.lines_.begin(), replacement.lines_.end());
}

void QuestDocument::append(const std::string& bytes) {
    const auto addition = parse(bytes);
    section_index_.reset();
    if (!lines_.empty() && lines_.back().ending.empty()) lines_.back().ending = newline();
    lines_.insert(lines_.end(), addition.lines_.begin(), addition.lines_.end());
}

void QuestDocument::remove(const Section& section) {
    section_index_.reset();
    // Preserve user comment lines independently, even when their task is deleted.
    for (size_t index = section.end; index > section.begin; --index) {
        if (!parse_field(lines_[index - 1], index - 1)) continue;
        auto& line = lines_[index - 1];
        const size_t comment = line.text.find(';');
        if (comment != std::string::npos) line.text = line.text.substr(comment);
        else lines_.erase(lines_.begin() + index - 1);
    }
}

std::vector<Diagnostic> QuestDocument::validate() const {
    std::vector<Diagnostic> result;
    std::set<std::string> names;
    std::set<int> packed_ids;
    std::set<int> row_headers;
    std::map<int,size_t> row_counts;
    std::map<int,size_t> row_lines;
    const auto blocks = sections();
    const int file_version = version();
    for (size_t index = 0; index < lines_.size(); ++index) {
        if (lines_[index].text.size() >= 999) {
            result.push_back({false,index + 1,quest::i18n::narrow("diagnostics.long_line")});
        }
    }
    for (const auto& block : blocks) {
        if (!names.insert(block.kind + "=" + block.name).second) {
            result.push_back({true, block.begin + 1, quest::i18n::narrow("diagnostics.duplicate_section") + block.name});
        }
        if (block.kind == "Quest") {
            const auto id = QuestId::parse(block.name);
            if (!id) {
                result.push_back({true, block.begin + 1, quest::i18n::narrow("diagnostics.invalid_quest_id") + block.name});
            } else {
                ++row_counts[id->group];
                row_lines.try_emplace(id->group,block.begin + 1);
                if (id->index == 0) row_headers.insert(id->group);
                if (!packed_ids.insert(id->packed()).second) {
                    result.push_back({true, block.begin + 1, quest::i18n::narrow("diagnostics.reused_packed_id") + block.name});
                }
                if (id->group >= 50) result.push_back({false, block.begin + 1, quest::i18n::narrow("diagnostics.too_many_rows")});
            }
        }
        if (!integer(block.value("TimeLine"))) result.push_back({false, block.begin + 1, quest::i18n::narrow("diagnostics.missing_timeline")});
        if (block.number("TimeLine") < 0) result.push_back({true, block.begin + 1, quest::i18n::narrow("diagnostics.negative_timeline")});
        if (block.number("BeginNext") && block.number("UnactualNext")) {
            result.push_back({false, block.begin + 1, quest::i18n::narrow("diagnostics.conflicting_level_flags")});
        }
    }
    for (const auto& [group,count] : row_counts) {
        if (!row_headers.contains(group)) result.push_back({false,row_lines.at(group),quest::i18n::narrow("diagnostics.missing_row_header") + std::to_string(group)});
        if (count >= 300) result.push_back({false,row_lines.at(group),quest::i18n::narrow("diagnostics.too_many_row_objects")});
    }
    for (const auto& block : blocks) {
        for (const auto& field : block.fields) {
            if (field.name == "NextQuests" || field.name == "LineQuests" || field.name == "StartingQuests" || field.name == "EndQuests") {
                std::istringstream input(field.value);
                int target = 0;
                if (field.name == "LineQuests") {
                    int type = 0;
                    if (!(input >> type) || type != 1) result.push_back({true, field.line + 1, quest::i18n::narrow("diagnostics.invalid_line_type")});
                }
                bool has_target = false;
                while (input >> target) {
                    has_target = true;
                    int packed = target;
                    if (field.name == "StartingQuests" || field.name == "EndQuests") {
                        const auto owner = QuestId::parse(block.name);
                        if (owner) packed = owner->group * 1000 + target;
                    }
                    if (field.name == "NextQuests" && file_version == 0) {
                        const auto owner = QuestId::parse(block.name);
                        if (owner) packed = owner->group * 1000 + target;
                    }
                    if (!packed_ids.contains(packed)) result.push_back({false, field.line + 1, quest::i18n::narrow("diagnostics.unresolved_quest") + std::to_string(target)});
                    if (field.name == "NextQuests" || field.name == "LineQuests") break;
                }
                if (!has_target) result.push_back({false, field.line + 1, quest::i18n::narrow("diagnostics.noncanonical_reference")});
            }
            if (field.name == "ChangeStateItem" || field.name == "SetFlagmanCoord") {
                std::istringstream input(field.value);
                std::vector<std::string> tokens;
                std::string token;
                while (input >> token) tokens.push_back(token);
                const std::set<std::string> states{"QS_NOTBEGUN", "QS_BEGUN", "QS_COMPLETED", "QS_FAILED"};
                bool valid = !tokens.empty() && states.contains(tokens.front());
                if (field.name == "ChangeStateItem") {
                    const std::set<std::string> actions{"CSI_TAKE", "CSI_DEATH", "CSI_BIRTH", "CSI_HAVE"};
                    valid = valid && tokens.size() == 3;
                    if (tokens.size() >= 2) valid = valid && actions.contains(tokens[1]);
                } else {
                    valid = valid && tokens.size() == 5;
                    for (size_t index = 1; index < tokens.size(); ++index) {
                        if (!integer(tokens[index])) result.push_back({false, field.line + 1, quest::i18n::narrow("diagnostics.truncated_coordinate") + tokens[index]});
                    }
                }
                if (!valid) result.push_back({true, field.line + 1, quest::i18n::narrow("diagnostics.invalid_parameters_prefix") + field.name + quest::i18n::narrow("diagnostics.invalid_parameters_suffix")});
            }
        }
    }
    return result;
}

}
