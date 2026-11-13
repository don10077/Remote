/**
 * @file notSentTest.cpp
 * @brief Тесты пометки «не отправлять на пульт».
 */

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

using namespace harmony;

namespace {

const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";
constexpr const char* kTv = "92555930";    ///< Hisense — в семи активностях
constexpr const char* kUnused = "68367800"; ///< Panasonic — ни в одной

ConfigDocument openSample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

/// Первая обычная активность (не «выключить всё»).
std::string firstActivity(const ConfigDocument& document) {
    for (const auto& activity : document.model().activities) {
        if (!activity.isPowerOff()) {
            return activity.id;
        }
    }
    return {};
}

/// Что получит пульт.
ConfigDocument remoteCopy(const ConfigDocument& document) {
    auto bytes = document.toEzHex(true);
    EXPECT_TRUE(bytes) << (bytes ? "" : bytes.error().message);
    auto copy = ConfigDocument::fromEzHex(*bytes, "remote");
    EXPECT_TRUE(copy);
    return std::move(*copy);
}

} // namespace

TEST(NotSent, MarkSurvivesAndUnmarkRestoresTheFile) {
    ConfigDocument document = openSample();
    const std::string original = document.toXml();
    const std::string activity = firstActivity(document);
    ASSERT_TRUE(document.setDeviceNotSent(kUnused, true));
    ASSERT_TRUE(document.setActivityNotSent(activity, true));
    EXPECT_TRUE(document.model().findDevice(kUnused)->notSent);
    EXPECT_TRUE(document.model().findActivity(activity)->notSent);
    EXPECT_TRUE(document.hasNotSent());

    // Пометка — комментарий; переживает сохранение и чтение.
    const std::string marked = document.toXml();
    EXPECT_NE(marked.find("<!--harmony:not-sent-->"), std::string::npos);
    auto reopened = ConfigDocument::fromXml(marked, "x");
    ASSERT_TRUE(reopened);
    EXPECT_TRUE(reopened->model().findDevice(kUnused)->notSent);
    EXPECT_TRUE(reopened->model().findActivity(activity)->notSent);

    // Сняли — файл байт в байт прежний.
    ASSERT_TRUE(document.setDeviceNotSent(kUnused, false));
    ASSERT_TRUE(document.setActivityNotSent(activity, false));
    EXPECT_FALSE(document.hasNotSent());
    EXPECT_EQ(document.toXml(), original);
    // Повторная пометка не дублируется.
    ASSERT_TRUE(document.setDeviceNotSent(kUnused, true));
    ASSERT_TRUE(document.setDeviceNotSent(kUnused, true));
    const std::string twice = document.toXml();
    EXPECT_EQ(twice.find("harmony:not-sent"), twice.rfind("harmony:not-sent"));
}

TEST(NotSent, NamesTheDeviceThatHoldsTheActivity) {
    ConfigDocument document = openSample();
    const model::Activity* watchTv = document.model().findActivity("79016674"); // «ТВ + AV»
    ASSERT_NE(watchTv, nullptr);
    EXPECT_TRUE(document.model().notSentDevicesOf(*watchTv).empty());

    ASSERT_TRUE(document.setDeviceNotSent(kTv, true));
    ASSERT_TRUE(document.setDeviceNotSent(kUnused, true)); // активности не нужен — в причины не попадает
    watchTv = document.model().findActivity("79016674");   // правка перечитала модель
    const auto blocking = document.model().notSentDevicesOf(*watchTv);
    ASSERT_EQ(blocking.size(), 1U);
    EXPECT_EQ(blocking.front()->id, kTv);
}

TEST(NotSent, DeviceTakesItsActivitiesAlong) {
    ConfigDocument document = openSample();
    const auto users = document.activitiesUsingDevice(kTv);
    ASSERT_FALSE(users.empty());
    ASSERT_TRUE(document.setDeviceNotSent(kTv, true));
    const auto held = document.activitiesNotSent();
    EXPECT_EQ(held.size(), users.size());

    const ConfigDocument remote = remoteCopy(document);
    EXPECT_EQ(remote.model().findDevice(kTv), nullptr);
    for (const auto& id : held) {
        EXPECT_EQ(remote.model().findActivity(id), nullptr) << id;
    }
    // Ничего не ссылается на убранное; у пульта лишних ошибок нет.
    EXPECT_EQ(remote.toXml().find(std::string(kTv)), std::string::npos);
    EXPECT_LE(countProblems(remote.check(), Problem::Severity::Error),
              countProblems(document.check(), Problem::Severity::Error));
    EXPECT_EQ(remote.toXml().find("harmony:not-sent"), std::string::npos);

    // В файл (не на пульт) уходит всё, с пометкой.
    auto saved = document.toEzHex(false);
    ASSERT_TRUE(saved);
    auto whole = ConfigDocument::fromEzHex(*saved, "file");
    ASSERT_TRUE(whole);
    ASSERT_NE(whole->model().findDevice(kTv), nullptr);
    EXPECT_TRUE(whole->model().findDevice(kTv)->notSent);
}

TEST(NotSent, ActivityAloneAndPowerOffRefused) {
    ConfigDocument document = openSample();
    const std::string activity = firstActivity(document);
    ASSERT_TRUE(document.setActivityNotSent(activity, true));
    EXPECT_EQ(document.activitiesNotSent(), std::vector<std::string>{activity});
    const ConfigDocument remote = remoteCopy(document);
    EXPECT_EQ(remote.model().findActivity(activity), nullptr);
    EXPECT_EQ(remote.model().devices.size(), document.model().devices.size());

    std::string powerOff;
    for (const auto& item : document.model().activities) {
        if (item.isPowerOff()) {
            powerOff = item.id;
        }
    }
    EXPECT_EQ(document.setActivityNotSent(powerOff, true).error().code, ErrorCode::Unsupported);
    EXPECT_EQ(document.setDeviceNotSent("nope", true).error().code, ErrorCode::NotFound);
}
