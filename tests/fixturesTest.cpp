/**
 * @file fixturesTest.cpp
 * @brief Фикстуры T2 в git: проверка, что файлы читаются и содержат задуманные решения.
 *
 * Тесты блоков M2/U2, A1/C1, C2/V6 читают эти же файлы; здесь фиксируется только то, что
 * фикстура пригодна: документ открывается, группы и места на месте. Ожидания следуют из
 * конструкции фикстуры (правило 19), поэтому тест зелёный на любой машине.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/model/remoteConfig.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

const std::filesystem::path kFixtures = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "fixtures";

const harmony::model::ControlGroup* groupOf(const harmony::model::RemoteConfig& model,
                                            const std::string& name) {
    if (model.devices.empty()) {
        return nullptr;
    }
    for (const auto& group : model.devices.front().controlGroups) {
        if (group.name == name) {
            return &group;
        }
    }
    return nullptr;
}

} // namespace

TEST(Fixtures, XmlUnknownTargetNamesModelWithoutProfile) {
    auto document = harmony::ConfigDocument::open(kFixtures / "xml-unknown-target.xml");
    ASSERT_TRUE(document) << document.error().message;
    // Модель 885 есть в каталоге, папки профиля у неё нет — это и проверяют A1–A5, C1.
    EXPECT_EQ(document->targetRemote(), "885");
    const auto& model = document->model();
    const auto* hard = groupOf(model, "HardButtons");
    ASSERT_NE(hard, nullptr);
    ASSERT_EQ(hard->buttons.size(), 3U);
    EXPECT_EQ(hard->buttons[0].name, "Play");
    EXPECT_EQ(hard->buttons[1].name, "SoftRight4");
    EXPECT_EQ(hard->buttons[2].name, "Number5");
    const auto* screen = groupOf(model, "Misc");
    ASSERT_NE(screen, nullptr);
    EXPECT_EQ(screen->buttons.size(), 2U);
    EXPECT_EQ(model.activities.size(), 1U);
}

TEST(Fixtures, XmlNoTargetHasNoModelProperty) {
    auto document = harmony::ConfigDocument::open(kFixtures / "xml-no-target.xml");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_EQ(document->model().property("HarmonyTargetRemote"), "");
    // Свойства нет, identity в XML нет — сегодня имя берётся по умолчанию (M2 меняет на каталог).
    EXPECT_EQ(document->targetRemote(), "900");
}

TEST(Fixtures, XmlEmptyPageSkipsExactlyOnePage) {
    auto document = harmony::ConfigDocument::open(kFixtures / "xml-empty-page.xml");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_EQ(document->targetRemote(), "900");
    const auto* screen = groupOf(document->model(), "Misc");
    ASSERT_NE(screen, nullptr);
    ASSERT_EQ(screen->buttons.size(), 12U);
    std::vector<int> positions;
    for (const auto& button : screen->buttons) {
        positions.push_back(button.position);
    }
    EXPECT_EQ(positions, (std::vector<int>{0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17}));
}

TEST(Fixtures, ProfileWithoutCommandSlots) {
    std::ifstream stream(kFixtures / "profile-no-slots.json", std::ios::binary);
    ASSERT_TRUE(stream);
    const auto remote = nlohmann::json::parse(stream, nullptr, false);
    ASSERT_FALSE(remote.is_discarded());
    ASSERT_TRUE(remote.contains("screens"));
    ASSERT_EQ(remote["screens"].size(), 1U);
    std::size_t slots = 0;
    for (const auto& cell : remote["screens"][0]["cells"]) {
        if (cell.value("kind", "") == "ScreenSlot") {
            ++slots;
        }
    }
    // Экран режима есть, слотов команд нет: V6 не рисует и пишет предупреждение (OD12).
    EXPECT_EQ(slots, 0U);
}
