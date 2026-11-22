/**
 * @file noProfileWidgetsTest.cpp
 * @brief T1, часть с Qt: решения, которые программа принимает по списку клавиш модели.
 *
 * `ConfigView::remoteHasKeys()` (`app/src/widgets/configView.cpp:811–813`) — это «у документа есть
 * целевая модель, и у неё есть клавиши»: он берёт `property("HarmonyTargetRemote")` и спрашивает
 * `RemoteView::hardKeyIds()`. Сам `ConfigView` в тестовые цели не входит (как и `MainWindow`
 * в T4), поэтому проверяется та же цепочка от документа из файла до гейта назначения.
 *
 * Ожидания — текущее поведение; где запланированный шаг его изменит, помечено в тексте теста.
 */

#include "../app/src/assignmentGate.h"
#include "../app/src/modern/remoteView.h"

#include "harmony/core/configDocument.h"

#include <QString>
#include <QStringList>
#include <gtest/gtest.h>

#include <string>

using harmony::ConfigDocument;
using harmony::app::RemoteView;
using harmony::app::assignmentAllowed;

namespace {

/// Документ с целевой моделью в свойстве (XML в памяти, без файлов).
harmony::Result<ConfigDocument> documentWithTarget(const std::string& target) {
    const std::string xml = "<Root><Properties><Property name=\"HarmonyTargetRemote\">" + target +
                            "</Property></Properties></Root>";
    return ConfigDocument::fromXml(xml, "widgets");
}

/// Список клавиш так, как его строит `ConfigView::remoteHasKeys()`.
QStringList keysOfDocument(const ConfigDocument& document) {
    return RemoteView::hardKeyIds(QString::fromStdString(document.model().property("HarmonyTargetRemote")));
}

} // namespace

// 525 — модель без профиля: список клавиш пуст, поэтому страница кнопок выключена (N6/N7).
TEST(NoProfileWidgets, ConfigViewFindsNoKeysForModelWithoutProfile) {
    const auto document = documentWithTarget("525");
    ASSERT_TRUE(document) << document.error().message;
    const QStringList keys = keysOfDocument(*document);
    EXPECT_TRUE(keys.isEmpty()) << "N7: изменится в A1 (K4) — список даст скан-таблица arch 9";
    EXPECT_FALSE(assignmentAllowed(keys)) << "N6: изменится в A1";
}

// Профиль есть — та же цепочка открывает назначение. Контроль, что предыдущий тест зелёный
// не из-за сломанного пути «документ → модель → клавиши».
TEST(NoProfileWidgets, ConfigViewFindsKeysForModelWithProfile) {
    const auto document = documentWithTarget("650");
    ASSERT_TRUE(document) << document.error().message;
    const QStringList keys = keysOfDocument(*document);
    EXPECT_FALSE(keys.isEmpty());
    EXPECT_TRUE(assignmentAllowed(keys));
}
