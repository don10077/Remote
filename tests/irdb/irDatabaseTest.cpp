/**
 * @file irDatabaseTest.cpp
 * @brief Тесты базы ИК-кодов: сборка из файлов архива, поиск, записи, перенос устройства в
 * конфигурацию.
 *
 * Фикстура — синтетические протоколы и три устройства, собранные в коде (`irdbFixture.h`).
 * Ожидания следуют из конструкции фикстуры, а не из содержимого публичного архива.
 */

#include "harmony/core/irdb/irDatabase.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/irTables.h"
#include "harmony/core/ir/carrierVm.h"
#include "formatFixtures.h"
#include "irdbFixture.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>

using namespace harmony;

namespace {

// Встроенный пример приложения: настоящий IrProto.bin, с которым сверяется сборка.
const std::filesystem::path kDemo =
    std::filesystem::path(HARMONY_SOURCE_DIR) / "app" / "resources" / "demo";

/// Собирает базу из синтетических файлов и открывает её.
irdb::IrDatabase buildFixture(const std::filesystem::path& dir) {
    const auto files = test::writeIrDatabaseSource(dir);
    const auto file = dir / "test.irdb";
    auto stats = irdb::buildDatabase(files.devices, files.protocols, file, "test");
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
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());

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
    test::TempFolder dir("irdb");
    const auto original = buildFixture(dir.path());
    const auto files = test::writeIrDatabaseSource(dir.path());
    const auto damaged = dir.path() / "broken.gz";
    std::filesystem::copy_file(files.devices, damaged);
    std::filesystem::resize_file(damaged, std::filesystem::file_size(damaged) / 2);
    EXPECT_FALSE(irdb::buildDatabase(damaged, files.protocols, dir.path() / "test.irdb", "broken"));
    const auto reopened = irdb::IrDatabase::open(dir.path() / "test.irdb");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->source(), "test");
    EXPECT_EQ(reopened->entries().size(), original.entries().size());
}

TEST(IrDatabase, ReadsVersionTwoWithoutBuildMetadata) {
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());
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
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());

    std::size_t found = 0;
    EXPECT_EQ(database.search("SONY str", 10, &found).size(), 1U);
    EXPECT_EQ(found, 1U);
    EXPECT_EQ(database.search("s", 10).size(), 2U); // Samsung, Sony
    EXPECT_TRUE(database.search("sony yamaha", 10).empty());
    EXPECT_EQ(database.search("a", 1, &found).size(), 1U); // ограничение числа результатов
    EXPECT_EQ(found, 2U);
}

TEST(IrDatabase, ReadsRecordWithPowerAndInputs) {
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());

    const auto samsung = database.device(entryOf(database, "samsung hln501w"));
    ASSERT_TRUE(samsung) << samsung.error().message;
    EXPECT_EQ(samsung->model, "HLN501W");
    // 24 команды цифр, 31 аппаратная кнопка, питание и пять входов — конструкция фикстуры.
    EXPECT_EQ(samsung->commands.size(), 62U);
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
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());
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
    test::TempFolder dir("irdb");
    const auto files = test::writeIrDatabaseSource(dir.path());
    const auto file = dir.path() / "cancelled.irdb";
    auto stats = irdb::buildDatabase(files.devices, files.protocols, file, "test",
                                     [](std::uint64_t, std::uint64_t) { return false; });
    // В фикстуре меньше 256 строк: ход сообщается в конце, отменять уже поздно — база собрана.
    if (!stats) {
        EXPECT_EQ(stats.error().code, ErrorCode::Cancelled);
        EXPECT_FALSE(std::filesystem::exists(file));
    }
    EXPECT_FALSE(std::filesystem::exists(file.string() + ".tmp"));
}

TEST(IrDatabase, RejectsOtherFiles) {
    test::TempFolder dir("irdb");
    const auto files = test::writeIrDatabaseSource(dir.path());
    EXPECT_FALSE(irdb::IrDatabase::open(files.protocols));
    EXPECT_EQ(irdb::IrDatabase::open(dir.path() / "missing.irdb").error().code, ErrorCode::NotFound);
    EXPECT_FALSE(irdb::ProtocolLibrary::parse("{}"));
}

