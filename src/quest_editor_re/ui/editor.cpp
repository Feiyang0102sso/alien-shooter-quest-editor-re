#include "../i18n/i18n.h"
#include "editor.h"
#include "controls.h"
#include "quest_properties.h"
#include "../core/edit_session.h"
#include "../game_files/path_selection.h"
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>


namespace quest {
namespace {

constexpr wchar_t kMainClass[] = L"QuestEditorRE.Main.v1";
constexpr wchar_t kGraphClass[] = L"QuestEditorRE.Graph.v1";
constexpr wchar_t kTextClass[] = L"QuestEditorRE.Text.v1";
constexpr int kToolbarHeight = 32;
constexpr int kStatusHeight = 36;
constexpr float kNodeWidth = 170;
constexpr float kNodeHeight = 100;
constexpr UINT kHostFinished = 0x029A029A;
constexpr UINT kOpen = 100;
constexpr UINT kSave = 101;
constexpr UINT kUndo = 102;
constexpr UINT kRedo = 103;
constexpr UINT kHelp = 106;
constexpr UINT kText = 108;
constexpr UINT kDelete = 112;
constexpr UINT kFullText = 125;
constexpr UINT kDone = 126;
constexpr UINT kHeadingText = 128;
constexpr UINT kProperties = 131;
constexpr UINT kCancel = 133;
constexpr UINT kMapTabs = 134;
constexpr UINT kDisconnectLine = 136;

struct Connection {
    std::string source;
    std::string target;
    bool branch = false;
};

void append_encoding_menus(HMENU parent) {
        HMENU cfg_encoding = CreatePopupMenu();
        HMENU text_encoding = CreatePopupMenu();
        const wchar_t* ansi = quest::i18n::wide("menu.encoding_ansi");
        if (GetACP() == CP_UTF8) ansi = quest::i18n::wide("menu.encoding_ansi_fallback");
        const wchar_t* encodings[] = {quest::i18n::wide("menu.encoding_russian"),quest::i18n::wide("menu.encoding_chinese"),ansi};
        for (int index = 0; index < 3; ++index) {
            AppendMenuW(cfg_encoding,MF_STRING,140+index,encodings[index]);
            AppendMenuW(text_encoding,MF_STRING,143+index,encodings[index]);
        }
        AppendMenuW(parent,MF_POPUP,reinterpret_cast<UINT_PTR>(cfg_encoding),quest::i18n::wide("menu.cfg_encoding"));
        AppendMenuW(parent,MF_POPUP,reinterpret_cast<UINT_PTR>(text_encoding),quest::i18n::wide("menu.mission_encoding"));
}

LRESULT CALLBACK embedded_parent_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                     UINT_PTR, DWORD_PTR child_window) {
    if (message == WM_SIZE && wparam != SIZE_MINIMIZED) {
        MoveWindow(reinterpret_cast<HWND>(child_window),0,0,LOWORD(lparam),HIWORD(lparam),TRUE);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window,embedded_parent_proc,66);
    return DefSubclassProc(window,message,wparam,lparam);
}

template<class T> void release(T*& object) {
    if (object) { object->Release(); object = nullptr; }
}

void show_error(HWND window, const std::exception& error) {
    const auto detail = decode(error.what(), CP_UTF8);
    MessageBoxW(window, (quest::i18n::wide("editor.operation_failed") + detail).c_str(), quest::i18n::wide("common.app_title"), MB_OK | MB_ICONERROR);
    OutputDebugStringA((std::string(quest::i18n::narrow("errors.log_prefix")) + error.what() + "\n").c_str());
}

struct TextDialog {
    HWND window = nullptr;
    HWND edit = nullptr;
    HWND label = nullptr;
    HWND apply = nullptr;
    HWND cancel = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
    bool accepted = false;
    std::wstring content;
    ~TextDialog() { if (font) DeleteObject(font); }

