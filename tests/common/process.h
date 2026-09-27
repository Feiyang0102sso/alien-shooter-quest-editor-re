#pragma once
#include "test_environment.h"
#include <thread>
#include <memory>

// Keep only this test process's top-level windows off-screen and non-activating.
// A separate message-pump thread keeps handling windows during process waits.
class QuietWindows {
    static inline thread_local DWORD test_process_ = 0;
    HANDLE ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD thread_id_ = 0;
    HWINEVENTHOOK hook_ = nullptr;
    std::thread thread_;
    static void CALLBACK window_event(HWINEVENTHOOK, DWORD, HWND window, LONG object,
        LONG child, DWORD, DWORD) {
        if (!window || object != OBJID_WINDOW || child != CHILDID_SELF) return;
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process != test_process_) return;
        if (GetAncestor(window, GA_ROOT) != window) return;
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        SetWindowLongPtrW(window, GWL_EXSTYLE, (style | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
        SetWindowPos(window, HWND_BOTTOM, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }
public:
    explicit QuietWindows(DWORD process) {
        ensure(ready_ != nullptr, "Cannot create quiet-window ready event");
        thread_ = std::thread([this, process] {
            test_process_ = process;
            thread_id_ = GetCurrentThreadId();
            MSG message{};
            PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
            hook_ = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr,
                window_event, process, 0, WINEVENT_OUTOFCONTEXT);
            SetEvent(ready_);
            if (hook_) {
                while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                UnhookWinEvent(hook_);
            }
        });
        WaitForSingleObject(ready_, INFINITE);
    }
    bool ready() const { return hook_ != nullptr; }
    ~QuietWindows() {
        PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
        thread_.join();
        CloseHandle(ready_);
    }
};

class TestProcess {
    std::unique_ptr<QuietWindows> windows_;
    HANDLE job_ = nullptr;
public:
    PROCESS_INFORMATION info{};
    TestProcess(const std::filesystem::path& executable, const std::wstring& arguments,
        const std::wstring& desktop = L"", WORD show = SW_HIDE) {
        STARTUPINFOW inherited{};
        GetStartupInfoW(&inherited);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        startup.wShowWindow = show;
        startup.lpDesktop = inherited.lpDesktop;
        if (!desktop.empty()) startup.lpDesktop = const_cast<wchar_t*>(desktop.c_str());
        std::wstring command = L"\"" + executable.wstring() + L"\" " + arguments;
        ensure(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED,
            nullptr, executable.parent_path().c_str(), &startup, &info) != FALSE,
            "Cannot start test application");
        try {
            // Closing this job also stops descendants if a test worker crashes or times out.
            job_ = CreateJobObjectW(nullptr, nullptr);
            ensure(job_ != nullptr, "Cannot create test process job");
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            ensure(SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
                "Cannot configure test process job");
            ensure(AssignProcessToJobObject(job_, info.hProcess), "Cannot isolate test process tree");
            if (desktop.empty()) {
                windows_ = std::make_unique<QuietWindows>(info.dwProcessId);
                ensure(windows_->ready(), "Cannot install quiet-window hook");
            }
            ensure(ResumeThread(info.hThread) != static_cast<DWORD>(-1), "Cannot resume test application");
        } catch (...) {
            // A constructor failure must not leave a suspended child holding game files.
            TerminateProcess(info.hProcess, 1);
            WaitForSingleObject(info.hProcess, 5000);
            CloseHandle(info.hThread);
            CloseHandle(info.hProcess);
            if (job_) CloseHandle(job_);
            throw;
        }
        CloseHandle(info.hThread);
    }
    TestProcess(const TestProcess&) = delete;
    TestProcess& operator=(const TestProcess&) = delete;
    ~TestProcess() {
        // Only terminate the exact child started by this test, never other game processes.
        if (WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT) {
            TerminateProcess(info.hProcess, 1);
            WaitForSingleObject(info.hProcess, 5000);
        }
        CloseHandle(info.hProcess);
        if (job_) CloseHandle(job_);
    }
    DWORD exit_code(DWORD timeout) {
        ensure(WaitForSingleObject(info.hProcess, timeout) == WAIT_OBJECT_0,
            "Test application timed out");
        DWORD code = 1;
        ensure(GetExitCodeProcess(info.hProcess, &code), "Cannot read child exit code");
        return code;
    }
    void wait(DWORD timeout) {
        ensure(exit_code(timeout) == 0,
            "Test application exited abnormally");
    }
};

struct TestWindowQuery {
    DWORD process;
    const wchar_t* type;
    HWND result = nullptr;
};
inline BOOL CALLBACK find_test_window(HWND window, LPARAM state) {
    auto& query = *reinterpret_cast<TestWindowQuery*>(state);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    wchar_t type[128]{};
    GetClassNameW(window, type, 128);
    if (process == query.process && std::wstring(type) == query.type) {
        query.result = window;
        return FALSE;
    }
    return TRUE;
}
inline void require_offscreen(HWND window) {
    RECT bounds{}, intersection{};
    GetWindowRect(window, &bounds);
    RECT screen{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
    screen.right = screen.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    screen.bottom = screen.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    ensure(!IsWindowVisible(window) || !IntersectRect(&intersection, &bounds, &screen),
        "Test window must remain hidden or off-screen");
}
inline HWND hidden_test_window(const TestProcess& process, const wchar_t* type) {
    const auto deadline = GetTickCount64() + 30000;
    while (GetTickCount64() < deadline) {
        TestWindowQuery query{process.info.dwProcessId, type};
        EnumWindows(find_test_window, reinterpret_cast<LPARAM>(&query));
        if (query.result) {
            SetWindowPos(query.result, HWND_BOTTOM, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            require_offscreen(query.result);
            return query.result;
        }
        ensure(WaitForSingleObject(process.info.hProcess, 0) == WAIT_TIMEOUT,
            "Test application exited before creating its window");
        Sleep(10);
    }
    throw std::runtime_error("Hidden test window did not appear");
}
