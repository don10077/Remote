/**
 * @file keyScansTest.cpp
 * @brief K2: скан-таблицы клавиш по семействам — имена из словаря, полнота таблицы,
 *        пометка для неподтверждённого кода.
 *
 * Тесты проверяют решения программы: каким именем она называет код в каждом семействе,
 * считает ли она таблицу исчерпывающей и что показывает, когда имени нет. Содержимое
 * файлов-источников (harmony-explorations, afterglow) тестом не сверяется — эти файлы не
 * в git, поэтому сверка сделана один раз и записана в отчёте `reports/K2.md` (правило 19).
 */

#include "harmony/core/keyScans.h"
#include "harmony/core/keyVocabulary.h"
#include "harmony/core/remoteCatalog.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace harmony;

namespace {

/// Имена словаря как множество — по нему проверяется, что таблица называет канон.
std::set<std::string> dictionaryNames() {
    std::set<std::string> names;
    for (const auto& entry : keyVocabulary())
        names.insert(std::string(entry.name));
    return names;
}

/// Сколько записей во всех таблицах: проверка не проходит вакуумно.
std::size_t totalEntries() {
    std::size_t total = 0;
    for (const auto& table : keyScanTables())
        total += table.entries.size();
    return total;
}

} // namespace

TEST(KeyScans, EveryNameInATableIsACanonicalDictionaryName) {
    ASSERT_TRUE(totalEntries() > 0) << "таблиц кодов нет — тест не проверяет ничего";
    const auto names = dictionaryNames();
    for (const auto& table : keyScanTables()) {
        for (const auto& entry : table.entries) {
            // Таблица называет именем словаря, а не выдуманным именем и не пометкой.
            EXPECT_TRUE(names.count(std::string(entry.name))) << entry.name;
            EXPECT_NE(keyInfo(entry.name), nullptr) << entry.name;
            EXPECT_FALSE(isTemporaryZoneName(entry.name)) << entry.name;
        }
    }
}

TEST(KeyScans, CodesInEveryTableAreUniqueAndSorted) {
    ASSERT_TRUE(totalEntries() > 0);
    for (const auto& table : keyScanTables()) {
        std::set<unsigned> seen;
        unsigned previous = 0;
        for (const auto& entry : table.entries) {
            EXPECT_TRUE(seen.insert(entry.scan).second) << table.architecture << "/" << entry.scan;
            EXPECT_GT(entry.scan, previous) << table.architecture << "/" << entry.scan;
            EXPECT_GE(entry.scan, 1u) << entry.scan;
            EXPECT_LE(entry.scan, 0x3Fu) << entry.scan;
            previous = entry.scan;
        }
    }
}

TEST(KeyScans, TheSameCodeNamesDifferentKeysInDifferentFamilies) {
    EXPECT_EQ(keyNameForScan(14, {}, 0x2C), "Play");        // 650
    EXPECT_EQ(keyNameForScan(8, "885", 0x2C), "SoftRight4"); // 885: тот же код — клавиша у экрана
    EXPECT_EQ(keyNameForScan(12, {}, 0x1E), "Play");        // One
    EXPECT_EQ(keyNameForScan(14, {}, 0x1E), "FastForward"); // и не Play на 650
}

TEST(KeyScans, CodesOfThe650ScreenCornersAreNamedByItsMeasurement) {
    // Правило владельца (20.11): эти четыре кода получают имена измерения 650, а не пометку.
    EXPECT_EQ(keyNameForScan(14, {}, 0x02), "SoftRight1");
    EXPECT_EQ(keyNameForScan(14, {}, 0x08), "SoftLeft1");
    EXPECT_EQ(keyNameForScan(14, {}, 0x09), "SoftLeft2");
    EXPECT_EQ(keyNameForScan(14, {}, 0x22), "SoftRight2");
    // И коды 0x37…0x3F в arch 14 нет: там их измеряли по 885, а не по 650.
    EXPECT_EQ(keyNameForScan(14, {}, 0x37), std::nullopt);
    EXPECT_EQ(keyNameForScan(8, "885", 0x37), "Pause");
}

TEST(KeyScans, ModelTableWinsAndFamilyTableIsTheFallback) {
    // У 300 и 350 разные device-клавиши на одном коде — таблица не только по семейству.
    EXPECT_EQ(keyNameForScan(16, "300", 0x05), "TV");       // на корпусе Tv — алиас
    EXPECT_EQ(keyNameForScan(16, "350", 0x05), "TvMedia");
    EXPECT_EQ(keyNameForScan(16, "300", 0x0B), "Teletext");
    EXPECT_EQ(keyNameForScan(16, "350", 0x0B), "Favorite3");
    // Модели без своей таблицы в этом семействе — решение по таблице семейства, то есть её нет.
    EXPECT_EQ(keyScanTable(16, "200"), nullptr);
    EXPECT_EQ(keyNameForScan(16, "200", 0x05), std::nullopt);

    // Имя модели сравнивается без регистра и без суффикса `i`.
    EXPECT_EQ(keyScanTable(16, "300"), keyScanTable(16, "300I"));
    EXPECT_EQ(keyNameForScan(16, "350i", 0x05), "TvMedia");
    EXPECT_EQ(keyScanTable(8, "885"), keyScanTable(8, "885i"));
}

