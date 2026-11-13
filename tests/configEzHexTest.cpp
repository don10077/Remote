/**
 * @file configEzHexTest.cpp
 * @brief Тесты документа в формате .EZHex: открытие дампа, сохранение без потерь, сборка для пульта.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/ezhex.h"
#include "harmony/core/formats/zipArchive.h"
#include "harmony/core/undo/documentEdit.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using harmony::ConfigDocument;
namespace formats = harmony::formats;

namespace {

const std::filesystem::path kData(HARMONY_TEST_DATA_DIR);
const std::filesystem::path kSample = kData / "ezhex" / "harmony900.EZHex";

formats::Bytes readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// Архив внутри .EZHex.
formats::ZipArchive archiveOf(const formats::Bytes& ezhex) {
    auto container = formats::parseEzHex(ezhex);
    EXPECT_TRUE(container) << container.error().message;
    auto zip = formats::readZip(container->payload);
    EXPECT_TRUE(zip) << zip.error().message;
    return std::move(*zip);
}

std::string textOf(const formats::ZipArchive& archive, const std::string& name) {
    const formats::ZipEntry* entry = archive.find(name);
    EXPECT_NE(entry, nullptr) << name;
    const auto content = formats::entryContent(*entry);
    return {content->begin(), content->end()};
}

class TempDir {
public:
    TempDir() : path_(std::filesystem::temp_directory_path() /
                      ("harmony-ezhex-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)))) {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() { std::filesystem::remove_all(path_); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST(ConfigEzHex, OpensRemoteDump) {
    auto document = ConfigDocument::open(kSample);
    ASSERT_TRUE(document) << document.error().message;

    EXPECT_EQ(document->model().devices.size(), 8U);
    EXPECT_EQ(document->model().activities.size(), 9U);
    EXPECT_EQ(document->name(), "harmony900");
    EXPECT_EQ(document->ezhexFile(), kSample);
    EXPECT_EQ(document->location(), kSample);
    EXPECT_FALSE(document->hasFolder());
    EXPECT_EQ(document->identity().skin, 61);
    EXPECT_TRUE(document->checkIrData()); // таблицы ИК-данных пришли из архива
}

TEST(ConfigEzHex, Recognizes1100IdentityButDoesNotEnableWriting) {
    for (const int skin : {62, 63, 64}) {
        auto container = formats::parseEzHex(readFile(kSample));
        ASSERT_TRUE(container);
        container->identity.architecture = 11;
        container->identity.skin = skin;
        container->header.clear();
        auto bytes = formats::writeEzHex(*container);
        ASSERT_TRUE(bytes);
        auto document = ConfigDocument::fromEzHex(*bytes, "1100 routing fixture");
        ASSERT_TRUE(document);
        EXPECT_EQ(document->targetRemote(), "1100");
        EXPECT_EQ(*document->toEzHex(false), *bytes);
        EXPECT_FALSE(document->toEzHex(true));
    }
}

TEST(ConfigEzHex, UnchangedDumpIsWrittenBackByteForByte) {
    auto document = ConfigDocument::open(kSample);
    ASSERT_TRUE(document);

    auto bytes = document->toEzHex(false);

    ASSERT_TRUE(bytes) << bytes.error().message;
    EXPECT_EQ(*bytes, readFile(kSample));
}

TEST(ConfigEzHex, EditKeepsOtherRemoteFilesIntact) {
    auto document = ConfigDocument::open(kSample);
    ASSERT_TRUE(document);
    ASSERT_TRUE(document->setDeviceLabel("82969357", "Ресивер"));

    auto bytes = document->toEzHex(false);
    ASSERT_TRUE(bytes);

    const auto before = archiveOf(readFile(kSample));
    const auto after = archiveOf(*bytes);
    ASSERT_EQ(after.entries.size(), before.entries.size());
    for (std::size_t i = 0; i < after.entries.size(); ++i) {
        EXPECT_EQ(after.entries[i].name, before.entries[i].name); // порядок прежний
        if (after.entries[i].name != "userconfig/UserConfiguration.xml") {
            EXPECT_EQ(after.entries[i].data, before.entries[i].data) << after.entries[i].name;
        }
    }
    auto reopened = ConfigDocument::fromEzHex(*bytes, "x");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->model().findDevice("82969357")->label, "Ресивер");
}

TEST(ConfigEzHex, ForRemoteFixesFileModes) {
    auto document = ConfigDocument::open(kSample);
    ASSERT_TRUE(document);
    const auto dump = archiveOf(readFile(kSample));
    ASSERT_EQ(dump.find("platformconfig/system_sound.dat")->unixMode(), 0100600U); // так в снятой копии

    auto bytes = document->toEzHex(true);
    ASSERT_TRUE(bytes);

    const auto archive = archiveOf(*bytes);
    EXPECT_EQ(archive.find("platformconfig/system_sound.dat")->unixMode(), 0100666U);
    EXPECT_EQ(archive.find(".preinstall")->unixMode(), 0100775U);
    EXPECT_EQ(archive.find("platformconfig/tiltcfg.dat")->unixMode(), 0100644U);
    EXPECT_EQ(archive.find("META-INF/")->unixMode(), 040777U);
}

TEST(ConfigEzHex, NewConfigBuildsCompletePackage) {
    const ConfigDocument document = ConfigDocument::createNew("Новая");

    auto bytes = document.toEzHex(true);
    ASSERT_TRUE(bytes) << bytes.error().message;

    auto container = formats::parseEzHex(*bytes);
    ASSERT_TRUE(container);
    EXPECT_EQ(container->identity.skin, 61);
    const auto archive = archiveOf(*bytes);
    const std::vector<std::string> expected = {
        "userconfig/", "userconfig/UserConfiguration.xml", "userconfig/ActionLists.xml",
        "userconfig/IrProto.bin", "userconfig/SsIr.bin", ".version", ".preinstall", ".postinstall",
        "META-INF/", "META-INF/MANIFEST.MF"};
    ASSERT_EQ(archive.entries.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(archive.entries[i].name, expected[i]);
    }
    // Скрипты — LF (с CRLF пульт не запустит их), .version — CRLF.
    const std::string script = textOf(archive, ".preinstall");
    EXPECT_TRUE(script.starts_with("#!/bin/sh\n"));
    EXPECT_EQ(script.find('\r'), std::string::npos);
    EXPECT_EQ(textOf(archive, ".version"), "0x04 0x00 0x00 0x00\r\n");
    auto reopened = ConfigDocument::fromEzHex(*bytes, "x");
    ASSERT_TRUE(reopened) << reopened.error().message;
    EXPECT_EQ(reopened->model().activities.size(), 1U);
}

TEST(ConfigEzHex, SaveAsEzHexBecomesLocation) {
    TempDir temp;
    auto document = ConfigDocument::open(kData / "userconfig");
    ASSERT_TRUE(document);
    const auto file = temp.path() / "копия.EZHex";

    ASSERT_TRUE(document->saveAsEzHex(file));
    EXPECT_EQ(document->location(), file);
    EXPECT_EQ(document->name(), "копия");
    EXPECT_FALSE(document->hasFolder());

    // Дальнейшие правки сохраняются в тот же файл.
    ASSERT_TRUE(document->setActivityLabel("79016674", "Телевизор"));
    ASSERT_TRUE(document->save());
    auto reopened = ConfigDocument::open(file);
    ASSERT_TRUE(reopened) << reopened.error().message;
    EXPECT_EQ(reopened->model().findActivity("79016674")->label, "Телевизор");
}

TEST(ConfigEzHex, DumpSavedToFolderGivesSameFiles) {
    TempDir temp;
    auto document = ConfigDocument::open(kSample);
    ASSERT_TRUE(document);

    ASSERT_TRUE(document->saveAs(temp.path()));

    EXPECT_EQ(readFile(temp.path() / "UserConfiguration.xml"), readFile(kData / "userconfig" / "UserConfiguration.xml"));
    EXPECT_EQ(readFile(temp.path() / "IrProto.bin"), readFile(kData / "userconfig" / "IrProto.bin"));
    EXPECT_TRUE(document->ezhexFile().empty());
}

TEST(ConfigEzHex, ReplaceContentKeepsLocationAndUndoes) {
    // Открыта новая конфигурация в папке; с пульта прочитан дамп — заменяем им содержимое.
    TempDir dir;
    auto current = harmony::ConfigDocument::createNew("Моя");
    ASSERT_TRUE(current.saveAs(dir.path() / "Моя"));
    const auto remote = harmony::ConfigDocument::open(kSample);
    ASSERT_TRUE(remote) << remote.error().message;

    auto command = harmony::undo::editDocument(current, "С пульта", [&](harmony::ConfigDocument& document) {
        return document.replaceContent(*remote);
    });
    ASSERT_TRUE(command) << command.error().message;

    EXPECT_EQ(current.folder(), dir.path() / "Моя");
    EXPECT_EQ(current.model().devices.size(), remote->model().devices.size());
    EXPECT_EQ(current.toXml(), remote->toXml());
    // В .EZHex пойдут и настройки пульта из дампа.
    const auto ezhex = current.toEzHex(false);
    ASSERT_TRUE(ezhex);
    EXPECT_EQ(archiveOf(*ezhex).entries.size(), archiveOf(readFile(kSample)).entries.size());

    (*command)->undo();
    EXPECT_TRUE(current.model().devices.empty());
}

TEST(ConfigEzHex, ContentWithoutTablesDropsThemFromTheArchive) {
    auto document = ConfigDocument::open(kSample);
    ASSERT_TRUE(document) << document.error().message;
    ASSERT_NE(archiveOf(*document->toEzHex(false)).find("userconfig/IrProto.bin"), nullptr);

    // Источник — только XML: архив (скрипты, настройки) остаётся, таблицы — нет.
    auto bare = ConfigDocument::fromXml(document->toXml(), "bare");
    ASSERT_TRUE(bare);
    ASSERT_TRUE(document->replaceContent(*bare));
    const auto saved = document->toEzHex(false);
    ASSERT_TRUE(saved) << saved.error().message;

    const auto archive = archiveOf(*saved);
    EXPECT_EQ(archive.find("userconfig/IrProto.bin"), nullptr);
    EXPECT_EQ(archive.find("userconfig/SsIr.bin"), nullptr);
    EXPECT_NE(archive.find(".preinstall"), nullptr);
    auto reopened = ConfigDocument::fromEzHex(*saved, "reopened");
    ASSERT_TRUE(reopened) << reopened.error().message;
    EXPECT_FALSE(reopened->checkIrData());
}
