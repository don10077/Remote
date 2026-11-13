/**
 * @file deviceReplacementTest.cpp
 * @brief Тесты замены устройства в активностях и подбора команд.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/macro.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

using namespace harmony;

namespace {

const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";
constexpr const char* kTv = "92555930"; ///< Hisense — в семи активностях

ConfigDocument openSample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

/// Новый телевизор с командами @p names (сигнал любой).
std::string newTv(ConfigDocument& document, std::initializer_list<const char*> names) {
    auto id = document.createDevice({"New TV", "Television", "Acme", "X1"});
    EXPECT_TRUE(id);
    const ir::IrSignal signal{38000, {9000, -4500, 560, -560, 560, -1690, 560, -40000}};
    for (const char* name : names) {
        EXPECT_TRUE(document.learnCommand(*id, name, signal)) << name;
    }
    return *id;
}

/// Сколько проблем-ошибок.
std::size_t errors(const ConfigDocument& document) {
    return countProblems(document.check(), Problem::Severity::Error);
}

} // namespace

TEST(DeviceReplacement, MatchesCommandsBySpellingAndSynonyms) {
    ConfigDocument document = openSample();
    const std::string id = newTv(document, {"VolumeMute", "Number1", "Power", "Volume_Up"});
    EXPECT_EQ(document.matchDeviceCommand(id, "Mute"), "VolumeMute");
    EXPECT_EQ(document.matchDeviceCommand(id, "1"), "Number1");
    EXPECT_EQ(document.matchDeviceCommand(id, "PowerToggle"), "Power");
    EXPECT_EQ(document.matchDeviceCommand(id, "VolumeUp"), "Volume_Up");
    EXPECT_EQ(document.matchDeviceCommand(id, "Teletext"), "");
}

TEST(DeviceReplacement, MovesEverythingToTheNewDevice) {
    ConfigDocument document = openSample();
    ASSERT_FALSE(document.activitiesUsingDevice(kTv).empty());
    const auto used = document.usedDeviceCommands(kTv);
    ASSERT_FALSE(used.empty());
    const std::size_t errorsBefore = errors(document);

    // У нового — часть команд прежнего: остальные кнопки уберутся.
    const std::string id =
        newTv(document, {"PowerToggle", "VolumeUp", "VolumeDown", "Mute", "ChannelUp", "ChannelDown",
                         "DirectionUp", "DirectionDown", "DirectionLeft", "DirectionRight", "Select", "Menu"});
    DeviceReplacement replacement;
    replacement.oldId = kTv;
    replacement.newId = id;
    replacement.removeOld = true;
    auto report = document.replaceDevice(replacement);
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_GT(report->activities, 0);
    EXPECT_GT(report->buttons, 0);
    EXPECT_GT(report->removed, 0);
    EXPECT_TRUE(report->oldRemoved);
    EXPECT_EQ(document.model().findDevice(kTv), nullptr);
    EXPECT_FALSE(document.activitiesUsingDevice(id).empty());
    // Ничего не ссылается на удалённое и не шлёт несуществующих команд.
    EXPECT_LE(errors(document), errorsBefore);
    EXPECT_EQ(document.toXml().find(std::string(kTv)), std::string::npos);
}

TEST(DeviceReplacement, OnlyChosenActivitiesAndExplicitMapping) {
    ConfigDocument document = openSample();
    const std::string id = newTv(document, {"Power", "Louder"});
    // Активность, где телевизор включается.
    std::string activity;
    for (const auto& item : document.model().activities) {
        if (std::ranges::find(item.powerOn, std::string(kTv)) != item.powerOn.end()) {
            activity = item.id;
            break;
        }
    }
    ASSERT_FALSE(activity.empty());
    DeviceReplacement replacement;
    replacement.oldId = kTv;
    replacement.newId = id;
    replacement.activities = {activity};
    // Первая команда телевизора в этой активности — на «Louder» по таблице.
    const auto used = document.usedDeviceCommands(kTv, {activity});
    ASSERT_FALSE(used.empty());
    replacement.commands = {{used.front().first, "Louder"}};
    replacement.removeOld = true; // ещё нужен в других активностях — не удаляется
    auto report = document.replaceDevice(replacement);
    ASSERT_TRUE(report);
    EXPECT_EQ(report->activities, 1);
    EXPECT_FALSE(report->oldRemoved);
    const model::Activity* changed = document.model().findActivity(activity);
    EXPECT_NE(std::ranges::find(changed->powerOn, id), changed->powerOn.end());
    EXPECT_EQ(std::ranges::find(changed->powerOn, std::string(kTv)), changed->powerOn.end());
    EXPECT_NE(std::ranges::find(changed->powerOff, std::string(kTv)), changed->powerOff.end());
    // Сопоставление из таблицы.
    bool louder = false;
    for (const auto& group : changed->controlGroups) {
        for (const auto& button : group.buttons) {
            louder = louder || button.actionId == id + "_Louder_Hold";
        }
    }
    EXPECT_TRUE(louder);
}

namespace {

/// Активность, где включается телевизор.
std::string tvActivity(const ConfigDocument& document) {
    for (const auto& item : document.model().activities) {
        if (std::ranges::find(item.powerOn, std::string(kTv)) != item.powerOn.end()) {
            return item.id;
        }
    }
    return {};
}

/// Действие кнопки @p key активности.
std::string keyAction(const ConfigDocument& document, const std::string& activity, const std::string& key) {
    for (const auto& group : document.model().findActivity(activity)->controlGroups) {
        for (const auto& button : group.buttons) {
            if (button.name == key) return button.actionId;
        }
    }
    return {};
}

} // namespace

TEST(DeviceReplacement, MacroStepsMoveToTheNewDevice) {
    ConfigDocument document = openSample();
    const std::string activity = tvActivity(document);
    ASSERT_FALSE(activity.empty());
    const auto& commands = document.model().findDevice(kTv)->commands;
    ASSERT_GE(commands.size(), 2U);
    const std::string kept = commands[0].name;
    const std::string lost = commands[1].name;
    ASSERT_TRUE(document.setHardMacro(ButtonOwner::activity(activity), "Red",
                                      {{kTv, StateStep::press(kept)}, {kTv, StateStep::pause(500)},
                                       {kTv, StateStep::press(lost)}}));
    const std::string action = keyAction(document, activity, "Red");
    ASSERT_TRUE(isMacroAction(action)) << action;
    // Команды макроса — в таблице сопоставления.
    const auto used = document.usedDeviceCommands(kTv, {activity});
    EXPECT_TRUE(std::ranges::any_of(used, [&](const auto& item) { return item.first == lost; }));

    const std::string id = newTv(document, {kept.c_str()});
    // Второй команде — «убрать» явно: подбор по синонимам мог бы её сопоставить.
    auto report = document.replaceDevice({kTv, id, {activity}, {{lost, ""}}, false});
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_EQ(report->macroSteps, 1);
    EXPECT_GE(report->removed, 1);

    auto steps = document.macro(action);
    ASSERT_TRUE(steps);
    ASSERT_EQ(steps->size(), 2U); // команда и пауза; шаг без соответствия убран
    EXPECT_EQ((*steps)[0].deviceId, id);
    EXPECT_EQ((*steps)[0].step.command, kept);
    EXPECT_EQ((*steps)[1].deviceId, id);
    EXPECT_EQ((*steps)[1].step.kind, StateStep::Kind::Delay);
    EXPECT_EQ(keyAction(document, activity, "Red"), action);
}

TEST(DeviceReplacement, MacroWithoutMatchingCommandsIsRemoved) {
    ConfigDocument document = openSample();
    const std::string activity = tvActivity(document);
    ASSERT_FALSE(activity.empty());
    const std::string command = document.model().findDevice(kTv)->commands.front().name;
    ASSERT_TRUE(document.setHardMacro(ButtonOwner::activity(activity), "Red", {{kTv, StateStep::press(command)}}));
    ASSERT_TRUE(isMacroAction(keyAction(document, activity, "Red")));

    const std::string id = newTv(document, {"SomethingElse"});
    DeviceReplacement replacement;
    replacement.oldId = kTv;
    replacement.newId = id;
    replacement.activities = {activity};
    replacement.commands = {{command, ""}}; // «убрать»
    auto report = document.replaceDevice(replacement);
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_EQ(report->macroSteps, 0);
    EXPECT_TRUE(keyAction(document, activity, "Red").empty());
}

TEST(DeviceReplacement, RejectsBadArguments) {
    ConfigDocument document = openSample();
    EXPECT_EQ(document.replaceDevice({kTv, "nope", {}, {}, false}).error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.replaceDevice({kTv, kTv, {}, {}, false}).error().code, ErrorCode::Format);
}
