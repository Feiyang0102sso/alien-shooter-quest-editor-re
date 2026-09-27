#include "i18n.h"
#include <windows.h>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <optional>

namespace quest::i18n {
namespace {
int module_anchor;
struct Text {
    std::string bytes;
    std::wstring display;
};
struct Catalog {
    int language = 2;
    std::filesystem::path directory;
    std::map<std::string,Text> entries;
};

HMODULE current_module() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&module_anchor),&module);
    return module;
}

std::filesystem::path module_directory() {
    wchar_t path[32768]{};
    GetModuleFileNameW(current_module(),path,32768);
    return std::filesystem::path(path).parent_path();
}

std::string strip(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    return text.substr(first,text.find_last_not_of(" \t\r")-first+1);
}

std::string file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}

int configured_language(const std::filesystem::path& directory) {
    auto bytes = file_bytes(directory/L"QuestEditor.cfg");
    if (bytes.starts_with("\xEF\xBB\xBF")) bytes.erase(0,3);
    std::istringstream input(bytes);
    std::string line;
    int value = 2;
    while (std::getline(input,line)) {
        line = line.substr(0,line.find_first_of(";#"));
        const auto equals = line.find('=');
        if (equals == std::string::npos || strip(line.substr(0,equals)) != "language") continue;
        const auto number = strip(line.substr(equals+1));
        if (number == "1") value = 1;
        else if (number == "2") value = 2;
    }
    return value;
}

std::optional<Text> translated_text(const std::string& quoted) {
    if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"') return {};
    Text text;
    for (size_t index = 1; index+1 < quoted.size(); ++index) {
        char character = quoted[index];
        if (character == '"') return {};
        if (character == '\\') {
            if (++index+1 >= quoted.size()) return {};
            switch (quoted[index]) {
            case 'n': character = '\n'; break;
            case 'r': character = '\r'; break;
            case 't': character = '\t'; break;
            case '0': character = '\0'; break;
            case '\\': character = '\\'; break;
            case '"': character = '"'; break;
            default: return {};
            }
        }
        text.bytes += character;
    }
    if (text.bytes.empty()) return text;
    const int length = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.bytes.data(),static_cast<int>(text.bytes.size()),nullptr,0);
    if (!length) return {};
    text.display.resize(length);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.bytes.data(),static_cast<int>(text.bytes.size()),text.display.data(),length);
    return text;
}

void read_translations(Catalog& catalog, std::string bytes, bool overrides) {
    if (bytes.starts_with("\xEF\xBB\xBF")) bytes.erase(0,3);
    std::istringstream input(bytes);
    std::string line;
    while (std::getline(input,line)) {
        line = strip(line);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const auto key = strip(line.substr(0,equals));
        if (overrides && !catalog.entries.contains(key)) continue;
        const auto text = translated_text(strip(line.substr(equals+1)));
        // Keep the embedded translation when an external entry is missing or malformed.
        if (text) catalog.entries[key] = *text;
    }
}

Catalog load_catalog(const std::filesystem::path& directory, int language) {
    Catalog catalog;
    catalog.directory = directory;
    catalog.language = language;
    const auto module = current_module();
    const auto resource = FindResourceW(module,MAKEINTRESOURCEW(200+language),RT_RCDATA);
    const auto loaded = LoadResource(module,resource);
    const auto data = static_cast<const char*>(LockResource(loaded));
    if (!data) throw std::runtime_error("Missing embedded translations");
    read_translations(catalog,{data,SizeofResource(module,resource)},false);
    std::filesystem::path filename = L"cn.ini";
    if (language == 1) filename = L"en.ini";
    read_translations(catalog,file_bytes(directory/L"i18n"/filename),true);
    return catalog;
}

Catalog& current() {
    static Catalog catalog = load_catalog(module_directory(),configured_language(module_directory()));
    return catalog;
}

void save_language(const Catalog& catalog, int language) {
    const auto path = catalog.directory/L"QuestEditor.cfg";
    std::ifstream input(path,std::ios::binary);
    if (!input && std::filesystem::exists(path)) throw std::runtime_error(narrow("errors.language_save_failed"));
    const std::string before{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    input.close();
    std::string after;
    bool found = false;
    for (size_t start = 0; start < before.size();) {
        size_t end = before.find('\n',start);
        if (end == std::string::npos) end = before.size();
        auto line = before.substr(start,end-start);
        size_t key_start = 0;
        if (start == 0 && line.starts_with("\xEF\xBB\xBF")) key_start = 3;
        const auto equals = line.find('=',key_start);
        if (equals != std::string::npos && strip(line.substr(key_start,equals-key_start)) == "language") {
            const auto comment = line.find_first_of(";#",equals+1);
            size_t value_start = equals+1;
            size_t value_end = line.size();
            if (comment != std::string::npos) value_end = comment;
            while (value_start < value_end && (line[value_start] == ' ' || line[value_start] == '\t')) ++value_start;
            while (value_end > value_start && (line[value_end-1] == ' ' || line[value_end-1] == '\t' || line[value_end-1] == '\r')) --value_end;
            line.replace(value_start,value_end-value_start,std::to_string(language));
            found = true;
        }
        after += line;
        if (end < before.size()) after += '\n';
        start = end+1;
    }
    if (!found) {
        if (!after.empty() && after.back() != '\n') after += "\r\n";
        after += "language = "+std::to_string(language)+"\r\n";
    }
    const auto temporary = path.wstring()+L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
    const HANDLE file = CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error(narrow("errors.language_save_failed"));
    DWORD written = 0;
    const bool saved = WriteFile(file,after.data(),static_cast<DWORD>(after.size()),&written,nullptr)
        && written == after.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!saved || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error(narrow("errors.language_save_failed"));
    }
}
}

void initialize(const std::filesystem::path& directory) {
    auto location = directory;
    if (location.empty()) location = module_directory();
    current() = load_catalog(location,configured_language(location));
}

int language() { return current().language; }

void set_language(int value) {
    if (value != 1 && value != 2) return;
    auto next = load_catalog(current().directory,value);
    save_language(current(),value);
    current() = std::move(next);
}

const wchar_t* wide(const char* key) { return current().entries.at(key).display.c_str(); }
const char* narrow(const char* key) { return current().entries.at(key).bytes.c_str(); }
}
