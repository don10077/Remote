/**
 * @file configAddTest.cpp
 * @brief Тесты добавления: перенос устройства с ИК-данными, новая активность, ActionLists.xml.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/irTables.h"
#include "harmony/core/undo/documentEdit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

using harmony::ActivityDraft;
using harmony::ConfigDocument;
using harmony::ErrorCode;
namespace formats = harmony::formats;

namespace {

const std::filesystem::path kData(HARMONY_TEST_DATA_DIR);
const std::filesystem::path kSampleDir = kData / "userconfig";

constexpr const char* kTv = "92555930";       ///< Hisense: протокол 1, входы HDMI1…
constexpr const char* kReceiver = "82969357"; ///< Denon: протоколы 2 и 3
constexpr const char* kPs4 = "90581251";      ///< Без команд и без состояния питания

std::string readText(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

formats::Bytes readBytes(const std::filesystem::path& file) {
    const std::string text = readText(file);
    return {text.begin(), text.end()};
}

/// Временная папка теста, удаляется в конце.
class TempDir {
public:
    TempDir() : path_(std::filesystem::temp_directory_path() /
                      ("harmony-add-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)))) {
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

ConfigDocument openSample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << document.error().message;
    return std::move(*document);
}

std::size_t count(const std::string& text, const std::string& what) {
    std::size_t n = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        ++n;
    }
    return n;
}

} // namespace

// --- Перенос устройства -------------------------------------------------------

TEST(ImportDevice, CarriesIrProtocolsIntoNewConfig) {
    TempDir temp;
    const ConfigDocument source = openSample();
    ConfigDocument target = ConfigDocument::createNew("Новая");

    auto id = target.importDevice(source, kReceiver);
    ASSERT_TRUE(id) << id.error().message;
    EXPECT_EQ(*id, kReceiver); // id свободен — сохраняется
    ASSERT_TRUE(target.saveAs(temp.path()));

    // Каждая команда указывает на тот же протокол, что и в источнике, — под новым номером.
    auto sourceTable = formats::parseIrProto(readBytes(kSampleDir / "IrProto.bin"));
    auto targetTable = formats::parseIrProto(readBytes(temp.path() / "IrProto.bin"));
    ASSERT_TRUE(sourceTable && targetTable);
    EXPECT_EQ(targetTable->entries.size(), 2U); // у ресивера два протокола из пяти
    const auto* before = source.model().findDevice(kReceiver);
    const auto* after = target.model().findDevice(kReceiver);
    ASSERT_NE(after, nullptr);
    ASSERT_EQ(after->commands.size(), before->commands.size());
    for (std::size_t i = 0; i < after->commands.size(); ++i) {
        const auto& old = before->commands[i];
        const auto& moved = after->commands[i];
        EXPECT_EQ(targetTable->entries[static_cast<std::size_t>(moved.protocol)],
                  sourceTable->entries[static_cast<std::size_t>(old.protocol)]) << moved.name;
        EXPECT_EQ(moved.code.substr(6), old.code.substr(6)) << moved.name; // меняется только номер
    }

    // Хеш протоколов — один и тот же в обоих XML и в обёртке IrProto.bin.
    const std::string hash = formats::protocolHash(readBytes(temp.path() / "IrProto.bin"));
    EXPECT_EQ(target.model().property("ProtocolCacheHash"), hash);
    EXPECT_NE(readText(temp.path() / "UserConfiguration.xml").find("<Hash>" + hash + "</Hash>"),
              std::string::npos);
    const std::string actions = readText(temp.path() / "ActionLists.xml");
    EXPECT_NE(actions.find("<Hash>" + hash + "</Hash>"), std::string::npos);
    // Действия кнопок устройства попали в ActionLists.xml.
    EXPECT_EQ(count(actions, "<ActionList "), 215U);
}

TEST(ImportDevice, NewDeviceIsSwitchedOffEverywhere) {
    const ConfigDocument source = openSample();
    ConfigDocument target = ConfigDocument::createNew("Новая");

    ASSERT_TRUE(target.importDevice(source, kTv));
    ASSERT_TRUE(target.importDevice(source, kPs4));

    const auto& powerOff = target.model().activities.front();
    EXPECT_NE(std::ranges::find(powerOff.powerOff, kTv), powerOff.powerOff.end());
    const std::string xml = target.toXml();
    // «Выключить всё» шлёт выключение только тому, чьё питание пульт отслеживает.
    EXPECT_NE(xml.find("<Parameter name=\"DeviceId\">92555930</Parameter><Parameter name=\"State\">Power"),
              std::string::npos);
    EXPECT_EQ(xml.find("<Parameter name=\"DeviceId\">90581251</Parameter><Parameter name=\"State\">Power"),
              std::string::npos);
}

TEST(ImportDevice, DuplicateGetsNewIdAndNameAndSharesProtocols) {
    TempDir temp;
    const ConfigDocument source = openSample();
    ConfigDocument target = openSample();

    auto id = target.importDevice(source, kTv);
    ASSERT_TRUE(id) << id.error().message;

    EXPECT_NE(*id, kTv);
    const auto* copy = target.model().findDevice(*id);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->label, "Hisense 65U7HQ (2)");
    // Ссылки внутри устройства переименованы: кнопки шлют команды копии.
    for (const auto& group : copy->controlGroups) {
        for (const auto& button : group.buttons) {
            EXPECT_TRUE(button.actionId.starts_with(*id + "_")) << button.actionId;
        }
    }
    // Протокол уже был — таблица не растёт, хеш прежний.
    ASSERT_TRUE(target.saveAs(temp.path()));
    EXPECT_EQ(readBytes(temp.path() / "IrProto.bin"), readBytes(kSampleDir / "IrProto.bin"));
    EXPECT_EQ(target.model().property("ProtocolCacheHash"), "0x8595F4FF");
}

TEST(ImportDevice, CarriesRawCodes) {
    // Источник: устройство с сырым кодом №1 из SsIr.bin и командой с протоколом 0.
    TempDir temp;
    const std::filesystem::path sourceDir = temp.path() / "источник";
    std::filesystem::create_directories(sourceDir);
    std::filesystem::copy_file(kData / "irtables" / "SsIr2.bin", sourceDir / "SsIr.bin");
    std::filesystem::copy_file(kSampleDir / "IrProto.bin", sourceDir / "IrProto.bin");
    std::ofstream(sourceDir / "UserConfiguration.xml", std::ios::binary)
        << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Root><Device><Id>11111111</Id><Type>Amplifier</Type>"
           "<Presentation><Label>Усилитель</Label></Presentation><Commands>"
           "<Command><Name>PowerOn</Name><Data><Protocol>-1</Protocol><Code>0xFFFF0100</Code></Data></Command>"
           "<Command><Name>Mute</Name><Data><Protocol>4</Protocol><Code>0x0400F40101</Code></Data></Command>"
           "</Commands></Device></Root>";
    auto source = ConfigDocument::open(sourceDir);
    ASSERT_TRUE(source) << source.error().message;
    ConfigDocument target = ConfigDocument::createNew("Новая");

    auto imported = target.importDevice(*source, "11111111");
    ASSERT_TRUE(imported) << imported.error().message;
    ASSERT_TRUE(target.saveAs(temp.path() / "цель"));

    const auto* device = target.model().findDevice("11111111");
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->commands[0].code, "0xFFFF0000"); // сырой код №1 стал №0
    EXPECT_EQ(device->commands[1].protocol, 0);
    EXPECT_EQ(device->commands[1].code, "0x0000F40101"); // протокол №4 стал №0
    auto raw = formats::parseSsIr(readBytes(temp.path() / "цель" / "SsIr.bin"));
    auto rawSource = formats::parseSsIr(readBytes(sourceDir / "SsIr.bin"));
    ASSERT_TRUE(raw && rawSource);
    ASSERT_EQ(raw->entries.size(), 1U);
    EXPECT_EQ(raw->entries[0], rawSource->entries[1]);
}

TEST(ImportDevice, FailsCleanlyWithoutSourceTables) {
    // Документ без IrProto.bin (только XML): перенос его устройств невозможен — и ничего не меняется.
    std::ifstream file(kSampleDir / "UserConfiguration.xml", std::ios::binary);
    const std::string xml{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    auto source = ConfigDocument::fromXml(xml, "без таблиц");
    ASSERT_TRUE(source);
    ConfigDocument target = ConfigDocument::createNew("Новая");
    const std::string before = target.toXml();

    const auto id = target.importDevice(*source, kTv);

    ASSERT_FALSE(id);
    EXPECT_EQ(id.error().code, ErrorCode::Format);
    EXPECT_EQ(target.toXml(), before);
    EXPECT_EQ(target.importDevice(*source, "нет").error().code, ErrorCode::NotFound);
}

TEST(ImportDevice, UndoRemovesDeviceAndProtocols) {
    TempDir temp;
    const ConfigDocument source = openSample();
    ConfigDocument target = ConfigDocument::createNew("Новая");
    harmony::undo::UndoStack stack;

    auto command = harmony::undo::editDocument(target, "Добавить", [&](ConfigDocument& d) {
        auto id = d.importDevice(source, kReceiver);
        return id ? harmony::Result<void>{} : std::unexpected(id.error());
    });
    ASSERT_TRUE(command);
    stack.push(std::move(*command));
    stack.undo();

    EXPECT_TRUE(target.model().devices.empty());
    ASSERT_TRUE(target.saveAs(temp.path()));
    EXPECT_TRUE(formats::parseIrProto(readBytes(temp.path() / "IrProto.bin"))->entries.empty());
}

// --- Новая активность ---------------------------------------------------------

TEST(AddActivity, AssignsButtonsByRoles) {
    ConfigDocument document = openSample();
    ActivityDraft draft;
    draft.label = "Кино";
    draft.type = "VirtualDvd";
    draft.devices = {kTv, kReceiver};
    draft.controlDevice = kTv;
    draft.volumeDevice = kReceiver;
    draft.displayDevice = kTv;
    draft.inputs = {{kTv, "HDMI2"}};

    auto id = document.addActivity(draft);
    ASSERT_TRUE(id) << id.error().message;

    const auto* activity = document.model().findActivity(*id);
    ASSERT_NE(activity, nullptr);
    EXPECT_EQ(document.model().activities.back().id, *id); // в конце списка
    EXPECT_EQ(activity->label, "Кино");
    EXPECT_EQ(activity->powerOn, (std::vector<std::string>{kTv, kReceiver}));
    EXPECT_EQ(activity->powerOff.size(), document.model().devices.size() - 2);

    std::map<std::string, std::string> keys;
    for (const auto& group : activity->controlGroups) {
        for (const auto& button : group.buttons) {
            keys[button.name] = button.actionId;
        }
    }
    EXPECT_EQ(keys["VolumeUp"], std::string(kReceiver) + "_VolumeUp_Hold");
    EXPECT_EQ(keys["Menu"], std::string(kTv) + "_Menu_Hold");
    EXPECT_EQ(keys["Number5"], std::string(kTv) + "_5_Hold");
    EXPECT_NE(document.toXml().find("<Parameter name=\"State\">Input</Parameter><Parameter "
                                    "name=\"Value\">HDMI2</Parameter>"),
              std::string::npos);
}

TEST(AddActivity, SavedActivityGetsActionLists) {
    TempDir temp;
    ConfigDocument document = openSample();
    ActivityDraft draft;
    draft.label = "Только ресивер";
    draft.type = "VirtualCdMulti";
    draft.devices = {kReceiver};

    ASSERT_TRUE(document.addActivity(draft));
    ASSERT_TRUE(document.saveAs(temp.path()));

    // Кнопки ресивера уже были в образце — новых действий нет; файл прежний.
    EXPECT_EQ(readText(temp.path() / "ActionLists.xml"), readText(kSampleDir / "ActionLists.xml"));
    auto reopened = ConfigDocument::open(temp.path());
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->model().activities.back().label, "Только ресивер");
}

TEST(AddActivity, AcceptsEveryHarmonyType) {
    // Все 23 типа Harmony 900: активность создаётся, тип и свойства — по типу.
    EXPECT_EQ(harmony::activityTypes().size(), 23U);
    for (const std::string& type : harmony::activityTypes()) {
        ConfigDocument document = openSample();
        ActivityDraft draft;
        draft.label = type;
        draft.type = type;
        draft.devices = {kTv};
        auto id = document.addActivity(draft);
        ASSERT_TRUE(id) << type << ": " << id.error().message;
        EXPECT_EQ(document.model().findActivity(*id)->type, type);
        EXPECT_NE(document.toXml().find("<Property name=\"ControlGroup_Hard Buttons\">True</Property>"),
                  std::string::npos);
    }
    // Радио — с каналами, но без телепрограммы.
    ConfigDocument document = openSample();
    ActivityDraft draft;
    draft.label = "Радио";
    draft.type = "VirtualRadioSimple";
    draft.devices = {kReceiver};
    ASSERT_TRUE(document.addActivity(draft));
    const std::string xml = document.toXml();
    const std::string added = xml.substr(xml.rfind("<Activity>"));
    EXPECT_NE(added.find("ChannelButtonBehaviour"), std::string::npos);
    EXPECT_EQ(added.find("GuideButtonMode"), std::string::npos);
}

TEST(AddActivity, RejectsBadDrafts) {
    ConfigDocument document = openSample();
    const std::string before = document.toXml();
    ActivityDraft draft;
    draft.label = "X";

    EXPECT_EQ(document.addActivity(draft).error().code, ErrorCode::Format); // нет устройств
    draft.devices = {"нет"};
    EXPECT_EQ(document.addActivity(draft).error().code, ErrorCode::NotFound);
    draft.devices = {kTv};
    draft.volumeDevice = kReceiver; // не включается в активности
    EXPECT_EQ(document.addActivity(draft).error().code, ErrorCode::Format);
    draft.volumeDevice.clear();
    draft.inputs = {{kTv, "SCART"}}; // такого входа нет
    EXPECT_EQ(document.addActivity(draft).error().code, ErrorCode::Format);
    draft.inputs.clear();
    draft.label.clear();
    EXPECT_EQ(document.addActivity(draft).error().code, ErrorCode::Format);
    EXPECT_EQ(document.toXml(), before);
}

TEST(AddActivity, WorksInNewConfigAfterImport) {
    TempDir temp;
    const ConfigDocument source = openSample();
    ConfigDocument document = ConfigDocument::createNew("С нуля");
    ASSERT_TRUE(document.importDevice(source, kTv));
    ActivityDraft draft;
    draft.label = "Смотреть ТВ";
    draft.devices = {kTv};

    auto id = document.addActivity(draft);
    ASSERT_TRUE(id) << id.error().message;
    ASSERT_TRUE(document.saveAs(temp.path()));

    auto reopened = ConfigDocument::open(temp.path());
    ASSERT_TRUE(reopened) << reopened.error().message;
    ASSERT_EQ(reopened->model().activities.size(), 2U);
    const auto& activity = reopened->model().activities.back();
    EXPECT_EQ(activity.type, "VirtualTelevisionN");
    EXPECT_FALSE(activity.controlGroups.empty());
    // Каждая кнопка активности описана в ActionLists.xml.
    const std::string actions = readText(temp.path() / "ActionLists.xml");
    for (const auto& group : activity.controlGroups) {
        for (const auto& button : group.buttons) {
            EXPECT_NE(actions.find("name=\"" + button.actionId + "\""), std::string::npos) << button.actionId;
        }
    }
}

TEST(AddActivity, PlanSkipsMissingCommands) {
    const ConfigDocument document = openSample();
    ActivityDraft draft;
    draft.devices = {kPs4};
    draft.controlDevice = kPs4;

    EXPECT_TRUE(harmony::planHardButtons(document.model(), draft).empty()); // у PS4 нет команд
}

TEST(ImportDevice, CheckIrDataExplainsMissingTables) {
    EXPECT_TRUE(openSample().checkIrData());
    std::ifstream file(kSampleDir / "UserConfiguration.xml", std::ios::binary);
    const std::string xml{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    auto bare = ConfigDocument::fromXml(xml, "только XML");
    ASSERT_TRUE(bare);
    const auto checked = bare->checkIrData();
    ASSERT_FALSE(checked);
    EXPECT_NE(checked.error().message.find("IrProto.bin"), std::string::npos);
}

TEST(ImportDevice, FailedBatchLeavesDocumentUntouched) {
    // Перенос нескольких устройств одним шагом: второе не переносится — первое тоже откатывается.
    const ConfigDocument source = openSample();
    ConfigDocument target = ConfigDocument::createNew("Новая");
    const std::string before = target.toXml();

    auto command = harmony::undo::editDocument(target, "Добавить", [&](ConfigDocument& d) -> harmony::Result<void> {
        if (auto first = d.importDevice(source, kTv); !first) {
            return std::unexpected(first.error());
        }
        auto second = d.importDevice(source, "нет такого");
        return second ? harmony::Result<void>{} : std::unexpected(second.error());
    });

    EXPECT_FALSE(command);
    EXPECT_EQ(target.toXml(), before);
}

// --- Новое устройство и выученные команды -------------------------------------

namespace {

harmony::ir::IrSignal necSignal(std::uint8_t command) {
    harmony::ir::IrSignal signal;
    signal.durations = {9000, -4500};
    const std::uint32_t frame = 0x04U | (0xFBU << 8) | (static_cast<std::uint32_t>(command) << 16) |
                                (static_cast<std::uint32_t>(~command & 0xFF) << 24);
    for (int bit = 0; bit < 32; ++bit) {
        signal.durations.push_back(560);
        signal.durations.push_back(((frame >> bit) & 1U) != 0 ? -1690 : -560);
    }
    signal.durations.push_back(560);
    signal.durations.push_back(-40000);
    return signal;
}

} // namespace

TEST(LearnedCommands, CreatesEmptyDeviceSwitchedOffEverywhere) {
    ConfigDocument document = openSample();
    harmony::DeviceDraft draft{.label = "Проектор", .type = "Projector", .manufacturer = "Epson", .model = "EH-TW"};

    auto id = document.createDevice(draft);
    ASSERT_TRUE(id) << id.error().message;

    const auto* device = document.model().findDevice(*id);
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->label, "Проектор");
    EXPECT_EQ(device->type, "Projector");
    EXPECT_TRUE(device->commands.empty());
    EXPECT_FALSE(device->powerControlled);
    for (const auto& activity : document.model().activities) {
        EXPECT_NE(std::ranges::find(activity.powerOff, *id), activity.powerOff.end()) << activity.label;
    }
    EXPECT_EQ(document.createDevice({}).error().code, ErrorCode::Format);
}

TEST(LearnedCommands, LearnsAndReplacesRawCommands) {
    TempDir temp;
    ConfigDocument document = ConfigDocument::createNew("С нуля");
    auto id = document.createDevice({.label = "ТВ", .type = "Television", .manufacturer = "", .model = ""});
    ASSERT_TRUE(id);

    ASSERT_TRUE(document.learnCommand(*id, "VolumeUp", necSignal(0x02)));
    ASSERT_TRUE(document.learnCommand(*id, "Mute", necSignal(0x09)));
    ASSERT_TRUE(document.learnCommand(*id, "VolumeUp", necSignal(0x03))); // выучили заново

    const auto* device = document.model().findDevice(*id);
    ASSERT_EQ(device->commands.size(), 2U);
    EXPECT_EQ(device->commands[0].protocol, -1);
    auto signal = document.commandSignal(*id, "VolumeUp");
    ASSERT_TRUE(signal);
    EXPECT_EQ(harmony::ir::decode(*signal)->command, 0x03U);

    // После сохранения и открытия — те же сигналы.
    ASSERT_TRUE(document.saveAs(temp.path()));
    auto reopened = ConfigDocument::open(temp.path());
    ASSERT_TRUE(reopened);
    EXPECT_EQ(harmony::ir::decode(*reopened->commandSignal(*id, "Mute"))->command, 0x09U);
    EXPECT_TRUE(reopened->checkIrData());
}

TEST(LearnedCommands, PowerCommandsDescribePower) {
    ConfigDocument document = ConfigDocument::createNew("С нуля");
    auto toggle = document.createDevice({.label = "Плеер", .type = "DvdCd", .manufacturer = "", .model = ""});
    auto discrete = document.createDevice({.label = "ТВ", .type = "Television", .manufacturer = "", .model = ""});
    ASSERT_TRUE(toggle && discrete);

    ASSERT_TRUE(document.learnCommand(*toggle, "PowerToggle", necSignal(0x10)));
    ASSERT_TRUE(document.learnCommand(*discrete, "PowerOn", necSignal(0x11)));
    EXPECT_FALSE(document.model().findDevice(*discrete)->powerControlled); // одного «вкл» мало
    ASSERT_TRUE(document.learnCommand(*discrete, "PowerOff", necSignal(0x12)));

    EXPECT_TRUE(document.model().findDevice(*toggle)->powerControlled);
    EXPECT_TRUE(document.model().findDevice(*discrete)->powerControlled);
    const std::string xml = document.toXml();
    EXPECT_NE(xml.find("<NextAction><Action><Target>Device</Target><Operation><Name>SendCommand</Name>"
                       "<Parameter name=\"DeviceId\">" + *toggle + "</Parameter><Parameter name=\"Command\">PowerToggle"),
              std::string::npos);
    EXPECT_NE(xml.find("<Name>Off</Name></SetAction>"), std::string::npos);
    // «Выключить всё» теперь выключает оба.
    EXPECT_NE(xml.find("<Parameter name=\"DeviceId\">" + *discrete + "</Parameter><Parameter name=\"State\">Power</Parameter>"),
              std::string::npos);
    // Новую активность с этим устройством пульт сможет включить.
    harmony::ActivityDraft activity;
    activity.label = "Кино";
    activity.devices = {*discrete};
    EXPECT_TRUE(document.addActivity(activity));
}

TEST(LearnedCommands, ClearsButtonsAndStartStepsBeforeRemoving) {
    ConfigDocument document = openSample();
    ASSERT_FALSE(document.commandUsage(kTv, "Home").empty()); // кнопки и запуск «ТВ + AV»

    ASSERT_TRUE(document.clearCommandUses(kTv, "Home"));
    EXPECT_TRUE(document.commandUsage(kTv, "Home").empty());
    EXPECT_TRUE(document.removeCommand(kTv, "Home"));

    // Описание питания так не чистится — его правят в своём окне.
    ASSERT_TRUE(document.clearCommandUses(kTv, "PowerToggle"));
    EXPECT_FALSE(document.commandUsage(kTv, "PowerToggle").empty());
    EXPECT_EQ(document.clearCommandUses(kTv, "НетТакой").error().code, ErrorCode::NotFound);
}

TEST(LearnedCommands, RemovesOnlyUnusedCommands) {
    ConfigDocument document = openSample();

    const auto usage = document.commandUsage(kTv, "Menu");
    EXPECT_FALSE(usage.empty());
    EXPECT_EQ(document.removeCommand(kTv, "Menu").error().code, ErrorCode::Unsupported);
    EXPECT_FALSE(document.commandUsage(kTv, "PowerToggle").empty()); // описание питания

    // У телевизора в образце каждая команда где-то задействована — удаляем новую.
    ASSERT_TRUE(document.learnCommand(kTv, "Netflix", necSignal(0x20)));
    ASSERT_TRUE(document.commandUsage(kTv, "Netflix").empty());
    ASSERT_TRUE(document.removeCommand(kTv, "Netflix"));
    EXPECT_EQ(document.model().findDevice(kTv)->findCommand("Netflix"), nullptr);
    EXPECT_EQ(document.removeCommand(kTv, "Netflix").error().code, ErrorCode::NotFound);
    // Команда по протоколу: сигнал собирается из IrProto.bin.
    const auto menu = document.commandDetails(kTv, "Menu");
    ASSERT_TRUE(menu) << menu.error().message;
    EXPECT_FALSE(menu->learned);
    EXPECT_FALSE(menu->signal.durations.empty());
}

// --- Предел SsIr.bin: номер сырого кода — один байт (до 255) -----------------------

namespace {

/// Новый документ с устройством, у которого в SsIr.bin уже 256 записей (номера 0…255).
struct FullRawTable {
    ConfigDocument document;
    std::string deviceId;
};

FullRawTable fullRawTable() {
    FullRawTable full{ConfigDocument::createNew("Полная"), {}};
    auto id = full.document.createDevice({.label = "ТВ", .type = "Television", .manufacturer = "", .model = ""});
    EXPECT_TRUE(id);
    full.deviceId = id ? *id : std::string();
    for (int i = 0; i <= 0xFF; ++i) {
        EXPECT_TRUE(full.document.learnCommand(full.deviceId, "C" + std::to_string(i),
                                               necSignal(static_cast<std::uint8_t>(i))))
            << i;
    }
    return full;
}

} // namespace

TEST(LearnedCommands, RefusesCodeNumberAbove255) {
    FullRawTable full = fullRawTable();
    const std::string before = full.document.toXml();
    harmony::ir::IrSignal other = necSignal(0x01);
    other.frequency = 40000; // другой сигнал — нужна новая запись

    const auto learned = full.document.learnCommand(full.deviceId, "Extra", other);

    ASSERT_FALSE(learned);
    EXPECT_EQ(learned.error().code, ErrorCode::Format);
    EXPECT_EQ(full.document.toXml(), before);
    // Сигнал, который уже есть в таблице, места не требует.
    EXPECT_TRUE(full.document.learnCommand(full.deviceId, "Again", necSignal(0x05)));
}

TEST(ImportDevice, RefusesRawCodeNumberAbove255) {
    TempDir temp;
    std::filesystem::copy_file(kData / "irtables" / "SsIr2.bin", temp.path() / "SsIr.bin");
    std::filesystem::copy_file(kSampleDir / "IrProto.bin", temp.path() / "IrProto.bin");
    std::ofstream(temp.path() / "UserConfiguration.xml", std::ios::binary)
        << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Root><Device><Id>11111111</Id><Type>Amplifier</Type>"
           "<Presentation><Label>Усилитель</Label></Presentation><Commands>"
           "<Command><Name>PowerOn</Name><Data><Protocol>-1</Protocol><Code>0xFFFF0100</Code></Data></Command>"
           "</Commands></Device></Root>";
    auto source = ConfigDocument::open(temp.path());
    ASSERT_TRUE(source) << source.error().message;
    FullRawTable full = fullRawTable();
    const std::string before = full.document.toXml();

    const auto imported = full.document.importDevice(*source, "11111111");

    ASSERT_FALSE(imported);
    EXPECT_EQ(imported.error().code, ErrorCode::Format);
    EXPECT_EQ(full.document.toXml(), before);
}
