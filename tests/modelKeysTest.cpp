/**
 * @file modelKeysTest.cpp
 * @brief K4: список клавиш модели без профиля — порядок, источники и полнота.
 *
 * Тесты проверяют решения программы: что она называет клавишами модели, в каком порядке,
 * откуда взято имя и считает ли список исчерпывающим. Профиль как файл здесь не читается:
 * ядро видит реестр оформлений, а имена зон и ячеек экранов передаёт приложение, поэтому
 * они переданы как данные. Эквивалентность порядка с `RemoteView::hardKeyIds()` — отдельный
 * тест на встроенных профилях (`modelKeysEquivalenceTest.cpp`).
 */

#include "harmony/core/modelKeys.h"
#include "harmony/core/configDocument.h"
#include "harmony/core/keyScans.h"
#include "harmony/core/keyVocabulary.h"
#include "harmony/core/modelInfo.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace harmony;

namespace {

/// Конфигурация с физическими кнопками: `Button.name` непустой — это аппаратная клавиша.
model::RemoteConfig configWithButtons(const std::vector<std::string>& names) {
    model::RemoteConfig config;
    model::Device device;
    device.id = "100";
    model::ControlGroup group;
    group.name = "HardButtons";
    for (const auto& name : names) {
        model::Button button;
        button.name = name;
        group.buttons.push_back(button);
    }
    device.controlGroups.push_back(group);
    config.devices.push_back(device);
    return config;
}

const ModelKey* keyNamed(const ModelKeys& keys, const std::string& name) {
    for (const auto& key : keys.keys)
        if (key.name == name)
            return &key;
    return nullptr;
}

std::vector<std::string> namesOf(const ModelKeys& keys) {
    std::vector<std::string> names;
    for (const auto& key : keys.keys)
        names.push_back(key.name);
    return names;
}

/// Порядок словаря — то, чем программа сортирует списки. -1 — имени в словаре нет.
int dictionaryRank(std::string_view name) {
    const auto& vocabulary = keyVocabulary();
    for (std::size_t i = 0; i < vocabulary.size(); ++i)
        if (vocabulary[i].name == name)
            return static_cast<int>(i);
    return -1;
}

} // namespace

TEST(ModelKeys, FamilyTableAnswersWhichKeysTheModelHasWithoutAProfile) {
    const auto keys = modelKeys(modelInfo("650"), {}, {}, nullptr);
    ASSERT_FALSE(keys.keys.empty()) << "без профиля список пуст — тест ничего не проверяет";

    // Список = таблица arch 14, и он исчерпывающий: этой таблице доверяем целиком.
    const auto* table = keyScanTable(14, {});
    ASSERT_TRUE(table);
    EXPECT_EQ(keys.keys.size(), table->entries.size());
    EXPECT_TRUE(keys.complete);
    EXPECT_FALSE(keys.vocabularyFallback);

    // Имя клавиши у экрана — из измерения 650, и имени экранных кнопок здесь нет:
    // у 650 нет ячейки с именем `Options`.
    EXPECT_TRUE(keyNamed(keys, "SoftLeft1"));
    EXPECT_FALSE(keyNamed(keys, "Options"));

    for (const auto& key : keys.keys) {
        EXPECT_EQ(key.source, KeySource::ScanTable) << key.name;
        const auto* info = keyInfo(key.name);
        ASSERT_TRUE(info) << key.name;
        EXPECT_EQ(key.category, info->category) << key.name;
    }

    // Порядок — по словарю, а не по скан-коду: он задаёт сортировку списков программы.
    for (std::size_t i = 1; i < keys.keys.size(); ++i)
        EXPECT_LT(dictionaryRank(keys.keys[i - 1].name), dictionaryRank(keys.keys[i].name))
            << keys.keys[i - 1].name << " → " << keys.keys[i].name;
}

