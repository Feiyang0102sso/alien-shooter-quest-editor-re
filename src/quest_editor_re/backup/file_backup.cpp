#include "file_backup.h"
#include "../game_files/file_io.h"
#include "../i18n/i18n.h"
#include <windows.h>
#include <stdexcept>

namespace quest {
static void write_temporary(const std::filesystem::path& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error(quest::i18n::narrow("errors.create_temporary_file"));
    DWORD written = 0;
    const bool success = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        && written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!success) {
        DeleteFileW(path.c_str());
        throw std::runtime_error(quest::i18n::narrow("errors.flush_temporary_file"));
    }
}

static std::filesystem::path backup_slot(const std::filesystem::path& path, int index) {
    return path.wstring()+L".qere."+std::to_wstring(index)+L".bak";
}

// Match only editor-generated numbered or legacy PID.tick.index backups; leave user backups alone.
static bool generated_backup(const std::wstring& filename, const std::wstring& base) {
    const auto prefix = base+L".qere.";
    if (!filename.starts_with(prefix) || !filename.ends_with(L".bak")) return false;
    const auto number = filename.substr(prefix.size(),filename.size()-prefix.size()-4);
    int groups = 1;
    bool has_digit = false;
    for (const wchar_t character : number) {
        if (character >= L'0' && character <= L'9') { has_digit = true; continue; }
        if (character != L'.' || !has_digit) return false;
        ++groups;
        has_digit = false;
    }
    return has_digit && (groups == 1 || groups == 3);
}

static bool rotate_backups(const FileChange& change, const std::filesystem::path& incoming) {
    for (int index = change.backup_limit; index > 1; --index) {
        const auto previous = backup_slot(change.path,index-1);
        const DWORD attributes = GetFileAttributesW(previous.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) continue;
            return false;
        }
        const auto next = backup_slot(change.path,index);
        if (!MoveFileExW(previous.c_str(),next.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
    }
    const auto latest = backup_slot(change.path,1);
    if (!MoveFileExW(incoming.c_str(),latest.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;

    // Clean old backups only after all files commit; rollback uses separate transaction backups.
    std::error_code error;
    auto directory = change.path.parent_path();
    if (directory.empty()) directory = L".";
    std::filesystem::directory_iterator entry(directory,error);
    if (error) return false;
    const std::filesystem::directory_iterator end;
    bool complete = true;
    while (entry != end) {
        const auto candidate = entry->path();
        const auto filename = candidate.filename().wstring();
        bool keep = false;
        for (int index = 1; index <= change.backup_limit; ++index) {
            if (filename == backup_slot(change.path,index).filename().wstring()) keep = true;
        }
        if (!keep && generated_backup(filename,change.path.filename().wstring())) {
            if (!DeleteFileW(candidate.c_str())) complete = false;
        }
        entry.increment(error);
        if (error) return false;
    }
    return complete;
}

bool save_files(const std::vector<FileChange>& changes) {
    struct Staged {
        FileChange change;
        std::filesystem::path temporary;
        std::filesystem::path backup;
        bool committed = false;
    };
    std::vector<Staged> staged;
    try {
        for (const auto& change : changes) {
            if (change.existed && change.before == change.after) continue;
            if (change.existed) {
                if (read_file(change.path) != change.before) throw std::runtime_error(quest::i18n::narrow("errors.file_changed"));
                const DWORD attributes = GetFileAttributesW(change.path.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)) throw std::runtime_error(quest::i18n::narrow("errors.file_read_only"));
            } else if (std::filesystem::exists(change.path)) {
                throw std::runtime_error(quest::i18n::narrow("errors.destination_exists"));
            }
            const std::wstring suffix = L".qere." + std::to_wstring(GetCurrentProcessId()) + L"."
                + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(staged.size());
            Staged item{change, change.path.wstring() + suffix + L".tmp", change.path.wstring() + suffix + L".bak"};
            write_temporary(item.temporary, change.after);
            staged.push_back(item);
        }
        for (auto& item : staged) {
            BOOL success = FALSE;
            if (item.change.existed) {
                // Recheck to narrow the race window for external edits.
                if (read_file(item.change.path) != item.change.before) throw std::runtime_error(quest::i18n::narrow("errors.file_changed_during_save"));
                success = ReplaceFileW(item.change.path.c_str(), item.temporary.c_str(), item.backup.c_str(), 0, nullptr, nullptr);
            } else {
                success = MoveFileExW(item.temporary.c_str(), item.change.path.c_str(), MOVEFILE_WRITE_THROUGH);
            }
            if (!success) throw std::runtime_error(quest::i18n::narrow("errors.replace_file_prefix") + std::to_string(GetLastError()) + quest::i18n::narrow("errors.replace_file_suffix"));
            item.committed = true;
        }
    } catch (...) {
        bool rollback_failed = false;
        for (auto item = staged.rbegin(); item != staged.rend(); ++item) {
            if (item->committed) {
                BOOL restored = FALSE;
                if (item->change.existed) restored = MoveFileExW(item->backup.c_str(), item->change.path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                else restored = DeleteFileW(item->change.path.c_str());
                if (!restored) rollback_failed = true;
            }
            DeleteFileW(item->temporary.c_str());
        }
        if (rollback_failed) throw std::runtime_error(quest::i18n::narrow("errors.rollback_incomplete"));
        throw;
    }
    bool backups_complete = true;
    for (const auto& item : staged) {
        if (item.change.existed && item.change.backup_limit > 0) {
            if (!rotate_backups(item.change,item.backup)) backups_complete = false;
        }
    }
    if (!backups_complete) OutputDebugStringA(quest::i18n::narrow("errors.backup_rotation_failed"));
    return backups_complete;
}

}
