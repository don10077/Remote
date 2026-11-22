/**
 * @file configEzHexTest.cpp
 * @brief Тесты документа в формате .EZHex: открытие дампа, сохранение без потерь, сборка для пульта.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/ezhex.h"
#include "harmony/core/formats/container.h"
#include "harmony/core/formats/zipArchive.h"
#include "harmony/core/undo/documentEdit.h"
#include "dumpFiles.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using harmony::ConfigDocument;
namespace formats = harmony::formats;

namespace {

// Дамп реального Harmony 900 (dumps/, не в git): без него тесты пропускаются (правило 19).
const std::filesystem::path kSample = harmony::test::dumpRoot() / "EZHex" / "harmony900" / "900.EZHex";
// Конфигурация реального пульта — встроенный пример приложения (tests/data/userconfig перенесён туда).
const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_SOURCE_DIR) / "app" / "resources" / "demo";

formats::Bytes readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// Minimal independent format fixture: one device and a four-duration infrared command.
formats::Bytes binaryHidFixture() {
    formats::EzHex file;
    file.identity.architecture=14;
    file.identity.skin=75;
    auto& data=file.payload;
    data.resize(224);
    const auto put=[&](std::size_t at,std::uint32_t value,unsigned width) {
        for(unsigned i=0;i<width;++i)data[at+i]=static_cast<std::uint8_t>(value>>(8*i));
    };
    std::copy_n("GSPM",4,data.begin());
    put(4,0x30000+220,4);
    data[9]=20;
    std::copy_n("LWJL",4,data.begin()+91);
    put(12+4*5,0x30000+128,3);
    data[128]=1;
    put(129,0x30000+132,3);
    put(133,1,2);
    put(135,0x30000+151,3);
    put(145,26316,3);
    put(148,13158,3);
    data[151]=1;
    put(152,0x30000+144,3);
    data[155]=1;
    put(156,0x30000+176,3);
    put(176,0x8000+9000,2);
    put(178,4500,2);
    put(180,0x8000+560,2);
    put(182,1690,2);
    std::copy_n("PTYY",4,data.begin()+220);
    put(218,formats::containerChecksum(data,218),2);
    return *formats::writeEzHex(file);
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

/// Проверки локального дампа пропускаются в CI, где его нет.
class ConfigEzHexDump : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::filesystem::is_regular_file(kSample)) {
            GTEST_SKIP() << "Local Harmony 900 dump is not installed: " << kSample;
        }
    }
};

} // namespace

TEST_F(ConfigEzHexDump, OpensRemoteDump) {
    auto document = ConfigDocument::open(kSample);
    ASSERT_TRUE(document) << document.error().message;

    EXPECT_EQ(document->model().devices.size(), 8U);
    EXPECT_EQ(document->model().activities.size(), 9U);
    EXPECT_EQ(document->name(), kSample.stem().string());
    EXPECT_EQ(document->ezhexFile(), kSample);
    EXPECT_EQ(document->location(), kSample);
    EXPECT_FALSE(document->hasFolder());
    EXPECT_EQ(document->identity().skin, 61);
    EXPECT_TRUE(document->checkIrData()); // таблицы ИК-данных пришли из архива
}

TEST(ConfigEzHex, RejectsTruncatedBinaryHidImages) {
    for (const std::string signature : {"GSPM", "AHCM", "TPTP"}) {
        formats::EzHex binary;
        binary.payload.assign(signature.begin(), signature.end());
        binary.identity.architecture = 14;
        binary.identity.skin = 75;
        const auto bytes = formats::writeEzHex(binary);
        ASSERT_TRUE(bytes);
        const auto document = ConfigDocument::fromEzHex(*bytes, "HID backup");
        ASSERT_FALSE(document);
        EXPECT_EQ(document.error().code, harmony::ErrorCode::Format);
    }
}

// Реальные дампы бинарных семейств: `dumps/` в git не входит, поэтому без неё тест
// пропускается (правило 19). Утверждаются решения программы — формат, отказ писать на пульт,
// сохранение исходного образа байт в байт и правка, пережившая сохранение. Числа содержимого
// конкретного дампа (устройства, активности, команды, skin) не утверждаются.
TEST(ConfigEzHex, ImportsPublicBinaryHidFamiliesWithTheirCommands) {
    struct Sample { const char* folder; const char* mask; };
    std::size_t checked = 0;
    for (const Sample sample : {Sample{"EZHex/harmony650", "Harmony_650.EZHex"}, Sample{"EZHex/harmony525", "config.EZHex"},
                                Sample{"EZHex/arch8", "H880-Bedroom.EZHex"}, Sample{"EZHex/harmony890", "H890-Bedroom-1.EZHex"}}) {
        SCOPED_TRACE(sample.folder);
        const auto path = harmony::test::dumpFile(sample.folder, sample.mask);
        if (path.empty()) continue;
        ++checked;
        auto document = ConfigDocument::open(path);
        ASSERT_TRUE(document) << document.error().message;
        EXPECT_EQ(document->backend()->format(), harmony::ConfigFormat::BinaryHid);
        EXPECT_TRUE(document->location().empty()); // Save prompts for a project; original dump is retained.
        EXPECT_FALSE(document->backend()->canWriteToRemote());
        EXPECT_FALSE(document->toEzHex(true));
        // Команды собраны из ИК-таблиц, а не оставлены текстом: хотя бы одна исполняется.
        std::size_t rendered = 0;
        for (const auto& device : document->model().devices)
            for (const auto& command : device.commands)
                if (document->commandDetails(device.id, command.name)) ++rendered;
        EXPECT_GT(rendered, 0U);
        const auto bytes = document->toEzHex(false);
        ASSERT_TRUE(bytes) << bytes.error().message;
        const auto archive = archiveOf(*bytes);
        ASSERT_NE(archive.find("source/Original.EZHex"), nullptr);
        const auto original = formats::entryContent(*archive.find("source/Original.EZHex"));
        ASSERT_TRUE(original);
        EXPECT_EQ(*original, readFile(path));
        auto reopened = ConfigDocument::fromEzHex(*bytes, "saved import");
        ASSERT_TRUE(reopened) << reopened.error().message;
        EXPECT_EQ(reopened->backend()->format(), harmony::ConfigFormat::BinaryHid);
        EXPECT_EQ(reopened->model().commandCount(), document->model().commandCount());
        EXPECT_EQ(reopened->model().devices.front().controlGroups.size(),
                  document->model().devices.front().controlGroups.size());
        ASSERT_TRUE(document->setDeviceLabel(document->model().devices.front().id, "Imported device"));
        const auto edited=document->toEzHex(false);
        ASSERT_TRUE(edited);
        auto editedDocument=ConfigDocument::fromEzHex(*edited,"edited import");
        ASSERT_TRUE(editedDocument);
        EXPECT_EQ(editedDocument->model().devices.front().label,"Imported device");
        EXPECT_EQ(*formats::entryContent(*archiveOf(*edited).find("source/Original.EZHex")),readFile(path));
    }
    if (checked == 0) GTEST_SKIP() << "dumps/ нет — дампы бинарных семейств только на машине владельца";
}

TEST(ConfigEzHex, RejectsBinaryDevicePointersOutsideTheImage) {
    auto container=formats::parseEzHex(binaryHidFixture());
    ASSERT_TRUE(container);
    const auto frame=formats::parseContainer(container->payload);
    ASSERT_TRUE(frame);
    const auto table=*frame->sections[5];
    std::fill_n(container->payload.begin()+static_cast<std::ptrdiff_t>(table+1),3,0xff);
    const auto sum=formats::containerChecksum(container->payload,frame->checksumOffset);
    container->payload[frame->checksumOffset]=static_cast<std::uint8_t>(sum);
    container->payload[frame->checksumOffset+1]=static_cast<std::uint8_t>(sum>>8);
    const auto bytes=formats::writeEzHex(*container);
    ASSERT_TRUE(bytes);
    const auto document=ConfigDocument::fromEzHex(*bytes,"bad pointer");
    ASSERT_FALSE(document);
    EXPECT_EQ(document.error().code,harmony::ErrorCode::Format);
}

TEST(ConfigEzHex, ImportsNativeHidWaveformWithoutAnExternalRuntime) {
    const auto bytes=binaryHidFixture();
    auto document=ConfigDocument::fromEzHex(bytes,"Synthetic 665");
    ASSERT_TRUE(document) << document.error().message;
    ASSERT_EQ(document->model().devices.size(),1U);
    ASSERT_EQ(document->model().devices[0].commands.size(),1U);
    const auto signal=document->commandSignal("hid0","Command1");
    ASSERT_TRUE(signal);
    EXPECT_EQ(signal->durations,(std::vector<int>{9000,-4500,560,-1690}));
    EXPECT_NEAR(signal->frequency,38000,2);
    EXPECT_EQ(document->backend()->format(),harmony::ConfigFormat::BinaryHid);
    EXPECT_FALSE(document->toEzHex(true));
    ASSERT_TRUE(document->setDeviceLabel("hid0","Receiver"));
    const auto saved=document->toEzHex(false);
    ASSERT_TRUE(saved);
    auto reopened=ConfigDocument::fromEzHex(*saved,"Reopened project");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->model().devices[0].label,"Receiver");
    EXPECT_EQ(*reopened->commandSignal("hid0","Command1"),*signal);
    EXPECT_EQ(*formats::entryContent(*archiveOf(*saved).find("source/Original.EZHex")),bytes);
}

TEST_F(ConfigEzHexDump, Recognizes1100IdentityButDoesNotEnableWriting) {
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

TEST_F(ConfigEzHexDump, UnchangedDumpIsWrittenBackByteForByte) {
    auto document = ConfigDocument::open(kSample);
    ASSERT_TRUE(document);

    auto bytes = document->toEzHex(false);

    ASSERT_TRUE(bytes) << bytes.error().message;
    EXPECT_EQ(*bytes, readFile(kSample));
}

TEST_F(ConfigEzHexDump, EditKeepsOtherRemoteFilesIntact) {
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

TEST_F(ConfigEzHexDump, ForRemoteFixesFileModes) {
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
    auto document = ConfigDocument::open(kSampleDir);
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

TEST_F(ConfigEzHexDump, DumpSavedToFolderGivesSameFiles) {
    TempDir temp;
    auto document = ConfigDocument::open(kSample);
    ASSERT_TRUE(document);

    ASSERT_TRUE(document->saveAs(temp.path()));

    const auto archive = archiveOf(readFile(kSample));
    EXPECT_EQ(readFile(temp.path() / "UserConfiguration.xml"),
              *formats::entryContent(*archive.find("userconfig/UserConfiguration.xml")));
    EXPECT_EQ(readFile(temp.path() / "IrProto.bin"),
              *formats::entryContent(*archive.find("userconfig/IrProto.bin")));
    EXPECT_TRUE(document->ezhexFile().empty());
}

TEST_F(ConfigEzHexDump, ReplaceContentKeepsLocationAndUndoes) {
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

TEST_F(ConfigEzHexDump, ContentWithoutTablesDropsThemFromTheArchive) {
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
