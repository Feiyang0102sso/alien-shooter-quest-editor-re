#pragma once
#include "test_environment.h"

class TemporaryFile {
    std::filesystem::path target_;
    std::filesystem::path backup_;
    bool existed_ = false;
    bool restored_ = false;
public:
    TemporaryFile(const std::filesystem::path& target, const std::filesystem::path& recovery)
        : target_(target), backup_(recovery / (target.filename().wstring() + L".original")) {
        std::filesystem::create_directories(recovery);
        ensure(!std::filesystem::exists(backup_), "Unrestored backup exists in out/test/recovery; restore it before retrying");
        existed_ = std::filesystem::exists(target_);
        if (existed_) std::filesystem::copy_file(target_, backup_);
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    void restore() {
        if (restored_) return;
        if (existed_) {
            std::filesystem::copy_file(backup_, target_, std::filesystem::copy_options::overwrite_existing);
            std::filesystem::remove(backup_);
        } else {
            std::filesystem::remove(target_);
        }
        restored_ = true;
    }
    ~TemporaryFile() {
        // Explicit restoration reports errors; this fallback protects exception paths.
        try { restore(); }
        catch (const std::exception& error) { ADD_FAILURE() << "Restore failed: " << error.what(); }
    }
};

// Isolate the engine's registry settings; never edit the player's FullScreen value.
class TestGameSettings {
    std::wstring key_path_ = L"SOFTWARE\\QuestEditorRE\\Tests\\Run-" + std::to_wstring(GetCurrentProcessId());
    HKEY key_ = nullptr;
public:
    TestGameSettings() {
        DWORD disposition = 0;
        ensure(RegCreateKeyExW(HKEY_CURRENT_USER, key_path_.c_str(), 0, nullptr, 0,
            KEY_ALL_ACCESS, nullptr, &key_, &disposition) == ERROR_SUCCESS,
            "Cannot create test registry settings");
        if (disposition != REG_CREATED_NEW_KEY) {
            RegCloseKey(key_);
            key_ = nullptr;
            throw std::runtime_error("Test registry key already exists; refusing to overwrite it");
        }
    }
    ~TestGameSettings() {
        RegCloseKey(key_);
        if (RegDeleteTreeW(HKEY_CURRENT_USER, key_path_.c_str()) != ERROR_SUCCESS) {
            ADD_FAILURE() << "Cannot remove private test registry key";
        }
    }
    void configure(const std::filesystem::path& source, const std::filesystem::path& target) {
        const DWORD windowed = 0;
        ensure(RegSetValueExW(key_, L"FullScreen", 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&windowed), sizeof(windowed)) == ERROR_SUCCESS,
            "Cannot force windowed test mode");
        const DWORD offscreen = static_cast<DWORD>(-32000);
        for (const auto* name : {L"WindowPositionX", L"WindowPositionY"}) {
            ensure(RegSetValueExW(key_, name, 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&offscreen), sizeof(offscreen)) == ERROR_SUCCESS,
                "Cannot configure off-screen startup");
        }
        write(target, quest::read_file(source));
        const auto registry = L"HKEY_CURRENT_USER\\" + key_path_;
        ensure(WritePrivateProfileStringW(L"common", L"RegPath", registry.c_str(), target.c_str()),
            "Cannot configure test registry path");
    }
};
