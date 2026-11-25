/**
 * @file modelKeysQtTest.cpp
 * @brief A1: имена зон и ячеек экрана для ядра собирает приложение, без проверки фото.
 *
 * `harmony::modelKeys()` (K4) не читает содержимое оформлений — оно знает только реестр.
 * Поэтому решение «какие имена даёт профиль» принимает `modelKeysQt`. Здесь проверяются
 * решения программы:
 *
 * 1. порядок списка для встроенных моделей совпадает с нынешним `RemoteView::hardKeyIds()` —
 *    это условие A1: перевод гейтов не меняет списки там, где фото корректно;
 * 2. имена профиля даёт файл, который читает приложение, а не ядро;
 * 3. для модели без таблицы кодов список — имена из документа, и он меняется вместе с
 *    документом: имя появляется, когда кнопка назначена, и исчезает, когда назначение снято;
 * 4. модель не выбрана — это не модель по умолчанию (Q3).
 */

#include "../app/src/modelKeysQt.h"
#include "../app/src/modern/remoteView.h"
#include "../app/src/remoteResources.h"
#include "profileNames.h"

#include "harmony/core/buttonOwner.h"
#include "harmony/core/configDocument.h"
#include "harmony/core/modelInfo.h"
#include "harmony/core/modelKeys.h"
#include "harmony/core/remoteProfile.h"

#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace harmony;

namespace {
namespace resources = harmony::app::remoteResources;
namespace paths = harmony::testpaths;

/// Каждый тест работает со своим снимком ресурсов: профили как данные, в git.
class ModelKeysQtTest : public testing::Test {
protected:
    QTemporaryDir root;
    void SetUp() override {
        ASSERT_TRUE(root.isValid());
        ASSERT_TRUE(resources::reload(root.path()).isEmpty());
    }
    void TearDown() override { resources::reload(root.path()); }
};

QStringList namesOf(const ModelKeys& keys) {
    QStringList names;
    for (const auto& key : keys.keys)
        names.append(QString::fromStdString(key.name));
    return names;
}

const ModelKey* keyNamed(const ModelKeys& keys, const std::string& name) {
    for (const auto& key : keys.keys)
        if (key.name == name)
            return &key;
    return nullptr;
}

/// Документ с целевой моделью и физической кнопкой (XML в памяти, без файлов).
harmony::Result<ConfigDocument> documentWithButton(const std::string& target, const std::string& key) {
    const std::string xml = "<Root><Properties><Property name=\"HarmonyTargetRemote\">" + target +
                            "</Property></Properties><User><Id>0</Id></User><Device><Id>100</Id>"
                            "<Type>Television</Type><Presentation><Label>Телевизор</Label>"
                            "<ControlGroup name=\"HardButtons\"><Button name=\"" + key +
                            "\"><Label>" + key + "</Label><Position>0</Position>"
                            "<ActionId>100_PowerToggle_Press</ActionId></Button></ControlGroup></Presentation>"
                            "<Commands><Command><Name>PowerToggle</Name><Data><Protocol>2</Protocol>"
                            "<Code>0x0A</Code></Data></Command></Commands></Device></Root>";
    return ConfigDocument::fromXml(xml, "a1");
}

} // namespace

// 1. С валидным фото список `modelKeysQt` — тот же, что нынешний `hardKeyIds()`.
TEST_F(ModelKeysQtTest, ForEveryBuiltInModelTheListMatchesHardKeyIds) {
    ASSERT_FALSE(bundledRemoteProfiles.empty()) << "встроенных профилей нет — тест ничего не проверяет";

    int checked = 0;
    for (const auto& profile : bundledRemoteProfiles) {
        const QString id = QString::fromStdString(profile.id);
        const QStringList expected = harmony::app::RemoteView::hardKeyIds(id);
        if (expected.isEmpty())
            continue;   // оформления с именами зон здесь нет: сравнивать нечего
        ++checked;
        EXPECT_EQ(harmony::app::modelKeyIds(id), expected) << profile.id;
    }

    // Проверка не вакуумная: хотя бы один встроенный профиль даёт список клавиш.
    EXPECT_GT(checked, 0);
}

