/**
 * @file buttonIconsTest.cpp
 * @brief Значки экранных кнопок: список прошивки, установка, снятие, проверка.
 */

#include "configFixtures.h"

#include "harmony/core/buttonIcons.h"
#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <span>
#include <vector>

using namespace harmony;

namespace {

constexpr const char* kTv = harmony::test::kTvId; ///< У него экранные кнопки с подписями

ConfigDocument sample() {
    return harmony::test::configFixture();
}

const model::Button* softAt(const ConfigDocument& document, int position) {
    for (const auto& group : document.model().findDevice(kTv)->controlGroups) {
        if (group.name == "HardButtons") continue;
        for (const auto& button : group.buttons) {
            if (button.position == position) return &button;
        }
    }
    return nullptr;
}

} // namespace

TEST(ButtonIcons, IconsOfTheSampleAreKnown) {
    for (const char* name : {"aspect", "tivo", "PS-Circle", "PS_Triangle", "teletext_off", "powerON"}) {
        EXPECT_TRUE(isKnownButtonIcon(modelInfo("900"), name)) << name;
    }
    EXPECT_FALSE(isKnownButtonIcon(modelInfo("900"), "Aspect")); // регистр важен
    EXPECT_FALSE(isKnownButtonIcon(modelInfo("900"), "netflix"));
}

// Список имён — данные семейства, а не один общий список (правило 6).
TEST(ButtonIcons, IconListIsDataOfTheModelFamily) {
    const auto asList = [](std::span<const std::string_view> names) {
        return std::vector<std::string_view>(names.begin(), names.end());
    };
    const auto general = asList(buttonIcons(modelInfo("")));
    EXPECT_FALSE(general.empty());
    // arch 15 (900) — список прошивки 900; модель не выбрана — прежний общий список,
    // который не проверен для модели.
    EXPECT_EQ(asList(buttonIcons(modelInfo("900"))), general);
    // arch 14 (650) — имён значков нет ни в одном источнике: кнопки текстовые, список пуст.
    EXPECT_TRUE(buttonIcons(modelInfo("650")).empty());
    EXPECT_TRUE(isKnownButtonIcon(modelInfo("900"), "aspect"));
    EXPECT_FALSE(isKnownButtonIcon(modelInfo("650"), "aspect"));
}

// Невозможное для семейства нельзя утверждать: новый значок refused, потому что мы не знаем,
// что у пульта есть такой значок. Уже стоящий в файле имя не трогаем.
TEST(ButtonIcons, NewIconIsRefusedForAFamilyWithoutIconNames) {
    auto document = sample();
    ASSERT_TRUE(document.setTargetRemote("650"));
    const auto owner = ButtonOwner::device(kTv);
    ASSERT_NE(softAt(document, 1), nullptr);
    EXPECT_EQ(document.setSoftButtonIcon(owner, 1, "star").error().code, ErrorCode::Format);
}

TEST(ButtonIcons, SetChangeAndRemove) {
    auto document = sample();
    const auto owner = ButtonOwner::device(kTv);
    const int position = 1; // «Ракурс»
    ASSERT_NE(softAt(document, position), nullptr);

    ASSERT_TRUE(document.setSoftButtonIcon(owner, position, "star"));
    EXPECT_EQ(softAt(document, position)->icon, "star");
    EXPECT_TRUE(softAt(document, position)->label.empty());
    EXPECT_NE(document.toXml().find("<Label></Label><Icon>star</Icon>"), std::string::npos);

    ASSERT_TRUE(document.setSoftButtonIcon(owner, position, ""));
    EXPECT_TRUE(softAt(document, position)->icon.empty());
    EXPECT_FALSE(softAt(document, position)->label.empty()); // подписана командой

    EXPECT_EQ(document.setSoftButtonIcon(owner, position, "netflix").error().code, ErrorCode::Format);
    EXPECT_EQ(document.setSoftButtonIcon(owner, 99, "star").error().code, ErrorCode::NotFound);
}

TEST(ButtonIcons, CheckWarnsAboutUnknownIcon) {
    auto document = sample();
    const std::string xml = document.toXml();
    std::string broken = xml;
    const auto at = broken.find("<Icon>aspect</Icon>");
    ASSERT_NE(at, std::string::npos);
    broken.replace(at, std::string("<Icon>aspect</Icon>").size(), "<Icon>netflix</Icon>");
    auto other = ConfigDocument::fromXml(broken, "broken");
    ASSERT_TRUE(other);
    bool found = false;
    for (const auto& problem : other->check()) {
        found = found || problem.what.format == "the remote has no icon “%1”: the button is drawn blank";
    }
    EXPECT_TRUE(found);
}

// Проверка утверждает «у пульта нет такого значка» только там, где семейство знает имена.
// Семейство без имён (arch 14) ничего не утверждает: имя из списка 900 для него не знание.
TEST(ButtonIcons, UnknownIconWarningDependsOnTheModelFamily) {
    std::string broken = sample().toXml();
    const auto at = broken.find("<Icon>aspect</Icon>");
    ASSERT_NE(at, std::string::npos);
    broken.replace(at, std::string("<Icon>aspect</Icon>").size(), "<Icon>netflix</Icon>");
    auto opened = ConfigDocument::fromXml(broken, "broken");
    ASSERT_TRUE(opened);
    auto document = std::move(*opened);

    const auto warns = [&document]() {
        for (const auto& problem : document.check()) {
            if (problem.what.format == "the remote has no icon “%1”: the button is drawn blank") return true;
        }
        return false;
    };

    ASSERT_TRUE(document.setTargetRemote("900"));
    EXPECT_TRUE(warns()) << "семейство знает имена значков прошивки";
    ASSERT_TRUE(document.setTargetRemote("650"));
    EXPECT_FALSE(warns()) << "семейство имён не знает — проверке нечего утверждать";
}
