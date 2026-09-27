#pragma once
#include <windows.h>
#include <string>

namespace quest {
// Use native system controls and buttons to preserve keyboard and accessibility semantics.
HWND child(HWND parent, const wchar_t* type, const wchar_t* text,
           DWORD style, int id, HFONT font);
std::wstring control_text(HWND control);
}
