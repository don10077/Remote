/**
 * @file noProfileWidgetsTest.cpp
 * @brief T1, часть с Qt: решения, которые программа принимает по документу и списку клавиш модели.
 *
 * `ConfigView::canAssignButtons()` (`app/src/widgets/configView.cpp`) — было `remoteHasKeys()`:
 * «у документа есть целевая модель, и у неё есть клавиши». После A1 это «есть документ»
 * (правило блока A, п. 4): список клавиш больше ничего не запрещает.
 *
 * Сам `ConfigView` в тестовые цели не входит (как и `MainWindow` в T4), поэтому проверяется та
 * же цепочка от документа до гейта: целевая модель → `modelKeysQt` → решение гейта.
 *
 * Ожидания — поведение после A1. Прежние фиксаторы T1 («525: список пуст, страница кнопок
 * выключена») перевёрнуты этим шагом: без профиля список даёт то, чем модель может ответить,
 * а страница открыта, потому что есть документ.
 */

#include "../app/src/assignmentGate.h"
#include "../app/src/modelKeysQt.h"
#include "../app/src/remoteResources.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/modelKeys.h"

#include <QString>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <string>

using harmony::ConfigDocument;
using harmony::app::ButtonAction;
using harmony::app::ButtonRef;
using harmony::app::buttonAllowed;
using harmony::app::gateOpen;
using harmony::app::modelKeysFor;

namespace {
namespace resources = harmony::app::remoteResources;

/// Профили как данные, в git: каждый тест работает со своим снимком ресурсов.
class NoProfileWidgetsTest : public testing::Test {
protected:
    QTemporaryDir root;
    void SetUp() override {
        ASSERT_TRUE(root.isValid());
        ASSERT_TRUE(resources::reload(root.path()).isEmpty());
    }
    void TearDown() override { resources::reload(root.path()); }
};

/// Документ с целевой моделью в свойстве (XML в памяти, без файлов).
harmony::Result<ConfigDocument> documentWithTarget(const std::string& target) {
    const std::string xml = "<Root><Properties><Property name=\"HarmonyTargetRemote\">" + target +
                            "</Property></Properties></Root>";
    return ConfigDocument::fromXml(xml, "widgets");
}

} // namespace

// 525 — модель без профиля и без таблицы кодов: прежде страница кнопок была выключена.
// После A1 она открыта, потому что есть документ; по модели отвечают словарь и имена документа.
TEST_F(NoProfileWidgetsTest, DocumentWithoutProfileOpensAssignment) {
    const auto document = documentWithTarget("525");
    ASSERT_TRUE(document) << document.error().message;
    ASSERT_FALSE(document->targetRemote().empty()) << "целевая модель потерялась";

    EXPECT_TRUE(gateOpen(true)) << "A1: гейт — есть документ, а не «есть клавиши»";

    const auto keys = modelKeysFor(QString::fromStdString(document->targetRemote()), &document->model());
    EXPECT_TRUE(keys.keys.empty()) << "в документе кнопок нет — имён из него тоже нет";
    EXPECT_FALSE(keys.complete);
    EXPECT_TRUE(keys.vocabularyFallback);
    EXPECT_TRUE(buttonAllowed(ButtonRef::hard("Teletext"), keys, ButtonAction::AssignTo, false))
        << "без исчерпывающего списка назначить можно любую клавишу словаря";
}

// Контроль: модель с профилем решается так же — по документу. Список при этом тот же, что
// прежде, потому что фото корректно.
TEST_F(NoProfileWidgetsTest, DocumentWithProfileOpensAssignmentTheSameWay) {
    const auto document = documentWithTarget("650");
    ASSERT_TRUE(document) << document.error().message;

    EXPECT_TRUE(gateOpen(true));
    const auto keys = modelKeysFor(QString::fromStdString(document->targetRemote()), &document->model());
    ASSERT_FALSE(keys.keys.empty());
    EXPECT_TRUE(keys.complete);
    const std::string name = keys.keys.front().name;
    EXPECT_TRUE(buttonAllowed(ButtonRef::hard(QString::fromStdString(name)), keys,
                              ButtonAction::AssignTo, false)) << name;
}
