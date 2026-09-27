#include <string>
#include "controls.h"
#include <commctrl.h>

namespace quest {
namespace {
LRESULT CALLBACK keyboard_proc(HWND window, UINT message, WPARAM key, LPARAM parameter,
                               UINT_PTR, DWORD_PTR) {
    if (message == WM_KEYDOWN) {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        HWND parent = GetParent(window);
        UINT command = 0;
        if (control && key == 'S') command = 101;
        if (control && key == 'O') command = 100;
        wchar_t type[32]{};
        GetClassNameW(window,type,32);
        if (control && std::wstring(type) != L"Edit") {
            if (key == 'Z') command = 102;
            if (key == 'Y') command = 103;
        }
        if (command) {
            SendMessageW(parent, WM_COMMAND, command, 0);
            return 0;
        }
        if (key == VK_TAB && !control) {
            const bool previous = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            HWND next = GetNextDlgTabItem(parent, window, previous);
            if (next) SetFocus(next);
            return 0;
        }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, keyboard_proc, 1);
    return DefSubclassProc(window, message, key, parameter);
}
}

std::wstring control_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring content(length + 1, L'\0');
    GetWindowTextW(control, content.data(), length + 1);
    content.resize(length);
    return content;
}

HWND child(HWND parent, const wchar_t* type, const wchar_t* text,
           DWORD style, int id, HFONT font) {
    HWND result = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 100, 26, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    // Install accelerators only for the main editor; text modals use their own dialog message loop.
    wchar_t parent_class[64]{};
    GetClassNameW(parent, parent_class, 64);
    if (std::wstring(parent_class) == L"QuestEditorRE.Main.v1") {
        SetWindowSubclass(result, keyboard_proc, 1, 0);
    }
    return result;
}

}