TEST(ModelKeys, ModelTableWinsForItsModelAndTheFamilyTableForTheRest) {
    const auto byModel = modelKeys(modelInfo("885"), {}, {}, nullptr);
    const auto byFamily = modelKeys(modelInfo("880"), {}, {}, nullptr);
    ASSERT_FALSE(byModel.keys.empty());
    ASSERT_FALSE(byFamily.keys.empty());

    // `Glow` подтверждён только у 885: по неполной таблице семейства нельзя решать,
    // что такой клавиши у 880 нет.
    EXPECT_TRUE(keyNamed(byModel, "Glow"));
    EXPECT_FALSE(keyNamed(byFamily, "Glow"));
    EXPECT_TRUE(byModel.complete);
    EXPECT_FALSE(byFamily.complete);

    // Всё, что есть в таблице семейства, есть и в списке модели.
    for (const auto& key : byFamily.keys)
        EXPECT_TRUE(keyNamed(byModel, key.name)) << key.name;
}

TEST(ModelKeys, ConfigurationNamesAreAddedAfterTheTable) {
    const auto config = configWithButtons({"Teletext"});
    const auto keys = modelKeys(modelInfo("885"), {}, {}, &config);
    ASSERT_FALSE(keys.keys.empty());

    // Имени `Teletext` в таблице arch 8 нет, но кнопка конфигурации остаётся клавишей —
    // и её источник называет конфигурацию.
    const auto* key = keyNamed(keys, "Teletext");
    ASSERT_TRUE(key);
    EXPECT_EQ(key->source, KeySource::Config);
    EXPECT_EQ(keys.keys.back().name, "Teletext");
    EXPECT_TRUE(keys.complete);
}

TEST(ModelKeys, WithoutATableOnlyTheConfigurationNamesKeys) {
    const auto config = configWithButtons({"Play"});
    const auto keys = modelKeys(modelInfo("525"), {}, {}, &config);

    // Таблицы arch 9 нет: список не пуст, но он не исчерпывающий, и UI предлагает словарь.
    ASSERT_EQ(keys.keys.size(), 1);
    EXPECT_EQ(keys.keys.front().name, "Play");
    EXPECT_EQ(keys.keys.front().source, KeySource::Config);
    EXPECT_FALSE(keys.complete);
    EXPECT_TRUE(keys.vocabularyFallback);
}

TEST(ModelKeys, NoModelNoProfileAndNoConfigurationFallsBackToTheDictionary) {
    const auto keys = modelKeys(modelInfo(""), {}, {}, nullptr);
    EXPECT_TRUE(keys.keys.empty());
    EXPECT_TRUE(keys.vocabularyFallback);
    EXPECT_FALSE(keys.complete);
}

TEST(ModelKeys, ProfileOrderWinsAndConfigurationKeysGoToTheEnd) {
    const auto config = configWithButtons({"Red"});
    const std::vector<std::string> profileKeys = {"VolumeUp", "Play"};
    const std::vector<std::string> screenKeys = {"Slow", "Menu"};
    const auto keys = modelKeys(modelInfo("1100"), profileKeys, screenKeys, &config);

    // Порядок — как у нынешнего hardKeyIds: зоны в порядке файла, затем ячейки экранов,
    // затем то, чего нет выше.
    ASSERT_EQ(namesOf(keys), std::vector<std::string>({"VolumeUp", "Play", "Slow", "Menu", "Red"}));
    EXPECT_EQ(keyNamed(keys, "VolumeUp")->source, KeySource::Profile);
    EXPECT_EQ(keyNamed(keys, "Play")->source, KeySource::Profile);
    EXPECT_EQ(keyNamed(keys, "Slow")->source, KeySource::ProfileScreen);
    EXPECT_EQ(keyNamed(keys, "Red")->source, KeySource::Config);

    // `Slow` — имя ячейки экрана, его нет в словаре, поэтому категории у него нет.
    EXPECT_FALSE(keyNamed(keys, "Slow")->category.has_value());
    EXPECT_TRUE(keys.complete);
    EXPECT_FALSE(keys.vocabularyFallback);
}

TEST(ModelKeys, AProfileDoesNotAddNamesFromTheTableOfItsFamily) {
    // Профиль отвечает за состав и порядок, когда он передан; таблица к нему не подмешивается.
    const std::vector<std::string> profileKeys = {"VolumeUp"};
    const auto keys = modelKeys(modelInfo("650"), profileKeys, {}, nullptr);
    ASSERT_EQ(namesOf(keys), std::vector<std::string>({"VolumeUp"}));
    EXPECT_EQ(keys.keys.front().source, KeySource::Profile);
    EXPECT_TRUE(keys.complete);
}

