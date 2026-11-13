/**
 * @file configButtonsTest.cpp
 * @brief Тесты правки кнопок активности: аппаратные, экранные, перенос, отмена.
 */

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using harmony::ConfigDocument;
using harmony::ErrorCode;

namespace {

const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";

constexpr const char* kWatchTv = "79016674"; ///< «ТВ + AV»: Hisense + ресивер
constexpr const char* kTv = "92555930";
constexpr const char* kReceiver = "82969357";
constexpr const char* kDune = "68380504"; ///< Не включается в «ТВ + AV»
const harmony::ButtonOwner kOwner = harmony::ButtonOwner::activity(kWatchTv);

ConfigDocument openSample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << document.error().message;
    return std::move(*document);
}

/// Действие кнопки в активности: аппаратной (по имени) или экранной (по месту); пусто — нет.
std::string actionOf(const ConfigDocument& document, const std::string& key, int position = -1) {
    for (const auto& group : document.model().findActivity(kWatchTv)->controlGroups) {
        for (const auto& button : group.buttons) {
            if ((!key.empty() && button.name == key) ||
                (key.empty() && button.name.empty() && button.position == position)) {
                return button.actionId;
            }
        }
    }
    return {};
}

const harmony::model::Button* softAt(const ConfigDocument& document, int position) {
    for (const auto& group : document.model().findActivity(kWatchTv)->controlGroups) {
        for (const auto& button : group.buttons) {
            if (button.name.empty() && button.position == position) {
                return &button;
            }
        }
    }
    return nullptr;
}

} // namespace

TEST(ActivityButtons, ReassignsHardButton) {
    ConfigDocument document = openSample();
    ASSERT_EQ(actionOf(document, "Menu"), std::string(kTv) + "_Menu_Hold");

    ASSERT_TRUE(document.setHardButton(kOwner, "Menu", kReceiver, "Setup"));

    EXPECT_EQ(actionOf(document, "Menu"), std::string(kReceiver) + "_Setup_Hold");
}

TEST(ActivityButtons, ExperimentalProjectSavesAssignmentsButCannotBeWritten) {
    ConfigDocument document = openSample();
    ASSERT_TRUE(document.setTargetRemote("1000"));
    ASSERT_TRUE(document.setHardButton(kOwner, "CustomModelKey", kReceiver, "Setup"));
    const auto saved = document.toEzHex(false);
    ASSERT_TRUE(saved);
    const auto reopened = ConfigDocument::fromEzHex(*saved, "experimental project");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->targetRemote(), "1000");
    EXPECT_EQ(actionOf(*reopened, "CustomModelKey"), std::string(kReceiver) + "_Setup_Hold");
    EXPECT_FALSE(reopened->toEzHex(true));
}

TEST(ActivityButtons, AssignsFreeKeyAndClearsIt) {
    ConfigDocument document = openSample();
    ASSERT_TRUE(actionOf(document, "Record").empty());

    ASSERT_TRUE(document.setHardButton(kOwner, "Record", kTv, "Sleep"));
    EXPECT_EQ(actionOf(document, "Record"), std::string(kTv) + "_Sleep_Hold");

    ASSERT_TRUE(document.setHardButton(kOwner, "Record", {}, {}));
    EXPECT_TRUE(actionOf(document, "Record").empty());
}

TEST(ActivityButtons, RejectsDeviceOutsideActivityAndUnknownCommand) {
    ConfigDocument document = openSample();
    const std::string before = document.toXml();

    EXPECT_EQ(document.setHardButton(kOwner, "Menu", kDune, "Menu").error().code, ErrorCode::Format);
    EXPECT_EQ(document.setHardButton(kOwner, "Menu", kTv, "НетТакой").error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.setHardButton(harmony::ButtonOwner::activity("нет"), "Menu", kTv, "Menu").error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.toXml(), before);
}

TEST(ActivityButtons, MovesAndCopiesHardButton) {
    ConfigDocument document = openSample();
    const std::string menu = actionOf(document, "Menu");

    ASSERT_TRUE(document.moveHardButton(kOwner, "Menu", "Record", true));
    EXPECT_EQ(actionOf(document, "Record"), menu);
    EXPECT_EQ(actionOf(document, "Menu"), menu);

    ASSERT_TRUE(document.moveHardButton(kOwner, "Menu", "Stop", false));
    EXPECT_EQ(actionOf(document, "Stop"), menu);
    EXPECT_TRUE(actionOf(document, "Menu").empty());

    EXPECT_EQ(document.moveHardButton(kOwner, "Menu", "Stop", false).error().code, ErrorCode::NotFound);
}

