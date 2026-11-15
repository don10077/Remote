/**
 * @file cliFilesTest.cpp
 * @brief Запись резервных копий консольной утилиты: свободное имя и запись через временный файл.
 */

#include "../cli/src/cliFiles.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

/// Временная папка теста; удаляется вместе с содержимым.
struct TempFolder {
    std::filesystem::path path;
    TempFolder() {
        path = std::filesystem::temp_directory_path() /
               ("harmony-cli-files-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TempFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(CliFiles, FreePathKeepsExistingFiles) {
    const TempFolder folder;
    const auto wanted = folder.path / "backup.EZHex";
    EXPECT_EQ(harmony::cli::freePath(wanted), wanted);

    ASSERT_TRUE(harmony::cli::writeFileAtomic(wanted, {1}));
    EXPECT_EQ(harmony::cli::freePath(wanted), folder.path / "backup-2.EZHex");

    ASSERT_TRUE(harmony::cli::writeFileAtomic(folder.path / "backup-2.EZHex", {2}));
    EXPECT_EQ(harmony::cli::freePath(wanted), folder.path / "backup-3.EZHex");
}

TEST(CliFiles, WriteLeavesNoTemporaryFile) {
    const TempFolder folder;
    const auto file = folder.path / "copy.EZHex";
    const std::vector<std::uint8_t> bytes = {0x50, 0x4B, 0x03, 0x04};

    ASSERT_TRUE(harmony::cli::writeFileAtomic(file, bytes));

    EXPECT_EQ(readBytes(file), bytes);
    EXPECT_FALSE(std::filesystem::exists(folder.path / "copy.EZHex.tmp"));
}

TEST(CliFiles, FailedWriteKeepsNothing) {
    const TempFolder folder;
    // Папки нет: записать некуда, и на её месте не должно появиться ни копии, ни временного файла.
    const auto file = folder.path / "missing" / "copy.EZHex";

    EXPECT_FALSE(harmony::cli::writeFileAtomic(file, {1, 2, 3}));

    EXPECT_FALSE(std::filesystem::exists(file));
    EXPECT_FALSE(std::filesystem::exists(folder.path / "missing" / "copy.EZHex.tmp"));
}
