#include "../i18n/i18n.h"
#include "edit_session.h"
#include "../game_files/path_selection.h"
#include "../game_files/file_io.h"
#include "../backup/file_backup.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <set>
#include <stdexcept>

namespace quest {

std::string EditSession::layout_bytes() const {
    std::ostringstream output;
    output << "# Quest Editor RE layout v1; not game data\n";
    for (const auto& [name, position] : layout) output << name << ' ' << position.x << ' ' << position.y << '\n';
    return output.str();
}

void EditSession::open(const std::filesystem::path& filename) {
    // Build the replacement session completely so read failures preserve unsaved edits.
    EditSession next;
    next.path = std::filesystem::absolute(game_configuration_path(filename));
    next.saved_configuration_ = read_file(next.path);
    const auto cfg_encoding = inspect_game_encoding(next.saved_configuration_);
    next.document = QuestDocument::parse(next.saved_configuration_);
    next.codepage = cfg_encoding.codepage;
    next.cfg_ansi = cfg_encoding.system_ansi;
    next.mission_path = mission_text_path(next.path);
    next.mission_existed_ = std::filesystem::exists(next.mission_path);
    if (next.mission_existed_) {
        next.mission = read_file(next.mission_path);
        next.saved_mission_ = next.mission;
        const auto mission_encoding = inspect_game_encoding(next.mission);
        next.mission_codepage = mission_encoding.codepage;
        next.mission_ansi = mission_encoding.system_ansi;
    }
    const std::filesystem::path layout_path = next.path.wstring() + L".qere-layout";
    next.layout_existed_ = std::filesystem::exists(layout_path);
    if (next.layout_existed_) {
        next.saved_layout_ = read_file(layout_path);
        std::istringstream input(next.saved_layout_);
        std::string row;
        while (std::getline(input, row)) {
            if (row.empty() || row.front() == '#') continue;
            std::istringstream values(row);
            std::string name;
            Position position;
            if (values >> name >> position.x >> position.y && std::isfinite(position.x) && std::isfinite(position.y)) {
                next.layout[name] = position;
            }
        }
    }
    // Normalize layout only for dirty-state comparison; do not create a sidecar on open.
    if (!next.layout_existed_) next.saved_layout_ = next.layout_bytes();
    next.saved_positions_ = next.layout;
    *this = std::move(next);
}

bool EditSession::dirty() const {
    if (path.empty()) return false;
    return document.bytes() != saved_configuration_ || mission != saved_mission_ || layout != saved_positions_;
}

bool EditSession::toggle_boundary(int timeline) {
    if (path.empty() || timeline <= 0) return false;
    std::vector<std::string> existing;
    for (const auto& section : document.sections()) {
        if (section.kind == "Map" && section.number("TimeLine") == timeline) existing.push_back(section.name);
    }
    checkpoint();
    if (!existing.empty()) {
        // Removing a boundary only disables its timeline; preserve rewards, unknown fields and comments.
        for (const auto& name : existing) document.set("Map",name,"TimeLine","0");
        return true;
    }
    std::string number = std::to_string(timeline+1);
    while (number.size() < 3) number.insert(number.begin(),'0');
    const std::string base = "Level"+number;
    std::string name = base;
    int suffix = 2;
    while (const auto record = document.find("Map",name)) {
        bool plain_boundary = record->number("TimeLine") == 0;
        for (const auto& field : record->fields) {
            if (field.name != "TimeLine") plain_boundary = false;
        }
        if (plain_boundary) {
            document.set("Map",name,"TimeLine",std::to_string(timeline));
            return true;
        }
        name = base+"_"+std::to_string(suffix++);
    }
    const auto ending = document.newline();
    document.append(";************"+ending+"Map="+name+ending+"TimeLine="+std::to_string(timeline)+ending);
    return true;
}

void EditSession::discard_changes() {
    document = QuestDocument::parse(saved_configuration_);
    mission = saved_mission_;
    layout = saved_positions_;
    undo_.clear();
    redo_.clear();
}

bool EditSession::save() {
    if (path.empty()) throw std::runtime_error(quest::i18n::narrow("errors.open_levels_first"));
    std::vector<FileChange> changes;
    changes.push_back({path, saved_configuration_, document.bytes(), true, quest::backup::max_copies});
    if (mission != saved_mission_) changes.push_back({mission_path, saved_mission_, mission, mission_existed_, quest::backup::max_copies});
    const std::string current_layout = layout_bytes();
    const bool layout_changed = layout != saved_positions_;
    if (layout_changed) changes.push_back({path.wstring() + L".qere-layout", saved_layout_, current_layout, layout_existed_});
    const bool backups_complete = save_files(changes);
    saved_configuration_ = document.bytes();
    saved_mission_ = mission;
    if (layout_changed) saved_layout_ = current_layout;
    saved_positions_ = layout;
    mission_existed_ = std::filesystem::exists(mission_path);
    layout_existed_ = std::filesystem::exists(path.wstring() + L".qere-layout");
    return backups_complete;
}

Snapshot EditSession::snapshot() const { return {document.bytes(), mission, layout}; }
void EditSession::restore(const Snapshot& snapshot) {
    document = QuestDocument::parse(snapshot.configuration);
    mission = snapshot.mission;
    layout = snapshot.layout;
}
void EditSession::checkpoint() {
    undo_.push_back(snapshot());
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
}
bool EditSession::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(snapshot());
    restore(undo_.back());
    undo_.pop_back();
    return true;
}
bool EditSession::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(snapshot());
    restore(redo_.back());
    redo_.pop_back();
    return true;
}

