#include "../i18n/i18n.h"
#include "quest_properties.h"
#include "controls.h"
#include "../game_files/file_io.h"
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <cwctype>

namespace quest {
namespace {
constexpr wchar_t kClass[] = L"QuestEditorRE.Properties.v1";
constexpr int kTabs = 200;
constexpr int kRaw = 201;
constexpr int kBody = 240;
constexpr int kHeading = 241;
constexpr int kComment = 242;
constexpr int kGiveItems = 232;
constexpr int kRemoveItems = 233;
constexpr int kGiveFilter = 234;
constexpr int kRemoveFilter = 235;
constexpr UINT kAppendSelectedItem = WM_APP + 1;

// Display one ID per line, but keep the engine's space-separated file format.
std::wstring item_text(const std::wstring& text, const std::wstring& separator) {
    std::wstring result;
    std::wstring item;
    for (wchar_t character : text + L" ") {
        if (iswspace(character) || character == L'^') {
            if (item.empty()) continue;
            if (!result.empty()) result += separator;
            result += item;
            item.clear();
        } else item += character;
    }
    return result;
}

LRESULT CALLBACK item_edit_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                     UINT_PTR subclass, DWORD_PTR) {
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, item_edit_procedure, subclass);
    if (message == WM_KEYDOWN && wparam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        SendMessageW(window, EM_SETSEL, 0, -1);
        return 0;
    }
    const auto result = DefSubclassProc(window, message, wparam, lparam);
    if (message == WM_PASTE) {
        const auto text = control_text(window);
        const auto normalized = item_text(text, L"\r\n");
        if (text != normalized) {
            SendMessageW(window, EM_SETSEL, 0, -1);
            SendMessageW(window, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(normalized.c_str()));
        }
    }
    return result;
}

struct Control {
    HWND window = nullptr;
    RECT bounds{};
    unsigned pages = 0;
};
struct Binding {
    HWND window = nullptr;
    std::string key;
    std::string prefix;
    std::wstring original;
    bool checkbox = false;
    bool checked = false;
};
struct Coordinates {
    std::string state;
    std::array<HWND,4> fields{};
    std::array<std::wstring,4> original;
};

struct Properties {
    HWND window = nullptr;
    HWND tabs = nullptr;
    HWND raw = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
    int page = 0;
    bool accepted = false;
    EditSession draft;
    Section current_section;
    std::string kind, name;
    std::wstring original_raw;
    std::vector<Control> controls;
    std::vector<Binding> bindings;
    std::vector<Coordinates> coordinates;
    std::vector<std::wstring> item_names;
    bool filtering_items = false;

    ~Properties() { if (font) DeleteObject(font); }

    HWND add(const wchar_t* type, const std::wstring& caption, int id, DWORD style,
             unsigned pages, int x, int y, int width, int height) {
        HWND control = child(window,type,caption.c_str(),style,id,font);
        controls.push_back({control,{x,y,x+width,y+height},pages});
        return control;
    }

    void label(const wchar_t* text, unsigned pages, int x, int y, int width) {
        add(L"STATIC",text,0,0,pages,x,y,width,21);
    }

    void field(const char* key, const wchar_t* caption, int id, unsigned pages,
               int x, int y, int width, int height = 25, const std::string& prefix = {}) {
        const auto& section = current_section;
        std::string value = section.value(key);
        size_t matches = 0;
        if (!prefix.empty()) {
            value.clear();
            for (const auto& item : section.fields) {
                if (item.name != key) continue;
                const auto tail = condition_tail(item.value,prefix);
                if (tail) { value = *tail; ++matches; }
            }
        }
        label(caption,pages,x,y,width);
        DWORD style = WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
        if (height > 25) style = WS_BORDER | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN;
        if (id == kGiveItems || id == kRemoveItems) style |= ES_AUTOHSCROLL | WS_HSCROLL;
        if (matches > 1) style |= ES_READONLY;
        auto original = decode(value,draft.codepage);
        if (id == kGiveItems || id == kRemoveItems) original = item_text(original, L"\r\n");
        if (matches > 1) original = quest::i18n::wide("properties.multiple_conditions") + original;
        HWND edit = add(L"EDIT",original,id,style,pages,x,y+21,width,height);
        SendMessageW(edit,EM_SETLIMITTEXT,1024*1024,0);
        bindings.push_back({edit,key,prefix,original});
    }

