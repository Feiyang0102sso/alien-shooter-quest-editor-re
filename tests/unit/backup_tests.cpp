#include "../common/test_environment.h"

namespace {
using Backup = test_support::Files;

TEST_F(Backup, CreatesBackupForEditedConfiguration) {
    const std::string mission = "<1_1_1>\r\n<Font=17>Hello\r\nworld\r\n[heading]\r\nTitle\r\n";
    const auto temporary = workspace / L"test-output";
    std::filesystem::create_directories(temporary);
    const auto config = temporary / L"levels.cfg";
    write(config, fixture);
    write(temporary / L"mission.txt", mission);
    quest::EditSession session;
    session.open(config);
    session.checkpoint();
    session.document.set("Quest", "1_1_1", "Comment", "saved");
    session.save();
    ASSERT_TRUE((quest::read_file(config) == session.document.bytes())) << "save edited configuration";
    bool backup = false;
    for (const auto& entry : std::filesystem::directory_iterator(temporary)) {
        if (entry.path().extension() == L".bak") backup = true;
    }
    ASSERT_TRUE((backup)) << "backup created";
}

TEST_F(Backup, RejectsExternalEdits) {
    const std::string mission = "<1_1_1>\r\n<Font=17>Hello\r\nworld\r\n[heading]\r\nTitle\r\n";
    const auto temporary = workspace / L"test-output";
    std::filesystem::create_directories(temporary);
    const auto config = temporary / L"levels.cfg";
    write(config, fixture);
    write(temporary / L"mission.txt", mission);
    quest::EditSession session;
    session.open(config);
    session.document.set("Quest", "1_1_1", "Comment", "next");
    write(config, "external change");
    EXPECT_THROW(([&] { session.save(); })(), std::exception) << "external changes block save";
    ASSERT_TRUE((quest::read_file(config) == "external change")) << "external content preserved";
}

TEST_F(Backup, RejectsReadOnlyTarget) {
    const std::string mission = "<1_1_1>\r\n<Font=17>Hello\r\nworld\r\n[heading]\r\nTitle\r\n";
    const auto temporary = workspace / L"test-output";
    std::filesystem::create_directories(temporary);
    const auto config = temporary / L"levels.cfg";
    write(config, fixture);
    write(temporary / L"mission.txt", mission);
    quest::EditSession session;
    session.open(config);
    write(config, fixture);
    session.open(config);
    session.document.set("Quest", "1_1_1", "Comment", "readonly");
    SetFileAttributesW(config.c_str(), FILE_ATTRIBUTE_READONLY);
    EXPECT_THROW(([&] { session.save(); })(), std::exception) << "read-only save fails";
    SetFileAttributesW(config.c_str(), FILE_ATTRIBUTE_NORMAL);
}

TEST_F(Backup, RollsBackWhenSecondFileIsLocked) {
    const std::string mission = "<1_1_1>\r\n<Font=17>Hello\r\nworld\r\n[heading]\r\nTitle\r\n";
    const auto temporary = workspace / L"test-output";
    std::filesystem::create_directories(temporary);
    const auto config = temporary / L"levels.cfg";
    write(config, fixture);
    write(temporary / L"mission.txt", mission);
    quest::EditSession session;
    session.open(config);
    const auto second = temporary / L"second.txt";
    write(second, "before");
    HANDLE lock = CreateFileW(second.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_TRUE((lock != INVALID_HANDLE_VALUE)) << "lock fixture";
    EXPECT_THROW(([&] { quest::save_files({{config, fixture, "first changed", true}, {second, "before", "after", true}}); })(), std::exception) << "second commit failure";
    CloseHandle(lock);
    ASSERT_TRUE((quest::read_file(config) == fixture)) << "multi-file save rollback";
    ASSERT_TRUE((quest::read_file(second) == "before")) << "failed file preserved";
}

TEST_F(Backup, RotatesBoundedHistoryAndRecoversAfterFailure) {
    const auto directory = workspace/L"db-and-backups";
    std::filesystem::create_directories(directory);
    const auto cfg = directory/L"levels.cfg";
    const auto db = directory/L"levels.db";
    const auto mission = directory/L"mission.txt";
    write(db,fixture);
    write(mission,"<first>\r\ninitial\r\n");
    quest::EditSession session;
    session.open(directory);
    ASSERT_TRUE((session.path == db)) <<"directory falls back to levels.db";
    session.document.set("Quest","1_1_1","Comment","DB source");
    ASSERT_TRUE((session.save())) <<"DB saves with backup rotation";
    const auto db_bytes = quest::read_file(db);
    ASSERT_TRUE((!std::filesystem::exists(cfg) && db_bytes.find("DB source") != std::string::npos)) <<
        "DB edit saves back to DB without creating CFG";
    write(cfg,fixture);
    session.open(directory);
    ASSERT_TRUE((session.path == cfg && session.document.bytes() == fixture)) <<"directory prefers CFG when both exist";
    session.open(db);
    ASSERT_TRUE((session.path == cfg)) <<"explicit same-name DB also respects CFG priority";

    const auto legacy = cfg.wstring()+L".qere.42.123.0.bak";
    const auto manual = cfg.wstring()+L".manual.bak";
    write(legacy,"old generated backup");
    write(manual,"user backup");
    std::vector<std::string> cfg_history{fixture};
    std::vector<std::string> mission_history{session.mission};
    for (int round = 1; round <= 5; ++round) {
        session.document.set("Quest","1_1_1","Comment","revision "+std::to_string(round));
        session.mission = "<first>\r\nrevision "+std::to_string(round)+"\r\n";
        ASSERT_TRUE((session.save())) <<"multi-file save completes backup rotation";
        cfg_history.push_back(session.document.bytes());
        mission_history.push_back(session.mission);
        ASSERT_TRUE((quest::read_file(cfg.wstring()+L".qere.1.bak") == cfg_history[round-1]
            && quest::read_file(mission.wstring()+L".qere.1.bak") == mission_history[round-1])) <<
            "slot 1 preserves immediately preceding CFG and mission content";
        if (round > 1) {
            ASSERT_TRUE((quest::read_file(cfg.wstring()+L".qere.2.bak") == cfg_history[round-2]
                && quest::read_file(mission.wstring()+L".qere.2.bak") == mission_history[round-2])) <<
                "slot 2 preserves second preceding content after repeated saves";
        }
        int cfg_backups = 0, mission_backups = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            const auto name = entry.path().filename().wstring();
            if (!name.ends_with(L".bak")) continue;
            if (name.starts_with(L"levels.cfg.qere.")) ++cfg_backups;
            if (name.starts_with(L"mission.txt.qere.")) ++mission_backups;
        }
        ASSERT_TRUE((cfg_backups <= quest::backup::max_copies && mission_backups <= quest::backup::max_copies)) <<"backup count bounded independently per game file";
    }
    ASSERT_TRUE((!std::filesystem::exists(legacy) && quest::read_file(manual) == "user backup")) <<
        "old generated backup removed while user backup remains";
    ASSERT_TRUE((quest::read_file(db) == db_bytes)) <<"CFG saves do not touch neighboring DB";
    const auto latest_backup = quest::read_file(cfg.wstring()+L".qere.1.bak");
    const auto older_backup = quest::read_file(cfg.wstring()+L".qere.2.bak");
    session.save();
    ASSERT_TRUE((quest::read_file(cfg.wstring()+L".qere.1.bak") == latest_backup
        && quest::read_file(cfg.wstring()+L".qere.2.bak") == older_backup)) <<"no-op save does not rotate backup history";

    HANDLE lock = CreateFileW(mission.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    ASSERT_TRUE((lock != INVALID_HANDLE_VALUE)) <<"lock mission during multi-file save";
    session.document.set("Quest","1_1_1","Comment","failed transaction");
    session.mission += "failed transaction\r\n";
    EXPECT_THROW(([&] { session.save(); })(), std::exception) <<"second file failure still rolls back with limited backups";
    CloseHandle(lock);
    ASSERT_TRUE((quest::read_file(cfg) == cfg_history.back() && quest::read_file(mission) == mission_history.back())) <<
        "failed save restores both original files";
    ASSERT_TRUE((quest::read_file(cfg.wstring()+L".qere.1.bak") == latest_backup
        && quest::read_file(cfg.wstring()+L".qere.2.bak") == older_backup)) <<"failed transaction leaves numbered backup history intact";
    session.open(cfg);
    const auto second_backup = cfg.wstring()+L".qere.2.bak";
    lock = CreateFileW(second_backup.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    ASSERT_TRUE((lock != INVALID_HANDLE_VALUE)) <<"lock old backup during rotation";
    session.document.set("Quest","1_1_1","Comment","saved despite locked backup");
    const bool backups_complete = session.save();
    CloseHandle(lock);
    ASSERT_TRUE((!backups_complete && !session.dirty() && quest::read_file(cfg) == session.document.bytes())) <<
        "backup failure reports saved content accurately and retains clean session";
    session.document.set("Quest","1_1_1","Comment","rotation recovered");
    ASSERT_TRUE((session.save())) <<"next successful save recovers backup rotation";
    int remaining = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto name = entry.path().filename().wstring();
        if (name.starts_with(L"levels.cfg.qere.") && name.ends_with(L".bak")) ++remaining;
    }
    ASSERT_TRUE((remaining == quest::backup::max_copies)) <<"recovered save removes temporary recovery backup beyond limit";
}


TEST_F(Backup, CreatesMissingFileWithoutOverwritingAnExternalArrival) {
    const auto path = workspace / "new.txt";
    ASSERT_TRUE(quest::save_files({{path, "", "first", false}}));
    EXPECT_EQ(quest::read_file(path), "first");
    EXPECT_THROW(quest::save_files({{path, "", "replacement", false}}), std::runtime_error);
    EXPECT_EQ(quest::read_file(path), "first");
}
TEST_F(Backup, EmptyTransactionDoesNotCreateFiles) {
    EXPECT_TRUE(quest::save_files({}));
    EXPECT_TRUE(std::filesystem::is_empty(workspace));
}

}
