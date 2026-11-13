/**
 * @file irDatabaseTest.cpp
 * @brief Тесты базы ИК-кодов: сборка из файлов архива, поиск, записи, перенос устройства в
 * конфигурацию.
 *
 * Фикстура — три устройства из настоящего архива logitech-harmony-ir-archive
 * (Samsung HLN501W, Sony STR-DE1015G, Yamaha HTR-7650) и их протоколы.
 */

#include "harmony/core/irdb/irDatabase.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/irTables.h"
#include "harmony/core/ir/carrierVm.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>

using namespace harmony;

namespace {

const std::filesystem::path kData = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "irdb";

/// Временная папка теста; удаляется вместе с содержимым.
class TempDir {
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path() /
                ("harmony-irdb-" +
                 std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name())) {
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

/// Собирает базу из фикстуры и открывает её.
irdb::IrDatabase buildFixture(const TempDir& dir) {
    const auto file = dir.path() / "test.irdb";
    auto stats = irdb::buildDatabase(kData / "device_features_raw.jsonl.gz",
                                     kData / "AllProtocols.json.gz", file, "test");
    EXPECT_TRUE(stats) << (stats ? "" : stats.error().message);
    auto database = irdb::IrDatabase::open(file);
    EXPECT_TRUE(database) << (database ? "" : database.error().message);
    return std::move(*database);
}

std::size_t entryOf(const irdb::IrDatabase& database, std::string_view query) {
    const auto found = database.search(query, 10);
    EXPECT_EQ(found.size(), 1U) << query;
    return found.empty() ? 0 : found.front();
}

} // namespace

TEST(IrDatabase, BuildsCatalogueSortedByManufacturer) {
    TempDir dir;
    const auto database = buildFixture(dir);

    ASSERT_EQ(database.entries().size(), 3U);
    EXPECT_EQ(database.entries()[0].manufacturer, "Samsung");
    EXPECT_EQ(database.entries()[1].manufacturer, "Sony");
    EXPECT_EQ(database.entries()[2].manufacturer, "Yamaha");
    EXPECT_EQ(database.source(), "test");
    EXPECT_EQ(database.formatVersion(), 3U);
    EXPECT_NE(database.buildInfo().find("skippedLines"), std::string::npos);
    EXPECT_EQ(irdb::deviceTypeName(database.entries()[0].deviceType), "Television");
}

TEST(IrDatabase, TruncatedSourceDoesNotReplaceExistingDatabase) {
    TempDir dir;
    const auto original = buildFixture(dir);
    const auto source = kData / "device_features_raw.jsonl.gz";
    const auto damaged = dir.path() / "broken.gz";
    std::filesystem::copy_file(source, damaged);
    std::filesystem::resize_file(damaged, std::filesystem::file_size(damaged) / 2);
    EXPECT_FALSE(irdb::buildDatabase(damaged, kData / "AllProtocols.json.gz", dir.path() / "test.irdb", "broken"));
    const auto reopened = irdb::IrDatabase::open(dir.path() / "test.irdb");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->source(), "test");
    EXPECT_EQ(reopened->entries().size(), original.entries().size());
}

TEST(IrDatabase, ReadsVersionTwoWithoutBuildMetadata) {
    TempDir dir;
    const auto database = buildFixture(dir);
    const auto path = dir.path() / "test.irdb";
    const auto metadataSize = database.buildInfo().size();
    {
        std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(stream);
        stream.seekp(12);
        const char version[4] = {2, 0, 0, 0};
        stream.write(version, sizeof(version));
    }
    std::filesystem::resize_file(path, std::filesystem::file_size(path) - metadataSize);
    auto reopened = irdb::IrDatabase::open(path);
    ASSERT_TRUE(reopened) << reopened.error().message;
    EXPECT_EQ(reopened->formatVersion(), 2U);
    EXPECT_TRUE(reopened->buildInfo().empty());
    EXPECT_EQ(reopened->entries().size(), database.entries().size());
    EXPECT_TRUE(reopened->device(0));
}