    void load_items() {
        const auto path = draft.path.parent_path() / L"Weapon.cfg";
        if (!std::filesystem::exists(path)) return;
        std::istringstream input(read_file(path));
        std::string line;
        while (std::getline(input, line)) {
            const auto comment = line.find(';');
            if (comment != std::string::npos) line.resize(comment);
            const auto equals = line.find('=');
            if (equals == std::string::npos) continue;
            const auto key = trim(line.substr(0, equals));
            if (key != "Weapon" && key != "Ammo" && key != "Armor"
                && key != "Implant" && key != "Equip" && key != "Objects") continue;
            std::string item_id;
            std::istringstream(line.substr(equals + 1)) >> item_id;
            if (!item_id.empty()) item_names.push_back(decode(item_id, draft.codepage));
        }
        std::sort(item_names.begin(), item_names.end());
        item_names.erase(std::unique(item_names.begin(), item_names.end()), item_names.end());
    }

    void filter_items(HWND combo, bool show) {
        if (filtering_items) return;
        filtering_items = true;
        const auto query = control_text(combo);
        const auto selection = SendMessageW(combo, CB_GETEDITSEL, 0, 0);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (const auto& item_id : item_names) {
            // Compare ordinal IDs case-insensitively without changing the saved spelling.
            bool matches = query.empty();
            for (size_t offset = 0; !matches && offset + query.size() <= item_id.size(); ++offset) {
                matches = CompareStringOrdinal(item_id.data() + offset, static_cast<int>(query.size()),
                    query.data(), static_cast<int>(query.size()), TRUE) == CSTR_EQUAL;
            }
            if (matches) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item_id.c_str()));
        }
        if (show) SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
        SetWindowTextW(combo, query.c_str());
        SendMessageW(combo, CB_SETEDITSEL, 0, selection);
        filtering_items = false;
    }

    void item_column(const char* key, const wchar_t* caption, int edit_id, int combo_id, int x) {
        label(caption, 4, x, 130, 270);
        label(quest::i18n::wide("properties.item_filter"), 4, x, 155, 270);
        HWND combo = add(L"COMBOBOX", L"", combo_id, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
            4, x, 179, 270, 230);
        SendMessageW(combo, CB_SETMINVISIBLE, 10, 0);
        COMBOBOXINFO info{sizeof(info)};
        GetComboBoxInfo(combo, &info);
        SendMessageW(info.hwndItem, EM_SETCUEBANNER, FALSE,
            reinterpret_cast<LPARAM>(quest::i18n::wide("properties.item_filter")));
        filter_items(combo, false);
        EnableWindow(combo, !item_names.empty());
        field(key, quest::i18n::wide("properties.item_list"), edit_id, 4, x, 219, 270, 262);
        SetWindowSubclass(GetDlgItem(window, edit_id), item_edit_procedure, 1, 0);
    }

    void append_item(int combo_id) {
        HWND combo = GetDlgItem(window, combo_id);
        auto index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (index == CB_ERR) index = SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
            reinterpret_cast<LPARAM>(control_text(combo).c_str()));
        if (index == CB_ERR && SendMessageW(combo, CB_GETCOUNT, 0, 0) == 1) index = 0;
        if (index == CB_ERR) { MessageBeep(MB_OK); return; }
        const auto length = SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
        std::wstring item_id(static_cast<size_t>(length) + 1, L'\0');
        SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(item_id.data()));
        item_id.resize(static_cast<size_t>(length));
        int edit_id = kGiveItems;
        if (combo_id == kRemoveFilter) edit_id = kRemoveItems;
        HWND edit = GetDlgItem(window, edit_id);
        auto text = item_text(control_text(edit), L"\r\n");
        if (!text.empty()) text += L"\r\n";
        text += item_id;
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
        SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
        SetWindowTextW(combo, L"");
        filter_items(combo, false);
        SetFocus(combo);
    }

    int focused_item_filter() const {
        const HWND focus = GetFocus();
        for (int id : {kGiveFilter, kRemoveFilter}) {
            HWND combo = GetDlgItem(window, id);
            if (combo && (focus == combo || IsChild(combo, focus))) return id;
        }
        return 0;
    }

    void check(const char* key, const wchar_t* caption, int id, int y) {
        HWND button = add(L"BUTTON",caption,id,BS_AUTOCHECKBOX | WS_TABSTOP,3,330,y,255,25);
        const bool checked = current_section.number(key) != 0;
        SendMessageW(button,BM_SETCHECK,checked ? BST_CHECKED : BST_UNCHECKED,0);
        bindings.push_back({button,key,{}, {},true,checked});
    }

    void coordinate_fields(const std::string& state, unsigned pages, int first_id) {
        Coordinates group;
        group.state = state;
        std::string value;
        size_t matches = 0;
        const auto& section = current_section;
        for (const auto& item : section.fields) {
            if (item.name != "SetFlagmanCoord") continue;
            const auto tail = condition_tail(item.value,state);
            if (tail) { value = *tail; ++matches; }
        }
        std::istringstream input(value);
        int count = 0;
        std::string token;
        while (input >> token) {
            if (count < 4) group.original[count] = decode(token,draft.codepage);
            ++count;
        }
        const bool complex = matches > 1 || (matches && count != 4);
        const wchar_t* caption = quest::i18n::wide("properties.coordinates");
        if (complex) caption = quest::i18n::wide("properties.complex_coordinates");
        label(caption,pages,14,43,300);
        const wchar_t* names[] = {quest::i18n::wide("properties.coordinate_x"),quest::i18n::wide("properties.coordinate_y"),quest::i18n::wide("properties.coordinate_z"),quest::i18n::wide("properties.coordinate_radius")};
        for (int index = 0; index < 4; ++index) {
            label(names[index],pages,14+index*76,64,70);
            DWORD style = WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
            if (complex) style |= ES_READONLY;
            group.fields[index] = add(L"EDIT",group.original[index],first_id+index,style,pages,14+index*76,83,70,25);
        }
        coordinates.push_back(group);
    }

    std::string heading_key() const {
        std::string key;
        std::istringstream(current_section.value("Coment2")) >> key;
        return key;
    }

    void text_field(int id, const wchar_t* caption, int y, int height) {
        std::string bytes;
        unsigned codepage = draft.mission_codepage;
        if (id == kBody) bytes = mission_text(draft.mission,name);
        else if (draft.document.version() <= 1) {
            bytes = current_section.value("Coment2");
            codepage = draft.codepage;
        } else bytes = mission_text(draft.mission,heading_key());
        const auto original = decode(bytes,codepage);
        label(caption,3,14,y,570);
        HWND edit = add(L"EDIT",original,id,WS_BORDER | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
            3,14,y+21,570,height);
        SendMessageW(edit,EM_SETLIMITTEXT,1024*1024,0);
        bindings.push_back({edit,{}, {},original});
    }

    void initialize() {
        current_section = *draft.document.find(kind,name);
        tabs = add(WC_TABCONTROLW,L"",kTabs,WS_TABSTOP,15,8,8,584,30);
        for (const wchar_t* caption : {quest::i18n::wide("properties.begin_tab"),quest::i18n::wide("properties.fail_tab"),quest::i18n::wide("properties.bonus_tab"),quest::i18n::wide("properties.raw_tab")}) {
            TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(caption);
            SendMessageW(tabs,TCM_INSERTITEMW,TabCtrl_GetItemCount(tabs),reinterpret_cast<LPARAM>(&item));
        }
        if (kind == "Quest") {
            for (int state = 0; state < 2; ++state) {
                const unsigned pages = 1u << state;
                std::string prefix = "QS_BEGUN";
                if (state) prefix = "QS_FAILED";
                coordinate_fields(prefix,pages,260+state*4);
                field("ChangeStateItem",quest::i18n::wide("properties.take_item"),211+state*10,pages,14,120,300,25,prefix+" CSI_TAKE");
                field("ChangeStateItem",quest::i18n::wide("properties.death_item"),212+state*10,pages,14,179,300,25,prefix+" CSI_DEATH");
                field("ChangeStateItem",quest::i18n::wide("properties.birth_item"),213+state*10,pages,14,238,300,25,prefix+" CSI_BIRTH");
                field("ChangeStateItem",quest::i18n::wide("properties.have_item"),214+state*10,pages,14,297,300,25,prefix+" CSI_HAVE");
            }
            check("Unneeded",quest::i18n::wide("properties.instant_complete"),250,52);
            check("BeginNext",quest::i18n::wide("properties.complete_next_level"),251,78);
            check("UnactualNext",quest::i18n::wide("properties.unactual_next_level"),252,104);
            check("Accomplished",quest::i18n::wide("properties.accomplished"),253,130);
            check("WinOnBegin",quest::i18n::wide("properties.win_on_begin"),254,156);
            field("SndBegun",quest::i18n::wide("properties.begin_sound"),255,3,330,194,120);
            field("SndComplete",quest::i18n::wide("properties.complete_sound"),256,3,464,194,120);
            field("DialogNumMAIN",quest::i18n::wide("properties.dialog_number"),257,3,330,253,254);
            label(quest::i18n::wide("properties.shared_hint"),3,330,314,254);
            text_field(kHeading,quest::i18n::wide("properties.heading_text"),350,56);
            text_field(kBody,quest::i18n::wide("properties.body_text"),433,65);
            field("Comment",quest::i18n::wide("properties.comment"),kComment,3,14,527,570);
            field("AddMoneyMAIN",quest::i18n::wide("properties.money"),230,4,14,60,270);
            field("AddExperienceMAIN",quest::i18n::wide("properties.experience"),231,4,314,60,270);
            load_items();
            item_column("AddItemMAIN", quest::i18n::wide("properties.add_items"), kGiveItems, kGiveFilter, 14);
            item_column("RemItem", quest::i18n::wide("properties.remove_items"), kRemoveItems, kRemoveFilter, 314);
            const wchar_t* hint = quest::i18n::wide("properties.items_hint");
            if (item_names.empty()) hint = quest::i18n::wide("properties.items_missing");
            add(L"STATIC", hint, 238, 0, 4, 14, 518, 570, 48);
            label(quest::i18n::wide("properties.advanced_hint"),4,14,570,570);
        } else {
            page = 3;
            TabCtrl_SetCurSel(tabs,page);
            EnableWindow(tabs,FALSE);
        }
        original_raw = decode(draft.document.block(current_section),draft.codepage);
        raw = add(L"EDIT",original_raw,kRaw,WS_BORDER | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN,
            8,14,55,570,495);
        SendMessageW(raw,EM_SETLIMITTEXT,1024*1024,0);
        label(quest::i18n::wide("properties.raw_hint"),8,14,560,570);
        add(L"BUTTON",quest::i18n::wide("common.ok"),IDOK,WS_TABSTOP | BS_DEFPUSHBUTTON,15,414,599,80,28);
        add(L"BUTTON",quest::i18n::wide("common.cancel"),IDCANCEL,WS_TABSTOP,15,504,599,80,28);
        layout();
    }

    void layout() {
        RECT client{}; GetClientRect(window,&client);
        const float scale = std::min(client.right/600.0f,client.bottom/640.0f);
        HFONT previous = font;
        font = CreateFontW(-static_cast<int>(14*scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,quest::i18n::wide("appearance.font_family"));
        for (const auto& control : controls) {
            const auto& bounds = control.bounds;
            MoveWindow(control.window,static_cast<int>(bounds.left*scale),static_cast<int>(bounds.top*scale),
                static_cast<int>((bounds.right-bounds.left)*scale),static_cast<int>((bounds.bottom-bounds.top)*scale),FALSE);
            ShowWindow(control.window,(control.pages & (1u << page)) ? SW_SHOW : SW_HIDE);
            SendMessageW(control.window,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        }
        if (previous) DeleteObject(previous);
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE | RDW_ALLCHILDREN);
    }

    // Synchronize fields only after raw CFG edits; normal pages share controls and uncommitted input.
    void reload_fields() {
        current_section = *draft.document.find(kind,name);
        for (auto& binding : bindings) {
            if (binding.checkbox) {
                binding.checked = current_section.number(binding.key) != 0;
                SendMessageW(binding.window,BM_SETCHECK,binding.checked ? BST_CHECKED : BST_UNCHECKED,0);
                continue;
            }
            std::string value = current_section.value(binding.key);
            unsigned codepage = draft.codepage;
            const int id = GetDlgCtrlID(binding.window);
            if (id == kBody) { value = mission_text(draft.mission,name); codepage = draft.mission_codepage; }
            else if (id == kHeading) {
                if (draft.document.version() <= 1) value = current_section.value("Coment2");
                else { value = mission_text(draft.mission,heading_key()); codepage = draft.mission_codepage; }
            }
            size_t matches = 0;
            if (!binding.prefix.empty()) {
                value.clear();
                for (const auto& field : current_section.fields) {
                    if (field.name != binding.key) continue;
                    const auto tail = condition_tail(field.value,binding.prefix);
                    if (tail) { value = *tail; ++matches; }
                }
                SendMessageW(binding.window,EM_SETREADONLY,matches > 1,0);
            }
            binding.original = decode(value,codepage);
            if (id == kGiveItems || id == kRemoveItems) binding.original = item_text(binding.original, L"\r\n");
            if (matches > 1) binding.original = quest::i18n::wide("properties.multiple_conditions") + binding.original;
            SetWindowTextW(binding.window,binding.original.c_str());
        }
        for (auto& group : coordinates) {
            std::string value;
            size_t matches = 0;
            for (const auto& field : current_section.fields) {
                if (field.name != "SetFlagmanCoord") continue;
                const auto tail = condition_tail(field.value,group.state);
                if (tail) { value = *tail; ++matches; }
            }
            group.original = {};
            std::istringstream input(value);
            std::string token;
            int count = 0;
            while (input >> token) {
                if (count < 4) group.original[count] = decode(token,draft.codepage);
                ++count;
            }
            for (size_t index = 0; index < group.fields.size(); ++index) {
                SendMessageW(group.fields[index],EM_SETREADONLY,matches > 1 || (matches && count != 4),0);
                SetWindowTextW(group.fields[index],group.original[index].c_str());
            }
        }
    }

    void collect() {
        // Validate each page's encoding and structure in a temporary copy before committing.
        EditSession candidate = draft;
        if (page == 3) {
            const auto content = control_text(raw);
            if (content != original_raw) {
                const auto original = draft.document.block(*draft.document.find(kind,name));
                const auto parsed = QuestDocument::parse(encode_edit(original,content,draft.codepage));
                std::string bytes;
                for (const auto& line : parsed.lines()) {
                    bytes += line.text;
                    if (!line.ending.empty()) bytes += draft.document.newline();
                }
                candidate.document.replace_block(*candidate.document.find(kind,name),bytes);
            }
        } else {
            for (const auto& group : coordinates) {
                std::array<std::wstring,4> values;
                size_t empty_count = 0;
                std::string combined;
                for (size_t index = 0; index < values.size(); ++index) {
                    values[index] = control_text(group.fields[index]);
                    const auto token = trim(encode(values[index],draft.codepage));
                    if (token.empty()) ++empty_count;
                    if (index) combined += " ";
                    combined += token;
                }
                if (values == group.original) continue;
                if (empty_count != 0 && empty_count != 4) throw std::runtime_error(quest::i18n::narrow("errors.incomplete_coordinates"));
                candidate.document.set_condition(name,"SetFlagmanCoord",group.state,trim(combined));
            }
            for (const auto& binding : bindings) {
                if (binding.checkbox) {
                    const bool checked = SendMessageW(binding.window,BM_GETCHECK,0,0) == BST_CHECKED;
                    if (checked != binding.checked) candidate.document.set(kind,name,binding.key,checked ? "1" : "0");
                    continue;
                }
                const auto content = control_text(binding.window);
                if (content == binding.original) continue;
                const int id = GetDlgCtrlID(binding.window);
                if (id == kHeading || id == kBody) {
                    if (id == kHeading && candidate.document.version() <= 1) {
                        auto bytes = encode(content,candidate.codepage);
                        for (char& character : bytes) if (character == '\r' || character == '\n') character = ' ';
                        candidate.document.set(kind,name,"Coment2",bytes);
                    } else {
                        std::string key = name;
                        if (id == kHeading) {
                            key = heading_key();
                            if (key.empty()) {
                                key = name+"Coment2";
                                candidate.document.set(kind,name,"Coment2",key);
                            }
                        }
                        candidate.mission = set_mission_text(candidate.mission,key,encode(content,candidate.mission_codepage));
                    }
                } else if (!binding.prefix.empty()) {
                    candidate.document.set_condition(name,binding.key,binding.prefix,trim(encode(content,candidate.codepage)));
                } else if (id == kGiveItems || id == kRemoveItems) {
                    candidate.document.set(kind, name, binding.key, encode(item_text(content, L" "), candidate.codepage));
                } else candidate.document.set(kind,name,binding.key,encode(content,candidate.codepage));
            }
        }
        draft = std::move(candidate);
    }

    void change_page() {
        const int next = TabCtrl_GetCurSel(tabs);
        if (next == page) return;
        try {
            if (page == 3 || next == 3) collect();
        }
        catch (...) { TabCtrl_SetCurSel(tabs,page); throw; }
        // Change visibility in a batch without destroying controls or repainting text one control at a time.
        SendMessageW(window,WM_SETREDRAW,FALSE,0);
        if (page == 3) reload_fields();
        if (next == 3) {
            current_section = *draft.document.find(kind,name);
            original_raw = decode(draft.document.block(current_section),draft.codepage);
            SetWindowTextW(raw,original_raw.c_str());
        }
        page = next;
        for (const auto& control : controls) {
            const bool visible = (control.pages & (1u << page)) != 0;
            if (visible != ((GetWindowLongW(control.window,GWL_STYLE) & WS_VISIBLE) != 0)) {
                ShowWindow(control.window,visible ? SW_SHOWNA : SW_HIDE);
            }
        }
        SendMessageW(window,WM_SETREDRAW,TRUE,0);
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        SetFocus(tabs);
    }
};

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* dialog = reinterpret_cast<Properties*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        dialog = static_cast<Properties*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        dialog->window = window;
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(dialog));
    }
    if (!dialog) return DefWindowProcW(window,message,wparam,lparam);
    try {
        if (message == WM_CREATE) { dialog->initialize(); return 0; }
        if (message == WM_SIZE) { dialog->layout(); return 0; }
        if (message == WM_DPICHANGED) {
            const auto* bounds = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(window,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER);
            return 0;
        }
        if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lparam)->idFrom == kTabs
            && reinterpret_cast<NMHDR*>(lparam)->code == TCN_SELCHANGE) { dialog->change_page(); return 0; }
        if (message == kAppendSelectedItem) {
            dialog->append_item(static_cast<int>(wparam));
            return 0;
        }
        if (message == WM_COMMAND) {
            const int id = LOWORD(wparam);
            if ((id == kGiveFilter || id == kRemoveFilter) && HIWORD(wparam) == CBN_EDITCHANGE) {
                dialog->filter_items(GetDlgItem(window, id), true);
                return 0;
            }
            if ((id == kGiveFilter || id == kRemoveFilter) && HIWORD(wparam) == CBN_SELENDOK) {
                // Let the native combo finish its selection before clearing the filter and list.
                PostMessageW(window, kAppendSelectedItem, id, 0);
                return 0;
            }
        }
        if (message == WM_COMMAND && LOWORD(wparam) == IDOK) {
            dialog->collect(); dialog->accepted = true; DestroyWindow(window); return 0;
        }
        if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) {
            DestroyWindow(window); return 0;
        }
    } catch (const std::exception& error) {
        MessageBoxW(window,decode(error.what(),CP_UTF8).c_str(),quest::i18n::wide("properties.uncommitted_title"),MB_OK | MB_ICONERROR);
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
}

