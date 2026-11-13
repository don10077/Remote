/**
 * @file configEditTest.cpp
 * @brief Тесты правки конфигурации: переименование, перестановка, удаление, отмена.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/undo/documentEdit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using harmony::ConfigDocument;
using harmony::ErrorCode;

namespace {

const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";

constexpr const char* kReceiver = "82969357";   ///< Используется в активностях
constexpr const char* kUnusedTv = "68367800";   ///< Panasonic: ни в одной активности
constexpr const char* kWatchTv = "79016674";    ///< «ТВ + AV»

ConfigDocument openSample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << document.error().message;
    return std::move(*document);
}

/// Порядковый номер активности в модели (-1 — нет).
int activityIndex(const ConfigDocument& document, const std::string& id) {
    const auto& activities = document.model().activities;
    for (std::size_t i = 0; i < activities.size(); ++i) {
        if (activities[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

TEST(ConfigEdit, RenamesDevice) {
    ConfigDocument document = openSample();

    ASSERT_TRUE(document.setDeviceLabel(kReceiver, "Ресивер Denon"));

    EXPECT_EQ(document.model().findDevice(kReceiver)->label, "Ресивер Denon");
    // Изменение попадает в сохраняемый текст и переживает повторное открытие.
    auto reopened = ConfigDocument::fromXml(document.toXml(), "x");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->model().findDevice(kReceiver)->label, "Ресивер Denon");
}

TEST(ConfigEdit, RenameChangesOnlyLabel) {
    ConfigDocument document = openSample();
    const std::string before = document.toXml();

    ASSERT_TRUE(document.setActivityLabel(kWatchTv, "Телевизор"));

    // Весь остальной файл — байт в байт прежний.
    std::string expected = before;
    const std::string from = "<Label>ТВ + AV</Label>";
    expected.replace(expected.find(from), from.size(), "<Label>Телевизор</Label>");
    EXPECT_EQ(document.toXml(), expected);
}

TEST(ConfigEdit, RejectsEmptyLabelAndUnknownIds) {
    ConfigDocument document = openSample();
    const std::string before = document.toXml();

    EXPECT_EQ(document.setDeviceLabel(kReceiver, "").error().code, ErrorCode::Format);
    EXPECT_EQ(document.setDeviceLabel("nope", "x").error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.setActivityLabel("nope", "x").error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.toXml(), before); // при ошибке документ не меняется
}

TEST(ConfigEdit, MovesActivity) {
    ConfigDocument document = openSample();
    ASSERT_EQ(activityIndex(document, kWatchTv), 1);

    ASSERT_TRUE(document.moveActivity(kWatchTv, 0));
    EXPECT_EQ(activityIndex(document, kWatchTv), 0);

    ASSERT_TRUE(document.moveActivity(kWatchTv, 100)); // за пределами — в конец
    EXPECT_EQ(activityIndex(document, kWatchTv),
              static_cast<int>(document.model().activities.size()) - 1);
}

TEST(ConfigEdit, RemovesActivityButNotPowerOff) {
    ConfigDocument document = openSample();
    const auto count = document.model().activities.size();

    ASSERT_TRUE(document.removeActivity(kWatchTv));
    EXPECT_EQ(document.model().activities.size(), count - 1);
    EXPECT_EQ(document.model().findActivity(kWatchTv), nullptr);

    EXPECT_EQ(document.removeActivity("-1").error().code, ErrorCode::Unsupported);
}

TEST(ConfigEdit, RefusesToRemoveUsedDevice) {
    ConfigDocument document = openSample();

    const auto removed = document.removeDevice(kReceiver);

    ASSERT_FALSE(removed);
    EXPECT_EQ(removed.error().code, ErrorCode::Unsupported);
    EXPECT_NE(removed.error().message.find("ТВ + AV"), std::string::npos) << removed.error().message;
}

TEST(ConfigEdit, RemovesUnusedDeviceWithItsPowerOffs) {
    ConfigDocument document = openSample();

    ASSERT_TRUE(document.removeDevice(kUnusedTv));

    EXPECT_EQ(document.model().findDevice(kUnusedTv), nullptr);
    const std::string xml = document.toXml();
    EXPECT_EQ(xml.find(kUnusedTv), std::string::npos) << "ссылки на устройство остались в файле";
    for (const auto& activity : document.model().activities) {
        EXPECT_FALSE(activity.usesDevice(kUnusedTv));
    }
}

TEST(ConfigEdit, RemovesDeviceFromActivitiesBeforeDeleting) {
    ConfigDocument document = openSample();
    constexpr const char* kTv = "92555930";     ///< Hisense: почти во всех активностях
    constexpr const char* kTvOnly = "79016665"; ///< «Просмотр ТВ»: других устройств нет

    const auto removed = document.removeDeviceFromActivities(kTv);

    ASSERT_TRUE(removed) << removed.error().message;
    EXPECT_EQ(*removed, std::vector<std::string>{"Просмотр ТВ"});
    EXPECT_EQ(document.model().findActivity(kTvOnly), nullptr);
    const auto* watchTv = document.model().findActivity(kWatchTv);
    ASSERT_NE(watchTv, nullptr);
    EXPECT_FALSE(harmony::model::RemoteConfig::activityNeedsDevice(*watchTv, kTv));
    EXPECT_EQ(watchTv->roleDevice("DEFAULT"), kReceiver); // управление — оставшемуся устройству
    for (const auto& [deviceId, input] : watchTv->inputs) {
        EXPECT_NE(deviceId, kTv) << input;
    }
    EXPECT_TRUE(document.activitiesUsingDevice(kTv).empty());
    EXPECT_TRUE(document.removeDevice(kTv));

    EXPECT_EQ(document.removeDeviceFromActivities("nope").error().code, ErrorCode::NotFound);
}

TEST(ConfigEdit, UndoAndRedoRestoreExactText) {
    ConfigDocument document = openSample();
    harmony::undo::UndoStack stack;
    const std::string original = document.toXml();

    auto command = harmony::undo::editDocument(document, "Переименовать", [](ConfigDocument& d) {
        return d.setDeviceLabel(kReceiver, "Ресивер");
    });
    ASSERT_TRUE(command);
    stack.push(std::move(*command));
    const std::string edited = document.toXml();
    EXPECT_EQ(document.model().findDevice(kReceiver)->label, "Ресивер");

    stack.undo();
    EXPECT_EQ(document.toXml(), original);
    EXPECT_EQ(document.model().findDevice(kReceiver)->label, "AV-ресивер-ЗАЛ");

    stack.redo();
    EXPECT_EQ(document.toXml(), edited);
}

TEST(ConfigEdit, FailedEditCreatesNoCommand) {
    ConfigDocument document = openSample();

    auto command = harmony::undo::editDocument(document, "Удалить", [](ConfigDocument& d) {
        return d.removeDevice(kReceiver);
    });

    EXPECT_FALSE(command);
}

TEST(ConfigEdit, EditedNewConfigKeepsTextFieldsAsPairs) {
    ConfigDocument document = ConfigDocument::createNew("Тест");

    ASSERT_TRUE(document.setActivityLabel("-1", "Всё выключить"));

    // Пустые текстовые поля пульт пишет парой тегов — так и остаётся после правки.
    EXPECT_NE(document.toXml().find("<FirstName></FirstName>"), std::string::npos);
    EXPECT_NE(document.toXml().find("<Label>Всё выключить</Label>"), std::string::npos);
}

TEST(ConfigEdit, ListsActivitiesUsingDevice) {
    ConfigDocument document = openSample();

    EXPECT_TRUE(document.activitiesUsingDevice(kUnusedTv).empty());
    const auto usedIn = document.activitiesUsingDevice(kReceiver);
    EXPECT_NE(std::ranges::find(usedIn, std::string("ТВ + AV")), usedIn.end());
}

TEST(ConfigEdit, MovesDeviceWithoutChangingCommandsOrActivities) {
    auto document = openSample();
    const auto before = document.toXml();
    const auto id = document.model().devices.back().id;
    const auto count = document.model().devices.size();
    const auto commandCount = document.model().devices.back().commands.size();
    ASSERT_TRUE(document.moveDevice(id, 0));
    EXPECT_EQ(document.model().devices.front().id, id);
    EXPECT_EQ(document.model().devices.front().commands.size(), commandCount);
    ASSERT_TRUE(document.moveDevice(id, count));
    EXPECT_EQ(document.model().devices.back().id, id);
    EXPECT_EQ(document.toXml(), before);
    const auto xml = document.toXml();
    EXPECT_FALSE(document.moveDevice("missing", 0));
    EXPECT_EQ(document.toXml(), xml);
}
