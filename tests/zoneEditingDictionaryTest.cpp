/**
 * @file zoneEditingDictionaryTest.cpp
 * @brief K5, этап А: `checkBody()` решает имена зон по словарю (`zoneEditor:B5`, п. 2).
 *
 * Правила (уровень — Warning везде; ошибкой остаётся только дубликат `id`):
 * 1. `id` — алиас словаря → предупреждение, где названо каноническое имя;
 * 2. `id` нет в словаре и имя не временное → предупреждение для **всех** видов, не только `Key`;
 * 3. `id` есть в словаре, но вид не допускается маской словаря → предупреждение;
 * 4. временное имя (`Auto`, `Zone3`, `Scan2C`, `Play_copy`) → предупреждение;
 * 5. имя словаря для своего вида → предупреждения нет;
 * 6. встроенные модели → ни одного предупреждения по именам (данные словаря это покрывают).
 *
 * Геометрия во всех тестах корректная, поэтому предупреждение, отнесённое на индекс зоны,
 * может быть только про имя. Предупреждение «нет зон вида Key» относится на -1.
 *
 * Красными сейчас не могут быть проверки, где предупреждение уже появляется по тому же факту
 * (имени нет в старом списке имён): такие проверки красны только по тексту — они названы
 * в отчёте.
 */

#include "../app/src/remoteResources.h"
#include "../app/src/zoneEditing.h"

#include "harmony/core/keyVocabulary.h"
#include "harmony/core/remoteProfile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSizeF>
#include <QStringList>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace ze = harmony::app::zoneEditing;

namespace {
namespace resources = harmony::app::remoteResources;

const QSizeF kPhoto(200, 300);

/// Зона с корректной геометрией: предупреждение о ней может быть только про имя.
QJsonObject zone(const QString& id, const QString& kind) {
    return {{"id", id}, {"kind", kind}, {"rect", QJsonArray{10, 20, 30, 40}}, {"radius", 4.0}};
}

/// Предупреждения, отнесённые на зону с индексом `index`.
QList<ze::Issue> warningsFor(const QList<ze::Issue>& issues, int index) {
    QList<ze::Issue> found;
    for (const auto& issue : issues)
        if (issue.level == ze::Issue::Level::Warning && issue.zone == index)
            found.append(issue);
    return found;
}

/// Каждый тест работает со своим снимком ресурсов: профили — данные, в git.
class ZoneDictionaryTest : public testing::Test {
protected:
    QTemporaryDir root;
    void SetUp() override {
        ASSERT_TRUE(root.isValid());
        ASSERT_TRUE(resources::reload(root.path()).isEmpty());
    }
    void TearDown() override { resources::reload(root.path()); }
};

} // namespace

// 1. Алиас словаря: предупреждение называет каноническое имя.
TEST_F(ZoneDictionaryTest, AliasZoneNameWarnsAboutCanonicalName) {
    const QJsonArray zones{zone("ScreenLeft1", "Key")};
    const auto issues = ze::checkBody(zones, kPhoto, std::nullopt);

    const auto found = warningsFor(issues, 0);
    ASSERT_EQ(found.size(), 1) << "проверка должна назвать имя и каноническое имя";
    EXPECT_TRUE(found.front().text.contains("SoftLeft1"))
        << "текст предупреждения: " << found.front().text.toStdString();
}

// 2. Имени нет в словаре: предупреждение есть для всех видов, не только для `Key`.
TEST_F(ZoneDictionaryTest, UnknownNameWarnsForEveryKind) {
    const auto forKey = ze::checkBody(QJsonArray{zone("Lef3", "Key")}, kPhoto, std::nullopt);
    EXPECT_EQ(warningsFor(forKey, 0).size(), 1) << " вида Key предупреждение уже было";

    // Красное ожидание: прежде для вида System имени в словаре не проверяли.
    const auto forSystem = ze::checkBody(QJsonArray{zone("Lef3", "System")}, kPhoto, std::nullopt);
    EXPECT_EQ(warningsFor(forSystem, 0).size(), 1) << "проверка имени по словарю не зависит от вида зоны";
}

