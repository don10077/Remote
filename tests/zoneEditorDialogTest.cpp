#include "../app/src/widgets/zoneEditorDialog.h"
#include "../app/src/remoteResources.h"
#include "../app/src/zoneEditing.h"
#include "../app/src/i18n.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QScopeGuard>
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

TEST(ZoneEditorDialog, AutoZonesFlowVisibleOnCanvas) {
    // Как при запуске приложения: рабочая папка заполняется встроенными ресурсами.
    // Папка — та, что рядом с exe: авто-зоны берут фото оттуда (remoteResources::folder()).
    // Устаревшие remote.json штатных пультов в ней заменяются встроенными, ошибки не будет.
    const auto error = harmony::app::remoteResources::reload();
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();
    harmony::app::ZoneEditorDialog dialog("600");
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
    const QString idleText = action->text();
    action->trigger();
    // Распознавание асинхронное: пока оно идёт, на кнопке «Остановить»; ждём возврата подписи.
    EXPECT_NE(action->text(), idleText);
    QElapsedTimer timer;
    timer.start();
    while (action->text() != idleText && timer.elapsed() < 60000) QApplication::processEvents(QEventLoop::AllEvents, 50);
    ASSERT_EQ(action->text(), idleText) << "auto zones did not finish in 60 s";
    // Регрессия: каждая распознанная кнопка добавляется отдельной зоной.
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

TEST(ZoneEditorDialog, AutoZonesCanBeStopped) {
    // Второе нажатие останавливает распознавание: процесс завершается, зоны не добавляются.
    const auto error = harmony::app::remoteResources::reload();
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();
    harmony::app::ZoneEditorDialog dialog("600");
    auto* canvas = dialog.findChild<harmony::app::ZoneCanvas*>();
    auto* action = dialog.findChild<QAction*>("autoZones");
    auto* selectAll = dialog.findChild<QAction*>("selectAll");
    auto* del = dialog.findChild<QAction*>("deleteZones");
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(action, nullptr);
    ASSERT_NE(selectAll, nullptr);
    ASSERT_NE(del, nullptr);
    dialog.show();
    QApplication::processEvents();
    selectAll->trigger();
    del->trigger();
    ASSERT_EQ(canvas->zones().size(), 0);
    const QString idleText = action->text();
    action->trigger();
    if (action->text() == idleText) GTEST_SKIP() << "Python is not available, nothing to stop";
    ASSERT_NE(dialog.findChild<QProcess*>("autoZonesProcess"), nullptr);
    action->trigger();
    EXPECT_EQ(action->text(), idleText);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1500) QApplication::processEvents(QEventLoop::AllEvents, 50);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(dialog.findChild<QProcess*>("autoZonesProcess"), nullptr);
    EXPECT_EQ(canvas->zones().size(), 0);
    EXPECT_TRUE(action->isEnabled());
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

TEST(ZoneEditorDialog, RemoteFieldsAreWrittenToFileRightAway) {
    // Редактор пишет в рабочую папку рядом с exe: подготовить её и вернуть штатный файл в конце.
    ASSERT_TRUE(harmony::app::remoteResources::reload().isEmpty());
    const QString path = QDir(harmony::app::remoteResources::folder()).filePath("remotes/600/remote.json");
    QFile in(path);
    ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    const QByteArray previous = in.readAll();
    in.close();
    const auto restore = qScopeGuard([&] {
        QFile out(path);
        if (out.open(QIODevice::WriteOnly)) out.write(previous);
        out.close();
        harmony::app::remoteResources::reload();
    });
    // В рабочей папке мог остаться файл прошлой версии: начинаем со штатного.
    QFile builtin(":/res/remotes/600/remote.json");
    ASSERT_TRUE(builtin.open(QIODevice::ReadOnly));
    const QByteArray original = builtin.readAll();
    {
        QFile out(path);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        ASSERT_EQ(out.write(original), original.size());
    }
    ASSERT_TRUE(harmony::app::remoteResources::reload().isEmpty());

    harmony::app::ZoneEditorDialog dialog("600");
    auto* touch = dialog.findChild<QCheckBox*>("zoneTouch");
    auto* format = dialog.findChild<QComboBox*>("zoneFormat");
    ASSERT_NE(touch, nullptr);
    ASSERT_NE(format, nullptr);
    EXPECT_FALSE(touch->isChecked());
    EXPECT_EQ(format->currentData().toString(), "arch14");
    touch->setChecked(true);
    QFile after(path);
    ASSERT_TRUE(after.open(QIODevice::ReadOnly));
    const QByteArray text = after.readAll();
    const auto remote = QJsonDocument::fromJson(text).object();
    EXPECT_TRUE(remote["capabilities"].toObject().value("touch").toBool());
    // Точечная правка: изменилась одна строка, остальной файл тот же.
    QByteArray expected = original;
    expected.replace("\"touch\": false", "\"touch\": true");
    EXPECT_EQ(text, expected);
}

TEST(ZoneEditorDialog, PanelRoleIsChosenFromList) {
    QTemporaryDir resources;
    ASSERT_TRUE(harmony::app::remoteResources::reload(resources.path()).isEmpty());
    harmony::app::ZoneEditorDialog dialog("1100");
    auto* layer = dialog.findChild<QComboBox*>("zoneLayer");
    auto* canvas = dialog.findChild<harmony::app::ZoneCanvas*>();
    auto* role = dialog.findChild<QComboBox*>("zonePanelRole");
    ASSERT_NE(role, nullptr);
    int selected = -1;
    for (int l = 1; l < layer->count() && selected < 0; ++l) {
        layer->setCurrentIndex(l);
        for (int i = 0; i < canvas->zones().size(); ++i)
            if (canvas->zones()[i].toObject().value("kind") == "ScreenPanel") { selected = i; break; }
    }
    ASSERT_GE(selected, 0);
    canvas->setSelection({selected});
    const QString before = canvas->zones()[selected].toObject().value("role").toString();
    EXPECT_EQ(role->currentData().toString(), before);
    const int other = role->findData(before == "side" ? "band" : "side");
    ASSERT_GE(other, 0);
    role->setCurrentIndex(other);
    ASSERT_TRUE(QMetaObject::invokeMethod(role, "activated", Qt::DirectConnection, Q_ARG(int, other)));
    EXPECT_EQ(canvas->zones()[selected].toObject().value("role").toString(), role->itemData(other).toString());
}

TEST(ZoneEditorDialog, TilesFollowTileAreaAndServiceItemsToggle) {
    QTemporaryDir resources;
    ASSERT_TRUE(harmony::app::remoteResources::reload(resources.path()).isEmpty());
    harmony::app::ZoneEditorDialog dialog("900");
    auto* layer = dialog.findChild<QComboBox*>("zoneLayer");
    auto* canvas = dialog.findChild<harmony::app::ZoneCanvas*>();
    ASSERT_NE(layer, nullptr);
    layer->setCurrentIndex(1);
    // На экране рамка — зона плиток; по умолчанию она обнимает плитки.
    ASSERT_TRUE(canvas->screen().has_value());
    const QRectF area = *canvas->screen();
    const auto tileRects = [canvas] {
        QList<QRectF> rects;
        for (const auto& value : canvas->zones())
            if (value.toObject().value("kind") == "ScreenSlot")
                rects.append(harmony::app::zoneEditing::rectOf(value.toObject()));
        return rects;
    };
    for (const QRectF& r : tileRects()) EXPECT_TRUE(area.contains(r));

    // Зона плиток уменьшена полями X, Y, W, H: плитки сразу ложатся в неё.
    canvas->setSelection({harmony::app::ZoneCanvas::SCREEN_ITEM});
    const QRectF smaller = area.adjusted(10, 20, -10, -10);
    const QList<qreal> values{smaller.x(), smaller.y(), smaller.width(), smaller.height()};
    for (int i = 0; i < 4; ++i) {
        auto* box = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("zoneRect%1").arg(i));
        ASSERT_NE(box, nullptr);
        box->setValue(values[i]);
    }
    auto* width = dialog.findChild<QDoubleSpinBox*>("zoneRect2");
    ASSERT_TRUE(QMetaObject::invokeMethod(width, "editingFinished", Qt::DirectConnection));
    ASSERT_TRUE(canvas->screen().has_value());
    EXPECT_EQ(*canvas->screen(), smaller);
    for (const QRectF& r : tileRects()) EXPECT_TRUE(smaller.contains(r)) << r.x() << " " << r.y();

    // Батарея: флажок добавляет ячейку Battery, снятый — убирает.
    auto* battery = dialog.findChild<QCheckBox*>("zoneServiceBattery");
    ASSERT_NE(battery, nullptr);
    EXPECT_FALSE(battery->isChecked());
    const auto count = [canvas] {
        int n = 0;
        for (const auto& value : canvas->zones()) n += value.toObject().value("id") == "Battery";
        return n;
    };
    battery->setChecked(true);
    EXPECT_EQ(count(), 1);
    battery->setChecked(false);
    EXPECT_EQ(count(), 0);
}

