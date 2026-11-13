/**
 * @file buttonIconsTest.cpp
 * @brief Значки экранных кнопок: список прошивки, установка, снятие, проверка.
 */

#include "harmony/core/buttonIcons.h"
#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace harmony;

namespace {

const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";
constexpr const char* kTv = "92555930"; ///< У него экранные кнопки с подписями

ConfigDocument sample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << document.error().message;
    return std::move(*document);
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
        EXPECT_TRUE(isKnownButtonIcon(name)) << name;
    }
    EXPECT_FALSE(isKnownButtonIcon("Aspect")); // регистр важен
    EXPECT_FALSE(isKnownButtonIcon("netflix"));
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
