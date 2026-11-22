/**
 * @file keyNamesTest.cpp
 * @brief Имена физических клавиш: один общий список для всех архитектур, независимо от профиля.
 */

#include "dumpFiles.h"

#include "harmony/core/keyNames.h"
#include "harmony/core/configDocument.h"
#include "harmony/core/model/remoteConfig.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/core/remoteProfile.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <string>

namespace {

/// Имена аппаратных кнопок импорта (у экранных кнопки имени нет).
std::set<std::string> hardNames(const harmony::model::RemoteConfig& config) {
    std::set<std::string> names;
    for (const auto& device : config.devices)
        for (const auto& group : device.controlGroups)
            for (const auto& button : group.buttons)
                if (!button.name.empty())
                    names.insert(button.name);
    return names;
}

} // namespace

TEST(KeyNames, OneSharedListForEveryArchitecture) {
    EXPECT_EQ(harmony::keyName(0x2C).value_or(""), "Play");        // 44
    EXPECT_EQ(harmony::keyName(0x2E).value_or(""), "Pause");       // 46
    EXPECT_EQ(harmony::keyName(0x33).value_or(""), "Select");      // 51
    EXPECT_EQ(harmony::keyName(0x36).value_or(""), "NumberEnter"); // 54
    EXPECT_EQ(harmony::keyName(0x0E).value_or(""), "VolumeUp");
    EXPECT_EQ(harmony::keyName(0x0B).value_or(""), "PageLeft");
    EXPECT_TRUE(harmony::isKnownKeyName("Play"));
    EXPECT_FALSE(harmony::isKnownKeyName("Scan44"));

    // Таблица плотная 0x01…0x3F: код без подтверждения получает UnknownKey<код>, а не
    // выдуманное имя, и это имя уникально.
    EXPECT_EQ(harmony::keyName(0x02), "UnknownKey02");
    EXPECT_EQ(harmony::keyLabel(0x02), "UnknownKey02");
    EXPECT_EQ(harmony::keyName(0x09), "UnknownKey09");
    EXPECT_EQ(harmony::keyName(0x3F), "UnknownKey3F");
    EXPECT_TRUE(harmony::isUnknownKeyName("UnknownKey09"));
    EXPECT_TRUE(harmony::isKnownKeyName("UnknownKey09"));
    EXPECT_FALSE(harmony::keyNameConfirmed(0x02));
    EXPECT_TRUE(harmony::keyNameConfirmed(0x2C));
    EXPECT_FALSE(harmony::keyName(0x00));          // 0x00 — не клавиша
    EXPECT_FALSE(harmony::keyName(0x40));          // за границей таблицы
    EXPECT_EQ(harmony::keyLabel(0x00), "Scan0");   // имя вне таблицы придумывать нельзя
    ASSERT_EQ(harmony::keyNames().size(), 63u);    // 0x01 … 0x3F
    for (unsigned scan = 1; scan <= 0x3F; ++scan) {
        const auto name = harmony::keyName(scan);
        ASSERT_TRUE(name) << scan;
        EXPECT_TRUE(harmony::isKnownKeyName(*name)) << scan;
    }

    // Подтверждённых имён 50, остальное — заполнитель. Имена уникальны: UnknownKey<N>
    // различает коды, поэтому такое имя годится на `id` зоны.
    unsigned confirmed = 0;
    std::set<std::string_view> unique;
    for (const auto& entry : harmony::keyNames()) {
        confirmed += harmony::isUnknownKeyName(entry.name) ? 0u : 1u;
        unique.insert(entry.name);
    }
    EXPECT_EQ(confirmed, 50u);
    EXPECT_EQ(unique.size(), harmony::keyNames().size());
}

TEST(KeyNames, ImportNamesKeysFromTheSharedListForEveryArchitecture) {
    // Настоящие дампы (dumps/, не в git): без них тест пропускается (правило 19).
    struct Sample { const char* folder; const char* mask; const char* catalogId; };
    std::size_t checked = 0;
    for (const Sample sample : {Sample{"EZHex/harmony525", "*.EZHex", "525"},
                                Sample{"EZHex/arch8", "H880-*.EZHex", "880"}}) {
        const auto path = harmony::test::dumpFile(sample.folder, sample.mask);
        if (path.empty()) continue;
        ++checked;
        SCOPED_TRACE(path.filename().string());
        auto document = harmony::ConfigDocument::open(path);
        ASSERT_TRUE(document) << document.error().message;

        // Имена клавиш даёт общая таблица, а не профиль: в этих дампах имён клавиш нет.
        // Каталог знает модель по skin, но папки профиля у неё нет.
        const auto* model = harmony::catalogModel(sample.catalogId);
        ASSERT_TRUE(model) << sample.catalogId;
        EXPECT_TRUE(harmony::remoteProfile(sample.catalogId) == nullptr);
        EXPECT_EQ(document->targetRemote(), sample.catalogId);
        const auto names = hardNames(document->model());
        ASSERT_FALSE(names.empty());

        // Требовать, что в дампе назначена конкретная клавиша, нельзя: в конфиге может быть
        // одна кнопка, и часть клавиатуры в нём не используется. Проверяем только то, что
        // имена назначенных клавиш взяты из общего списка и не придуманы.
        for (const auto& name : names) {
            EXPECT_TRUE(harmony::isKnownKeyName(name)) << name;
            if (harmony::isUnknownKeyName(name)) {
                const unsigned code = std::stoul(name.substr(harmony::kUnknownKeyPrefix.size()), nullptr, 16);
                EXPECT_FALSE(harmony::keyNameConfirmed(code)) << name;
                continue;
            }
            if (name.starts_with("Scan")) {
                const unsigned code = static_cast<unsigned>(std::stoi(name.substr(4)));
                EXPECT_FALSE(harmony::keyName(code)) << name;
            }
        }
    }
    if (checked == 0) GTEST_SKIP() << "dumps/ нет — дампы бинарных семейств только на машине владельца";
}