TEST(ActivityButtons, MoveOntoAssignedKeySwaps) {
    ConfigDocument document = openSample();
    const std::string menu = actionOf(document, "Menu");
    ASSERT_TRUE(document.setHardButton(kOwner, "Record", kTv, "Sleep"));
    const std::string record = actionOf(document, "Record");

    // Как у экранных: назначение занятой кнопки не теряется, а переходит на освободившуюся.
    ASSERT_TRUE(document.moveHardButton(kOwner, "Menu", "Record", false));
    EXPECT_EQ(actionOf(document, "Record"), menu);
    EXPECT_EQ(actionOf(document, "Menu"), record);

    // Копия по-прежнему заменяет назначение.
    ASSERT_TRUE(document.moveHardButton(kOwner, "Record", "Menu", true));
    EXPECT_EQ(actionOf(document, "Menu"), menu);
}

TEST(ActivityButtons, AddsSoftButtonOnFreePlace) {
    ConfigDocument document = openSample();
    const int position = document.freeSoftPosition(kOwner);
    EXPECT_EQ(position, 6); // занято 0…5

    ASSERT_TRUE(document.setSoftButton(kOwner, position, "Сон", kTv, "Sleep"));

    const auto* button = softAt(document, position);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->label, "Сон");
    EXPECT_EQ(button->actionId, std::string(kTv) + "_Sleep_Hold");
    EXPECT_EQ(document.freeSoftPosition(kOwner), 7);
}

TEST(ActivityButtons, LabelReplacesIconButEmptyLabelKeepsIt) {
    ConfigDocument document = openSample();
    const auto* before = softAt(document, 0);
    ASSERT_NE(before, nullptr);
    ASSERT_FALSE(before->icon.empty());
    const std::string icon = before->icon;

    ASSERT_TRUE(document.setSoftButton(kOwner, 0, {}, kTv, "Menu"));
    EXPECT_EQ(softAt(document, 0)->icon, icon); // без подписи значок остаётся
    EXPECT_EQ(softAt(document, 0)->actionId, std::string(kTv) + "_Menu_Hold");

    ASSERT_TRUE(document.setSoftButton(kOwner, 0, "Меню", kTv, "Menu"));
    EXPECT_TRUE(softAt(document, 0)->icon.empty());
    EXPECT_EQ(softAt(document, 0)->label, "Меню");
}

TEST(ActivityButtons, MovesSwapsAndRemovesSoftButtons) {
    ConfigDocument document = openSample();
    const std::string first = actionOf(document, {}, 0);
    const std::string second = actionOf(document, {}, 1);

    ASSERT_TRUE(document.moveSoftButton(kOwner, 0, 1)); // занято — обмен
    EXPECT_EQ(actionOf(document, {}, 1), first);
    EXPECT_EQ(actionOf(document, {}, 0), second);

    ASSERT_TRUE(document.moveSoftButton(kOwner, 1, 13)); // на свободное место другой страницы
    EXPECT_EQ(actionOf(document, {}, 13), first);
    EXPECT_EQ(softAt(document, 1), nullptr);

    ASSERT_TRUE(document.removeSoftButton(kOwner, 13));
    EXPECT_EQ(softAt(document, 13), nullptr);
    EXPECT_EQ(document.removeSoftButton(kOwner, 13).error().code, ErrorCode::NotFound);
}

TEST(ActivityButtons, NewActivityGetsSoftGroupBeforeHardButtons) {
    ConfigDocument document = openSample();
    harmony::ActivityDraft draft;
    draft.label = "Новая";
    draft.devices = {kTv};
    auto id = document.addActivity(draft);
    ASSERT_TRUE(id);

    ASSERT_TRUE(document.setSoftButton(harmony::ButtonOwner::activity(*id), 0, "Вход", kTv, "InputNext"));

    const std::string xml = document.toXml();
    const auto activity = xml.find("<Label>Новая</Label>");
    const auto soft = xml.find("<ControlGroup name=\"Misc\">", activity);
    const auto hard = xml.find("<ControlGroup name=\"HardButtons\">", activity);
    ASSERT_NE(soft, std::string::npos);
    EXPECT_LT(soft, hard); // как у пульта: экранные кнопки, затем аппаратные
}

// --- Кнопки устройства --------------------------------------------------------

TEST(DeviceButtons, EditsOwnButtonsOnly) {
    ConfigDocument document = openSample();
    const auto owner = harmony::ButtonOwner::device(kTv);

    ASSERT_TRUE(document.setHardButton(owner, "Record", kTv, "Sleep"));
    EXPECT_EQ(document.setHardButton(owner, "Stop", kReceiver, "Setup").error().code, ErrorCode::Format);

    const auto* tv = document.model().findDevice(kTv);
    bool found = false;
    for (const auto& group : tv->controlGroups) {
        for (const auto& button : group.buttons) {
            found = found || (button.name == "Record" && button.actionId == std::string(kTv) + "_Sleep_Hold");
        }
    }
    EXPECT_TRUE(found);

    const int position = document.freeSoftPosition(owner);
    EXPECT_EQ(position, 15); // у Hisense заняты 0…14
    ASSERT_TRUE(document.setSoftButton(owner, position, "Сон", kTv, "Sleep"));
    ASSERT_TRUE(document.moveSoftButton(owner, position, 0)); // обмен с «Питанием»
    ASSERT_TRUE(document.removeSoftButton(owner, 0));
}
