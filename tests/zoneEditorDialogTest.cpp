#include "../app/src/widgets/zoneEditorDialog.h"
#include "../app/src/remoteResources.h"
#include "../app/src/zoneEditing.h"
#include "../app/src/i18n.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QLabel>
#include <QMouseEvent>
#include <QUndoStack>
#include <QTemporaryDir>
#include <QDir>
#include <gtest/gtest.h>

TEST(ZoneEditorDialog, ActionFieldsUpdateJsonAndPreview) {
    QTemporaryDir resources;
    ASSERT_TRUE(harmony::app::remoteResources::reload(resources.path()).isEmpty());
    const auto preview = qEnvironmentVariable("HARMONY_ZONE_PREVIEW_DIR");
    if (!preview.isEmpty()) harmony::app::i18n::install(*qobject_cast<QApplication*>(qApp), "ru");
    harmony::app::ZoneEditorDialog dialog("1100");
    dialog.show();
    QApplication::processEvents();
    auto* layer = dialog.findChild<QComboBox*>("zoneLayer");
    auto* canvas = dialog.findChild<harmony::app::ZoneCanvas*>();
    auto* action = dialog.findChild<QComboBox*>("zoneAction");
    ASSERT_NE(layer, nullptr);
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(action, nullptr);
    if (!preview.isEmpty()) {
        ASSERT_TRUE(QDir().mkpath(preview));
        ASSERT_TRUE(dialog.grab().save(preview + "/body.png"));
    }
    int selected = -1;
    for (int l = 1; l < layer->count() && selected < 0; ++l) {
        layer->setCurrentIndex(l);
        for (int i = 0; i < canvas->zones().size(); ++i) {
            const auto kind = canvas->zones()[i].toObject().value("kind").toString();
            if (kind == "ScreenKey" || kind == "ScreenSide") { selected = i; break; }
        }
    }
    ASSERT_GE(selected, 0);
    canvas->setSelection({selected});
    const auto before = canvas->zones()[selected].toObject();
    const int previous = action->findData("previous");
    ASSERT_GE(previous, 0);
    action->setCurrentIndex(previous);
    ASSERT_TRUE(QMetaObject::invokeMethod(action, "activated", Qt::DirectConnection, Q_ARG(int, previous)));
    const auto after = canvas->zones()[selected].toObject();
    EXPECT_EQ(after.value("page").toInt(), -1);
    EXPECT_FALSE(after.contains("key"));
    EXPECT_FALSE(after.contains("goto"));
    EXPECT_EQ(after.value("rect"), before.value("rect"));
    EXPECT_EQ(after.value("id"), before.value("id"));
    if (!preview.isEmpty()) {
        QApplication::processEvents();
        ASSERT_TRUE(dialog.grab().save(preview + "/screen.png"));
    }
    if (!preview.isEmpty()) harmony::app::i18n::install(*qobject_cast<QApplication*>(qApp), "en");
}

TEST(ZoneEditorDialog, ScreenToggleRestoresOriginalRectangle) {
    harmony::app::ZoneEditorDialog dialog("900");
    auto* canvas = dialog.findChild<harmony::app::ZoneCanvas*>();
    auto* screen = dialog.findChild<QCheckBox*>("zoneScreenArea");
    ASSERT_NE(screen, nullptr);
    ASSERT_TRUE(canvas->screen());
    const auto before = canvas->screen();
    screen->setChecked(false);
    EXPECT_FALSE(canvas->screen());
    screen->setChecked(true);
    EXPECT_EQ(canvas->screen(), before);
}

