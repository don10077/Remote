/**
 * @file configDocumentTest.cpp
 * @brief Тесты документа конфигурации: создание, открытие, сохранение без потерь.
 *
 * Основа — конфигурация и таблицы, собранные в коде (`configFixtures.h`, `formatFixtures.h`):
 * ожидания следуют из конструкции фикстуры (правило 19). Проверка «читается ли файл реального
 * пульта» — отдельный тест через `dumpFiles.h`; без `dumps/` он пропускается.
 */

#include "configFixtures.h"
#include "dumpFiles.h"

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <string>

using harmony::ConfigDocument;
using harmony::ErrorCode;

namespace {

using harmony::test::kConfigXml;
using harmony::test::readText;
using harmony::test::writeFile;

ConfigDocument openFolder(const std::filesystem::path& folder) {
    auto document = ConfigDocument::open(folder);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

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
    harmony::test::ConfigFixtureFolder fixture;
    ConfigDocument original = openFolder(fixture.path());
    harmony::test::TempFolder temp("save-failed");
    ASSERT_TRUE(original.saveAs(temp.path()));
    const std::string xmlBefore = readText(temp.path() / "UserConfiguration.xml");
    const std::string actionsBefore = readText(temp.path() / "ActionLists.xml");

    // IrProto.bin нельзя записать, если на его месте каталог: сохранение должно сорваться
    // и вернуть то, что уже было записано.
    std::filesystem::remove(temp.path() / "IrProto.bin");
    std::filesystem::create_directory(temp.path() / "IrProto.bin");
    auto replacement = ConfigDocument::createNew("replacement");
    EXPECT_FALSE(replacement.saveAs(temp.path()));

    EXPECT_EQ(readText(temp.path() / "UserConfiguration.xml"), xmlBefore);
    EXPECT_EQ(readText(temp.path() / "ActionLists.xml"), actionsBefore);
    EXPECT_TRUE(std::filesystem::is_directory(temp.path() / "IrProto.bin"));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / ".harmony-save"));
}

TEST(ConfigDocument, SaveAsCreatesFolderAndReopens) {
    harmony::test::TempFolder temp("save-as");
    ConfigDocument document = ConfigDocument::createNew("Тест");

    ASSERT_TRUE(document.saveAs(temp.path() / "Спальня"));

    EXPECT_EQ(document.name(), "Спальня");
    const auto reopened = ConfigDocument::open(temp.path() / "Спальня");
    ASSERT_TRUE(reopened) << reopened.error().message;
    EXPECT_EQ(reopened->name(), "Спальня");
    EXPECT_TRUE(reopened->model().devices.empty());
    ASSERT_EQ(reopened->model().activities.size(), 1U);
    EXPECT_TRUE(reopened->model().activities.front().isPowerOff());
}

TEST(ConfigDocument, SavingKeepsTheReadTextByteForByte) {
    harmony::test::ConfigFixtureFolder fixture;
    ConfigDocument document = openFolder(fixture.path());
    harmony::test::TempFolder temp("lossless");

    ASSERT_TRUE(document.saveAs(temp.path())) << "фикстура читается без ошибок";

    // Ничего не переформатировано: сохранённый текст совпадает с прочитанным.
    EXPECT_EQ(readText(temp.path() / "UserConfiguration.xml"), kConfigXml);
    EXPECT_EQ(readText(temp.path() / "IrProto.bin"), readText(fixture.path() / "IrProto.bin"));
    EXPECT_EQ(readText(temp.path() / "SsIr.bin"), readText(fixture.path() / "SsIr.bin"));
}

TEST(ConfigDocument, SaveAsCopiesCompanionFiles) {
    harmony::test::TempFolder temp("companions");
    const auto files = harmony::test::codeTables();

    ConfigDocument::Files input = files;
    input.userConfiguration = kConfigXml;
    input.actionLists = "<Root><DeviceList/></Root>";
    auto document = ConfigDocument::fromFiles(input, "пример");
    ASSERT_TRUE(document) << document.error().message;
    ASSERT_TRUE(document->saveAs(temp.path()));

    // Файлы таблиц копируются байт в байт; ActionLists.xml пишется, даже когда макросов нет.
    EXPECT_FALSE(readText(temp.path() / "ActionLists.xml").empty());
    EXPECT_EQ(readText(temp.path() / "IrProto.bin"),
              std::string(files.irProto.begin(), files.irProto.end()));
    EXPECT_EQ(readText(temp.path() / "SsIr.bin"), std::string(files.ssIr.begin(), files.ssIr.end()));
}