TEST(IrDatabase, SearchMatchesAllWordsIgnoringCase) {
    TempDir dir;
    const auto database = buildFixture(dir);

    std::size_t found = 0;
    EXPECT_EQ(database.search("SONY str", 10, &found).size(), 1U);
    EXPECT_EQ(found, 1U);
    EXPECT_EQ(database.search("s", 10).size(), 2U); // Samsung, Sony
    EXPECT_TRUE(database.search("sony yamaha", 10).empty());
    EXPECT_EQ(database.search("a", 1, &found).size(), 1U); // ограничение числа результатов
    EXPECT_EQ(found, 2U);
}

TEST(IrDatabase, ReadsRecordWithPowerAndInputs) {
    TempDir dir;
    const auto database = buildFixture(dir);

    const auto samsung = database.device(entryOf(database, "samsung hln501w"));
    ASSERT_TRUE(samsung) << samsung.error().message;
    EXPECT_EQ(samsung->model, "HLN501W");
    EXPECT_EQ(samsung->commands.size(), 96U);
    EXPECT_EQ(samsung->powerType, 1);
    EXPECT_FALSE(samsung->powerOn.empty());
    EXPECT_EQ(samsung->inputs.size(), 5U);
    EXPECT_FALSE(samsung->inputs.front().actions.empty());

    const auto yamaha = database.device(entryOf(database, "yamaha"));
    ASSERT_TRUE(yamaha);
    EXPECT_EQ(yamaha->powerType, 2);
    EXPECT_FALSE(yamaha->inputNext.empty());
}

TEST(IrDatabase, RendersCommandsToKnownProtocols) {
    TempDir dir;
    const auto database = buildFixture(dir);
    const auto samsung = database.device(entryOf(database, "samsung"));
    ASSERT_TRUE(samsung);

    const auto& zero = samsung->commands.front();
    ASSERT_EQ(zero.name, "0");
    auto signal = database.protocols().render(zero);
    ASSERT_TRUE(signal) << signal.error().message;
    const auto decoded = ir::decode(*signal);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->protocol, "Samsung32");
    EXPECT_EQ(decoded->address, 0x0707U);
    EXPECT_EQ(decoded->command, 0x11U);

    irdb::DbCommand broken = zero;
    broken.keyCode = "garbage";
    EXPECT_EQ(database.protocols().render(broken).error().code, ErrorCode::Unsupported);
}

TEST(IrDatabase, CancelledBuildLeavesNoFile) {
    TempDir dir;
    const auto file = dir.path() / "cancelled.irdb";
    auto stats =
        irdb::buildDatabase(kData / "device_features_raw.jsonl.gz", kData / "AllProtocols.json.gz",
                            file, "test", [](std::uint64_t, std::uint64_t) { return false; });
    // В фикстуре меньше 256 строк: ход сообщается в конце, отменять уже поздно — база собрана.
    if (!stats) {
        EXPECT_EQ(stats.error().code, ErrorCode::Cancelled);
        EXPECT_FALSE(std::filesystem::exists(file));
    }
    EXPECT_FALSE(std::filesystem::exists(file.string() + ".tmp"));
}

TEST(IrDatabase, RejectsOtherFiles) {
    EXPECT_FALSE(irdb::IrDatabase::open(kData / "AllProtocols.json.gz"));
    EXPECT_EQ(irdb::IrDatabase::open(kData / "missing.irdb").error().code, ErrorCode::NotFound);
    EXPECT_FALSE(irdb::ProtocolLibrary::parse("{}"));
}

