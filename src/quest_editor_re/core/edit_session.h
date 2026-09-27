#pragma once
#include "quest_document.h"
#include "../encoding/text_encoding.h"
#include "mission_text.h"
#include <filesystem>
#include <map>

namespace quest {

struct Position {
    float x = 0;
    float y = 0;
    bool operator==(const Position&) const = default;
};

struct Snapshot {
    std::string configuration;
    std::string mission;
    std::map<std::string, Position> layout;
};

// Editing state coordinates task data, mission text, layout, saving and undo/redo.
// History stores raw text snapshots; small files favor simple, verifiable snapshots.
class EditSession {
public:
    QuestDocument document;
    std::string mission;
    std::filesystem::path path;
    std::filesystem::path mission_path;
    std::map<std::string, Position> layout;
    unsigned codepage = default_game_codepage();
    unsigned mission_codepage = default_game_codepage();
    bool cfg_ansi = true;
    bool mission_ansi = true;

    void open(const std::filesystem::path& filename);
    bool save();
    void discard_changes();
    bool dirty() const;
    void checkpoint();
    bool undo();
    bool redo();
    Position position(const Section& section) const;
    std::map<std::string, Position> default_positions() const;
    std::string create(int group, int timeline);
    std::string create_between(int group, int timeline, Position position,
                               const std::string& previous, const std::string& next);
    std::string create_linked(const std::string& source, int group, int timeline,
                              Position position, bool branch);
    void connect(const std::string& source, const std::string& target, bool branch);
    void disconnect(const std::string& source, bool branch);
    bool toggle_boundary(int timeline);
    void erase(const std::string& name);
    void erase_many(const std::vector<std::string>& names);
    Snapshot snapshot() const;
    void restore(const Snapshot& snapshot);
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
private:
    std::string saved_configuration_;
    std::string saved_mission_;
    std::string saved_layout_;
    std::map<std::string, Position> saved_positions_;
    bool mission_existed_ = false;
    bool layout_existed_ = false;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    std::string layout_bytes() const;
};


}