    void update_font() {
        const HFONT previous = font;
        font = CreateFontW(-MulDiv(15,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH,quest::i18n::wide("appearance.font_family"));
        for (HWND control : {edit,label,apply,cancel}) {
            if (control) SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        }
        if (previous) DeleteObject(previous);
    }
};

LRESULT CALLBACK text_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* dialog = reinterpret_cast<TextDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        dialog = static_cast<TextDialog*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
    }
    if (!dialog) return DefWindowProcW(window, message, wparam, lparam);
    if (message == WM_CREATE) {
        dialog->label = child(window, L"STATIC", quest::i18n::wide("mission.editing_hint"), 0, 0, dialog->font);
        dialog->edit = child(window, L"EDIT", dialog->content.c_str(), WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN, 10, dialog->font);
        SendMessageW(dialog->edit, EM_SETLIMITTEXT, 16 * 1024 * 1024, 0);
        dialog->apply = child(window, L"BUTTON", quest::i18n::wide("mission.apply"), WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, dialog->font);
        dialog->cancel = child(window, L"BUTTON", quest::i18n::wide("common.cancel"), WS_TABSTOP, IDCANCEL, dialog->font);
        return 0;
    }
    if (message == WM_SIZE) {
        const int width = MulDiv(LOWORD(lparam),96,dialog->dpi);
        const int height = MulDiv(HIWORD(lparam),96,dialog->dpi);
        const auto place = [&](HWND control,int x,int y,int w,int h) {
            MoveWindow(control,MulDiv(x,dialog->dpi,96),MulDiv(y,dialog->dpi,96),
                MulDiv(w,dialog->dpi,96),MulDiv(h,dialog->dpi,96),TRUE);
        };
        place(dialog->label, 16, 14, width - 32, 30);
        place(dialog->edit, 16, 48, width - 32, std::max(40, height - 108));
        place(dialog->apply, width - 218, height - 46, 96, 30);
        place(dialog->cancel, width - 112, height - 46, 96, 30);
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize = {MulDiv(540,dialog->dpi,96),MulDiv(360,dialog->dpi,96)};
        return 0;
    }
    if (message == WM_DPICHANGED) {
        dialog->dpi = HIWORD(wparam);
        dialog->update_font();
        const auto* rectangle = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window,nullptr,rectangle->left,rectangle->top,
            rectangle->right-rectangle->left,rectangle->bottom-rectangle->top,SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK) {
        dialog->content = control_text(dialog->edit);
        dialog->accepted = true;
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL) { DestroyWindow(window); return 0; }
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool edit_text(HWND owner, HINSTANCE module, const std::wstring& title, std::wstring& content) {
    TextDialog dialog;
    dialog.dpi = GetDpiForWindow(owner);
    dialog.update_font();
    dialog.content = content;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const int width = std::min<int>(MulDiv(850,dialog.dpi,96),work.right-work.left-24);
    const int height = std::min<int>(MulDiv(620,dialog.dpi,96),work.bottom-work.top-24);
    dialog.window = CreateWindowExW(WS_EX_DLGMODALFRAME, kTextClass, title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, width, height, owner, nullptr, module, &dialog);
    if (!dialog.window) throw std::runtime_error(quest::i18n::narrow("errors.open_text_editor"));
    EnableWindow(owner, FALSE);
    SetFocus(dialog.edit);
    MSG message{};
    while (IsWindow(dialog.window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN
            && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SendMessageW(dialog.window,WM_COMMAND,IDOK,0);
            continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            DestroyWindow(dialog.window);
            break;
        }
        if (!IsDialogMessageW(dialog.window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (dialog.accepted) content = dialog.content;
    return dialog.accepted;
}

class Editor {
public:
    HWND window = nullptr;
    HWND owner = nullptr;
    HWND graph = nullptr;
    HINSTANCE module = nullptr;
    bool embedded = false;
    UINT dpi = 96;
    EditSession session;
    bool picking = false;
    int selected_group = 0;
    int selected_index = 0;
    HFONT font = nullptr;
    HBRUSH background = nullptr;
    HWND status = nullptr;
    HWND done = nullptr;
    HWND cancel = nullptr;
    HWND map_tabs = nullptr;
    std::vector<int> level_timelines;
    std::map<std::string, std::wstring> previews;
    std::map<std::string, Position> default_positions;
    std::string selected;
    std::string selected_kind = "Quest";
    std::set<std::string> selection;
    std::optional<Connection> selected_connection;
    float zoom = 0.65f;
    Position offset{94, -24};
    Position mouse_start;
    Position offset_start;
    bool panning = false;
    bool connecting = false;
    bool boxing = false;
    bool connection_branch = false;
    Position cursor;
    std::string connection_source;
    const char* notice_key = "editor.open_notice";
    ID2D1Factory* factory = nullptr;
    IDWriteFactory* text_factory = nullptr;
    ID2D1DCRenderTarget* target = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteTextFormat* body_font = nullptr;
    IDWriteTextFormat* heading_font = nullptr;

    ~Editor() {
        if (embedded && IsWindow(owner)) RemoveWindowSubclass(owner,embedded_parent_proc,66);
        release(brush); release(target); release(body_font); release(heading_font);
        release(text_factory); release(factory);
        if (font) DeleteObject(font);
        if (background) DeleteObject(background);
    }

    void initialize() {
        dpi = GetDpiForWindow(window);
        font = CreateFontW(-MulDiv(13,dpi,96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, quest::i18n::wide("appearance.font_family"));
        background = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        done = child(window, L"BUTTON", quest::i18n::wide("common.ok"), WS_TABSTOP | BS_DEFPUSHBUTTON, kDone, font);
        cancel = child(window, L"BUTTON", quest::i18n::wide("common.cancel"), WS_TABSTOP, kCancel, font);
        map_tabs = child(window, WC_TABCONTROLW, L"", WS_TABSTOP | TCS_SINGLELINE, kMapTabs, font);
        graph = CreateWindowExW(0, kGraphClass, quest::i18n::wide("editor.canvas_name"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL,
            0, 0, 100, 100, window, nullptr, module, this);
        status = child(window, L"STATIC", L"", SS_LEFTNOWORDWRAP, 0, font);
        rebuild_menu();
        layout_controls();
        refresh();
    }

    void rebuild_menu() {
        HMENU menu = CreateMenu();
        HMENU file_menu = CreatePopupMenu();
        AppendMenuW(file_menu, MF_STRING, kOpen, quest::i18n::wide("menu.open"));
        AppendMenuW(file_menu, MF_STRING, kSave, quest::i18n::wide("menu.save"));
        AppendMenuW(file_menu, MF_STRING, kFullText, quest::i18n::wide("menu.full_mission"));
        append_encoding_menus(file_menu);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file_menu), quest::i18n::wide("menu.file"));
        HMENU edit_menu = CreatePopupMenu();
        AppendMenuW(edit_menu, MF_STRING, kUndo, quest::i18n::wide("menu.undo"));
        AppendMenuW(edit_menu, MF_STRING, kRedo, quest::i18n::wide("menu.redo"));
        AppendMenuW(edit_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(edit_menu, MF_STRING, kProperties, quest::i18n::wide("menu.properties"));
        AppendMenuW(edit_menu, MF_STRING, kDelete, quest::i18n::wide("menu.delete_tasks"));
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edit_menu), quest::i18n::wide("menu.edit"));
        AppendMenuW(menu,MF_STRING,kHelp,quest::i18n::wide("menu.help"));
        HMENU languages = CreatePopupMenu();
        AppendMenuW(languages,MF_STRING,151,quest::i18n::wide("menu.language_cn"));
        AppendMenuW(languages,MF_STRING,150,quest::i18n::wide("menu.language_en"));
        CheckMenuRadioItem(languages,150,151,149+i18n::language(),MF_BYCOMMAND);
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(languages),quest::i18n::wide("menu.language"));
        const HMENU previous = GetMenu(window);
        if (SetMenu(window,menu)) {
            if (previous) DestroyMenu(previous);
            DrawMenuBar(window);
        } else DestroyMenu(menu);
    }

    void place(HWND control, int x, int y, int width, int height) const {
        MoveWindow(control,MulDiv(x,dpi,96),MulDiv(y,dpi,96),
            MulDiv(width,dpi,96),MulDiv(height,dpi,96),TRUE);
    }

    void update_dpi(UINT next) {
        dpi = next;
        HFONT previous = font;
        font = CreateFontW(-MulDiv(13,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,quest::i18n::wide("appearance.font_family"));
        for (HWND control = GetWindow(window,GW_CHILD); control; control = GetWindow(control,GW_HWNDNEXT)) {
            SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        }
        if (previous) DeleteObject(previous);
        if (target) target->SetDpi(static_cast<float>(dpi),static_cast<float>(dpi));
        layout_controls();
    }

    void layout_controls() {
        RECT client{}; GetClientRect(window, &client);
        const int width = MulDiv(client.right,96,dpi);
        const int height = MulDiv(client.bottom,96,dpi);
        place(map_tabs,4,3,width-8,28);
        place(done,width-166,height-31,76,25);
        place(cancel,width-84,height-31,76,25);
        place(graph,4,kToolbarHeight,std::max(10,width-8),std::max(10,height-kToolbarHeight-kStatusHeight));
        place(status,8,height-29,std::max(10,width-188),23);
        InvalidateRect(graph, nullptr, FALSE);
    }

    void update_status() {
        // Refresh after opening files or switching display encoding; each file has its own selection.
        HMENU file_menu = GetSubMenu(GetMenu(window),0);
        UINT cfg_selected = 140;
        UINT mission_selected = 143;
        if (session.codepage == 936) cfg_selected = 141;
        if (session.mission_codepage == 936) mission_selected = 144;
        if (session.cfg_ansi) cfg_selected = 142;
        if (session.mission_ansi) mission_selected = 145;
        CheckMenuRadioItem(file_menu,140,142,cfg_selected,MF_BYCOMMAND);
        CheckMenuRadioItem(file_menu,143,145,mission_selected,MF_BYCOMMAND);
        std::wstring text = i18n::wide(notice_key);
        if (selected_connection) {
            const auto& edge = *selected_connection;
            text = decode(edge.source,session.codepage)+quest::i18n::wide("common.reference_arrow")+decode(edge.target,session.codepage);
            if (edge.branch) text += quest::i18n::wide("editor.selected_red_line");
            else text += quest::i18n::wide("editor.selected_black_line");
        }
        if (session.dirty()) text += quest::i18n::wide("editor.unsaved_status");
        SetWindowTextW(status, text.c_str());
        std::wstring caption = quest::i18n::wide("common.app_title");
        if (!session.path.empty()) caption += quest::i18n::wide("common.title_separator") + session.path.parent_path().filename().wstring();
        if (session.dirty()) caption += quest::i18n::wide("common.modified_title");
        if (picking) caption += quest::i18n::wide("editor.picking_title");
        SetWindowTextW(window, caption.c_str());
    }

    void refresh_map_tabs() {
        const int previous = TabCtrl_GetCurSel(map_tabs);
        TabCtrl_DeleteAllItems(map_tabs);
        level_timelines.clear();
        if (!session.path.empty()) level_timelines.push_back(0);
        for (const auto& section : session.document.sections()) {
            if (section.kind != "Map") continue;
            // Map reward records with TimeLine=0 are not separate level boundaries.
            if (section.number("TimeLine") > 0) level_timelines.push_back(section.number("TimeLine"));
        }
        std::sort(level_timelines.begin(),level_timelines.end());
        level_timelines.erase(std::unique(level_timelines.begin(),level_timelines.end()),level_timelines.end());
        for (size_t index = 0; index < level_timelines.size(); ++index) {
            auto label = std::to_wstring(index+1);
            TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = label.data();
            SendMessageW(map_tabs,TCM_INSERTITEMW,index,reinterpret_cast<LPARAM>(&item));
        }
        if (!level_timelines.empty()) TabCtrl_SetCurSel(map_tabs,std::clamp(previous,0,static_cast<int>(level_timelines.size())-1));
    }

    void refresh() {
        if (selected_connection) {
            const auto source = session.document.find("Quest",selected_connection->source);
            const auto target_id = QuestId::parse(selected_connection->target);
            if (!source || !target_id || !session.document.find("Quest",selected_connection->target)
                || session.document.connection_target(*source,selected_connection->branch) != target_id->packed()) {
                selected_connection.reset();
            }
        }
        default_positions = session.default_positions();
        previews.clear();
        const auto text_blocks = mission_texts(session.mission);
        for (const auto& item : session.document.sections()) {
            if (item.kind != "Quest") continue;
            std::wstring preview;
            const auto text_block = text_blocks.find(item.name);
            if (text_block != text_blocks.end()) preview = decode(text_block->second,session.mission_codepage);
            for (;;) {
                const auto tag = preview.find(L"<Font=");
                if (tag == std::wstring::npos) break;
                const auto end = preview.find(L'>',tag);
                if (end == std::wstring::npos) break;
                preview.erase(tag,end-tag+1);
            }
            for (wchar_t& character : preview) {
                if (character == L'\r' || character == L'\n') character = L' ';
            }
            previews[item.name] = preview;
        }
        refresh_map_tabs();
        update_status();
        layout_controls();
        InvalidateRect(graph, nullptr, FALSE);
    }

    void refresh_selection() {
        // Selection and drag previews do not edit CFG; avoid rebuilding text previews, layout and level tabs.
        update_status();
        InvalidateRect(graph,nullptr,FALSE);
    }

    bool save() {
        const auto diagnostics = session.document.validate();
        for (const auto& item : diagnostics) {
            if (item.error) throw std::runtime_error(quest::i18n::narrow("errors.configuration_line_prefix")+std::to_string(item.line)+quest::i18n::narrow("errors.configuration_line_separator")+item.message);
        }
        const bool backups_complete = session.save();
        notice_key = "editor.saved_notice";
        if (!backups_complete) {
            notice_key = "editor.backup_warning";
            MessageBoxW(window,i18n::wide(notice_key),quest::i18n::wide("common.app_title"),MB_OK | MB_ICONWARNING);
        }
        refresh();
        return true;
    }

    bool pending_session() {
        if (!session.dirty()) return true;
        const int answer = MessageBoxW(window, quest::i18n::wide("editor.save_question"), quest::i18n::wide("editor.unsaved_title"), MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL) return false;
        if (answer == IDYES) return save();
        session.discard_changes();
        return true;
    }

    void open_file() {
        if (!pending_session()) return;
        wchar_t filename[32768]{};
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = window;
        dialog.lpstrFilter = quest::i18n::wide("editor.open_filter");
        dialog.lpstrFile = filename;
        dialog.nMaxFile = 32768;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&dialog)) load(filename);
    }

    void load(const std::filesystem::path& path) {
        session.open(path);
        selected_connection.reset();
        selection.clear(); selected.clear();
        offset = {94, -24}; zoom = 0.65f;
        notice_key = "editor.loaded_notice";
        refresh();
    }

    void select(const std::string& name, const std::string& kind, bool additive = false) {
        selected_connection.reset();
        selected = name; selected_kind = kind;
        if (!additive) selection.clear();
        if (kind == "Quest") selection.insert(name);
        refresh_selection();
    }

    Position world(Position point) const { return {(point.x - offset.x) / zoom, (point.y - offset.y) / zoom}; }
    Position screen(Position point) const { return {point.x * zoom + offset.x, point.y * zoom + offset.y}; }

    void toggle_boundary(Position point) {
        if (picking || hit(point)) return;
        const auto location = world(point);
        const int column = static_cast<int>(std::floor((location.x-80)/206));
        const float within = location.x-(80+column*206);
        // Boundaries sit left of the next column; the initial TimeLine=0 cannot be removed.
        if (within <= kNodeWidth || column < 0) return;
        if (session.toggle_boundary(column+1)) {
            notice_key = "editor.boundary_notice";
            refresh();
        }
    }

    // Gray cells share the drawing grid's 206x124 spacing; cursor coordinates are not card origins.
    std::optional<Position> empty_cell(Position point) const {
        const auto location = world(point);
        const float column = std::floor((location.x-80)/206);
        const float row = std::floor((location.y-90)/124);
        if (column < 0 || row < 0) return {};
        const Position origin{80+column*206,90+row*124};
        if (location.x > origin.x+kNodeWidth || location.y > origin.y+kNodeHeight) return {};
        // Only gray cell interiors create tasks; gaps between cells do not.
        if (zoom > 0.3f && location.y < origin.y+17/zoom) return {};
        return origin;
    }

    Position node_position(const Section& section) const {
        const auto custom = session.layout.find(section.name);
        if (custom != session.layout.end()) return custom->second;
        const auto found = default_positions.find(section.name);
        if (found != default_positions.end()) return found->second;
        return {80.0f + section.number("TimeLine") * 206.0f,90};
    }

    int row_at(float y) const {
        std::map<int,float> tops;
        for (const auto& [name,position] : default_positions) {
            const auto id = QuestId::parse(name);
            if (!id) continue;
            if (!tops.contains(id->group)) tops[id->group] = position.y;
            tops[id->group] = std::min(tops[id->group],position.y);
        }
        int row = 1;
        float last_bottom = 90;
        for (const auto& [group,top] : tops) {
            if (y < top) return row;
            row = group;
            last_bottom = top + 124;
        }
        for (const auto& [name,position] : default_positions) {
            const auto id = QuestId::parse(name);
            if (id && id->group == row) last_bottom = std::max(last_bottom,position.y + 124);
        }
        if (y >= last_bottom) row += 1 + static_cast<int>((y-last_bottom)/124);
        return std::clamp(row,1,49);
    }

    void create_in_cell(Position location) {
        std::optional<Section> previous, next;
        float previous_x = 0, next_x = 0;
        for (const auto& section : session.document.sections()) {
            if (section.kind != "Quest") continue;
            const auto id = QuestId::parse(section.name);
            if (!id || id->index == 0) continue;
            const auto at = node_position(section);
            if (std::abs(at.y-location.y) > 1) continue;
            if (at.x < location.x && (!previous || at.x > previous_x)) {
                previous = section; previous_x = at.x;
            }
            if (at.x > location.x && (!next || at.x < next_x)) {
                next = section; next_x = at.x;
            }
        }
        int group = row_at(location.y);
        if (previous) group = QuestId::parse(previous->name)->group;
        else if (next) group = QuestId::parse(next->name)->group;
        std::string before, after;
        if (previous) before = previous->name;
        if (next) after = next->name;
        const int timeline = static_cast<int>(std::round((location.x-80)/206));
        const auto name = session.create_between(group,timeline,location,before,after);
        select(name,"Quest");
        refresh();
    }

    std::optional<Section> hit(Position point) const {
        const auto logical = world(point);
        const auto sections = session.document.sections();
        // Match drawing order: overlapping hit tests prefer the last-drawn card.
        for (auto item = sections.rbegin(); item != sections.rend(); ++item) {
            const auto& section = *item;
            if (section.kind != "Quest") continue;
            const auto position = node_position(section);
            if (logical.x >= position.x && logical.x <= position.x + kNodeWidth
                && logical.y >= position.y && logical.y <= position.y + kNodeHeight) return section;
        }
        return {};
    }

    std::optional<Connection> hit_connection(Position point, bool cycle = false) const {
        std::map<int,Section> by_id;
        const auto sections = session.document.sections();
        for (const auto& section : sections) {
            if (section.kind != "Quest") continue;
            const auto id = QuestId::parse(section.name);
            if (id) by_id[id->packed()] = section;
        }
        std::vector<std::pair<Connection,float>> candidates;
        float closest_squared = 36; // Six screen DIPs keep hit testing usable regardless of canvas zoom.
        for (const auto& section : sections) {
            if (section.kind != "Quest") continue;
            for (int branch = 0; branch < 2; ++branch) {
                const auto destination = by_id.find(session.document.connection_target(section,branch != 0));
                if (destination == by_id.end()) continue;
                const auto from = node_position(section), to = node_position(destination->second);
                const auto a = screen({from.x+kNodeWidth,from.y+kNodeHeight/2});
                const auto b = screen({to.x,to.y+kNodeHeight/2});
                const float dx = b.x-a.x, dy = b.y-a.y;
                const float length_squared = dx*dx+dy*dy;
                if (length_squared < 1) continue;
                const float along = std::clamp(((point.x-a.x)*dx+(point.y-a.y)*dy)/length_squared,0.0f,1.0f);
                const float distance_x = point.x-a.x-along*dx;
                const float distance_y = point.y-a.y-along*dy;
                const float distance_squared = distance_x*distance_x+distance_y*distance_y;
                if (distance_squared <= 36) {
                    closest_squared = std::min(closest_squared,distance_squared);
                    candidates.push_back({{section.name,destination->second.name,branch != 0},distance_squared});
                }
            }
        }
        std::vector<Connection> nearest;
        for (auto item = candidates.rbegin(); item != candidates.rend(); ++item) {
            if (item->second <= closest_squared+0.5f) nearest.push_back(item->first);
        }
        if (nearest.empty()) return {};
        if (selected_connection) {
            for (size_t index = 0; index < nearest.size(); ++index) {
                if (nearest[index].source == selected_connection->source && nearest[index].branch == selected_connection->branch) {
                    if (cycle) index = (index+1)%nearest.size();
                    return nearest[index];
                }
            }
        }
        // Select the last-drawn overlapping line first; repeated clicks cycle so both can be disconnected.
        return nearest.front();
    }

    void select_connection(const Connection& connection) {
        selected_connection = connection;
        selected.clear(); selection.clear(); selected_kind = "Quest";
        refresh_selection();
    }

    void disconnect_selected() {
        if (!selected_connection) return;
        const auto edge = *selected_connection;
        session.disconnect(edge.source,edge.branch);
        selected_connection.reset();
        notice_key = "editor.disconnected_notice";
        refresh();
    }

    void edit_mission(bool full, bool heading = false) {
        if (session.path.empty()) return;
        std::string key = selected;
        const auto section = session.document.find(selected_kind, selected);
        if (!full && (!section || section->kind != "Quest")) return;
        if (heading && section && session.document.version() <= 1) {
            auto content = decode(section->value("Coment2"), session.codepage);
            if (edit_text(window, module, quest::i18n::wide("mission.legacy_heading_title"), content)) {
                auto encoded = encode(content, session.codepage);
                for (char& character : encoded) {
                    if (character == '\r' || character == '\n') character = ' ';
                }
                session.checkpoint();
                session.document.set(selected_kind, selected, "Coment2", encoded);
                refresh();
            }
            return;
        }
        bool assign_heading_key = false;
        if (heading && section) {
            std::istringstream reference(section->value("Coment2"));
            reference >> key;
            if (!reference) key.clear();
            if (key.empty()) { key = selected + "Coment2"; assign_heading_key = true; }
        }
        std::wstring content;
        std::wstring caption;
        if (full) {
            content = decode(session.mission, session.mission_codepage);
            caption = quest::i18n::wide("mission.full_text_title");
        } else {
            content = decode(mission_text(session.mission, key), session.mission_codepage);
            caption = quest::i18n::wide("mission.body_title_prefix") + decode(key, session.codepage) + quest::i18n::wide("common.reference_end");
            if (heading) caption = quest::i18n::wide("mission.heading_title_prefix") + decode(key, session.codepage) + quest::i18n::wide("common.reference_end");
            if (content.empty()) caption += quest::i18n::wide("mission.missing_key_suffix");
        }
        if (edit_text(window, module, caption, content)) {
            std::string original = session.mission;
            if (!full) original = mission_text(session.mission,key);
            const auto encoded = encode_edit(original, content, session.mission_codepage);
            std::string changed = encoded;
            if (!full) changed = set_mission_text(session.mission, key, encoded);
            if (changed != session.mission || assign_heading_key) {
                session.checkpoint(); session.mission = changed;
                if (assign_heading_key) session.document.set(selected_kind, selected, "Coment2", key);
            }
            refresh();
        }
    }

    void finish(bool discard = false) {
        if (discard) {
            session.discard_changes();
        }
        if (!discard && !pending_session()) return;
        const HWND host = owner;
        const HWND self = window;
        if (host && IsWindow(host)) {
            ShowWindow(self, SW_HIDE);
            // Synchronous notification can reenter or unload the DLL; do not access members afterward.
            SendMessageW(host, WM_COMMAND, kHostFinished, 0);
        } else DestroyWindow(self);
    }

    void command(UINT id) {
        if (id == 150 || id == 151) {
            i18n::set_language(static_cast<int>(id)-149);
            rebuild_menu();
            SetWindowTextW(done,i18n::wide("common.ok"));
            SetWindowTextW(cancel,i18n::wide("common.cancel"));
            SetWindowTextW(graph,i18n::wide("editor.canvas_name"));
            release(body_font);
            release(heading_font);
            update_dpi(dpi);
            refresh();
            return;
        }
        if (id >= 140 && id <= 145) {
            unsigned codepage = 1251;
            if (id == 141 || id == 144) codepage = 936;
            if (id == 142 || id == 145) codepage = default_game_codepage();
            if (id < 143) {
                decode(session.document.bytes(),codepage);
                session.codepage = codepage;
                session.cfg_ansi = id == 142;
            } else {
                decode(session.mission,codepage);
                session.mission_codepage = codepage;
                session.mission_ansi = id == 145;
            }
            refresh();
            return;
        }
        if (picking && id != kOpen && id != kHelp && id != kDone && id != kCancel) return;
        switch (id) {
        case kProperties:
            if (session.document.find(selected_kind,selected)) {
                if (edit_properties(window,module,session,selected_kind,selected)) refresh();
                SetFocus(graph);
            }
            break;
        case kOpen: open_file(); break;
        case kSave: save(); break;
        case kUndo: { session.undo(); refresh(); } break;
        case kRedo: { session.redo(); refresh(); } break;
        case kDelete: {
            if (selected_connection) { disconnect_selected(); break; }
            if (selected_kind == "Map" && !selected.empty()) {
                const auto section = session.document.find("Map",selected);
                if (!section) break;
                if (MessageBoxW(window,quest::i18n::wide("editor.delete_map_question"),
                    quest::i18n::wide("editor.delete_map_title"),MB_YESNO | MB_ICONQUESTION) != IDYES) break;
                session.checkpoint();
                session.document.remove(*section);
                selected.clear();
                refresh();
                break;
            }
            if (selection.empty()) break;
            if (MessageBoxW(window, quest::i18n::wide("editor.delete_tasks_question"), quest::i18n::wide("editor.delete_tasks_title"), MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            session.erase_many(std::vector<std::string>(selection.begin(), selection.end()));
            selection.clear(); selected.clear(); refresh(); break;
        }
        case kDisconnectLine: disconnect_selected(); break;
        case kText: edit_mission(false); break;
        case kHeadingText: edit_mission(false, true); break;
        case kFullText: edit_mission(true); break;
        case kDone:
            if (!picking && !session.path.empty() && session.dirty()) save();
            finish(); break;
        case kCancel:
            if (session.dirty()
                && MessageBoxW(window,quest::i18n::wide("editor.discard_question"),quest::i18n::wide("editor.discard_title"),MB_OKCANCEL | MB_ICONQUESTION) != IDOK) break;
            finish(true); break;
        case kHelp:
            MessageBoxW(window,
                quest::i18n::wide("editor.help_body"),
                quest::i18n::wide("editor.help_title"), MB_OK | MB_ICONINFORMATION); break;
        }
    }

    void graphics() {
        if (!factory && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory))) throw std::runtime_error(quest::i18n::narrow("errors.direct2d_initialization"));
        if (!text_factory && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&text_factory)))) throw std::runtime_error(quest::i18n::narrow("errors.directwrite_initialization"));
        if (!body_font) text_factory->CreateTextFormat(quest::i18n::wide("appearance.font_family"), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12, quest::i18n::wide("appearance.body_locale"), &body_font);
        if (!heading_font) text_factory->CreateTextFormat(quest::i18n::wide("appearance.font_family"), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12, quest::i18n::wide("appearance.heading_locale"), &heading_font);
        if (!body_font || !heading_font) throw std::runtime_error(quest::i18n::narrow("errors.text_format_initialization"));
        if (!target) {
            auto properties = D2D1::RenderTargetProperties();
            properties.dpiX = static_cast<float>(dpi);
            properties.dpiY = static_cast<float>(dpi);
            properties.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
            if (FAILED(factory->CreateDCRenderTarget(&properties, &target))) throw std::runtime_error(quest::i18n::narrow("errors.canvas_initialization"));
            if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0), &brush))) throw std::runtime_error(quest::i18n::narrow("errors.brush_initialization"));
        }
    }

    void color(UINT32 rgb, float alpha = 1) { brush->SetColor(D2D1::ColorF(rgb, alpha)); }
    void text(const std::wstring& content, D2D1_RECT_F rectangle, UINT32 rgb, bool heading = false) {
        color(rgb);
        IDWriteTextFormat* format = body_font;
        if (heading) format = heading_font;
        target->DrawTextW(content.c_str(), static_cast<UINT32>(content.size()), format, rectangle, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void draw_connection(Position start, Position end, bool branch, bool selected_line = false) {
        color(0x202020);
        if (branch) color(0x800000);
        const auto a = screen(start); const auto b = screen(end);
        float width = 1;
        if (selected_line) width = 3;
        target->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), brush, width);
        if (selected_line) {
            // Show stored direction only when selected to distinguish reverse red links from black links.
            const float dx = b.x-a.x, dy = b.y-a.y;
            const float length = std::sqrt(dx*dx+dy*dy);
            if (length > 10) {
                const Position middle{(a.x+b.x)/2,(a.y+b.y)/2};
                const Position wing1{middle.x-7*dx/length+4*dy/length,middle.y-7*dy/length-4*dx/length};
                const Position wing2{middle.x-7*dx/length-4*dy/length,middle.y-7*dy/length+4*dx/length};
                target->DrawLine(D2D1::Point2F(wing1.x,wing1.y),D2D1::Point2F(middle.x,middle.y),brush,2);
                target->DrawLine(D2D1::Point2F(wing2.x,wing2.y),D2D1::Point2F(middle.x,middle.y),brush,2);
            }
        }
    }

    void draw_flags(const Section& section, const D2D1_RECT_F& body) {
        const char* fields[] = {"Unneeded","BeginNext","Accomplished","WinOnBegin","UnactualNext"};
        const UINT32 colors[] = {0xFF0000,0x0000FF,0x00FF00,0x00FFFF,0x008000};
        const float spacing = std::min(9.0f,(body.bottom-body.top)/5);
        const float radius = spacing*0.42f;
        for (size_t index = 0; index < std::size(fields); ++index) {
            if (!section.number(fields[index])) continue;
            const float x = body.right-radius-2;
            const float y = body.top+(static_cast<float>(index)+0.5f)*spacing;
            ID2D1PathGeometry* diamond = nullptr;
            if (FAILED(factory->CreatePathGeometry(&diamond))) continue;
            ID2D1GeometrySink* sink = nullptr;
            if (SUCCEEDED(diamond->Open(&sink))) {
                sink->BeginFigure(D2D1::Point2F(x,y-radius),D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(D2D1::Point2F(x+radius,y));
                sink->AddLine(D2D1::Point2F(x,y+radius));
                sink->AddLine(D2D1::Point2F(x-radius,y));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                if (SUCCEEDED(sink->Close())) {
                    color(colors[index]); target->FillGeometry(diamond,brush);
                    color(0x202020); target->DrawGeometry(diamond,brush,1);
                }
                sink->Release();
            }
            diamond->Release();
        }
    }

    void paint(HDC supplied = nullptr) {
        PAINTSTRUCT paint_state{};
        HDC context = supplied;
        if (!context) context = BeginPaint(graph, &paint_state);
        // Use the same drawing path for the screen and WM_PRINTCLIENT for unobtrusive verification.
        struct PaintScope {
            HWND window;
            PAINTSTRUCT* state;
            ~PaintScope() { if (state) EndPaint(window, state); }
        } scope{graph, nullptr};
        if (!supplied) scope.state = &paint_state;
        graphics();
        RECT client{}; GetClientRect(graph, &client);
        if (FAILED(target->BindDC(context, &client))) throw std::runtime_error(quest::i18n::narrow("errors.canvas_dc_binding"));
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0xC8C8C8));
        const auto size = target->GetSize();
        // Keep the recognizable original empty grid without card decorations or background dots.
        const auto visible_start = world({0,0});
        const auto visible_end = world({size.width,size.height});
        color(0xBFBFBF);
        for (float x = 80 + std::floor((visible_start.x-80)/206)*206; x < visible_end.x; x += 206) {
            for (float y = 90 + std::floor((visible_start.y-90)/124)*124; y < visible_end.y; y += 124) {
                const auto at = screen({x,y});
                target->FillRectangle(D2D1::RectF(at.x,at.y+17,at.x+kNodeWidth*zoom,at.y+kNodeHeight*zoom),brush);
            }
        }
        const auto sections = session.document.sections();
        std::map<int, Section> by_id;
        for (const auto& section : sections) {
            if (section.kind == "Quest") { const auto id = QuestId::parse(section.name); if (id) by_id[id->packed()] = section; }
        }
        // Map boundaries reference the game timeline and do not follow independent node layout offsets.
        for (size_t index = 0; index < level_timelines.size(); ++index) {
            const int timeline = level_timelines[index];
            const float x = screen({60.0f + timeline * 206.0f, 0}).x;
            color(0xA04040); target->DrawLine(D2D1::Point2F(x, 24), D2D1::Point2F(x, size.height), brush, 1);
            text(quest::i18n::wide("editor.level_prefix") + std::to_wstring(index+1), D2D1::RectF(x + 4, 3, x + 130, 22), 0x303030);
        }
        for (const auto& section : sections) {
            if (section.kind != "Quest") continue;
            for (int branch = 0; branch < 2; ++branch) {
                const int packed = session.document.connection_target(section, branch != 0);
                const auto found = by_id.find(packed);
                if (found == by_id.end()) continue;
                const auto from = node_position(section); const auto to = node_position(found->second);
                const bool selected_line = selected_connection && selected_connection->source == section.name
                    && selected_connection->branch == (branch != 0);
                draw_connection({from.x + kNodeWidth, from.y + kNodeHeight / 2}, {to.x, to.y + kNodeHeight / 2}, branch != 0,selected_line);
            }
        }
        for (const auto& section : sections) {
            if (section.kind != "Quest") continue;
            const auto position = screen(node_position(section));
            const auto rectangle = D2D1::RectF(position.x, position.y, position.x + kNodeWidth * zoom, position.y + kNodeHeight * zoom);
            if (rectangle.right < 0 || rectangle.left > size.width || rectangle.bottom < 0 || rectangle.top > size.height) continue;
            const auto id = QuestId::parse(section.name);
            bool header = false; if (id) header = id->index == 0;
            auto body = rectangle;
            if (zoom > 0.3f) body.top += 17;
            color(0xFFFFFF); if (header) color(0xFFB8B8);
            target->FillRectangle(body,brush);
            if (selection.contains(section.name)) {
                color(0x303080);
                target->DrawRectangle(rectangle,brush,1);
            }
            if (zoom > 0.3f) {
                std::wstring caption = decode(section.name, session.codepage);
                if (id) caption = std::to_wstring(id->group) + quest::i18n::wide("common.quest_id_separator") + std::to_wstring(id->index);
                text(caption,D2D1::RectF(rectangle.left+2,rectangle.top,rectangle.right-2,body.top),0x101010,true);
                auto content = decode(section.value("Comment"),session.codepage);
                const auto preview = previews.find(section.name);
                if (content.empty() && preview != previews.end()) content = preview->second;
                text(content,D2D1::RectF(body.left+3,body.top+2,body.right-12,body.bottom-2),0x101010);
            }
            draw_flags(section,body);
        }
        if (connecting) {
            const auto source = session.document.find("Quest", connection_source);
            if (source) { const auto position = node_position(*source); draw_connection({position.x + kNodeWidth, position.y + kNodeHeight / 2}, world(cursor), connection_branch); }
        }
        if (boxing) {
            color(0x2678C7, 0.12f);
            const auto rectangle = D2D1::RectF(std::min(mouse_start.x,cursor.x), std::min(mouse_start.y,cursor.y), std::max(mouse_start.x,cursor.x), std::max(mouse_start.y,cursor.y));
            target->FillRectangle(rectangle, brush); color(0x2678C7); target->DrawRectangle(rectangle, brush);
        }
        if (session.path.empty()) {
            text(quest::i18n::wide("editor.empty_heading"), D2D1::RectF(44, 70, size.width - 20, 110), 0x243D54, true);
            text(quest::i18n::wide("editor.empty_body"), D2D1::RectF(44, 115, size.width - 20, 185), 0x647E95);
        }
        if (picking) text(quest::i18n::wide("editor.picking_overlay"), D2D1::RectF(14, size.height - 34, size.width - 10, size.height - 6), 0x2678C7, true);
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) { release(brush); release(target); }
        update_scrollbars();
    }

    void update_scrollbars() {
        RECT client{}; GetClientRect(graph,&client);
        float right = 1600, bottom = 900;
        for (const auto& section : session.document.sections()) {
            if (section.kind != "Quest") continue;
            const auto at = node_position(section);
            right = std::max(right,at.x+kNodeWidth+206);
            bottom = std::max(bottom,at.y+kNodeHeight+124);
        }
        SCROLLINFO horizontal{sizeof(horizontal),SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
        horizontal.nMin = -200; horizontal.nMax = static_cast<int>(right);
        horizontal.nPage = static_cast<UINT>(client.right*96.0f/dpi/zoom);
        horizontal.nPos = static_cast<int>(-offset.x/zoom);
        SetScrollInfo(graph,SB_HORZ,&horizontal,TRUE);
        SCROLLINFO vertical = horizontal;
        vertical.nMin = -24; vertical.nMax = static_cast<int>(bottom);
        vertical.nPage = static_cast<UINT>(client.bottom*96.0f/dpi/zoom);
        vertical.nPos = static_cast<int>(-offset.y/zoom);
        SetScrollInfo(graph,SB_VERT,&vertical,TRUE);
    }

    void scroll(UINT message, WPARAM parameter) {
        int bar = SB_HORZ;
        if (message == WM_VSCROLL) bar = SB_VERT;
        SCROLLINFO info{sizeof(info),SIF_ALL};
        GetScrollInfo(graph,bar,&info);
        int position = info.nPos;
        switch (LOWORD(parameter)) {
        case SB_LINEUP: position -= 40; break;
        case SB_LINEDOWN: position += 40; break;
        case SB_PAGEUP: position -= info.nPage; break;
        case SB_PAGEDOWN: position += info.nPage; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: position = info.nTrackPos; break;
        case SB_TOP: position = info.nMin; break;
        case SB_BOTTOM: position = info.nMax; break;
        }
        position = std::clamp(position,info.nMin,std::max(info.nMin,info.nMax-static_cast<int>(info.nPage)+1));
        if (bar == SB_HORZ) offset.x = -position*zoom; else offset.y = -position*zoom;
        InvalidateRect(graph,nullptr,FALSE);
    }

    void mouse_down(UINT message, WPARAM keys, Position point) {
        SetFocus(graph); mouse_start = point; cursor = point; offset_start = offset;
        if (message == WM_MBUTTONDOWN || (GetKeyState(VK_SPACE) & 0x8000)) { panning = true; SetCapture(graph); return; }
        const auto section = hit(point);
        if (section) {
            if (picking) {
                const auto id = QuestId::parse(section->name);
                if (id) { selected_group = id->group; selected_index = id->index; finish(); }
                return;
            }
            if ((keys & MK_CONTROL) && selection.contains(section->name)) {
                selection.erase(section->name);
                selected.clear();
                if (!selection.empty()) selected = *selection.begin();
                refresh_selection();
                return;
            }
            if (!selection.contains(section->name) || (keys & MK_CONTROL)) select(section->name, "Quest", (keys & MK_CONTROL) != 0);
            else { selected = section->name; selected_kind = "Quest"; refresh_selection(); }
            // Dragging anywhere on the node body creates links; nodes no longer move freely.
            if (keys & MK_CONTROL) return;
            connecting = true; connection_source = section->name; connection_branch = false;
            SetCapture(graph);
        } else if (!picking) {
            const auto connection = hit_connection(point,true);
            if (connection) { select_connection(*connection); return; }
            selected_connection.reset();
            if (!(keys & MK_CONTROL)) selection.clear();
            boxing = true; SetCapture(graph); InvalidateRect(graph, nullptr, FALSE);
        }
    }

    void mouse_move(Position point) {
        cursor = point;
        if (panning) offset = {offset_start.x + point.x - mouse_start.x, offset_start.y + point.y - mouse_start.y};
        if (connecting) {
            const auto source = session.document.find("Quest",connection_source);
            if (source) connection_branch = world(point).x < node_position(*source).x;
        }
        if (panning || connecting || boxing) InvalidateRect(graph, nullptr, FALSE);
    }

    void mouse_up(Position point) {
        // Release capture before fallible edits so error dialogs cannot leave a link drag active.
        const bool was_connecting = connecting;
        const bool was_boxing = boxing;
        bool changed = false;
        panning = false;
       
        connecting = false;
        boxing = false;
        ReleaseCapture();
        if (was_connecting) {
            const auto destination = hit(point);
            if (destination && destination->name != connection_source) {
                const auto source = session.document.find("Quest",connection_source);
                if (source) {
                    session.connect(connection_source,destination->name,destination->number("TimeLine") < source->number("TimeLine"));
                    changed = true;
                }
            }
            else if (!destination && std::abs(point.x - mouse_start.x) + std::abs(point.y - mouse_start.y) > 15) {
                const auto cell = empty_cell(point);
                if (!cell) { refresh_selection(); return; }
                const auto location = *cell;
                const int group = row_at(location.y);
                const int timeline = std::max(0, static_cast<int>(std::round((location.x - 80) / 206)));
                const auto source = session.document.find("Quest", connection_source);
                if (!source) return;
                connection_branch = timeline < source->number("TimeLine");
                if ((!connection_branch && timeline <= source->number("TimeLine"))
                    || (connection_branch && timeline >= source->number("TimeLine"))) {
                    throw std::runtime_error(quest::i18n::narrow("errors.connection_timeline"));
                }
                const auto name = session.create_linked(connection_source, group, timeline, location, connection_branch);
                selected = name; selected_kind = "Quest"; selection = {name};
                changed = true;
            }
        }
        if (was_boxing) {
            const auto a = world(mouse_start), b = world(point);
            for (const auto& section : session.document.sections()) {
                if (section.kind != "Quest") continue;
                const auto position = node_position(section);
                if (position.x >= std::min(a.x,b.x) && position.y >= std::min(a.y,b.y)
                    && position.x + kNodeWidth <= std::max(a.x,b.x) && position.y + kNodeHeight <= std::max(a.y,b.y)) selection.insert(section.name);
            }
        }
        panning = false; connecting = false; boxing = false;
        ReleaseCapture();
        if (changed) refresh();
        else refresh_selection();
    }
};

