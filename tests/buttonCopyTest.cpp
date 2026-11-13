/**
 * @file buttonCopyTest.cpp
 * @brief Тесты копирования кнопок активности в другие активности.
 */

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

using namespace harmony;

namespace {

const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";

ConfigDocument openSample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

/// Кнопка активности: аппаратная по имени или экранная по месту.
const model::Button* hard(const model::Activity& activity, const std::string& key) {
    for (const auto& group : activity.controlGroups) {
        for (const auto& button : group.buttons) {
            if (group.name == "HardButtons" && button.name == key) {
                return &button;
            }
        }
    }
    return nullptr;
}

const model::Button* soft(const model::Activity& activity, int position) {
    for (const auto& group : activity.controlGroups) {
        for (const auto& button : group.buttons) {
            if (group.name == "Misc" && button.position == position) {
                return &button;
            }
        }
    }
    return nullptr;
}

/// Вся раскладка @p from → @p to.
ButtonCopy layout(const std::string& from, const std::string& to) {
    ButtonCopy copy;
    copy.fromActivity = from;
    copy.toActivities = {to};
    return copy;
}

std::string deviceOf(const model::Button& button) {
    return button.actionId.substr(0, button.actionId.find('_'));
}

/// Две обычные активности: у первой есть и аппаратные, и экранные кнопки.
std::pair<std::string, std::string> twoActivities(const ConfigDocument& document) {
    std::string source;
    std::string target;
    for (const auto& activity : document.model().activities) {
        if (activity.isPowerOff()) {
            continue;
        }
        const bool rich = std::ranges::any_of(activity.controlGroups, [](const auto& g) { return g.name == "Misc"; }) &&
                          std::ranges::any_of(activity.controlGroups, [](const auto& g) { return g.name == "HardButtons"; });
        if (source.empty() && rich) {
            source = activity.id;
        } else if (target.empty()) {
            target = activity.id;
        }
    }
    return {source, target};
}

} // namespace

TEST(ButtonCopy, WholeLayoutReplacesWhereTheDeviceWorks) {
    ConfigDocument document = openSample();
    const auto [source, target] = twoActivities(document);
    ASSERT_FALSE(source.empty());
    ASSERT_FALSE(target.empty());
    const std::size_t errorsBefore = countProblems(document.check(), Problem::Severity::Error);
    const model::Activity before = *document.model().findActivity(source);

    auto report = document.copyButtons({source, {target}, true, true, {}, {}, true});
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_EQ(report->activities, 1);
    EXPECT_GT(report->hard + report->soft, 0);

    const model::Activity& after = *document.model().findActivity(target);
    int skipped = 0;
    for (const auto& group : before.controlGroups) {
        for (const auto& button : group.buttons) {
            const bool works = std::ranges::find(after.powerOn, deviceOf(button)) != after.powerOn.end();
            const model::Button* copied =
                group.name == "HardButtons" ? hard(after, button.name) : soft(after, button.position);
            if (works) {
                ASSERT_NE(copied, nullptr) << button.actionId;
                EXPECT_EQ(copied->actionId, button.actionId);
                EXPECT_EQ(copied->label, button.label);
            } else {
                ++skipped;
            }
        }
    }
    EXPECT_EQ(report->skipped, skipped);
    // Кнопок на невключённые устройства не появилось — ошибок проверки не прибавилось.
    EXPECT_LE(countProblems(document.check(), Problem::Severity::Error), errorsBefore);
}

TEST(ButtonCopy, OneButtonKeepsOrFindsAFreePlace) {
    ConfigDocument document = openSample();
    const auto [source, target] = twoActivities(document);
    // Копии, а не ссылки: copyButtons() перестраивает модель документа (detail::mutate
    // заменяет impl.model), и любые ссылки на активности после вызова повисают.
    const model::Activity* fromPtr = document.model().findActivity(source);
    const model::Activity* toPtr = document.model().findActivity(target);
    ASSERT_NE(fromPtr, nullptr);
    ASSERT_NE(toPtr, nullptr);
    const model::Activity from = *fromPtr;
    // Экранная кнопка, чьё устройство включается в target.
    const model::Activity to = *toPtr;
    const model::Button* chosen = nullptr;
    for (const auto& group : from.controlGroups) {
        for (const auto& button : group.buttons) {
            if (group.name == "Misc" && chosen == nullptr &&
                std::ranges::find(to.powerOn, deviceOf(button)) != to.powerOn.end()) {
                chosen = &button;
            }
        }
    }
    ASSERT_NE(chosen, nullptr);
    const model::Button button = *chosen;
    // Место уже занято другой кнопкой (или нет) — с replace=false кнопка встаёт на свободное.
    const bool taken = soft(to, button.position) != nullptr;
    const int freePlace = document.freeSoftPosition(ButtonOwner::activity(target));

    auto report = document.copyButtons({source, {target}, false, true, {}, {button.position}, false});
    ASSERT_TRUE(report) << report.error().message;
    EXPECT_EQ(report->soft, 1);
    EXPECT_EQ(report->hard, 0);
    const model::Activity& after = *document.model().findActivity(target);
    const model::Button* placed = soft(after, taken ? freePlace : button.position);
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(placed->actionId, button.actionId);

    // Аппаратная кнопка, которая уже есть в target: без замены — остаётся прежней.
    for (const auto& group : after.controlGroups) {
        for (const auto& key : group.buttons) {
            if (group.name == "HardButtons" && hard(from, key.name) != nullptr &&
                hard(from, key.name)->actionId != key.actionId) {
                const std::string kept = key.actionId;
                const std::string name = key.name;
                auto again = document.copyButtons({source, {target}, true, false, {name}, {}, false});
                ASSERT_TRUE(again);
                EXPECT_EQ(again->kept + again->skipped, 1);
                EXPECT_EQ(hard(*document.model().findActivity(target), name)->actionId, kept);
                return;
            }
        }
    }
}

TEST(ButtonCopy, RejectsBadActivities) {
    ConfigDocument document = openSample();
    const auto [source, target] = twoActivities(document);
    EXPECT_EQ(document.copyButtons(layout("nope", target)).error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.copyButtons(layout(source, "nope")).error().code, ErrorCode::NotFound);
    std::string powerOff;
    for (const auto& activity : document.model().activities) {
        if (activity.isPowerOff()) {
            powerOff = activity.id;
        }
    }
    EXPECT_EQ(document.copyButtons(layout(source, powerOff)).error().code, ErrorCode::Unsupported);
    // В саму себя — ничего не меняется.
    auto self = document.copyButtons(layout(source, source));
    ASSERT_TRUE(self);
    EXPECT_EQ(self->activities, 0);
}
