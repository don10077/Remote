// Фиксаторы гейта назначения (T4). Ожидания — текущее поведение; переворачиваются в A1.
//
// MainWindow в тестовых целях нет (`mainWindow*.cpp` не входит в `harmony_gui_tests`),
// поэтому тест вызывает гейт напрямую со списком, который передают места вызова:
// `mainWindowButtons.cpp:93, 165, 185, 212, 402` и `mainWindowClipboard.cpp:22, 36`.

#include "../app/src/assignmentGate.h"
#include "../app/src/modern/remoteView.h"

#include <QString>
#include <QStringList>
#include <gtest/gtest.h>

using harmony::app::assignmentAllowed;
using harmony::app::RemoteView;

TEST(AssignmentGate, ModelWithProfileOpensGate) {
    for (const char* id : {"900", "650", "1100"}) {
        const QStringList keys = RemoteView::hardKeyIds(QString::fromLatin1(id));
        ASSERT_FALSE(keys.isEmpty()) << id;
        EXPECT_TRUE(assignmentAllowed(keys)) << id;
    }
}

// 525 — модель без профиля и без скан-таблицы в приложении: список пуст, гейт закрыт.
TEST(AssignmentGate, ModelWithoutProfileKeepsGateClosed) {
    const QStringList keys = RemoteView::hardKeyIds("525");
    ASSERT_TRUE(keys.isEmpty()) << "N7: изменится в A1 (K4) — список даст скан-таблица arch 9";
    EXPECT_FALSE(assignmentAllowed(keys)) << "изменится в A1";
}

// Без документа места вызова передают пустой список — гейт закрыт.
TEST(AssignmentGate, NoDocumentMeansEmptyList) {
    EXPECT_FALSE(assignmentAllowed(QStringList()));
}

// Экранные кнопки 1000/1100 — назначаемые клавиши модели (K4): они в списке, поэтому
// гейт открыт и для них.
TEST(AssignmentGate, ScreenCellsAreInListForModelsThatHaveThem) {
    const QStringList keys = RemoteView::hardKeyIds("1100");
    ASSERT_TRUE(assignmentAllowed(keys));
    EXPECT_TRUE(keys.contains("Play")) << "Play — key ячейки экрана транспорта";
    EXPECT_TRUE(keys.contains("Slow"));
}