std::unique_ptr<Editor> active_editor;
bool desired_topmost = true;

LRESULT CALLBACK graph_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        editor = static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
    }
    if (!editor) return DefWindowProcW(window, message, wparam, lparam);
    const Position point{GET_X_LPARAM(lparam) * 96.0f / editor->dpi, GET_Y_LPARAM(lparam) * 96.0f / editor->dpi};
    try {
        switch (message) {
        case WM_PAINT: editor->paint(); return 0;
        case WM_PRINTCLIENT: editor->paint(reinterpret_cast<HDC>(wparam)); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_HSCROLL: case WM_VSCROLL: editor->scroll(message,wparam); return 0;
        case WM_LBUTTONDOWN: case WM_MBUTTONDOWN: editor->mouse_down(message, wparam, point); return 0;
        case WM_MOUSEMOVE: editor->mouse_move(point); return 0;
        case WM_LBUTTONUP: case WM_MBUTTONUP: editor->mouse_up(point); return 0;
        case WM_CAPTURECHANGED: editor->panning = false; editor->connecting = false; editor->boxing = false; return 0;
        case WM_LBUTTONDBLCLK:
            if (!editor->picking && editor->hit(point)) {
                editor->select(editor->hit(point)->name,"Quest");
                editor->command(kProperties);
            } else if (!editor->picking) {
                const auto cell = editor->empty_cell(point);
                if (!cell) return 0;
                const auto location = *cell;
                editor->create_in_cell(location);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            POINT local{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}; ScreenToClient(window, &local);
            Position at{local.x * 96.0f / editor->dpi,local.y * 96.0f / editor->dpi};
            const auto before = editor->world(at);
            editor->zoom = std::clamp(editor->zoom * std::pow(1.12f, GET_WHEEL_DELTA_WPARAM(wparam) / 120.0f), 0.08f, 2.5f);
            editor->offset = {at.x - before.x * editor->zoom, at.y - before.y * editor->zoom};
            InvalidateRect(window, nullptr, FALSE); return 0;
        }
        case WM_RBUTTONUP: editor->toggle_boundary(point); return 0;
        case WM_KEYDOWN:
            if (wparam == VK_TAB) SetFocus(GetNextDlgTabItem(editor->window, window, (GetKeyState(VK_SHIFT) & 0x8000) != 0));
            else if (wparam == VK_DELETE) editor->command(kDelete);
            else if (wparam == VK_RETURN) editor->command(kProperties);
            else if (wparam == VK_ESCAPE) { editor->panning=false; editor->connecting=false; editor->boxing=false; editor->selected_connection.reset(); editor->update_status(); ReleaseCapture(); InvalidateRect(window,nullptr,FALSE); }
            else if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (wparam == 'S') editor->command(kSave);
                if (wparam == 'O') editor->command(kOpen);
                if (wparam == 'Z') editor->command(kUndo);
                if (wparam == 'Y') editor->command(kRedo);
                if (wparam == 'A') {
                    editor->selected_connection.reset();
                    for (const auto& section : editor->session.document.sections()) {
                        if (section.kind == "Quest") editor->selection.insert(section.name);
                    }
                    InvalidateRect(window,nullptr,FALSE);
                }
            }
            return 0;
        }
    } catch (const std::exception& error) { show_error(editor->window, error); }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK main_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        editor = static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        editor->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
    }
    if (!editor) return DefWindowProcW(window, message, wparam, lparam);
    try {
        switch (message) {
        case WM_CREATE: editor->initialize(); return 0;
        case WM_SIZE: editor->layout_controls(); return 0;
        case WM_NOTIFY: {
            const auto* change = reinterpret_cast<NMHDR*>(lparam);
            if (change->code != TCN_SELCHANGE) break;
            if (change->idFrom == kMapTabs) {
                const int index = TabCtrl_GetCurSel(editor->map_tabs);
                if (index >= 0 && index < static_cast<int>(editor->level_timelines.size())) {
                    editor->offset.x = 32-(60.0f+editor->level_timelines[index]*206.0f)*editor->zoom;
                    editor->offset.y = -24;
                    InvalidateRect(editor->graph,nullptr,FALSE);
                }
            }
            return 0;
        }
        case WM_DPICHANGED: {
            editor->update_dpi(HIWORD(wparam));
            const auto* bounds = reinterpret_cast<RECT*>(lparam);
            if (!editor->embedded) SetWindowPos(window,nullptr,bounds->left,bounds->top,
                bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
            info->ptMinTrackSize = {std::min<int>(MulDiv(760,editor->dpi,96),work.right-work.left),
                std::min<int>(MulDiv(520,editor->dpi,96),work.bottom-work.top)};
            return 0;
        }
        case WM_COMMAND: {
            const UINT id = LOWORD(wparam), notification = HIWORD(wparam);
            if (notification == BN_CLICKED || notification == 0) editor->command(id);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            SetTextColor(reinterpret_cast<HDC>(wparam),GetSysColor(COLOR_BTNTEXT));
            SetBkColor(reinterpret_cast<HDC>(wparam),GetSysColor(COLOR_BTNFACE));
            return reinterpret_cast<LRESULT>(editor->background);
        case WM_ERASEBKGND: {
            RECT client{}; GetClientRect(window,&client); FillRect(reinterpret_cast<HDC>(wparam),&client,editor->background); return 1;
        }
        case WM_CLOSE: editor->finish(); return 0;
        case WM_NCDESTROY: editor->window = nullptr; break;
        }
    } catch (const std::exception& error) { show_error(window, error); if (message == WM_CREATE) return -1; }
    return DefWindowProcW(window, message, wparam, lparam);
}

void register_class(HINSTANCE module, const wchar_t* name, WNDPROC procedure, UINT style) {
    WNDCLASSEXW type{sizeof(type)};
    type.hInstance = module; type.lpszClassName = name; type.lpfnWndProc = procedure;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    type.style = style; type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error(quest::i18n::narrow("errors.register_window_class"));
}

}