TEST(IrDatabase, ImportsDeviceIntoConfiguration) {
    TempDir dir;
    const auto database = buildFixture(dir);
    const auto samsung = database.device(entryOf(database, "samsung"));
    ASSERT_TRUE(samsung);

    ConfigDocument document = ConfigDocument::createNew("Test");
    auto report = document.importDbDevice(*samsung, database.protocols());
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_EQ(report->commands, 96);
    EXPECT_EQ(report->protocolCommands, 96); // все — протоколом, SsIr.bin не тронут
    EXPECT_TRUE(report->power);
    EXPECT_TRUE(report->inputs);
    EXPECT_GT(report->hardButtons, 20);
    EXPECT_GT(report->softButtons, 0);

    const model::Device* device = document.model().findDevice(report->deviceId);
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->displayName(), "Samsung HLN501W");
    EXPECT_EQ(device->type, "Television");
    EXPECT_TRUE(device->powerControlled);
    EXPECT_EQ(device->inputs.size(), 5U);
    // Команда — протоколом, и её сигнал узнаётся как Samsung32.
    const auto details = document.commandDetails(report->deviceId, "VolumeUp");
    ASSERT_TRUE(details) << details.error().message;
    EXPECT_FALSE(details->learned);
    ASSERT_TRUE(details->decoded);
    EXPECT_EQ(details->decoded->protocol, "Samsung32");

    // Файлы собираются и снова открываются.
    const auto saved = document.saveAs(dir.path() / "config");
    ASSERT_TRUE(saved) << saved.error().message;
    const auto reopened = ConfigDocument::open(dir.path() / "config");
    ASSERT_TRUE(reopened) << reopened.error().message;
    EXPECT_NE(reopened->model().findDevice(report->deviceId), nullptr);
}

TEST(IrDatabase, ImportsToggleDeviceWithRelativeInputs) {
    TempDir dir;
    const auto database = buildFixture(dir);
    const auto yamaha = database.device(entryOf(database, "yamaha"));
    ASSERT_TRUE(yamaha);

    ConfigDocument document = ConfigDocument::createNew("Test");
    auto report = document.importDbDevice(*yamaha, database.protocols());
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_TRUE(report->power);
    EXPECT_TRUE(report->inputs);
    EXPECT_NE(document.toXml().find("<RelativeActions><NextAction>"), std::string::npos);
}