// 3. Имя словаря, но вид не допускается маской словаря: предупреждение.
TEST_F(ZoneDictionaryTest, DictionaryNameForWrongKindWarns) {
    ASSERT_TRUE(harmony::keyInfo("VolumeUp")) << "VolumeUp — имя словаря";
    ASSERT_FALSE(harmony::keyInfo("VolumeUp")->kinds & static_cast<unsigned>(harmony::PhysicalKind::ScreenSoft))
        << "VolumeUp допустим только как зона вида Key — иначе тест решает не то";

    const auto issues = ze::checkBody(QJsonArray{zone("VolumeUp", "ScreenSoft")}, kPhoto, std::nullopt);
    EXPECT_EQ(warningsFor(issues, 0).size(), 1) << "имя не подходит для вида ScreenSoft";

    // Контроль: то же имя для своего вида — предупреждения нет.
    const auto ok = ze::checkBody(QJsonArray{zone("VolumeUp", "Key")}, kPhoto, std::nullopt);
    EXPECT_TRUE(warningsFor(ok, 0).isEmpty()) << "имя словаря для своего вида не предупреждение";
}

// 4. Временное имя: предупреждение говорит, что имя временное, а не что имени нет в списке.
TEST_F(ZoneDictionaryTest, TemporaryZoneNameWarnsAsTemporary) {
    ASSERT_TRUE(harmony::isTemporaryZoneName("Auto")) << "Auto — временное имя";

    const auto issues = ze::checkBody(QJsonArray{zone("Auto", "Key")}, kPhoto, std::nullopt);
    const auto found = warningsFor(issues, 0);
    ASSERT_EQ(found.size(), 1) << "проверка должна назвать имя";
    EXPECT_TRUE(found.front().text.contains("temporary"))
        << "текст предупреждения должен говорить о временном имени: " << found.front().text.toStdString();
}

// 5. Данные словаря: каждая зона встроенных моделей — каноническое имя словаря, которое
//   допускается своим видом, и не временное. Ни одного нового предупреждения.
TEST_F(ZoneDictionaryTest, BuiltInProfilesHaveNoNameWarnings) {
    ASSERT_FALSE(harmony::bundledRemoteProfiles.empty()) << "встроенных профилей нет — тест ничего не проверяет";

    int checked = 0;
    for (const auto& profile : harmony::bundledRemoteProfiles) {
        const QString id = QString::fromUtf8(profile.id.data(), static_cast<int>(profile.id.size()));
        const auto hardware = resources::remote(id).value("hardware").toObject();
        ASSERT_FALSE(hardware.isEmpty()) << id.toStdString() << ": оформление не попало в снимок";

        const auto size = hardware.value("size").toArray();
        const QSizeF photo(size[0].toDouble(), size[1].toDouble());
        for (const auto& entry : hardware.value("zones").toArray()) {
            const auto body = entry.toObject();
            const QString name = body.value("id").toString();
            const QString kind = body.value("kind").toString();
            if (name.isEmpty())
                continue;
            ++checked;

            const auto* info = harmony::keyInfo(name.toStdString());
            ASSERT_NE(info, nullptr) << id.toStdString() << ": " << name.toStdString()
                                    << " — имени нет в словаре";
            const auto physical = harmony::physicalKind(kind.toStdString());
            ASSERT_TRUE(physical.has_value()) << id.toStdString() << ": " << kind.toStdString()
                                             << " — вида нет в словаре";
            EXPECT_TRUE(info->kinds & static_cast<unsigned>(*physical))
                << id.toStdString() << ": " << name.toStdString()
                << " не допускается видом " << kind.toStdString();
            EXPECT_FALSE(harmony::isTemporaryZoneName(name.toStdString()))
                << id.toStdString() << ": " << name.toStdString() << " — временное имя";

            const auto issues = ze::checkBody(QJsonArray{entry}, photo, std::nullopt);
            const auto found = warningsFor(issues, 0);
            EXPECT_TRUE(found.isEmpty())
                << id.toStdString() << ": " << name.toStdString() << " (" << kind.toStdString() << ")"
                << (found.isEmpty() ? "" : found.front().text.toStdString());
        }
    }
    EXPECT_GT(checked, 0) << "зон в встроенных профилях нет — тест ничего не проверяет";
}

// 6. Правила словаря проверяют только зоны корпуса (`hardware.zones`). Имена ячеек экрана
//   (поле `key`) — не физические зоны: словарём они не проверяются (решение K4).
TEST_F(ZoneDictionaryTest, ScreenCellsAreNotCheckedByDictionaryRules) {
    const QJsonObject cell{{"id", "Slow"}, {"kind", "ScreenSlot"},
                           {"rect", QJsonArray{10, 20, 30, 40}}};
    ASSERT_FALSE(harmony::keyInfo("Slow")) << "Slow — имени нет в словаре: иначе тест решает не то";

    const auto issues = ze::checkScreen(QJsonArray{cell}, QSizeF(200, 300), {}, {});
    EXPECT_TRUE(warningsFor(issues, 0).isEmpty()) << "имя ячейки экрана словарём не проверяется";
}