TEST(ModelKeys, ScreenCellNamesAreAssignableKeysEvenOutsideTheDictionary) {
    const std::vector<std::string> screenKeys = {"Play", "Slow"};
    const auto keys = modelKeys(modelInfo("1100"), {}, screenKeys, nullptr);
    ASSERT_EQ(keys.keys.size(), 2);
    EXPECT_EQ(keyNamed(keys, "Play")->source, KeySource::ProfileScreen);
    EXPECT_EQ(keyNamed(keys, "Play")->category, KeyCategory::Transport);
    EXPECT_FALSE(keyNamed(keys, "Slow")->category.has_value());
    EXPECT_TRUE(keys.complete);
}

TEST(ModelKeys, PrintedNamesAreDataForAFamilyWithNoCodes) {
    const auto keys = modelKeys(modelInfo("touch"), {}, {}, nullptr);
    ASSERT_FALSE(keys.keys.empty()) << "таблицы надписей по модели нет — тест ничего не проверяет";

    // Touch: 27 клавиш на клавиатуре, каждая названа по надписи. `Home` и `Favorites` —
    // зоны сенсорной панели, не клавиши, поэтому их в списке физических клавиш нет.
    EXPECT_EQ(keys.keys.size(), 27u);
    for (const auto& key : keys.keys) {
        EXPECT_EQ(key.source, KeySource::Printed) << key.name;
        EXPECT_TRUE(keyInfo(key.name)) << key.name;   // имя — канон словаря
    }
    EXPECT_TRUE(keyNamed(keys, "Play"));
    EXPECT_TRUE(keyNamed(keys, "DVR"));               // надпись `Dvr` записана каноном
    EXPECT_FALSE(keyNamed(keys, "Home"));
    EXPECT_FALSE(keyNamed(keys, "Favorites"));

    // Порядок списка — по словарю, как у скан-таблиц: надписи в источнике идут по алфавиту,
    // но сортировку списков программы задаёт словарь.
    for (std::size_t i = 1; i < keys.keys.size(); ++i)
        EXPECT_LT(dictionaryRank(keys.keys[i - 1].name), dictionaryRank(keys.keys[i].name))
            << keys.keys[i - 1].name << " → " << keys.keys[i].name;

    // Надпись — слабый источник: по одному ей список не исчерпывающий, но запасной путь
    // к всему словарю уже не нужен.
    EXPECT_FALSE(keys.complete);
    EXPECT_FALSE(keys.vocabularyFallback);
}

TEST(ModelKeys, EveryNameIsNamedOnceAndTheFirstSourceWins) {
    const auto config = configWithButtons({"Play", "Teletext"});
    const std::vector<std::string> profileKeys = {"Play"};
    const auto keys = modelKeys(modelInfo("885"), profileKeys, {}, &config);
    ASSERT_FALSE(keys.keys.empty());

    // `Play` есть и в таблице, и в конфигурации — имя встречается один раз, источник тот,
    // кто ответил раньше.
    int plays = 0;
    for (const auto& key : keys.keys)
        if (key.name == "Play")
            ++plays;
    EXPECT_EQ(plays, 1);
    EXPECT_EQ(keyNamed(keys, "Play")->source, KeySource::Profile);
    EXPECT_EQ(keyNamed(keys, "Teletext")->source, KeySource::Config);

    std::vector<std::string> names = namesOf(keys);
    std::vector<std::string> unique = names;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    EXPECT_EQ(unique.size(), names.size());
}