TEST(ZoneEditorDialog, LeftButtonPansCanvasOnEmptySpace) {
    harmony::app::ZoneEditorDialog dialog("1100");
    auto* canvas = dialog.findChild<harmony::app::ZoneCanvas*>();
    ASSERT_NE(canvas, nullptr);
    canvas->resize(800, 600);
    canvas->fitView();
    canvas->zoomTo(2.0); // вписанный слой виден целиком: сдвиг заметен только в увеличенном масштабе
    const auto send = [canvas](QEvent::Type type, const QPoint& pos, Qt::MouseButton button,
                               Qt::MouseButtons buttons) {
        QMouseEvent event(type, pos, pos, button, buttons, Qt::NoModifier);
        QApplication::sendEvent(canvas, &event);
    };
    // Пустое место: точка, под которой нет зоны.
    QPoint empty;
    for (int y = 0; y < canvas->height() && empty.isNull(); y += 4)
        for (int x = 0; x < canvas->width() && empty.isNull(); x += 4)
            if (canvas->itemAt(QPointF(x, y)) == harmony::app::ZoneCanvas::NO_ITEM) empty = {x, y};
    ASSERT_TRUE(!empty.isNull());
    const QRectF before = canvas->visibleLayerRect();

    // Щелчок без движения не двигает холст, но снимает выделение.
    canvas->setSelection({0});
    send(QEvent::MouseButtonPress, empty, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, empty, Qt::LeftButton, Qt::NoButton);
    EXPECT_EQ(canvas->visibleLayerRect(), before);
    EXPECT_TRUE(canvas->selection().isEmpty());

    // Перетаскивание левой кнопкой по пустому месту сдвигает холст вправо-вниз.
    send(QEvent::MouseButtonPress, empty, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, {empty.x() + 60, empty.y() + 40}, Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, {empty.x() + 60, empty.y() + 40}, Qt::LeftButton, Qt::NoButton);
    const QRectF after = canvas->visibleLayerRect();
    EXPECT_NE(after, before);
    EXPECT_LT(after.left(), before.left());
    EXPECT_LT(after.top(), before.top());
}

TEST(ZoneEditorDialog, Harmony900HasDifferentActivityAndCommandLayers) {
    harmony::app::ZoneEditorDialog dialog("900");
    auto* layer = dialog.findChild<QComboBox*>("zoneLayer");
    auto* canvas = dialog.findChild<harmony::app::ZoneCanvas*>();
    ASSERT_NE(layer, nullptr);
    const auto preview = qEnvironmentVariable("HARMONY_ZONE_PREVIEW_DIR");
    QList<int> counts;
    for (int index = 1; index < layer->count(); ++index) {
        layer->setCurrentIndex(index);
        int slotCount = 0;
        for (const auto& zone : canvas->zones()) if (zone.toObject().value("kind") == "ScreenSlot") ++slotCount;
        counts.append(slotCount);
        if (!preview.isEmpty()) {
            dialog.show();
            QApplication::processEvents();
            EXPECT_TRUE(dialog.grab().save(preview + QString("/900-layer-%1.png").arg(index)));
        }
    }
    EXPECT_TRUE(counts.contains(3));
    EXPECT_TRUE(counts.contains(6));
}

