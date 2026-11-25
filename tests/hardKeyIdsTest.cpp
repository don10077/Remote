/**
 * @file hardKeyIdsTest.cpp
 * @brief K5, этап А: контракт `RemoteView::hardKeyIds()` — обёртка над `modelKeyIds()`.
 *
 * Здесь только ожидания (правило 2 [00-INDEX.md]), кода шага ещё нет. Красные — те решения,
 * где список меняется; зелёные — решения, уже принятые шагом A1 (`modelKeysQt`), и проверки
 * фактов, которые красными быть не могут (перечислены в отчёте).
 *
 * Решения шага:
 * 1. `hardKeyIds(id)` отвечает тем же списком, что `modelKeyIds(id)`: без профиля список не
 *    пуст — его даёт таблица семейства или имена конфигурации;
 * 2. пустой id — модель не выбрана: модель по умолчанию не подставляется (Q3, U1);
 * 3. Запасной список sharedKeyIds удалён в A3: выбор клавиш проверяется через диалог.
 * 4. размер фото и геометрия зоны в решении о списке не участвуют: для пульта, который
 *    `reload()` принял, они выполнены самой загрузкой (`checkModel()`), поэтому для пультов в
 *    снимке список не меняется;
 * 5. подсказка панели пульта видна пульту с профилем, а не модели с непустым списком клавиш:
 *    без профиля кнопки назначаются в таблице (тест добавлен на этапе Б и был красным до кода).
 */

#include "../app/src/modelKeysQt.h"
#include "../app/src/modern/remotePanel.h"
#include "../app/src/modern/remoteView.h"
#include "../app/src/remoteResources.h"

#include "harmony/core/model/remoteConfig.h"
#include "harmony/core/modelInfo.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/core/remoteProfile.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QStringList>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <string>

using harmony::app::modelKeyIds;
using harmony::app::RemoteView;

namespace {
namespace resources = harmony::app::remoteResources;

/// Профили как данные, в git: каждый тест работает со своим снимком ресурсов.
class HardKeyIdsTest : public testing::Test {
protected:
    QTemporaryDir root;
    void SetUp() override {
        ASSERT_TRUE(root.isValid());
        ASSERT_TRUE(resources::reload(root.path()).isEmpty());
    }
    void TearDown() override { resources::reload(root.path()); }
};

/// Записать profile.json в папку тестового снимка.
void writeFixture(const QString& root, const QString& id, const QJsonObject& remote) {
    ASSERT_TRUE(QDir().mkpath(root + "/remotes/" + id));
    QFile file(root + "/remotes/" + id + "/profile.json");
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const QByteArray text = QJsonDocument(remote).toJson();
    ASSERT_EQ(file.write(text), text.size());
    file.close();
}

} // namespace

// 1. Модель без профиля отвечает таблицей семейства: список не пуст (было пуст — поведение меняется).
TEST_F(HardKeyIdsTest, ModelWithoutProfileAnswersByItsTable) {
    ASSERT_EQ(harmony::remoteProfile("885"), nullptr) << "885 должна быть моделью без оформления";

    const auto list = RemoteView::hardKeyIds("885");
    EXPECT_FALSE(list.isEmpty()) << "K5: без профиля список даёт таблицу arch 8";
    EXPECT_EQ(list, modelKeyIds("885")) << "hardKeyIds — обёртка над modelKeyIds";
}

// 2. Обёртка отвечает тем же списком для каждой модели каталога, включая модели без профиля.
TEST_F(HardKeyIdsTest, WrapperAnswersTheSameListForEveryCatalogModel) {
    int withoutProfile = 0;
    for (const auto& model : harmony::remoteCatalog()) {
        const QString id = QString::fromStdString(std::string(model.id));
        if (harmony::remoteProfile(model.id) == nullptr)
            ++withoutProfile;
        EXPECT_EQ(RemoteView::hardKeyIds(id), modelKeyIds(id)) << model.id;
    }
    EXPECT_GT(withoutProfile, 0) << "в каталоге нет моделей без профиля — тест ничего не проверяет";
}