TEST(ModelKeys, FixtureConfigurationNamesAreAddedAfterTheTable) {
    const auto document = harmony::ConfigDocument::open(
        std::filesystem::path(HARMONY_TEST_DATA_DIR) / "fixtures" / "xml-unknown-target.xml");
    ASSERT_TRUE(document) << document.error().message;

    // 885 без профиля: список = таблица модели плюс физические кнопки конфигурации.
    // Источник у имени тот, кто ответил первым: имена таблицы — ScanTable.
    const auto* table = keyScanTable(8, "885");
    ASSERT_TRUE(table);
    const auto keys = modelKeys(modelInfo("885"), {}, {}, &document->model());
    EXPECT_TRUE(keys.complete);

    std::size_t configOnly = 0;
    std::vector<std::string> configNames;
    for (const auto& device : document->model().devices)
        for (const auto& group : device.controlGroups)
            for (const auto& button : group.buttons) {
                if (button.name.empty())
                    continue;   // экранная кнопка: имени клавиши здесь нет
                configNames.push_back(button.name);
                const auto* key = keyNamed(keys, button.name);
                ASSERT_TRUE(key) << button.name;
                const bool inTable = std::any_of(table->entries.begin(), table->entries.end(),
                                                 [&](const KeyScan& entry) {
                        return entry.name == button.name;
                    });
                EXPECT_EQ(key->source, inTable ? KeySource::ScanTable : KeySource::Config)
                    << button.name;
                configOnly += !inTable;
            }

    ASSERT_FALSE(configNames.empty()) << "фикстура без физических кнопок — тест ничего не проверяет";
    EXPECT_EQ(keys.keys.size(), table->entries.size() + configOnly);
    // Имена только из конфигурации — в конце списка: таблица отвечает раньше.
    for (std::size_t i = keys.keys.size() - configOnly; i < keys.keys.size(); ++i)
        EXPECT_EQ(keys.keys[i].source, KeySource::Config) << keys.keys[i].name;
}

TEST(ModelKeys, CompletenessFollowsTheSourceThatAnsweredAboutTheModel) {
    // Профиль передан — список исчерпывающий, даже когда таблица семейства неполная.
    const std::vector<std::string> profileKey = {"Play"};
    EXPECT_TRUE(modelKeys(modelInfo("880"), profileKey, {}, nullptr).complete);
    // Только таблица: полнота — её полнота.
    EXPECT_TRUE(modelKeys(modelInfo("885"), {}, {}, nullptr).complete);
    EXPECT_FALSE(modelKeys(modelInfo("880"), {}, {}, nullptr).complete);
    // Ни профиля, ни таблицы — полноты нет.
    EXPECT_FALSE(modelKeys(modelInfo("525"), {}, {}, nullptr).complete);
}

TEST(ModelKeys, FamilyWithoutCodeTablesAnswersByNamesOnlyWhereTheyExist) {
    // Arch 11 (1100) и arch 17 без таблицы надписей: отвечать нечем, поэтому список даёт
    // только конфигурация, и он не исчерпывающий.
    for (const auto* model : {"1100", "950"}) {
        const auto keys = modelKeys(modelInfo(model), {}, {}, nullptr);
        EXPECT_TRUE(keys.keys.empty()) << model;
        EXPECT_FALSE(keys.complete) << model;
        EXPECT_TRUE(keys.vocabularyFallback) << model;

        const auto config = configWithButtons({"Play"});
        const auto withConfig = modelKeys(modelInfo(model), {}, {}, &config);
        ASSERT_EQ(withConfig.keys.size(), 1U) << model;
        EXPECT_EQ(withConfig.keys.front().source, KeySource::Config) << model;
        EXPECT_FALSE(withConfig.complete) << model;
    }

    // Touch — таблица надписей есть, значит запасной путь к словарю уже не нужен.
    const auto touch = modelKeys(modelInfo("touch"), {}, {}, nullptr);
    EXPECT_FALSE(touch.keys.empty());
    EXPECT_FALSE(touch.complete);
    EXPECT_FALSE(touch.vocabularyFallback);

    // Имя из конфигурации не отменяет надпись: в списке есть оба — конфигурация отвечает
    // как источник конкретного имени. Предупреждение о расхождении пишет проверка (C1),
    // называя оба значения (правило 18); здесь фиксируется, что оба значения в списке.
    const auto touchConfig = configWithButtons({"Teletext"});
    const auto both = modelKeys(modelInfo("touch"), {}, {}, &touchConfig);
    EXPECT_EQ(both.keys.size(), touch.keys.size() + 1);
    EXPECT_TRUE(keyNamed(both, "Teletext"));
    EXPECT_EQ(keyNamed(both, "Teletext")->source, KeySource::Config);
    EXPECT_TRUE(keyNamed(both, "Select"));   // надпись `Select` остаётся
}