TEST(ZoneEditorDialog, AutoZonesFlowVisibleOnCanvas880) {
    // Диагностика: «нашло 81, но ни одной не видно».
    QTemporaryDir resources;
    const QString src = QCoreApplication::applicationDirPath() + QStringLiteral("/../../../dist/win/resources");
    ASSERT_TRUE(QDir(src).exists());
    ASSERT_TRUE(QDir().mkpath(resources.path()));
    ASSERT_TRUE(QFile::copy(src + "/layouts.json", resources.path() + "/layouts.json"));
    ASSERT_TRUE(QDir().mkpath(resources.path() + "/remotes/880"));
    for (const auto& f : QDir(src + "/remotes/880").entryList(QDir::Files))
        ASSERT_TRUE(QFile::copy(src + "/remotes/880/" + f, resources.path() + "/remotes/880/" + f));
    ASSERT_TRUE(QDir().mkpath(resources.path() + "/sprites"));
    for (const auto& f : QDir(src + "/sprites").entryList(QDir::Files))
        ASSERT_TRUE(QFile::copy(src + "/sprites/" + f, resources.path() + "/sprites/" + f));
    ASSERT_TRUE(harmony::app::remoteResources::reload(resources.path()).isEmpty());
    // runAutoZones берёт фото из <exedir>/resources — дадим ему туда путь.
    const QString exeres = QCoreApplication::applicationDirPath() + "/resources";
    ASSERT_TRUE(QDir().mkpath(exeres + "/remotes/880"));
    QFile::remove(exeres + "/remotes/880/photo.png");
    ASSERT_TRUE(QFile::copy(src + "/remotes/880/photo.png", exeres + "/remotes/880/photo.png"));
    harmony::app::ZoneEditorDialog dialog("880");
    auto* canvas = dialog.findChild<harmony::app::ZoneCanvas*>();
    auto* action = dialog.findChild<QAction*>("autoZones");
    auto* selectAll = dialog.findChild<QAction*>("selectAll");
    auto* del = dialog.findChild<QAction*>("deleteZones");
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(action, nullptr);
    dialog.show();
    QApplication::processEvents();
    ASSERT_GT(canvas->zones().size(), 0);
    if (selectAll) selectAll->trigger();
    if (del) del->trigger();
    QApplication::processEvents();
    EXPECT_EQ(canvas->zones().size(), 0);
    action->trigger();
    for (int i = 0; i < 20; ++i) QApplication::processEvents();
    // Регрессия: «Recognized 81» должно означать 81 зону на холсте, а не одну
    // «пустую» (целый массив зон однажды попадал в слой как один элемент).
    EXPECT_GE(canvas->zones().size(), 25);
    for (int i = 0; i < canvas->zones().size(); ++i)
        EXPECT_FALSE(canvas->zones()[i].toObject().value("id").toString().isEmpty());
    // Авто-зоны не пересекаются друг с другом (касание краями допустимо).
    for (int i = 0; i < canvas->zones().size(); ++i)
        for (int j = i + 1; j < canvas->zones().size(); ++j) {
            const QRectF a = harmony::app::zoneEditing::rectOf(canvas->zones()[i].toObject());
            const QRectF b = harmony::app::zoneEditing::rectOf(canvas->zones()[j].toObject());
            const QRectF common = a.intersected(b);
            EXPECT_TRUE(common.isEmpty() || common.width() * common.height() == 0) << i << " " << j;
        }
}

TEST(ZoneEditorDialog, AutoZonesActionOnlyOnBodyLayer) {
    QTemporaryDir resources;
    ASSERT_TRUE(harmony::app::remoteResources::reload(resources.path()).isEmpty());
    harmony::app::ZoneEditorDialog dialog("1000");
    auto* action = dialog.findChild<QAction*>("autoZones");
    ASSERT_NE(action, nullptr);
    EXPECT_TRUE(action->isEnabled()); // слой корпуса
    auto* layer = dialog.findChild<QComboBox*>("zoneLayer");
    ASSERT_NE(layer, nullptr);
    ASSERT_GT(layer->count(), 1);
    layer->setCurrentIndex(1); // слой экрана
    EXPECT_FALSE(action->isEnabled());
    layer->setCurrentIndex(0);
    EXPECT_TRUE(action->isEnabled());
}

TEST(ZoneEditorDialog, SelectAllSelectsEveryZoneOfLayer) {
    QTemporaryDir resources;
    ASSERT_TRUE(harmony::app::remoteResources::reload(resources.path()).isEmpty());
    harmony::app::ZoneEditorDialog dialog("1000");
    auto* action = dialog.findChild<QAction*>(QStringLiteral("selectAll"));
    auto* canvas = dialog.findChild<harmony::app::ZoneCanvas*>();
    ASSERT_NE(action, nullptr);
    ASSERT_NE(canvas, nullptr);
    const int zones = canvas->zones().size();
    ASSERT_GT(zones, 0);
    EXPECT_TRUE(action->isEnabled()); // в слое корпуса есть зоны
    action->trigger();
    ASSERT_EQ(canvas->selection().size(), zones);
    for (int i = 0; i < zones; ++i) EXPECT_TRUE(canvas->selection().contains(i));
}