// 3. Пустой id — модель не выбрана: модель по умолчанию не подставляется (Q3).
TEST_F(HardKeyIdsTest, EmptyIdIsNotTheDefaultModel) {
    EXPECT_EQ(RemoteView::hardKeyIds(""), modelKeyIds("")) << "пустой id решается как модель без профиля";
    EXPECT_NE(RemoteView::hardKeyIds(""), RemoteView::hardKeyIds("900"))
        << "модель по умолчанию (900) здесь не участвует (U1)";
    EXPECT_TRUE(harmony::modelInfo("").id.empty()) << "модель не выбрана";
}

// 5. Факт: пульт с несовпавшим размером фото в снимок не попадает, поэтому имён из его профиля
//   в списке нет. Это не решение K5 — оно зависит от ответа на вопрос отчёта.
TEST_F(HardKeyIdsTest, ProfileRejectedBySizeMismatchIsNotInTheSnapshot) {
    const QString id = "k5fixture";

    QImage image(100, 120, QImage::Format_ARGB32);            // фото 100×120
    image.fill(Qt::white);
    ASSERT_TRUE(QDir().mkpath(root.path() + "/remotes/" + id));
    ASSERT_TRUE(image.save(root.path() + "/remotes/" + id + "/photo.png", "PNG"));

    const QJsonObject remote = {
        {"id", id},
        {"name", "K5 fixture"},
        {"hardware", QJsonObject{
            {"image", "photo.png"},
            {"size", QJsonArray{100, 100}},                  // размер не совпал с фото
            {"zones", QJsonArray{QJsonObject{
                {"id", "Power"}, {"kind", "Key"},
                {"rect", QJsonArray{10, 10, 20, 20}}, {"radius", 4.0}}}},
        }},
        {"screens", QJsonArray{QJsonObject{
            {"id", "devices"},
            {"ref", QJsonArray{100, 100}},
            {"cells", QJsonArray{QJsonObject{{"key", "Play"}, {"rect", QJsonArray{5, 5, 10, 10}}}}},
        }}},
    };
    writeFixture(root.path(), id, remote);

    const auto errors = resources::reload(root.path());
    EXPECT_TRUE(resources::skippedIds().contains(id))
        << "оформление с несовпавшим размером должно быть отклонено: " << errors.toStdString();
    EXPECT_TRUE(resources::modelError(id).contains("hardware.size")) << resources::modelError(id).toStdString();
    EXPECT_TRUE(resources::remote(id).isEmpty()) << "отклонённый пульт не входит в снимок";
    EXPECT_EQ(harmony::remoteProfile(id.toStdString()), nullptr);

    // Модели нет в каталоге: ядру отвечать нечем. Если владелец выберет чтение файла напрямую
    // (вопрос отчёта), здесь появятся `Power` и `Play`.
    EXPECT_TRUE(modelKeyIds(id).isEmpty()) << "сегодня имён такого профиля в списке нет";
    EXPECT_EQ(RemoteView::hardKeyIds(id), modelKeyIds(id)) << "обёртка отвечает тем же списком";
}

// 6. Подсказка панели пульта — по решению «есть профиль», а не по непустому списку клавиш.
//   Без профиля кнопки назначаются в таблице, и подсказка «нажмите кнопку пульта» не нужна.
TEST_F(HardKeyIdsTest, PanelHintFollowsProfileNotTheKeyList) {
    ASSERT_EQ(harmony::remoteProfile("885"), nullptr) << "885 должна быть моделью без оформления";
    ASSERT_FALSE(modelKeyIds("885").isEmpty()) << "список не пуст — иначе тест решает не то";

    harmony::model::RemoteConfig config;
    harmony::app::RemotePanel panel;
    panel.resize(500, 650);
    panel.setConfig(&config, "885");
    panel.show();
    QApplication::processEvents();

    auto* hint = panel.findChild<QLabel*>("remoteHint");
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->isVisible()) << "K5: подсказка видна только пульту с профилем";

    // Контроль: пульту с профилем подсказка нужна.
    panel.setConfig(&config, "900");
    QApplication::processEvents();
    EXPECT_TRUE(hint->isVisible());
}
