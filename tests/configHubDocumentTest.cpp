/**
 * @file configHubDocumentTest.cpp
 * @brief Документ конфигурации Harmony Hub (правка без записи на пульт) и нейтральный вид ConfigBackend.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/hubConfig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using harmony::ConfigDocument;
using harmony::ConfigFormat;
using harmony::ErrorCode;

namespace {

const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";

/// Родные файлы ресурсов хаба: одно устройство, активность и «Выключить всё».
constexpr std::string_view kHubJson = R"({
  "DeviceList.json": {"DevicesWithFeatures": [
    {"Device": {"Id-": 74521691, "Name": "Smart TV", "Manufacturer": "LG", "Model": "OLED65C4",
                "DeviceType": 1, "DeviceTypeDisplayName": "Television"},
     "Commands": [
       {"Name": "PowerOn", "KeyCode": "G:Toshiba 32 Bit:(0x20DF3AC5)(Repeat)():3", "TransportType": 1}
     ]}
  ]},
  "ActivityList.json": {"Activities": [
    {"Id-": 42111479, "Name": "Smart TV", "ActivityOrder": 1, "Roles": [
       {"DeviceId-": 74521691, "SelectedInput": {"Id-": 1, "Name": "HDMI 3"},
        "__type": "DisplayActivityRole:#Logitech.Harmony.Services"}]},
    {"Id-": -1, "Name": "PowerOff", "ActivityOrder": 0, "Roles": []}
  ]}
})";

/// Временная папка теста, удаляется в конце.
class TempDir {
public:
    TempDir() : path_(std::filesystem::temp_directory_path() /
                      ("harmony-hub-test-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)))) {
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

/// Записывает текст в файл и возвращает путь.
std::filesystem::path writeText(const std::filesystem::path& file, std::string_view text) {
    std::ofstream(file, std::ios::binary) << text;
    return file;
}

} // namespace

TEST(ConfigHubDocument, OpensHubFileAsEditableDocument) {
    const TempDir temp;
    const auto file = writeText(temp.path() / "Мой хаб.json", kHubJson);

    const auto document = ConfigDocument::open(file);
    ASSERT_TRUE(document) << document.error().message;

    const auto backend = document->backend();
    EXPECT_EQ(backend->format(), ConfigFormat::Hub);
    EXPECT_FALSE(backend->canWriteToRemote());
    EXPECT_EQ(document->name(), "Мой хаб");
    ASSERT_EQ(document->model().devices.size(), 1U);
    EXPECT_EQ(document->model().devices[0].displayName(), "Smart TV");
    EXPECT_EQ(document->model().userActivityCount(), 1U);
    EXPECT_EQ(document->model().commandCount(), 1U);
}

TEST(ConfigHubDocument, OpensHubFileWithByteOrderMark) {
    const TempDir temp;
    const auto file = writeText(temp.path() / "hub.json", std::string("\xEF\xBB\xBF") + std::string(kHubJson));

    const auto document = ConfigDocument::open(file);
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_EQ(document->backend()->format(), ConfigFormat::Hub);
}

TEST(ConfigHubDocument, BrokenJsonIsReportedWithFileName) {
    const TempDir temp;
    const auto file = writeText(temp.path() / "broken.json", R"({"nothing":"useful"})");

    const auto document = ConfigDocument::open(file);
    ASSERT_FALSE(document);
    EXPECT_EQ(document.error().code, ErrorCode::Format);
    EXPECT_NE(document.error().message.find("broken.json"), std::string::npos) << document.error().message;
}

namespace {

ConfigDocument hubDocument() {
    auto document = ConfigDocument::fromHub(*harmony::formats::parseHubConfig(kHubJson), "hub");
    EXPECT_TRUE(document) << document.error().message;
    return std::move(*document);
}

} // namespace

TEST(ConfigHubDocument, HubModelSurvivesConversionToXml) {
    const auto document = hubDocument();

    const auto& device = document.model().devices.at(0);
    EXPECT_EQ(device.id, "74521691");
    EXPECT_EQ(device.manufacturer, "LG");
    ASSERT_EQ(device.commands.size(), 1U);
    EXPECT_EQ(device.commands[0].name, "PowerOn");
    EXPECT_EQ(device.commands[0].transport, harmony::model::Transport::Ir); // TransportType 1
    EXPECT_FALSE(device.commands[0].native.empty());
    EXPECT_FALSE(device.commands[0].protocolName.empty());
    const auto activity = document.model().activities.at(0);
    ASSERT_EQ(activity.roles.size(), 1U);
    EXPECT_EQ(activity.roles[0].deviceId, "74521691");
}

/// Синтетический MapList.json в форме, описанной в harmony-hub-control: две раскладки одной
/// активности (у пульта с экраном и у «Smart Control»), аппаратные и экранные кнопки.
constexpr std::string_view kMapListJson = R"({
  "DeviceList.json": {"DevicesWithFeatures": [
    {"Device": {"Id-": 74521691, "Name": "Smart TV", "Manufacturer": "LG", "Model": "OLED65C4"},
     "Commands": [{"Name": "PowerOn"}, {"Name": "VolumeUp"}, {"Name": "Home"}]}
  ]},
  "ActivityList.json": {"Activities": [
    {"Id-": 101, "Name": "Watch TV", "ActivityOrder": 1, "Roles": []}
  ]},
  "MapList.json": {"ButtonMaps": [
    {"ButtonMapIdentifier": "16414Activity101", "ActivityId-": 101, "Buttons": [
      {"__type": "HardRemoteButton", "ButtonKey": "Menu",
       "ButtonAction": {"__type": "ButtonCommandAction", "DeviceId-": 74521691, "CommandName": "Home"}}
    ]},
    {"ButtonMapIdentifier": "16422Activity101", "ActivityId-": 101, "Buttons": [
      {"__type": "HardRemoteButton", "ButtonKey": "VolumeUp",
       "ButtonAction": {"__type": "ButtonCommandAction", "DeviceId-": 74521691, "CommandName": "VolumeUp"},
       "ButtonLongPressAction": {"__type": "ButtonCommandAction", "DeviceId-": 74521691, "CommandName": "Home"}},
      {"__type": "HardRemoteButton", "ButtonKey": "Red",
       "ButtonAction": {"__type": "ButtonSequenceAction", "SequenceId": 900001}},
      {"__type": "SoftRemoteButton", "TextOnRemote": "Power",
       "ButtonAction": {"__type": "ButtonCommandAction", "DeviceId-": 74521691, "CommandName": "PowerOn"}}
    ]},
    {"ButtonMapIdentifier": "16422Root", "Buttons": [
      {"__type": "HardRemoteButton", "ButtonKey": "WatchTVActivity",
       "ButtonAction": {"__type": "ButtonActivityAction", "ActivityId-": 101}}
    ]}
  ]}
})";

TEST(ConfigHubDocument, ParsesMapListButtons) {
    const auto hub = harmony::formats::parseHubConfig(kMapListJson);
    ASSERT_TRUE(hub) << hub.error().message;
    ASSERT_EQ(hub->buttonMaps.size(), 3U);

    const auto& map = hub->buttonMaps.at(1);
    EXPECT_EQ(map.identifier, "16422Activity101");
    EXPECT_EQ(map.activityId, "101");
    EXPECT_EQ(map.skipped, 1); // последовательность «Red»
    ASSERT_EQ(map.buttons.size(), 3U); // нажатие и долгое нажатие VolumeUp, экранная «Power»
    EXPECT_EQ(map.buttons[0].key, "VolumeUp");
    EXPECT_FALSE(map.buttons[0].longPress);
    EXPECT_TRUE(map.buttons[1].longPress);
    EXPECT_EQ(map.buttons[2].label, "Power");
    EXPECT_TRUE(hub->buttonMaps.at(2).activityId.empty());
    EXPECT_EQ(hub->buttonMaps.at(2).skipped, 1); // действие-активность не переносится
}

TEST(ConfigHubDocument, ActivityButtonsComeFromTheRichestMap) {
    const auto hub = harmony::formats::parseHubConfig(kMapListJson);
    ASSERT_TRUE(hub) << hub.error().message;
    auto document = ConfigDocument::fromHub(*hub, "hub");
    ASSERT_TRUE(document) << document.error().message;

    const auto& groups = document->model().activities.at(0).controlGroups;
    ASSERT_EQ(groups.size(), 2U);
    EXPECT_EQ(groups[0].name, "HardKeys");
    ASSERT_EQ(groups[0].buttons.size(), 1U); // долгое нажатие не переносится
    EXPECT_EQ(groups[0].buttons[0].name, "VolumeUp");
    EXPECT_EQ(groups[0].buttons[0].actionId, "74521691_VolumeUp_Press");
    EXPECT_EQ(groups[1].name, "Screen");
    EXPECT_EQ(groups[1].buttons.at(0).label, "Power");

    // Кнопки переживают XML: после сохранения в XML и чтения обратно они на месте.
    const auto reopened = ConfigDocument::fromXml(document->toXml(), "again");
    ASSERT_TRUE(reopened) << reopened.error().message;
    EXPECT_EQ(reopened->model().activities.at(0).controlGroups.size(), 2U);
}

TEST(ConfigHubDocument, MapListAloneIsAcceptedAndWithoutItNothingChanges) {
    const auto alone = harmony::formats::parseHubConfig(R"({"ButtonMaps": []})");
    ASSERT_TRUE(alone);
    EXPECT_TRUE(alone->buttonMaps.empty());

    // Без MapList.json у активности просто нет кнопок.
    auto document = hubDocument();
    EXPECT_TRUE(document.model().activities.at(0).controlGroups.empty());
}

TEST(ConfigHubDocument, EditsWorkAndUndoRestores) {
    auto document = hubDocument();
    const auto before = document.snapshot();

    ASSERT_TRUE(document.setDeviceLabel("74521691", "Другое имя"));
    EXPECT_EQ(document.model().devices[0].displayName(), "Другое имя");
    // Поля хаба не теряются при правке.
    EXPECT_FALSE(document.model().devices[0].commands[0].native.empty());

    ASSERT_TRUE(document.restore(before));
    EXPECT_EQ(document.model().devices[0].displayName(), "Smart TV");
}

TEST(ConfigHubDocument, SavesToFolderAndReopens) {
    const TempDir temp;
    auto document = hubDocument();
    ASSERT_TRUE(document.setDeviceLabel("74521691", "Телевизор"));

    const auto target = temp.path() / "out";
    ASSERT_TRUE(document.saveAs(target));
    const auto reopened = ConfigDocument::open(target);
    ASSERT_TRUE(reopened) << reopened.error().message;
    EXPECT_EQ(reopened->model().devices.at(0).displayName(), "Телевизор");
}

TEST(ConfigHubDocument, RemoteWriteIsRefused) {
    auto document = hubDocument();
    EXPECT_FALSE(document.backend()->canWriteToRemote());

    const auto ezhex = document.toEzHex(true);
    ASSERT_FALSE(ezhex);
    EXPECT_EQ(ezhex.error().code, ErrorCode::Unsupported);
}

TEST(ConfigBackend, Harmony900DocumentIsEditableAndSharesModel) {
    const auto document = ConfigDocument::open(kSampleDir);
    ASSERT_TRUE(document) << document.error().message;

    const auto backend = document->backend();
    EXPECT_EQ(backend->format(), ConfigFormat::Harmony900);
    EXPECT_TRUE(backend->canWriteToRemote());
    EXPECT_EQ(&backend->model(), &document->model());
    EXPECT_EQ(backend->name(), document->name());
    EXPECT_EQ(backend->location(), document->location());
    EXPECT_EQ(backend->location(), kSampleDir);
}

TEST(ConfigBackend, NewDocumentHasNoLocation) {
    const auto document = ConfigDocument::createNew("Тест");

    const auto backend = document.backend();
    EXPECT_EQ(backend->format(), ConfigFormat::Harmony900);
    EXPECT_TRUE(backend->canWriteToRemote());
    EXPECT_TRUE(backend->location().empty());
}

TEST(ConfigBackend, Harmony900EditStillWorks) {
    auto document = ConfigDocument::open(kSampleDir);
    ASSERT_TRUE(document) << document.error().message;
    ASSERT_FALSE(document->model().devices.empty());
    const std::string id = document->model().devices[0].id;

    ASSERT_TRUE(document->setDeviceLabel(id, "Переименовано"));
    EXPECT_EQ(document->model().devices[0].displayName(), "Переименовано");
}
