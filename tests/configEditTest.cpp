/**
 * @file configEditTest.cpp
 * @brief Тесты правки конфигурации: переименование, перестановка, удаление, отмена.
 *
 * Основа — конфигурация, собранная в коде (`configFixtures.h`), поэтому ожидания следуют из
 * конструкции фикстуры, а не из содержимого файла реального пульта (правило 19).
 */

#include "configFixtures.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/undo/documentEdit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using harmony::ConfigDocument;
using harmony::ErrorCode;

namespace {

using harmony::test::kActivityId;
using harmony::test::kConfigXml;
using harmony::test::kDvdActivityId;
using harmony::test::kPlayerId;
using harmony::test::kPowerOffId;
using harmony::test::kReceiverId;
using harmony::test::kTvId;

ConfigDocument openConfig() { return harmony::test::openFixture(kConfigXml); }

/// Порядковый номер активности в модели (−1 — нет).
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
    ConfigDocument document = openConfig();

    ASSERT_TRUE(document.setDeviceLabel(kReceiverId, "Ресивер Denon"));

    EXPECT_EQ(document.model().findDevice(kReceiverId)->label, "Ресивер Denon");
    // Изменение попадает в сохраняемый текст и переживает повторное открытие.
    const auto reopened = ConfigDocument::fromXml(document.toXml(), "x");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->model().findDevice(kReceiverId)->label, "Ресивер Denon");
}

TEST(ConfigEdit, RenameChangesOnlyLabel) {
    ConfigDocument document = openConfig();
    const std::string before = document.toXml();

    ASSERT_TRUE(document.setActivityLabel(kActivityId, "Телевизор"));

    // Весь остальной файл — байт в байт прежний.
    std::string expected = before;
    const std::string from = "<Label>Смотреть ТВ</Label>";
    expected.replace(expected.find(from), from.size(), "<Label>Телевизор</Label>");
    EXPECT_EQ(document.toXml(), expected);
}

TEST(ConfigEdit, RejectsEmptyLabelAndUnknownIds) {
    ConfigDocument document = openConfig();
    const std::string before = document.toXml();

    EXPECT_EQ(document.setDeviceLabel(kReceiverId, "").error().code, ErrorCode::Format);
    EXPECT_EQ(document.setDeviceLabel("nope", "x").error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.setActivityLabel("nope", "x").error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.toXml(), before);  // при ошибке документ не меняется
}

TEST(ConfigEdit, MovesActivity) {
    ConfigDocument document = openConfig();
    const std::size_t total = document.model().activities.size();

    ASSERT_TRUE(document.moveActivity(kActivityId, static_cast<int>(total) - 1));
    EXPECT_EQ(activityIndex(document, kActivityId), static_cast<int>(total) - 1);

    ASSERT_TRUE(document.moveActivity(kActivityId, 0));
    EXPECT_EQ(activityIndex(document, kActivityId), 0);

    // За пределами — в конец.
    ASSERT_TRUE(document.moveActivity(kActivityId, 100));
    EXPECT_EQ(activityIndex(document, kActivityId), static_cast<int>(total) - 1);
}

TEST(ConfigEdit, RemovesActivityButNotPowerOff) {
    ConfigDocument document = openConfig();
    const std::size_t count = document.model().activities.size();

    ASSERT_TRUE(document.removeActivity(kActivityId));
    EXPECT_EQ(document.model().activities.size(), count - 1);
    EXPECT_EQ(document.model().findActivity(kActivityId), nullptr);

    EXPECT_EQ(document.removeActivity(kPowerOffId).error().code, ErrorCode::Unsupported);
}

TEST(ConfigEdit, RefusesToRemoveUsedDevice) {
    ConfigDocument document = openConfig();

    const auto removed = document.removeDevice(kReceiverId);

    ASSERT_FALSE(removed);
    EXPECT_EQ(removed.error().code, ErrorCode::Unsupported);
    // В отказе названа активность, которая устройство использует.
    EXPECT_NE(removed.error().message.find("Смотреть ТВ"), std::string::npos) << removed.error().message;
}

TEST(ConfigEdit, RemovesUnusedDeviceWithItsPowerOffs) {
    ConfigDocument document = openConfig();
    const auto id = document.createDevice({"Проектор", "Projector", "Epson", "TW"});
    ASSERT_TRUE(id);

    ASSERT_TRUE(document.removeDevice(*id));

    EXPECT_EQ(document.model().findDevice(*id), nullptr);
    EXPECT_EQ(document.toXml().find(*id), std::string::npos) << "ссылки на устройство остались в файле";
    for (const auto& activity : document.model().activities) {
        EXPECT_FALSE(activity.usesDevice(*id));
    }
}

