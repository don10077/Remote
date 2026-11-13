/**
 * @file macroTest.cpp
 * @brief Кнопки-макросы: шаги в ActionLists.xml, сохранение, проверка, использование команд.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/macro.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

using namespace harmony;

namespace {

const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";

ConfigDocument sample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << document.error().message;
    return std::move(*document);
}

/// Активность и два её устройства с командами (для шагов макроса).
struct Scene {
    std::string activity;
    std::string first;
    std::string firstCommand;
    std::string second;
    std::string secondCommand;
};

Scene pick(const ConfigDocument& document) {
    for (const auto& activity : document.model().activities) {
        std::vector<const model::Device*> devices;
        for (const auto& id : activity.powerOn) {
            const auto* device = document.model().findDevice(id);
            if (device != nullptr && !device->commands.empty()) devices.push_back(device);
        }
        if (devices.size() >= 2) {
            return {activity.id, devices[0]->id, devices[0]->commands.front().name, devices[1]->id,
                    devices[1]->commands.back().name};
        }
    }
    ADD_FAILURE() << "no activity with two devices";
    return {};
}

Macro steps(const Scene& s) {
    Macro out;
    out.push_back({s.first, StateStep::press(s.firstCommand)});
    out.push_back({s.first, StateStep::pause(1500)});
    out.push_back({s.second, StateStep::press(s.secondCommand)});
    return out;
}

std::string hardAction(const ConfigDocument& document, const std::string& activity, const std::string& key) {
    for (const auto& group : document.model().findActivity(activity)->controlGroups) {
        for (const auto& button : group.buttons) {
            if (button.name == key) return button.actionId;
        }
    }
    return {};
}

} // namespace

TEST(Macro, NameRecognition) {
    EXPECT_TRUE(isMacroAction("79016674_macro_1"));
    EXPECT_TRUE(isMacroAction("79016674_macro_12"));
    EXPECT_FALSE(isMacroAction("92555930_Menu_Hold"));
    EXPECT_FALSE(isMacroAction("79016674_macro_"));
    EXPECT_FALSE(isMacroAction("_macro_1"));
}

TEST(Macro, HardButtonMacroIsStoredAndSurvivesSaving) {
    auto document = sample();
    const Scene s = pick(document);
    ASSERT_TRUE(document.setHardMacro(ButtonOwner::activity(s.activity), "Red", steps(s)));
    const std::string action = hardAction(document, s.activity, "Red");
    ASSERT_TRUE(isMacroAction(action)) << action;
    auto read = document.macro(action);
    ASSERT_TRUE(read) << read.error().message;
    EXPECT_EQ(*read, steps(s));

    // После записи в .EZHex и чтения обратно — тот же макрос; проверка не видит ошибок в нём.
    auto ezhex = document.toEzHex(false);
    ASSERT_TRUE(ezhex) << ezhex.error().message;
    auto reopened = ConfigDocument::fromEzHex(*ezhex, "reopened");
    ASSERT_TRUE(reopened) << reopened.error().message;
    auto again = reopened->macro(action);
    ASSERT_TRUE(again) << again.error().message;
    EXPECT_EQ(*again, steps(s));
    for (const auto& problem : reopened->check()) {
        EXPECT_EQ(problem.what.format.find("macro"), std::string::npos) << problem.what.message;
    }

    // Снова одна команда — макрос из ActionLists.xml уходит при сохранении.
    ASSERT_TRUE(reopened->setHardButton(ButtonOwner::activity(s.activity), "Red", s.first, s.firstCommand));
    auto plain = ConfigDocument::fromEzHex(*reopened->toEzHex(false), "plain");
    ASSERT_TRUE(plain);
    EXPECT_EQ(plain->macro(action).error().code, ErrorCode::NotFound);
}

TEST(Macro, EditingKeepsTheSameAction) {
    auto document = sample();
    const Scene s = pick(document);
    const auto owner = ButtonOwner::activity(s.activity);
    ASSERT_TRUE(document.setHardMacro(owner, "Red", steps(s)));
    const std::string action = hardAction(document, s.activity, "Red");
    Macro shorter = steps(s);
    shorter.pop_back();
    ASSERT_TRUE(document.setHardMacro(owner, "Red", shorter));
    EXPECT_EQ(hardAction(document, s.activity, "Red"), action);
    EXPECT_EQ(*document.macro(action), shorter);
}

TEST(Macro, SoftMacroNeedsALabelAndValidSteps) {
    auto document = sample();
    const Scene s = pick(document);
    const auto owner = ButtonOwner::activity(s.activity);
    const int position = document.freeSoftPosition(owner);
    EXPECT_EQ(document.setSoftMacro(owner, position, "", steps(s)).error().code, ErrorCode::Format);
    EXPECT_EQ(document.setSoftMacro(owner, position, "Night", {}).error().code, ErrorCode::Format);
    Macro broken = steps(s);
    broken.front().step.command = "NoSuchCommand";
    EXPECT_EQ(document.setSoftMacro(owner, position, "Night", broken).error().code, ErrorCode::NotFound);
    ASSERT_TRUE(document.setSoftMacro(owner, position, "Night", steps(s)));
    // Кнопки устройства шлют только ему самому.
    EXPECT_EQ(document.setHardMacro(ButtonOwner::device(s.first), "Red", steps(s)).error().code, ErrorCode::Format);
}

TEST(Macro, CommandUsedByAMacroIsNotRemovedAndUndoRestores) {
    auto document = sample();
    const Scene s = pick(document);
    const auto before = document.snapshot();
    const std::string xmlBefore = document.toXml();
    ASSERT_TRUE(document.setHardMacro(ButtonOwner::activity(s.activity), "Red", steps(s)));
    const auto usage = document.commandUsage(s.second, s.secondCommand);
    EXPECT_TRUE(std::ranges::any_of(usage, [](const auto& use) {
        return use.place == ConfigDocument::CommandUse::Place::MacroButton;
    }));
    EXPECT_EQ(document.removeCommand(s.second, s.secondCommand).error().code, ErrorCode::Unsupported);
    ASSERT_TRUE(document.restore(before));
    EXPECT_EQ(document.toXml(), xmlBefore);
}