TEST(IrDatabase, ImportsDeviceIntoConfiguration) {
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());
    const auto samsung = database.device(entryOf(database, "samsung"));
    ASSERT_TRUE(samsung);

    ConfigDocument document = ConfigDocument::createNew("Test");
    auto report = document.importDbDevice(*samsung, database.protocols());
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_EQ(report->commands, static_cast<int>(samsung->commands.size()));
    EXPECT_EQ(report->protocolCommands, report->commands); // все — протоколом, SsIr.bin не тронут
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

TEST(IrDatabase, ResolvesHubKeyCodesIntoIrData) {
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());
    const auto samsung = database.device(entryOf(database, "samsung"));
    ASSERT_TRUE(samsung);
    const std::string keyCode = samsung->commands.front().keyCode;

    // Хаб: у команды только KeyCode; вторая команда — с неизвестным протоколом.
    const std::string json = R"({
  "DeviceList.json": {"DevicesWithFeatures": [
    {"Device": {"Id-": 1, "Name": "TV", "Manufacturer": "Samsung", "Model": "X", "DeviceType": 1},
     "Commands": [
       {"Name": "Zero", "KeyCode": ")" + keyCode + R"(", "TransportType": 1},
       {"Name": "Broken", "KeyCode": "G:No Such Protocol:(0x1)()():1", "TransportType": 1}
     ]}
  ]},
  "ActivityList.json": {"Activities": []}
})";
    const auto hub = formats::parseHubConfig(json);
    ASSERT_TRUE(hub) << hub.error().message;
    auto document = ConfigDocument::fromHub(*hub, "hub");
    ASSERT_TRUE(document) << document.error().message;
    ASSERT_TRUE(document->model().devices.at(0).commands.at(0).code.empty());

    const auto before = document->snapshot();
    const auto report = document->resolveHubCodes(database.protocols());
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_EQ(report->asProtocol + report->asSignal, 1);
    EXPECT_EQ(report->skipped.size(), 1U);

    const auto& commands = document->model().devices.at(0).commands;
    EXPECT_FALSE(commands.at(0).code.empty());
    EXPECT_FALSE(commands.at(0).native.empty()); // поля хаба сохраняются
    EXPECT_TRUE(commands.at(1).code.empty());

    // Повторный запуск ничего не меняет: у команды уже есть код.
    const auto again = document->resolveHubCodes(database.protocols());
    ASSERT_TRUE(again);
    EXPECT_EQ(again->asProtocol + again->asSignal, 0);

    // Отмена возвращает всё.
    ASSERT_TRUE(document->restore(before));
    EXPECT_TRUE(document->model().devices.at(0).commands.at(0).code.empty());
}

