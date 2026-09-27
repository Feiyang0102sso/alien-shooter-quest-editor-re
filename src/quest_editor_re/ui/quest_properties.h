#pragma once
#include <windows.h>
#include "../core/edit_session.h"

namespace quest {
// Modal properties edit a session copy; OK commits once and Cancel creates no undo entry.
bool edit_properties(HWND owner, HINSTANCE module, EditSession& session,
                     const std::string& kind, const std::string& name);
}