Position EditSession::position(const Section& section) const {
    const auto custom = layout.find(section.name);
    if (custom != layout.end()) return custom->second;
    const auto defaults = default_positions();
    const auto found = defaults.find(section.name);
    if (found != defaults.end()) return found->second;
    return {80.0f + section.number("TimeLine") * 206.0f,90.0f};
}

std::map<std::string, Position> EditSession::default_positions() const {
    const auto sections = document.sections();
    std::map<int,std::map<int,int>> counts;
    int last_group = 1;
    for (const auto& section : sections) {
        if (section.kind != "Quest") continue;
        const auto id = QuestId::parse(section.name);
        if (!id) continue;
        last_group = std::max(last_group,id->group);
        if (id->index != 0) ++counts[id->group][section.number("TimeLine")];
    }
    std::map<int,float> row_tops;
    float top = 90;
    for (int group = 1; group <= last_group; ++group) {
        row_tops[group] = top;
        int slots = 1;
        for (const auto& [timeline,count] : counts[group]) {
            static_cast<void>(timeline);
            slots = std::max(slots,count);
        }
        top += slots * 124.0f;
    }
    std::map<int,std::map<int,int>> used;
    std::map<std::string,Position> result;
    for (const auto& section : sections) {
        if (section.kind != "Quest") continue;
        const auto id = QuestId::parse(section.name);
        if (!id) continue;
        if (id->index == 0) {
            result[section.name] = {-126,row_tops[id->group]};
        } else {
            const int timeline = section.number("TimeLine");
            const int slot = used[id->group][timeline]++;
            result[section.name] = {80.0f + timeline * 206.0f,row_tops[id->group] + slot * 124.0f};
        }
    }
    return result;
}

std::string EditSession::create(int group, int timeline) {
    if (path.empty()) throw std::runtime_error(quest::i18n::narrow("errors.open_configuration_first"));
    if (group < 1 || group >= 50 || timeline < 0) throw std::runtime_error(quest::i18n::narrow("errors.invalid_row_timeline"));
    int last_index = -1;
    int level = 1;
    int row_count = 0;
    std::set<int> headers;
    for (const auto& section : document.sections()) {
        if (section.kind != "Quest") continue;
        const auto id = QuestId::parse(section.name);
        if (id && id->index == 0) headers.insert(id->group);
        if (id && id->group == group) {
            ++row_count;
            last_index = std::max(last_index, id->index);
            level = id->level;
        }
    }
    if (last_index >= 999) throw std::runtime_error(quest::i18n::narrow("errors.no_free_quest_ids"));
    int new_objects = 1;
    if (!headers.contains(group)) ++new_objects;
    if (row_count + new_objects >= 300) throw std::runtime_error(quest::i18n::narrow("errors.row_capacity"));
    checkpoint();
    const std::string ending = document.newline();
    // The engine enumerates headers from 1 to the group count; fill intermediate headers.
    for (int row = 1; row <= group; ++row) {
        if (headers.contains(row)) continue;
        const QuestId header{level, row, 0};
        document.append(";************" + ending + "Quest=" + header.text() + ending + "LevelNum=" + std::to_string(level) + ending + "TimeLine=0" + ending);
    }
    const QuestId id{level, group, std::max(1, last_index + 1)};
    document.append(";************" + ending + "Quest=" + id.text() + ending + "LevelNum=" + std::to_string(level) + ending + "TimeLine=" + std::to_string(timeline) + ending);
    return id.text();
}