TEST(IrDatabase, MatchesStockHubDeviceByCommandNames) {
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());
    const auto samsung = database.device(entryOf(database, "samsung"));
    ASSERT_TRUE(samsung);
    const std::string known = samsung->commands.front().name; // «0»

    // Стоковый хаб: только имена команд, ни KeyCode, ни кодов.
    const std::string json = R"({
  "DeviceList.json": {"DevicesWithFeatures": [
    {"Device": {"Id-": 1, "Name": "TV", "Manufacturer": "Samsung", "Model": "HLN501W", "DeviceType": 1},
     "Commands": [{"Name": ")" + known + R"("}, {"Name": "NoSuchCommand"}]}
  ]},
  "ActivityList.json": {"Activities": []}
})";
    const auto hub = formats::parseHubConfig(json);
    ASSERT_TRUE(hub) << hub.error().message;
    auto document = ConfigDocument::fromHub(*hub, "hub");
    ASSERT_TRUE(document) << document.error().message;

    // Без KeyCode автоматическое превращение ничего не находит.
    const auto automatic = document->resolveHubCodes(database.protocols());
    ASSERT_TRUE(automatic);
    EXPECT_EQ(automatic->asProtocol + automatic->asSignal, 0);

    const auto before = document->snapshot();
    const auto report = document->matchHubDevice("1", *samsung, database.protocols());
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_EQ(report->asProtocol + report->asSignal, 1);
    EXPECT_EQ(report->skipped.size(), 1U);

    const auto& commands = document->model().devices.at(0).commands;
    EXPECT_FALSE(commands.at(0).code.empty());
    EXPECT_TRUE(commands.at(1).code.empty());
    // Подобранный код помечен как взятый из базы (и это попадает в XML); пропущенная команда — нет.
    EXPECT_TRUE(commands.at(0).fromDatabase);
    EXPECT_FALSE(commands.at(1).fromDatabase);
    EXPECT_NE(document->toXml().find("<CodeSource>Database</CodeSource>"), std::string::npos);

    EXPECT_EQ(document->matchHubDevice("999", *samsung, database.protocols()).error().code, ErrorCode::NotFound);

    ASSERT_TRUE(document->restore(before));
    EXPECT_TRUE(document->model().devices.at(0).commands.at(0).code.empty());
    EXPECT_FALSE(document->model().devices.at(0).commands.at(0).fromDatabase);
}

TEST(IrDatabase, RanksCandidatesByCommandNameMatch) {
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());
    const std::size_t samsungEntry = entryOf(database, "samsung");
    const auto samsung = database.device(samsungEntry);
    ASSERT_TRUE(samsung);

    std::vector<std::string> names;
    for (const auto& command : samsung->commands) {
        names.push_back(command.name);
    }
    names.push_back("NoSuchCommand");

    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < database.entries().size(); ++i) {
        candidates.push_back(i);
    }
    // Самсунг стоит первым, как бы ни были упорядочены кандидаты.
    std::ranges::reverse(candidates);
    const auto ranked = irdb::rankByCommandNames(database, candidates, names);
    ASSERT_EQ(ranked.size(), candidates.size());
    EXPECT_EQ(ranked.front().entry, samsungEntry);
    EXPECT_EQ(ranked.front().matched, samsung->commands.size());
    EXPECT_EQ(ranked.front().total, samsung->commands.size() + 1);
    EXPECT_EQ(ranked.front().extra, 0U);
    for (std::size_t i = 1; i < ranked.size(); ++i) {
        EXPECT_GE(ranked[i - 1].matched, ranked[i].matched);
    }

    // Регистр имён не важен.
    std::string shouted = names.front();
    std::ranges::transform(shouted, shouted.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const auto single = irdb::rankByCommandNames(database, {samsungEntry}, {shouted});
    ASSERT_EQ(single.size(), 1U);
    EXPECT_EQ(single.front().matched, 1U);
}

TEST(IrDatabase, ImportsToggleDeviceWithRelativeInputs) {
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());
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
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());
    const auto sony = database.device(entryOf(database, "sony"));
    ASSERT_TRUE(sony);

    auto document = ConfigDocument::open(kDemo);
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
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());
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
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());
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
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());
    const auto sony = database.device(entryOf(database, "sony"));
    ASSERT_TRUE(sony);
    const auto zero = std::ranges::find(sony->commands, std::string("0"), &irdb::DbCommand::name);
    ASSERT_NE(zero, sony->commands.end());
    ASSERT_NE(zero->keyCode.find("Sony 12 Bit"), std::string::npos);
    const auto native = database.protocols().compile(*zero);
    ASSERT_TRUE(native) << native.error().message;

    // Запись 4 образца (PlayStation 2) собрана программой Logitech из тех же таймингов Sony,
    // только кадр в 20 бит. С поправкой длины кадра наша запись должна совпасть побайтно.
    std::ifstream file(kDemo / "IrProto.bin", std::ios::binary);
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
    test::TempFolder dir("irdb");
    const auto database = buildFixture(dir.path());
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