TEST(ConfigEdit, RemovesDeviceFromActivitiesBeforeDeleting) {
    ConfigDocument document = openConfig();

    // Активность, где было только это устройство, удаляется целиком.
    const auto removed = document.removeDeviceFromActivities(kPlayerId);
    ASSERT_TRUE(removed) << removed.error().message;
    EXPECT_EQ(*removed, (std::vector<std::string>{"Плеер"}));
    EXPECT_EQ(document.model().findActivity(kDvdActivityId), nullptr);
    EXPECT_TRUE(document.activitiesUsingDevice(kPlayerId).empty());
    ASSERT_TRUE(document.removeDevice(kPlayerId));

    EXPECT_EQ(document.removeDeviceFromActivities("nope").error().code, ErrorCode::NotFound);
}

TEST(ConfigEdit, RoleGoesToTheRemainingDevice) {
    ConfigDocument document = openConfig();

    ASSERT_TRUE(document.removeDeviceFromActivities(kReceiverId));

    const auto* activity = document.model().findActivity(kActivityId);
    ASSERT_NE(activity, nullptr);
    EXPECT_FALSE(harmony::model::RemoteConfig::activityNeedsDevice(*activity, kReceiverId));
    // Освободившуюся роль получает первое оставшееся устройство.
    EXPECT_EQ(activity->roleDevice("DEFAULT"), kTvId);
    for (const auto& [deviceId, input] : activity->inputs) {
        EXPECT_NE(deviceId, std::string(kReceiverId)) << input;
    }
    EXPECT_TRUE(document.activitiesUsingDevice(kReceiverId).empty());
}

TEST(ConfigEdit, UndoAndRedoRestoreExactText) {
    ConfigDocument document = openConfig();
    harmony::undo::UndoStack stack;
    const std::string original = document.toXml();

    auto command = harmony::undo::editDocument(document, "Переименовать", [](ConfigDocument& d) {
        return d.setDeviceLabel(kReceiverId, "Ресивер");
    });
    ASSERT_TRUE(command);
    stack.push(std::move(*command));
    const std::string edited = document.toXml();
    EXPECT_EQ(document.model().findDevice(kReceiverId)->label, "Ресивер");

    stack.undo();
    EXPECT_EQ(document.toXml(), original);
    EXPECT_EQ(document.model().findDevice(kReceiverId)->label, "Ресивер");
    EXPECT_EQ(document.model().findDevice(kReceiverId)->manufacturer, "Denon");

    stack.redo();
    EXPECT_EQ(document.toXml(), edited);
}

TEST(ConfigEdit, FailedEditCreatesNoCommand) {
    ConfigDocument document = openConfig();

    const auto command = harmony::undo::editDocument(document, "Удалить", [](ConfigDocument& d) {
        return d.removeDevice(kReceiverId);
    });

    EXPECT_FALSE(command);
}

TEST(ConfigEdit, EditedNewConfigKeepsTextFieldsAsPairs) {
    ConfigDocument document = ConfigDocument::createNew("Тест");

    ASSERT_TRUE(document.setActivityLabel(kPowerOffId, "Всё выключить"));

    // Пустые текстовые поля пульт пишет парой тегов — так и остаётся после правки.
    EXPECT_NE(document.toXml().find("<FirstName></FirstName>"), std::string::npos);
    EXPECT_NE(document.toXml().find("<Label>Всё выключить</Label>"), std::string::npos);
}

TEST(ConfigEdit, ListsActivitiesUsingDevice) {
    ConfigDocument document = openConfig();
    const auto fresh = document.createDevice({"Проектор", "Projector", "Epson", "TW"});
    ASSERT_TRUE(fresh);

    EXPECT_TRUE(document.activitiesUsingDevice(*fresh).empty());
    const auto usedIn = document.activitiesUsingDevice(kReceiverId);
    EXPECT_NE(std::ranges::find(usedIn, std::string("Смотреть ТВ")), usedIn.end());
}

TEST(ConfigEdit, MovesDeviceWithoutChangingCommandsOrActivities) {
    ConfigDocument document = openConfig();
    const std::string before = document.toXml();
    const std::size_t count = document.model().devices.size();
    const std::size_t commands = document.model().findDevice(kPlayerId)->commands.size();

    ASSERT_TRUE(document.moveDevice(kPlayerId, 0));
    EXPECT_EQ(document.model().devices.front().id, kPlayerId);
    EXPECT_EQ(document.model().devices.front().commands.size(), commands);

    ASSERT_TRUE(document.moveDevice(kPlayerId, static_cast<int>(count)));
    EXPECT_EQ(document.model().devices.back().id, kPlayerId);
    EXPECT_EQ(document.toXml(), before);

    const auto moved = document.moveDevice("missing", 0);
    ASSERT_FALSE(moved);
    EXPECT_EQ(document.toXml(), before);
}