TEST(ZoneEditorDialog, CommonTileAreaMovesTilesOnAllScreens) {
    QTemporaryDir resources;
    ASSERT_TRUE(harmony::app::remoteResources::reload(resources.path()).isEmpty());
    harmony::app::ZoneEditorDialog dialog("900"); // у всех экранов 900 одна зона плиток
    auto* layer = dialog.findChild<QComboBox*>("zoneLayer");
    auto* canvas = dialog.findChild<harmony::app::ZoneCanvas*>();
    auto* own = dialog.findChild<QCheckBox*>("zoneOwnArea");
    ASSERT_NE(own, nullptr);
    ASSERT_GE(layer->count(), 3);
    layer->setCurrentIndex(1);
    EXPECT_FALSE(own->isChecked());
    ASSERT_TRUE(canvas->screen().has_value());
    const QRectF smaller = canvas->screen()->adjusted(0, 20, 0, 0);
    canvas->setSelection({harmony::app::ZoneCanvas::SCREEN_ITEM});
    auto* y = dialog.findChild<QDoubleSpinBox*>("zoneRect1");
    auto* h = dialog.findChild<QDoubleSpinBox*>("zoneRect3");
    y->setValue(smaller.y());
    h->setValue(smaller.height());
    ASSERT_TRUE(QMetaObject::invokeMethod(h, "editingFinished", Qt::DirectConnection));
    // Другой экран без своей зоны получил ту же зону.
    layer->setCurrentIndex(2);
    ASSERT_TRUE(canvas->screen().has_value());
    EXPECT_EQ(*canvas->screen(), smaller);
    // Своя зона: правка открытого экрана других не трогает.
    own->setChecked(true);
    canvas->setSelection({harmony::app::ZoneCanvas::SCREEN_ITEM});
    y->setValue(smaller.y() + 10);
    h->setValue(smaller.height() - 10);
    ASSERT_TRUE(QMetaObject::invokeMethod(h, "editingFinished", Qt::DirectConnection));
    layer->setCurrentIndex(1);
    EXPECT_EQ(*canvas->screen(), smaller);
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