// 2. Содержимое профиля читает приложение: ядро знает только реестр оформлений.
TEST_F(ModelKeysQtTest, ProfileNamesComeFromTheFileTheAppReads) {
    const QString id = "a1fixture";
    ASSERT_TRUE(QDir().mkpath(root.path() + "/remotes/" + id));

    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    ASSERT_TRUE(image.save(root.path() + "/" + paths::photoKey(id), "PNG"));

    const QJsonObject remote = {
        {"id", id},
        {"name", "A1 fixture"},
        {"hardware", QJsonObject{
            {"image", resources::kPhotoFile},
            {"size", QJsonArray{100, 100}},
            {"zones", QJsonArray{
                QJsonObject{{"id", "Power"}, {"kind", "Key"},
                            {"rect", QJsonArray{10, 10, 20, 20}}, {"radius", 4.0}},
                // Вид не `Key`: такая зона в список клавиш не входит.
                QJsonObject{{"id", "Battery"}, {"kind", "System"},
                            {"rect", QJsonArray{40, 40, 20, 20}}, {"radius", 4.0}},
            }},
        }},
        {"screens", QJsonArray{QJsonObject{
            {"id", "devices"},
            {"ref", QJsonArray{100, 100}},
            {"cells", QJsonArray{
                QJsonObject{{"key", "Play"}, {"rect", QJsonArray{5, 5, 10, 10}}},
                // Ячейка без поля `key` — не назначаемая клавиша.
                QJsonObject{{"rect", QJsonArray{20, 20, 10, 10}}},
            }},
        }}},
    };
    QFile file(root.path() + "/" + paths::profileKey(id));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const QByteArray text = QJsonDocument(remote).toJson();
    ASSERT_EQ(file.write(text), text.size());
    file.close();

    ASSERT_TRUE(resources::reload(root.path()).isEmpty());
    const auto info = modelInfo(id.toStdString());
    ASSERT_TRUE(info.hasProfile) << "оформление не принято — проверять нечего";
    EXPECT_FALSE(info.known) << "модели нет в каталоге: ядро о ней знает только реестр";

    const auto keys = harmony::app::modelKeysFor(id, nullptr);
    // Зоны в порядке файла, затем ячейки экранов: порядка таблицы здесь нет — таблицы нет.
    EXPECT_EQ(namesOf(keys), QStringList({"Power", "Play"}));
    EXPECT_TRUE(keys.complete) << "профиль ответил — список исчерпывающий";
    ASSERT_TRUE(keyNamed(keys, "Power"));
    ASSERT_TRUE(keyNamed(keys, "Play"));
    EXPECT_EQ(keyNamed(keys, "Power")->source, KeySource::Profile);
    EXPECT_EQ(keyNamed(keys, "Play")->source, KeySource::ProfileScreen);
    EXPECT_FALSE(keyNamed(keys, "Battery"));

    // Те же правила, что у `hardKeyIds()`: совпадение — условие A1.
    EXPECT_EQ(harmony::app::modelKeyIds(id), harmony::app::RemoteView::hardKeyIds(id));
}

// 3. Для модели без таблицы кодов список — имена документа, и он меняется с документом.
TEST_F(ModelKeysQtTest, ModelWithoutTableAnswersByDocumentNamesAndFollowsEdits) {
    auto document = documentWithButton("525", "Teletext");
    ASSERT_TRUE(document) << document.error().message;

    const auto keys = harmony::app::modelKeysFor("525", &document->model());
    EXPECT_EQ(namesOf(keys), QStringList({"Teletext"}));
    EXPECT_FALSE(keys.complete) << "имена из документа не могут быть исчерпывающими";
    EXPECT_TRUE(keys.vocabularyFallback) << "по модели ответить нечем: UI предлагает словарь";

    // Назначили ещё одну клавишу — имя появляется в списке.
    const auto applied = document->setHardButton(ButtonOwner::device("100"), "VolumeUp", "100", "PowerToggle");
    ASSERT_TRUE(applied) << applied.error().message;
    const auto afterAdd = harmony::app::modelKeysFor("525", &document->model());
    EXPECT_EQ(namesOf(afterAdd), QStringList({"Teletext", "VolumeUp"}));

    // Сняли назначение — кнопка из документа удалена, имя уходит из списка.
    const auto cleared = document->setHardButton(ButtonOwner::device("100"), "VolumeUp", {}, {});
    ASSERT_TRUE(cleared) << cleared.error().message;
    const auto afterRemove = harmony::app::modelKeysFor("525", &document->model());
    EXPECT_EQ(namesOf(afterRemove), QStringList({"Teletext"}));
}

// 4. Модель не выбрана — это не модель по умолчанию (Q3).
TEST_F(ModelKeysQtTest, EmptyModelIdIsNotTheDefaultModel) {
    const auto document = documentWithButton("525", "Teletext");
    ASSERT_TRUE(document) << document.error().message;

    const auto keys = harmony::app::modelKeysFor("", &document->model());
    // Модели нет: отвечают только имена документа, и они не исчерпывающие.
    EXPECT_EQ(namesOf(keys), QStringList({"Teletext"}));
    EXPECT_FALSE(keys.complete);
    EXPECT_TRUE(keys.vocabularyFallback);
    EXPECT_TRUE(modelInfo("").id.empty()) << "модель не выбрана";

    // Нынешний `hardKeyIds("")` подставляет модель по умолчанию (900) — в A1 так не решается.
    EXPECT_NE(harmony::app::modelKeyIds("", &document->model()),
             harmony::app::RemoteView::hardKeyIds("")) << "Q3: документ без модели — как модель без профиля";
    EXPECT_EQ(harmony::app::modelKeyIds("", &document->model()), QStringList({"Teletext"}));

    // Документ без целевой модели — тот же ответ, что у модели без профиля.
    const auto noTarget = ConfigDocument::fromXml("<Root><Properties></Properties></Root>", "a1");
    ASSERT_TRUE(noTarget) << noTarget.error().message;
    EXPECT_TRUE(harmony::app::modelKeyIds("", &noTarget->model()).empty());
}
