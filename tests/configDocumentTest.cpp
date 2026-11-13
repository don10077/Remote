/**
 * @file configDocumentTest.cpp
 * @brief Тесты документа конфигурации: создание, открытие, сохранение без потерь.
 */

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using harmony::ConfigDocument;
using harmony::ErrorCode;

namespace {

const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";

/// Читает файл целиком.
std::string readFile(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

/// Временная папка теста, удаляется в конце.
class TempDir {
public:
    TempDir() : path_(std::filesystem::temp_directory_path() /
                      ("harmony-test-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)))) {
        std::filesystem::remove_all(path_);
    }
    ~TempDir() { std::filesystem::remove_all(path_); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST(ConfigDocument, NewConfigHasOnlyPowerOff) {
    const ConfigDocument document = ConfigDocument::createNew("Гостиная");

    EXPECT_EQ(document.name(), "Гостиная");
    EXPECT_FALSE(document.hasFolder());
    EXPECT_TRUE(document.model().devices.empty());
    ASSERT_EQ(document.model().activities.size(), 1U);
    EXPECT_TRUE(document.model().activities.front().isPowerOff());
    EXPECT_EQ(document.model().activities.front().label, "Power off");
    EXPECT_EQ(document.model().property("version"), "1.0");
    EXPECT_EQ(document.model().property("LastUpdated").size(), 15U); // «20261025 094400»
}

TEST(ConfigDocument, NewConfigNeedsSaveAs) {
    ConfigDocument document = ConfigDocument::createNew("Тест");

    const auto saved = document.save();

    ASSERT_FALSE(saved);
    EXPECT_EQ(saved.error().code, ErrorCode::Unsupported);
}

TEST(ConfigDocument, FailedFolderSaveRestoresAlreadyReplacedFiles) {
    TempDir temp;
    auto original = ConfigDocument::open(kSampleDir);
    ASSERT_TRUE(original);
    ASSERT_TRUE(original->saveAs(temp.path()));
    const auto xmlBefore = readFile(temp.path() / "UserConfiguration.xml");
    const auto actionsBefore = readFile(temp.path() / "ActionLists.xml");
    std::filesystem::remove(temp.path() / "IrProto.bin");
    std::filesystem::create_directory(temp.path() / "IrProto.bin");
    auto replacement = ConfigDocument::createNew("replacement");
    EXPECT_FALSE(replacement.saveAs(temp.path()));
    EXPECT_EQ(readFile(temp.path() / "UserConfiguration.xml"), xmlBefore);
    EXPECT_EQ(readFile(temp.path() / "ActionLists.xml"), actionsBefore);
    EXPECT_TRUE(std::filesystem::is_directory(temp.path() / "IrProto.bin"));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / ".harmony-save"));
}

TEST(ConfigDocument, SaveAsCreatesFolderAndReopens) {
    TempDir temp;
    ConfigDocument document = ConfigDocument::createNew("Тест");

    const auto saved = document.saveAs(temp.path() / "Спальня");

    ASSERT_TRUE(saved) << saved.error().message;
    EXPECT_EQ(document.name(), "Спальня");
    const auto reopened = ConfigDocument::open(temp.path() / "Спальня");
    ASSERT_TRUE(reopened) << reopened.error().message;
    EXPECT_EQ(reopened->model().activities.size(), 1U);
    EXPECT_EQ(reopened->name(), "Спальня");
}

TEST(ConfigDocument, SavingSampleIsLossless) {
    TempDir temp;
    auto document = ConfigDocument::open(kSampleDir);
    ASSERT_TRUE(document) << document.error().message;

    const auto saved = document->saveAs(temp.path() / "копия");

    ASSERT_TRUE(saved) << saved.error().message;
    // Файл совпадает байт в байт — ничего не потеряно и не переформатировано.
    EXPECT_EQ(readFile(temp.path() / "копия" / "UserConfiguration.xml"),
              readFile(kSampleDir / "UserConfiguration.xml"));
}

TEST(ConfigDocument, SaveAsCopiesCompanionFiles) {
    TempDir temp;
    auto document = ConfigDocument::open(kSampleDir);
    ASSERT_TRUE(document) << document.error().message;

    ASSERT_TRUE(document->saveAs(temp.path()));

    for (const auto companion : ConfigDocument::kCompanionFiles) {
        EXPECT_EQ(readFile(temp.path() / companion), readFile(kSampleDir / companion)) << companion;
    }
}

TEST(ConfigDocument, SaveWritesIntoOwnFolder) {
    TempDir temp;
    ConfigDocument document = ConfigDocument::createNew("Тест");
    ASSERT_TRUE(document.saveAs(temp.path()));
    std::filesystem::remove(temp.path() / "UserConfiguration.xml");

    const auto saved = document.save();

    ASSERT_TRUE(saved) << saved.error().message;
    EXPECT_TRUE(std::filesystem::exists(temp.path() / "UserConfiguration.xml"));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / "UserConfiguration.xml.tmp"));
}

TEST(ConfigDocument, OpenMissingFolderIsNotFound) {
    const auto document = ConfigDocument::open(kSampleDir / "нет-такой");

    ASSERT_FALSE(document);
    EXPECT_EQ(document.error().code, ErrorCode::NotFound);
}

TEST(ConfigDocument, FromFilesKeepsAllFiles) {
    TempDir temp;
    ConfigDocument::Files files;
    files.userConfiguration = readFile(kSampleDir / "UserConfiguration.xml");
    files.actionLists = readFile(kSampleDir / "ActionLists.xml");
    const std::string irProto = readFile(kSampleDir / "IrProto.bin");
    const std::string ssIr = readFile(kSampleDir / "SsIr.bin");
    files.irProto.assign(irProto.begin(), irProto.end());
    files.ssIr.assign(ssIr.begin(), ssIr.end());

    auto document = ConfigDocument::fromFiles(files, "пример");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_FALSE(document->hasFolder());
    ASSERT_TRUE(document->saveAs(temp.path()));

    for (const char* name : {"UserConfiguration.xml", "ActionLists.xml", "IrProto.bin", "SsIr.bin"}) {
        EXPECT_EQ(readFile(temp.path() / name), readFile(kSampleDir / name)) << name;
    }
}

TEST(ConfigDocument, NewConfigSavesCompleteSet) {
    TempDir temp;
    ConfigDocument document = ConfigDocument::createNew("Новая");

    ASSERT_TRUE(document.saveAs(temp.path()));

    // Пульту нужны все четыре файла, даже если устройств ещё нет.
    for (const char* name : {"UserConfiguration.xml", "ActionLists.xml", "IrProto.bin", "SsIr.bin"}) {
        EXPECT_TRUE(std::filesystem::exists(temp.path() / name)) << name;
    }
    auto reopened = ConfigDocument::open(temp.path());
    ASSERT_TRUE(reopened) << reopened.error().message;
}

TEST(ConfigDocument, LeftoverStagingFolderBlocksOpeningButFinishedOneDoesNot) {
    TempDir temp;
    auto document = ConfigDocument::open(kSampleDir);
    ASSERT_TRUE(document);
    ASSERT_TRUE(document->saveAs(temp.path()));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / ".harmony-save"));

    // Прерванное сохранение: его файлы нужны для восстановления — открыть нельзя.
    std::filesystem::create_directory(temp.path() / ".harmony-save");
    const auto interrupted = ConfigDocument::open(temp.path());
    ASSERT_FALSE(interrupted);
    EXPECT_EQ(interrupted.error().code, ErrorCode::Io);
    EXPECT_NE(interrupted.error().message.find(".harmony-save"), std::string::npos);
    std::filesystem::remove(temp.path() / ".harmony-save");

    // Папка, которую не удалось удалить после успешного сохранения, открытию не мешает,
    // а следующее сохранение её убирает.
    const auto done = temp.path() / ".harmony-save.done-20260101-000000";
    std::filesystem::create_directories(done / "sub");
    auto reopened = ConfigDocument::open(temp.path());
    ASSERT_TRUE(reopened) << reopened.error().message;
    ASSERT_TRUE(reopened->save());
    EXPECT_FALSE(std::filesystem::exists(done));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / ".harmony-save"));
}

TEST(ConfigDocument, SavingWithoutTablesRemovesStaleTableFiles) {
    TempDir temp;
    auto document = ConfigDocument::open(kSampleDir);
    ASSERT_TRUE(document);
    ASSERT_TRUE(document->saveAs(temp.path()));
    ASSERT_TRUE(std::filesystem::exists(temp.path() / "IrProto.bin"));

    // Содержимое без таблиц: прежние IrProto.bin и SsIr.bin не подходят к новому XML.
    auto bare = ConfigDocument::fromXml(readFile(kSampleDir / "UserConfiguration.xml"), "bare");
    ASSERT_TRUE(bare);
    ASSERT_TRUE(document->replaceContent(*bare));
    ASSERT_TRUE(document->save());

    EXPECT_FALSE(std::filesystem::exists(temp.path() / "IrProto.bin"));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / "SsIr.bin"));
    EXPECT_TRUE(std::filesystem::exists(temp.path() / "UserConfiguration.xml"));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / ".harmony-save"));
    auto reopened = ConfigDocument::open(temp.path());
    ASSERT_TRUE(reopened) << reopened.error().message;
    EXPECT_FALSE(reopened->checkIrData());
}