std::string EditSession::create_between(int group, int timeline, Position position,
                                    const std::string& previous, const std::string& next) {
    // Insert into the empty cell between neighboring tasks; group all link changes into one undo step.
    EditSession draft;
    draft.path = path;
    draft.document = document;
    draft.layout = layout;
    const auto name = draft.create(group,timeline);
    draft.layout[name] = position;
    if (!previous.empty()) draft.connect(previous,name,false);
    if (!next.empty()) draft.connect(name,next,false);
    checkpoint();
    document = std::move(draft.document);
    layout = std::move(draft.layout);
    return name;
}

void EditSession::connect(const std::string& source, const std::string& target, bool branch) {
    const auto id = QuestId::parse(target);
    if (!id || source == target || !document.find("Quest", source) || !document.find("Quest", target)) throw std::runtime_error(quest::i18n::narrow("errors.invalid_connection"));
    const int source_time = document.find("Quest", source)->number("TimeLine");
    const int target_time = document.find("Quest", target)->number("TimeLine");
    if (!branch && source_time >= target_time) throw std::runtime_error(quest::i18n::narrow("errors.main_connection_direction"));
    if (branch && source_time <= target_time) throw std::runtime_error(quest::i18n::narrow("errors.branch_connection_direction"));
    std::string key = "NextQuests";
    std::string value = std::to_string(id->packed());
    if (!branch && document.version() == 0) {
        const auto origin = QuestId::parse(source);
        if (!origin || origin->group != id->group) throw std::runtime_error(quest::i18n::narrow("errors.legacy_connection_group"));
        value = std::to_string(id->index);
    }
    if (branch) {
        key = "LineQuests";
        value = "1 " + value;
    }
    checkpoint();
    document.set("Quest", source, key, value);
}

std::string EditSession::create_linked(const std::string& source, int group, int timeline,
                                   Position position, bool branch) {
    // Validate compound gestures in a temporary session; commit atomically as one undo step.
    EditSession draft;
    draft.path = path;
    draft.document = document;
    draft.layout = layout;
    const auto name = draft.create(group, timeline);
    draft.layout[name] = position;
    draft.connect(source, name, branch);
    checkpoint();
    document = std::move(draft.document);
    layout = std::move(draft.layout);
    return name;
}

void EditSession::disconnect(const std::string& source, bool branch) {
    const auto section = document.find("Quest", source);
    if (!section) return;
    std::string key = "NextQuests";
    if (branch) key = "LineQuests";
    const auto block = QuestDocument::parse(document.block(*section));
    std::string replacement;
    size_t index = 0;
    for (const auto& line : block.lines()) {
        const auto field = parse_field(line, index++);
        if (field && field->name == key) {
            const size_t comment = line.text.find(';');
            if (comment != std::string::npos) replacement += line.text.substr(comment) + line.ending;
        } else replacement += line.text + line.ending;
    }
    checkpoint();
    document.replace_block(*section, replacement);
}

void EditSession::erase(const std::string& name) {
    const auto id = QuestId::parse(name);
    if (!id || id->index == 0) throw std::runtime_error(quest::i18n::narrow("errors.protected_header"));
    const auto section = document.find("Quest", name);
    if (!section) return;
    checkpoint();
    document.remove(*section);
    layout.erase(name);
    // External script references are unknown: keep IDs stable and diagnose dangling references.
}

void EditSession::erase_many(const std::vector<std::string>& names) {
    EditSession draft;
    draft.document = document;
    draft.layout = layout;
    for (const auto& name : names) {
        const auto id = QuestId::parse(name);
        if (id && id->index != 0) draft.erase(name);
    }
    if (draft.document.bytes() == document.bytes()) return;
    checkpoint();
    document = std::move(draft.document);
    layout = std::move(draft.layout);
}

}
