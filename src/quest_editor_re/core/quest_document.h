#pragma once

#include <optional>
#include <string>
#include <vector>

namespace quest {

// Keep original file bytes; decode only at the display interface.
struct Line {
    std::string text;
    std::string ending;
};

struct Field {
    std::string name;
    std::string value;
    size_t line = 0;
};

struct Section {
    std::string kind;
    std::string name;
    size_t begin = 0;
    size_t end = 0;
    std::vector<Field> fields;
    std::string value(const std::string& key) const;
    int number(const std::string& key, int fallback = 0) const;
};

struct QuestId {
    int level = 1;
    int group = 1;
    int index = 0;
    static std::optional<QuestId> parse(const std::string& text);
    int packed() const;
    std::string text() const;
};

struct Diagnostic {
    bool error = false;
    size_t line = 0;
    std::string message;
};

// Lossless task configuration model: parsing, field edits and validation only.
class QuestDocument {
public:
    static QuestDocument parse(const std::string& bytes);
    std::string bytes() const;
    std::vector<Section> sections() const;
    std::optional<Section> find(const std::string& kind, const std::string& name) const;
    std::string block(const Section& section) const;
    void set(const std::string& kind, const std::string& name,
             const std::string& key, const std::string& value);
    // Edit one state/action slot; ambiguous repeated slots require raw CFG editing.
    void set_condition(const std::string& name, const std::string& key,
                       const std::string& prefix, const std::string& value);
    void replace_block(const Section& section, const std::string& bytes);
    void append(const std::string& bytes);
    void remove(const Section& section);
    std::vector<Diagnostic> validate() const;
    int version() const;
    int connection_target(const Section& section, bool branch) const;
    std::string newline() const;
    const std::vector<Line>& lines() const { return lines_; }
private:
    std::vector<Line> lines_;
    // The index accelerates reads; original lines remain authoritative for saving and undo.
    mutable std::optional<std::vector<Section>> section_index_;
    const std::vector<Section>& indexed_sections() const;
};

std::string trim(const std::string& text);
std::optional<std::string> condition_tail(const std::string& value, const std::string& prefix);
std::optional<int> integer(const std::string& text);
std::optional<Field> parse_field(const Line& line, size_t index);

}