HWND create_editor(HINSTANCE module, HWND owner, const std::filesystem::path& initial_path) {
    try {
        if (active_editor && active_editor->window && !active_editor->pending_session()) return active_editor->window;
        destroy_editor();
        i18n::initialize();
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES | ICC_TAB_CLASSES}; InitCommonControlsEx(&controls);
        register_class(module, kMainClass, main_proc, CS_HREDRAW | CS_VREDRAW);
        register_class(module, kGraphClass, graph_proc, CS_DBLCLKS);
        register_class(module, kTextClass, text_proc, CS_HREDRAW | CS_VREDRAW);
        active_editor = std::make_unique<Editor>();
        active_editor->module = module; active_editor->owner = owner;
        active_editor->embedded = owner && !desired_topmost;
        DWORD style = WS_OVERLAPPEDWINDOW;
        DWORD extended_style = WS_EX_APPWINDOW;
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT, width = 1100, height = 720;
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        UINT window_dpi = GetDpiForSystem();
        if (owner) window_dpi = GetDpiForWindow(owner);
        width = std::min<int>(MulDiv(width,window_dpi,96),work.right-work.left-24);
        height = std::min<int>(MulDiv(height,window_dpi,96),work.bottom-work.top-24);
        if (active_editor->embedded) {
            // The original launcher calls SetTopMost(0) before embedding the DLL window in its container.
            style = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN;
            extended_style = 0;
            x = 0;
            y = 0;
            RECT bounds{};
            GetClientRect(owner,&bounds);
            if (bounds.right < 760 || bounds.bottom < 520) {
                SetWindowPos(owner,nullptr,0,0,1100,720,SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                GetClientRect(owner,&bounds);
            }
            width = bounds.right;
            height = bounds.bottom;
        }
        const HWND window = CreateWindowExW(extended_style, kMainClass, quest::i18n::wide("common.app_title"), style,
            x,y,width,height,owner,nullptr,module,active_editor.get());
        if (!window) { active_editor.reset(); return nullptr; }
        if (active_editor->embedded) SetWindowSubclass(owner,embedded_parent_proc,66,reinterpret_cast<DWORD_PTR>(window));
        const auto configuration_path = game_configuration_path(initial_path);
        if (std::filesystem::exists(configuration_path)) active_editor->load(configuration_path);
        ShowWindow(window, SW_SHOW);
        topmost(desired_topmost);
        SetForegroundWindow(window); SetFocus(active_editor->graph);
        return window;
    } catch (const std::exception& error) {
        show_error(owner,error);
        destroy_editor();
        return nullptr;
    }
}

void destroy_editor() {
    if (!active_editor) return;
    HINSTANCE module = active_editor->module;
    if (active_editor->window && IsWindow(active_editor->window)) DestroyWindow(active_editor->window);
    active_editor.reset();
    // Legacy hosts call FreeLibrary; unregister window classes that still reference DLL code.
    UnregisterClassW(kTextClass,module); UnregisterClassW(kGraphClass,module); UnregisterClassW(kMainClass,module);
}
void set_pick_mode() {
    if (!active_editor) return;
    active_editor->picking = true; active_editor->selected_group = 0; active_editor->selected_index = 0;
    active_editor->notice_key = "editor.picking_notice";
    active_editor->refresh();
}
void selected_data(int* group, int* index) {
    if (group) { *group = 0; if (active_editor) *group = active_editor->selected_group; }
    if (index) { *index = 0; if (active_editor) *index = active_editor->selected_index; }
}
void topmost(bool enabled) {
    desired_topmost = enabled;
    if (active_editor && active_editor->window && !active_editor->embedded) {
        HWND order = HWND_NOTOPMOST; if (enabled) order = HWND_TOPMOST;
        SetWindowPos(active_editor->window, order, 0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

}