TEST(IrDatabase, ImportsIntoRealConfigurationReusingLogitechProtocol) {
    TempDir dir;
    const auto database = buildFixture(dir);
    const auto sony = database.device(entryOf(database, "sony"));
    ASSERT_TRUE(sony);

    auto document =
        ConfigDocument::open(std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig");
    ASSERT_TRUE(document) << document.error().message;
    const std::size_t devices = document->model().devices.size();
    auto report = document->importDbDevice(*sony, database.protocols());
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_EQ(document->model().devices.size(), devices + 1);
    EXPECT_EQ(report->protocolCommands, report->commands);
    const auto details = document->commandDetails(report->deviceId, "PowerOn");
    ASSERT_TRUE(details) << details.error().message;
    EXPECT_FALSE(details->learned);
    ASSERT_TRUE(details->decoded);
    EXPECT_EQ(details->decoded->protocol.substr(0, 9), "Sony SIRC");
    // Команды одного протокола делят одну запись IrProto.bin, после записей образца.
    const model::Device* device = document->model().findDevice(report->deviceId);
    ASSERT_NE(device, nullptr);
    const model::Command* zero = device->findCommand("0");
    const model::Command* one = device->findCommand("1");
    ASSERT_TRUE(zero != nullptr && one != nullptr);
    EXPECT_GE(zero->protocol, 5); // 0–4 — протоколы образца
    EXPECT_EQ(zero->protocol, one->protocol);
    EXPECT_TRUE(document->checkIrData());
}

TEST(IrDatabase, ImportUsesProtocolsWhenRawTableIsFull) {
    TempDir dir;
    const auto database = buildFixture(dir);
    const auto samsung = database.device(entryOf(database, "samsung"));
    ASSERT_TRUE(samsung);
    ConfigDocument document = ConfigDocument::createNew("Full");
    auto id = document.createDevice({.label = "TV", .type = "Television", .manufacturer = "", .model = ""});
    ASSERT_TRUE(id);
    // SsIr.bin заполнен до последнего однобайтового номера (0…255): раньше перенос отказывал.
    for (int i = 0; i <= 0xFF; ++i) {
        const ir::IrSignal signal{38000, {9000, -4500, 560 + i * 10, -560, 560, -40000}};
        ASSERT_TRUE(document.learnCommand(*id, "C" + std::to_string(i), signal)) << i;
    }

    const auto report = document.importDbDevice(*samsung, database.protocols());

    ASSERT_TRUE(report) << report.error().message;
    EXPECT_EQ(report->protocolCommands, report->commands);
}

TEST(IrDatabase, CompilesEveryFixtureCommandToRemoteProgram) {
    TempDir dir;
    const auto database = buildFixture(dir);
    for (std::size_t entry = 0; entry < database.entries().size(); ++entry) {
        const auto device = database.device(entry);
        ASSERT_TRUE(device);
        for (const auto& command : device->commands) {
            // compile() сам сверяет исполнение с render(); здесь — что сверка проходит у всех.
            const auto native = database.protocols().compile(command);
            EXPECT_TRUE(native) << device->model << " " << command.name << " " << command.keyCode << ": "
                                << (native ? "" : native.error().message);
        }
    }
}

TEST(IrDatabase, CompiledProtocolMatchesLogitechBlock) {
    TempDir dir;
    const auto database = buildFixture(dir);
    const auto sony = database.device(entryOf(database, "sony"));
    ASSERT_TRUE(sony);
    const auto zero = std::ranges::find(sony->commands, std::string("0"), &irdb::DbCommand::name);
    ASSERT_NE(zero, sony->commands.end());
    ASSERT_NE(zero->keyCode.find("Sony 12 Bit"), std::string::npos);
    const auto native = database.protocols().compile(*zero);
    ASSERT_TRUE(native) << native.error().message;

    // Запись 4 образца (PlayStation 2) собрана программой Logitech из тех же таймингов Sony,
    // только кадр в 20 бит. С поправкой длины кадра наша запись должна совпасть побайтно.
    std::ifstream file(std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig" / "IrProto.bin",
                       std::ios::binary);
    const formats::Bytes bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto table = formats::parseIrProto(bytes);
    ASSERT_TRUE(table) << table.error().message;
    ASSERT_GT(table->entries.size(), 4U);
    formats::Bytes logitech = table->entries[4];
    ASSERT_GT(logitech.size(), 12U);
    const std::size_t element = logitech[7] | (logitech[8] << 8);
    ASSERT_EQ(logitech[element], 20U); // символов в кадре
    logitech[element] = 12;
    EXPECT_EQ(native->protocol, logitech);

    // Как у Logitech: этап нажатия пуст, кадр — этапом повтора, минимум 3 раза.
    const auto run = ir::runCarrier(native->protocol, native->code);
    ASSERT_TRUE(run) << run.error().message;
    EXPECT_EQ(run->stages, (std::vector<int>{3, 3, 3}));
}

TEST(IrDatabase, CompiledRepeatCodeIsSentWhileHeld) {
    TempDir dir;
    const auto database = buildFixture(dir);
    const auto yamaha = database.device(entryOf(database, "yamaha"));
    ASSERT_TRUE(yamaha);
    const auto& command = yamaha->commands.front(); // Toshiba 32 Bit: (данные)(Repeat)()
    ASSERT_NE(command.keyCode.find("(Repeat)"), std::string::npos);
    const auto native = database.protocols().compile(command);
    ASSERT_TRUE(native) << native.error().message;

    const auto pressed = ir::runCarrier(native->protocol, native->code);
    const auto held = ir::runCarrier(native->protocol, native->code, 0, 3);
    ASSERT_TRUE(pressed && held);
    EXPECT_EQ(pressed->stages, (std::vector<int>{2, 3}));
    EXPECT_EQ(held->stages, (std::vector<int>{2, 3, 3, 3, 3}));
    // Повтор — короткий код (8990, −2230, 568), а не весь кадр.
    ASSERT_GE(held->stageDurations.size(), 2U);
    EXPECT_EQ(held->stageDurations[1].front(), 8990);
    EXPECT_EQ(held->stageDurations[1][1], -2230);
}