TEST(KeyScans, IncompleteFamilyTableDoesNotClaimThatAKeyExists) {
    // Arch 8: таблица по модели 885 подтверждена целиком, общая таблица семейства —
    // пересечение 885 и 880, и коды, подтверждённые только у 885, в ней отсутствуют.
    const auto* byModel = keyScanTable(8, "885");
    const auto* byFamily = keyScanTable(8, "");
    ASSERT_TRUE(byModel) << byModel;
    ASSERT_TRUE(byFamily) << byFamily;
    EXPECT_TRUE(byModel->complete);
    EXPECT_FALSE(byFamily->complete);

    EXPECT_EQ(keyNameForScan(8, "885", 0x01), "Activities");
    EXPECT_EQ(keyNameForScan(8, "880", 0x01), std::nullopt);
    EXPECT_EQ(keyNameForScan(8, "720", 0x01), std::nullopt);

    // Всё, что есть в неполной таблице семейства, должно быть и в таблице модели — иначе
    // программа утверждает имя, которого у подтверждения нет.
    for (const auto& entry : byFamily->entries)
        EXPECT_EQ(keyNameForScan(8, "885", entry.scan), entry.name) << entry.scan;
}

TEST(KeyScans, CompletenessIsRecordedForEveryFamilyAndModel) {
    EXPECT_TRUE(keyScanTableComplete(14, {}));
    EXPECT_TRUE(keyScanTableComplete(12, "one"));
    EXPECT_TRUE(keyScanTableComplete(8, "885"));
    EXPECT_TRUE(keyScanTableComplete(16, "300"));
    EXPECT_TRUE(keyScanTableComplete(16, "350"));
    EXPECT_TRUE(keyScanTableComplete(18, "800"));

    // Неполные или отсутствующие таблицы — false: по отсутствию имени нельзя решать,
    // что клавиши у модели нет (K4, C1).
    EXPECT_FALSE(keyScanTableComplete(8, "880"));
    EXPECT_FALSE(keyScanTableComplete(8, "720"));
    EXPECT_FALSE(keyScanTableComplete(8, "785"));
    EXPECT_FALSE(keyScanTableComplete(16, "200"));
    EXPECT_FALSE(keyScanTableComplete(9, "525"));
    EXPECT_FALSE(keyScanTableComplete(10, "890"));
    EXPECT_FALSE(keyScanTableComplete(11, "1100"));
    EXPECT_FALSE(keyScanTableComplete(15, "900"));
    EXPECT_FALSE(keyScanTableComplete(17, "touch"));
    EXPECT_FALSE(keyScanTableComplete(0, {}));
}

TEST(KeyScans, CodeOutsideTheTableHasNoNameAndIsShownAsAPlaceholder) {
    EXPECT_EQ(keyNameForScan(9, "525", 0x2C), std::nullopt); // таблицы arch 9 нет
    EXPECT_EQ(keyNameForScan(14, {}, 0x00), std::nullopt);   // 0x00 — не клавиша
    EXPECT_EQ(keyNameForScan(14, {}, 0x40), std::nullopt);   // за границей скан-кода

    // Пометка для отображения — не имя: у неподтверждённого кода имени нет (правило 6).
    EXPECT_EQ(scanPlaceholder(0x2C), "Scan2C");
    EXPECT_EQ(scanPlaceholder(0x05), "Scan05");
    EXPECT_EQ(keyInfo(scanPlaceholder(0x2C)), nullptr);
    EXPECT_TRUE(isTemporaryZoneName(scanPlaceholder(0x2C)));
}

TEST(KeyScans, EveryTableBelongsToAFamilyKnownByTheCatalog) {
    // Ключ таблицы — номер архитектуры (`RemoteModel::architecture`, M1), а не id модели.
    ASSERT_TRUE(totalEntries() > 0);
    for (const auto& table : keyScanTables()) {
        EXPECT_NE(table.architecture, 0) << table.model;
        if (table.model.empty())
            continue;
        const auto* model = catalogModel(table.model);
        ASSERT_TRUE(model) << table.model;
        EXPECT_EQ(model->architecture, table.architecture) << table.model;
        // И таблица по модели находится по её id так же, как по id из каталога.
        EXPECT_EQ(keyScanTable(table.architecture, table.model), &table) << table.model;
    }
}