TEST(ConfigDocument, SaveWritesIntoOwnFolder) {
    harmony::test::ConfigFixtureFolder fixture;
    ConfigDocument document = openFolder(fixture.path());
    ASSERT_TRUE(document.save());
    std::filesystem::remove(fixture.path() / "UserConfiguration.xml");

    ASSERT_TRUE(document.save());

    EXPECT_EQ(readText(fixture.path() / "UserConfiguration.xml"), kConfigXml);
    EXPECT_FALSE(std::filesystem::exists(fixture.path() / "UserConfiguration.xml.tmp"));
}

TEST(ConfigDocument, OpenMissingFolderIsNotFound) {
    harmony::test::TempFolder temp("missing");
    const auto document = ConfigDocument::open(temp.path() / "нет-такой");

    ASSERT_FALSE(document);
    EXPECT_EQ(document.error().code, ErrorCode::NotFound);
}

TEST(ConfigDocument, NewConfigWritesCompleteSet) {
    harmony::test::TempFolder temp("complete");
    ConfigDocument document = ConfigDocument::createNew("Новая");

    ASSERT_TRUE(document.saveAs(temp.path()));

    // Пульту нужны все четыре файла, даже если устройств ещё нет.
    for (const char* name : {"UserConfiguration.xml", "ActionLists.xml", "IrProto.bin", "SsIr.bin"}) {
        EXPECT_TRUE(std::filesystem::exists(temp.path() / name)) << name;
    }
    EXPECT_TRUE(openFolder(temp.path()).checkIrData());
}

TEST(ConfigDocument, LeftoverStagingFolderBlocksOpeningButFinishedOneDoesNot) {
    harmony::test::ConfigFixtureFolder fixture;
    ConfigDocument document = openFolder(fixture.path());
    harmony::test::TempFolder temp("staging");
    ASSERT_TRUE(document.saveAs(temp.path()));
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
    ConfigDocument reopened = openFolder(temp.path());
    ASSERT_TRUE(reopened.save());
    EXPECT_FALSE(std::filesystem::exists(done));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / ".harmony-save"));
}

TEST(ConfigDocument, SavingWithoutTablesRemovesStaleTableFiles) {
    harmony::test::ConfigFixtureFolder fixture;
    ConfigDocument document = openFolder(fixture.path());
    harmony::test::TempFolder temp("stale");
    ASSERT_TRUE(document.saveAs(temp.path()));

    // Содержимое без таблиц: прежние IrProto.bin и SsIr.bin не подходят к новому XML.
    const auto bare = ConfigDocument::fromXml(kConfigXml, "bare");
    ASSERT_TRUE(bare);
    ASSERT_TRUE(document.replaceContent(*bare));
    ASSERT_TRUE(document.save());

    EXPECT_FALSE(std::filesystem::exists(temp.path() / "IrProto.bin"));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / "SsIr.bin"));
    EXPECT_EQ(readText(temp.path() / "UserConfiguration.xml"), kConfigXml);
    EXPECT_FALSE(std::filesystem::exists(temp.path() / ".harmony-save"));
    EXPECT_FALSE(openFolder(temp.path()).checkIrData());
}

TEST(ConfigDocument, RealDumpFolderIsSavedBackByteForByte) {
    const auto dump = harmony::test::dumpFile("EZHex/harmony900", "*.EZHex");
    if (dump.empty()) GTEST_SKIP() << "dumps/EZHex/harmony900 нет — только на машине владельца";

    const auto archive = harmony::test::archiveOf(dump);
    const std::string xml = harmony::test::entryText(archive, "userconfig/UserConfiguration.xml");
    if (xml.empty()) GTEST_SKIP() << "в дампе нет userconfig/UserConfiguration.xml";

    std::map<std::string, std::string> tables;
    for (const char* table : {"IrProto.bin", "SsIr.bin"}) {
        const auto bytes = harmony::test::entryData(archive, std::string("userconfig/") + table);
        if (bytes.empty()) GTEST_SKIP() << "в дампе нет userconfig/" + std::string(table);
        tables[table] = std::string(bytes.begin(), bytes.end());
    }

    harmony::test::TempFolder temp("dump");
    harmony::test::writeFile(temp.path() / "UserConfiguration.xml", xml);
    for (const auto& [name, content] : tables) {
        writeFile(temp.path() / name, content);
    }

    ConfigDocument document = openFolder(temp.path());
    ASSERT_TRUE(document.save()) << "использован файл: " << dump.string();
    EXPECT_EQ(readText(temp.path() / "UserConfiguration.xml"), xml);
    for (const auto& [name, content] : tables) {
        EXPECT_EQ(readText(temp.path() / name), content) << name;
    }
}