bool edit_properties(HWND owner, HINSTANCE module, EditSession& session,
                     const std::string& kind, const std::string& name) {
    Properties dialog;
    // Property drafts do not need a copy of the undo history.
    dialog.draft.document = session.document; dialog.draft.mission = session.mission;
    dialog.draft.path = session.path;
    dialog.draft.codepage = session.codepage; dialog.draft.mission_codepage = session.mission_codepage;
    dialog.kind = kind; dialog.name = name;
    dialog.dpi = GetDpiForWindow(owner);
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = procedure; type.hInstance = module; type.lpszClassName = kClass;
    type.hCursor = LoadCursorW(nullptr,IDC_ARROW); type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error(quest::i18n::narrow("errors.register_properties"));
    RECT area{};
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    area = monitor.rcWork;
    const int height = std::min<int>(MulDiv(680,dialog.dpi,96),area.bottom-area.top-16);
    const int width = std::min<int>(MulDiv(616,dialog.dpi,96),area.right-area.left-16);
    const auto caption = quest::i18n::wide("properties.title_prefix")+decode(name,session.codepage);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME,kClass,caption.c_str(),WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,
        width,height,owner,nullptr,module,&dialog);
    if (!window) { UnregisterClassW(kClass,module); throw std::runtime_error(quest::i18n::narrow("errors.open_properties")); }
    // The legacy launcher embeds a WS_CHILD editor; disable its top-level container during modal editing.
    const HWND container = GetAncestor(owner,GA_ROOTOWNER);
    const bool owner_enabled = IsWindowEnabled(owner) != FALSE;
    const bool container_enabled = IsWindowEnabled(container) != FALSE;
    EnableWindow(owner,FALSE);
    if (container != owner) EnableWindow(container,FALSE);
    ShowWindow(window,SW_SHOW); SetFocus(dialog.tabs);
    MSG message{};
    while (IsWindow(window)) {
        const int result = GetMessageW(&message,nullptr,0,0);
        if (result <= 0) { if (!result) PostQuitMessage(static_cast<int>(message.wParam)); break; }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            const int filter = dialog.focused_item_filter();
            if (filter && SendDlgItemMessageW(window, filter, CB_GETDROPPEDSTATE, 0, 0)) {
                SendDlgItemMessageW(window, filter, CB_SHOWDROPDOWN, FALSE, 0); continue;
            }
            SendMessageW(window,WM_COMMAND,IDCANCEL,0); continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && (GetKeyState(VK_CONTROL)&0x8000)) {
            SendMessageW(window,WM_COMMAND,IDOK,0); continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            const int filter = dialog.focused_item_filter();
            if (filter) { dialog.append_item(filter); continue; }
        }
        if (!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (IsWindow(window)) DestroyWindow(window);
    if (container != owner && IsWindow(container)) EnableWindow(container,container_enabled);
    if (IsWindow(owner)) { EnableWindow(owner,owner_enabled); SetActiveWindow(owner); }
    UnregisterClassW(kClass,module);
    if (!dialog.accepted) return false;
    const auto before = session.snapshot(), after = dialog.draft.snapshot();
    if (before.configuration != after.configuration || before.mission != after.mission) {
        session.checkpoint(); session.document = std::move(dialog.draft.document); session.mission = std::move(dialog.draft.mission);
    }
    return true;
}
}
