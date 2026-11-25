/**
 * @file keyVocabularyTest.cpp
 * @brief K1: словарь имён клавиш — один на все семейства, без скан-кодов и без профиля.
 *
 * Тесты проверяют решения программы: какие имена словарь принимает, как он разрешает алиасы,
 * какие имена он считает временными и какой источник у каждого имени. Содержимое таблиц
 * соседних проектов тестом не сверяется (правило 19) — эта сверка сделана один раз и
 * записана в отчёте `reports/K1.md`.
 */

#include "harmony/core/keyVocabulary.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace harmony;

namespace {

/// Имена словаря как множество — по нему проверяются уникальность и покрытие.
std::set<std::string> dictionaryNames() {
    std::set<std::string> names;
    for (const auto& entry : keyVocabulary())
        names.insert(std::string(entry.name));
    return names;
}

bool contains(const std::vector<std::string_view>& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

TEST(KeyVocabulary, EveryNameIsUniqueAndAllowsAKind) {
    ASSERT_FALSE(keyVocabulary().empty());
    std::set<std::string> seen;
    for (const auto& entry : keyVocabulary()) {
        EXPECT_TRUE(seen.insert(std::string(entry.name)).second) << entry.name;
        EXPECT_NE(entry.kinds, 0u) << entry.name;
    }
}

TEST(KeyVocabulary, EveryAliasPointsToAnExistingCanonicalNameAndIsNotItself) {
    ASSERT_FALSE(keyAliases().empty());
    const auto names = dictionaryNames();
    for (const auto& alias : keyAliases()) {
        EXPECT_NE(alias.alias, alias.canonical) << alias.alias;
        EXPECT_TRUE(names.count(std::string(alias.canonical))) << alias.alias << " -> " << alias.canonical;
        EXPECT_EQ(canonicalKeyName(alias.alias), alias.canonical) << alias.alias;
        // Алиас — не имя словаря.
        EXPECT_EQ(keyInfo(alias.alias), nullptr) << alias.alias;
    }
}

TEST(KeyVocabulary, CanonicalNameResolvesAliasesAndKeepsItsOwnName) {
    EXPECT_EQ(canonicalKeyName("ScreenRight4"), "SoftRight4");
    EXPECT_EQ(canonicalKeyName("AllOff"), "Off");
    EXPECT_EQ(canonicalKeyName("Play"), "Play");
    EXPECT_EQ(canonicalKeyName("Lef3"), std::nullopt);
    EXPECT_EQ(canonicalKeyName("GLOW"), std::nullopt);  // регистр важен
}

TEST(KeyVocabulary, ScreenSoftAllowsScreenKeysButNotTransportKeys) {
    const auto names = keyNamesForKind(PhysicalKind::ScreenSoft);
    for (const auto* expected : {"SoftLeft1", "SoftLeft2", "SoftLeft3", "SoftLeft4",
                                 "SoftRight1", "SoftRight2", "SoftRight3", "SoftRight4",
                                 "Options", "Devices"})
        EXPECT_TRUE(contains(names, expected)) << expected;
    EXPECT_FALSE(contains(names, "Play"));
}

TEST(KeyVocabulary, PagePrevAllowsExactlyPageLeft) {
    const auto names = keyNamesForKind(PhysicalKind::PagePrev);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "PageLeft");
}

TEST(KeyVocabulary, ScreenCommandsAreNotDictionaryNames) {
    // `Settings` и `Slow` — команды экрана, а не имена физических клавиш.
    EXPECT_EQ(keyInfo("Settings"), nullptr);
    EXPECT_EQ(keyInfo("Slow"), nullptr);
}

TEST(KeyVocabulary, TemporaryZoneNamesAreRecognised) {
    for (const auto* name : {"Auto", "Auto12", "Zone", "Zone3", "Scan44", "Play_copy"})
        EXPECT_TRUE(isTemporaryZoneName(name)) << name;
    for (const auto* name : {"Play", "Autoplay"})
        EXPECT_FALSE(isTemporaryZoneName(name)) << name;
}

TEST(KeyVocabulary, OrderOfGroupsIsSetByTheDictionary) {
    // Порядок записей задаёт порядок групп в интерфейсе; список зон модели его не меняет.
    const std::vector<KeyCategory> order = {
        KeyCategory::Service,   KeyCategory::Activity,  KeyCategory::Device,
        KeyCategory::Favorite,  KeyCategory::Navigation, KeyCategory::Dpad,
        KeyCategory::Volume,    KeyCategory::Channel,   KeyCategory::Transport,
        KeyCategory::Digit,     KeyCategory::Color,     KeyCategory::Screen,
        KeyCategory::Page,
    };
    KeyCategory previous = order.front();
    for (const auto& entry : keyVocabulary()) {
        // Категория идёт по порядку списка, внутри группы порядок — порядок словаря.
        EXPECT_TRUE(entry.category == previous ||
                   std::find(order.begin(), order.end(), entry.category) >
                       std::find(order.begin(), order.end(), previous))
            << entry.name;
        previous = entry.category;
    }
}

TEST(KeyVocabulary, SourceOfANameIsRecordedByTheSourceItCameFrom) {
    // Правило 8: у имени есть источник, и выбирается сильнейший подтверждённый.
    const std::vector<std::pair<std::string_view, NameSource>> expected = {
        {"Play", NameSource::Measured},      // имя получено измерением
        {"Number5", NameSource::Config},     // имя есть в фактической конфигурации
        {"TV", NameSource::LogitechMap},     // словарь Logitech по модели
        {"Teletext", NameSource::Printed},   // только надпись на корпусе
        {"Options", NameSource::Unknown},    // документа с источником нет (правило 9)
    };
    for (const auto& [name, source] : expected) {
        const auto* info = keyInfo(name);
        ASSERT_NE(info, nullptr) << name;
        EXPECT_EQ(info->source, source) << name;
    }
}

TEST(KeyVocabulary, NameKnownOnlyThroughItsPrintedAliasIsPrinted) {
    // `SoftLeft1` в данных подтверждается только печатным алиасом `ScreenLeft1`.
    const std::vector<std::pair<std::string_view, NameSource>> expected = {
        {"SoftLeft1", NameSource::Printed},
        {"PageLeft", NameSource::Printed},
    };
    for (const auto& [name, source] : expected) {
        const auto* info = keyInfo(name);
        ASSERT_NE(info, nullptr) << name;
        EXPECT_EQ(info->source, source) << name;
    }
}

TEST(KeyVocabulary, DictionaryCoversEveryZoneNameOfBuiltInProfiles) {
    // Имена зон выбираются только из словаря; профиль не придумывает новых имён
    // (кроме временных). Фикстура — встроенные профили, они лежат в git.
    const auto profiles = std::filesystem::path(HARMONY_SOURCE_DIR) / "app" / "resources" / "remotes";
    ASSERT_TRUE(std::filesystem::exists(profiles));
    std::size_t checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(profiles)) {
        const auto file = entry.path() / "profile.json";
        if (!std::filesystem::exists(file))
            continue;
        std::fstream stream(file);
        const auto remote = nlohmann::json::parse(stream, nullptr, false);
        ASSERT_FALSE(remote.is_discarded()) << file;
        const auto zones = remote.value("hardware", nlohmann::json::object()).value("zones", nlohmann::json::array());
        ASSERT_FALSE(zones.empty()) << file;
        for (const auto& zone : zones) {
            const std::string id = zone.value("id", "");
            if (id.empty() || isTemporaryZoneName(id))
                continue;
            EXPECT_NE(canonicalKeyName(id), std::nullopt) << file.filename().string() << ": " << id;
            ++checked;
        }
    }
    // Тест не должен проходить, если ни одна зона не проверена.
    EXPECT_GT(checked, 0u);
}
