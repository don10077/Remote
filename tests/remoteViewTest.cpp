#include "../app/src/modern/remoteView.h"
#include "../app/src/modern/remotePanel.h"
#include "../app/src/modern/modernShell.h"
#include "../app/src/remoteResources.h"
#include "../app/src/labels.h"
#include "../app/src/theme.h"
#include "harmony/core/remoteProfile.h"
#include "harmony/core/modelInfo.h"
#include "harmony/core/writePolicy.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMouseEvent>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QToolButton>
#include <QLabel>
#include <QTemporaryDir>
#include <algorithm>
#include <utility>
#include <gtest/gtest.h>

namespace {
/** Отправить щелчок в координатах виджета без зависимости от оконного менеджера. */
void click(harmony::app::RemoteView& view, const QPointF& point) {
    QMouseEvent event(QEvent::MouseButtonPress, point, point, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &event);
    QMouseEvent release(QEvent::MouseButtonRelease, point, point, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &release);
}

/** Точка в координатах фото (profile.json) → координаты виджета, как их считает RemoteView. */
QPointF widgetPoint(const harmony::app::RemoteView& view, const QPointF& photoPoint) {
    const QSize photo = view.sizeHint();
    const qreal scale = photo.width() > photo.height()
        ? view.width() / static_cast<qreal>(photo.width())
        : std::min(view.width() / static_cast<qreal>(photo.width()),
                   view.height() / static_cast<qreal>(photo.height()));
    const qreal w = photo.width() * scale;
    const qreal h = photo.height() * scale;
    return {view.width() - w + photoPoint.x() * scale,
            (view.height() - h) / 2.0 + photoPoint.y() * scale};
}

/**
 * Один раз загрузить ресурсы так же, как приложение: описание пульта читается из снимка
 * (remoteOf), а не напрямую из :/res — так у ячеек экранов уже есть rect.
 */
void ensureResourcesLoaded() {
    static QTemporaryDir folder;
    static const bool loaded = [] {
        const QString error = harmony::app::remoteResources::reload(folder.path());
        EXPECT_TRUE(error.isEmpty()) << error.toStdString();
        return error.isEmpty();
    }();
    ASSERT_TRUE(loaded);
}

/** remote.json пульта из снимка ресурсов; пустой объект, если пульта нет. */
QJsonObject remoteOf(const QString& id) {
    ensureResourcesLoaded();
    return harmony::app::remoteResources::remote(id);
}

/** Размер фото из remote.json: тесты не должны дублировать данные ресурсов. */
QSize profilePhotoSize(const QString& id) {
    const auto size = remoteOf(id).value("hardware").toObject().value("size").toArray();
    EXPECT_EQ(size.size(), 2);
    return {size[0].toInt(), size[1].toInt()};
}

/** Рамка дисплея из remote.json, в пикселях фото. */
QRectF profileScreen(const QString& id) {
    const auto rect = remoteOf(id).value("display").toObject().value("rect").toArray();
    EXPECT_EQ(rect.size(), 4);
    return {rect[0].toDouble(), rect[1].toDouble(),
            rect[2].toDouble(), rect[3].toDouble()};
}
}

TEST(RemoteView, BundledProfilesMatchPhotosAndHaveExplicitZones) {
    ensureResourcesLoaded(); // до циклов: reload заменяет harmony::remoteProfiles
    for (const auto& profile : harmony::remoteProfiles) {
        SCOPED_TRACE(std::string(profile.id));
        const QString image = QString::fromUtf8(profile.image.data());
        const QPixmap photo(image);
        ASSERT_FALSE(photo.isNull());
        const auto object = remoteOf(QString::fromStdString(profile.id));
        ASSERT_FALSE(object.isEmpty());
        const auto hardware = object.value("hardware").toObject();
        const auto size = hardware.value("size").toArray();
        ASSERT_EQ(size.size(), 2);
        EXPECT_EQ(photo.size(), QSize(size[0].toInt(), size[1].toInt()));
        EXPECT_EQ(object.value("id").toString().toStdString(), profile.id);
        ASSERT_TRUE(hardware.value("zones").isArray());
        QStringList commandKeys;
        QSet<QString> ids;
        const QSet<QString> kinds{"Key", "System", "ScreenActs", "ScreenSoft", "PagePrev", "PageNext"};
        for (const auto& entry : hardware.value("zones").toArray()) {
            ASSERT_TRUE(entry.isObject());
            const auto zone = entry.toObject();
            const QString id = zone.value("id").toString();
            EXPECT_FALSE(id.isEmpty());
            EXPECT_FALSE(ids.contains(id));
            ids.insert(id);
            const auto rect = zone.value("rect").toArray();
            ASSERT_EQ(rect.size(), 4);
            for (const auto& coordinate : rect) EXPECT_TRUE(coordinate.isDouble());
            const QRectF bounds(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble());
            EXPECT_TRUE(bounds.isValid());
            EXPECT_TRUE(QRectF(photo.rect()).contains(bounds));
            EXPECT_TRUE(zone.value("radius").isDouble());
            EXPECT_TRUE(kinds.contains(zone.value("kind").toString()));
            if (zone.value("kind").toString() == "Key") commandKeys.append(id);
        }
        // Назначаемые кнопки экранов модели ("key" ячеек) идут после физических:
        // экраны по алфавиту id, ячейки в порядке remote.json.
        const auto screens = object.value("screens").toArray();
        ASSERT_FALSE(screens.isEmpty());
        QMap<QString, QJsonArray> cellsByScreen;
        for (const auto& screen : screens)
            cellsByScreen.insert(screen.toObject().value("id").toString(), screen.toObject().value("cells").toArray());
        for (const auto& cells : std::as_const(cellsByScreen)) {
            for (const auto& entry : cells) {
                const QString key = entry.toObject().value("key").toString();
                if (!key.isEmpty() && !commandKeys.contains(key)) commandKeys.append(key);
            }
        }
        EXPECT_EQ(harmony::app::RemoteView::hardKeyIds(QString::fromUtf8(profile.id.data())), commandKeys);
        // W3: в профиле нет права записи — решение даёт каталог (configWriteTested).
        EXPECT_EQ(harmony::canWriteTo(harmony::modelInfo(profile.id)).verdict,
                  profile.id == "900" ? harmony::WriteVerdict::Allowed : harmony::WriteVerdict::Experimental)
            << profile.id;
    }
}

TEST(RemoteView, SwitchModelsClearsOldTargetsAndPreservesAspectRatio) {
    ensureResourcesLoaded();
    harmony::app::RemoteView view;
    QString clicked;
    QObject::connect(&view, &harmony::app::RemoteView::keyClicked,
                     [&clicked](const QString& id) { clicked = id; });
    view.resize(400, 1500);
    click(view, {85, 750});
    EXPECT_EQ(clicked, "VolumeUp");
    for (const auto* id : {"1000", "1100", "1100i", "600", "650", "665", "700", "one"}) {
        view.setRemoteProfile(QString::fromLatin1(id));
        const QSize photo = profilePhotoSize(QString::fromLatin1(id));
        const bool landscape = photo.width() > photo.height();
        EXPECT_EQ(view.sizeHint(), photo);
        EXPECT_EQ(view.heightForWidth(340), 340 * photo.height() / photo.width());
        if (landscape) {
            EXPECT_LT(view.minimumSizeHint().height(), 200);
        }
        ASSERT_NE(harmony::remoteProfile(id), nullptr);
        const auto definitions =
            remoteOf(QString::fromLatin1(id)).value("hardware").toObject().value("zones").toArray();
        ASSERT_FALSE(definitions.isEmpty());
        view.resize(view.sizeHint());
        for (const auto& entry : definitions) {
            const auto zone = entry.toObject();
            if (zone.value("kind").toString() != "Key") continue;
            const auto rect = zone.value("rect").toArray();
            ASSERT_EQ(rect.size(), 4);
            clicked.clear();
            click(view, {rect[0].toDouble() + rect[2].toDouble() / 2,
                         rect[1].toDouble() + rect[3].toDouble() / 2});
            EXPECT_EQ(clicked, zone.value("id").toString()) << id;
            break;
        }
        clicked.clear();
        view.grab();
        // Неизвестная модель гарантированно без разметки: старые зоны должны исчезнуть.
        view.setRemoteProfile("unknown-test-model");
        view.resize(400, 1500);
        click(view, {85, 750});
        EXPECT_TRUE(clicked.isEmpty());
        EXPECT_TRUE(harmony::app::RemoteView::hardKeyIds("unknown-test-model").isEmpty());
    }
    view.setRemoteProfile("900");
    view.resize(200, 750);
    click(view, {42.5, 375});
    EXPECT_EQ(clicked, "VolumeUp");
}

TEST(RemoteView, SwitchModelClearsLcdHitTargets) {
    harmony::app::RemoteView view;
    // размер холста берём из ресурсов, чтобы попасть в первую экранную ячейку
    view.resize(view.sizeHint());
    const QRectF screen = profileScreen("900");
    const QPointF softSlot(screen.x() + screen.width() * 0.15,
                           screen.y() + screen.height() * 0.28);
    view.setScreen(harmony::app::RemoteView::Screen::SoftButtons);
    view.setSoftButtons("Device", {"Power"});
    int hits = 0;
    QObject::connect(&view, &harmony::app::RemoteView::softButtonClicked,
                     [&hits](int) { ++hits; });
    view.grab();
    click(view, softSlot);
    EXPECT_EQ(hits, 1);
    view.setRemoteProfile("unknown-test-model");
    click(view, softSlot);
    EXPECT_EQ(hits, 1);
    view.grab();
    click(view, softSlot);
    EXPECT_EQ(hits, 1);
}

TEST(RemoteView, ActivityTilesAndFallbackFollowSelectedProfile) {
    harmony::app::RemoteView view;
    view.setRemoteProfile("1000");
    view.resize(view.sizeHint());
    view.setActivities({{"A", {}, false}, {"B", {}, false}, {"C", {}, false}, {"D", {}, false}});
    int selected = -1;
    QObject::connect(&view, &harmony::app::RemoteView::activityClicked,
                     [&selected](int index) { selected = index; });
    view.grab();
    click(view, {243, 260}); // centered fourth tile in the second row
    EXPECT_EQ(selected, 3);

    // неизвестный режим откатывается к activities, если он есть у профиля
    view.selectScreenMode("unknown-mode");
    EXPECT_EQ(view.screenMode(), "activities");
    view.setRemoteProfile("1100");
    EXPECT_EQ(view.screenMode(), "activities");
    EXPECT_TRUE(view.screenModes().contains("transport"));

    // у 600-серии активности и команды на одной раскладке 2x2, экран не сенсорный
    view.setRemoteProfile("600");
    EXPECT_EQ(view.screenMode(), "activities");
    EXPECT_EQ(view.screen(), harmony::app::RemoteView::Screen::Activities);
    EXPECT_FALSE(view.screenModes().contains("transport"));
    view.resize(view.sizeHint());
    view.grab();
    click(view, {243, 260}); // по самому экрану клик ничего не выбирает
    EXPECT_EQ(selected, 3);

    view.setRemoteProfile("665");
    EXPECT_EQ(view.screenMode(), "activities");

    // у 1000 теперь те же режимы, что у 1100
    view.setRemoteProfile("1000");
    for (const auto* mode : {"activities", "command", "favorites", "transport", "keypad"})
        EXPECT_TRUE(view.screenModes().contains(mode)) << mode;
}

TEST(RemoteView, ScreenKeysSwitchScreensAndAreAssignable) {
    using harmony::app::RemoteView;
    // транспорт, цифры и Menu/Guide/Info/Exit экрана — назначаемые кнопки модели
    const QStringList keys = RemoteView::hardKeyIds("1100");
    for (const auto* key : {"Play", "Pause", "Slow", "Number5", "NumberStar", "Menu", "Exit"})
        EXPECT_TRUE(keys.contains(key)) << key;
    // у 1000, как у 1100, кнопок Page нет: стрелки на экране команд листают страницы
    EXPECT_FALSE(RemoteView::hardKeyIds("1000").contains("UpArrow"));
    EXPECT_FALSE(keys.contains("CornerTL"));
    EXPECT_FALSE(keys.contains("Help"));

    RemoteView view;
    view.setRemoteProfile("1100");
    view.resize(view.sizeHint()); // координаты виджета = пиксели фото
    const QRectF screen = profileScreen("1100");
    const auto at = [&screen](qreal x, qreal y) { // единицы раскладки 320×240 → фото
        return QPointF(screen.x() + x * screen.width() / 320.0, screen.y() + y * screen.height() / 240.0);
    };
    QString key, system;
    QObject::connect(&view, &RemoteView::keyClicked, [&key](const QString& id) { key = id; });
    QObject::connect(&view, &RemoteView::systemKeyClicked,
                     [&system](const QString& id) { system = id; });

    view.selectScreenMode("transport");
    view.grab();
    click(view, at(158, 53)); // Play
    EXPECT_EQ(key, "Play");
    click(view, at(158, 10)); // Help
    EXPECT_EQ(system, "Help");
    click(view, at(17, 225)); // угол «123»
    EXPECT_EQ(view.screenMode(), "keypad");
    view.grab();
    click(view, at(160, 99)); // Number5
    EXPECT_EQ(key, "Number5");
    click(view, at(302, 225)); // звезда: экранные кнопки
    EXPECT_EQ(view.screenMode(), "command");
    EXPECT_EQ(view.screen(), RemoteView::Screen::SoftButtons);
    click(view, at(302, 14)); // сердце: избранное
    EXPECT_EQ(view.screenMode(), "favorites");
    EXPECT_EQ(view.screen(), RemoteView::Screen::SoftButtons);
    EXPECT_EQ(RemoteView::screenModeName("transport"), "Media");
}

TEST(RemoteView, ComposedScreensReferenceExistingSprites) {
    // Картинки экранов (layers, плитки, фон) — отдельные файлы из resources/sprites, встроенные
    // в ресурсы; у каждого экрана с "compose" есть слои.
    ensureResourcesLoaded();
    for (const auto& profile : harmony::remoteProfiles) {
        SCOPED_TRACE(std::string(profile.id));
        const auto screens = remoteOf(QString::fromStdString(profile.id)).value("screens").toArray();
        ASSERT_FALSE(screens.isEmpty());
        for (const auto& value : screens) {
            const auto screen = value.toObject();
            const std::string id = screen.value("id").toString().toStdString();
            for (const char* field : {"background", "cell"}) {
                const QString image = screen.value(field).toObject().value("image").toString();
                if (!image.isEmpty()) { EXPECT_TRUE(QFile::exists(":/res/" + image)) << image.toStdString(); }
            }
            const auto layers = screen.value("layers").toArray();
            for (const auto& layer : layers) {
                const QString file = layer.toObject().value("image").toString();
                EXPECT_TRUE(file.startsWith("sprites/")) << id << ": " << file.toStdString();
                EXPECT_TRUE(QFile::exists(":/res/" + file)) << file.toStdString();
            }
            if (screen.value("compose").toBool()) { EXPECT_FALSE(layers.isEmpty()) << "no layers: " << id; }
        }
    }
    // собранный экран рисуется и угол текущего экрана открывает себя же без ошибок
    harmony::app::RemoteView view;
    view.setRemoteProfile("1000");
    view.resize(view.sizeHint());
    view.setSoftButtons("Watch TV", {"FOX", "", "SUN"}); // плитки-картинки, в том числе пустая
    for (const auto* mode : {"activities", "command", "favorites", "transport", "keypad"}) {
        view.selectScreenMode(mode);
        EXPECT_FALSE(view.grab().isNull()) << mode;
    }
}

TEST(RemoteView, CompactScreenActivitiesFollowSideKeys) {
    harmony::app::RemoteView view;
    view.setRemoteProfile("650");
    view.resize(view.sizeHint());
    view.setScreen(harmony::app::RemoteView::Screen::Activities);
    view.setActivities({{"A", {}, false}, {"B", {}, false}, {"C", {}, false}});
    QList<int> clicked;
    QObject::connect(&view, &harmony::app::RemoteView::activityClicked,
                     [&clicked](int index) { clicked.append(index); });
    view.grab();
    click(view, {76, 319});  // SoftLeft1
    click(view, {371, 383}); // SoftRight2: четвёртой активности нет
    EXPECT_EQ(clicked, (QList<int>{0}));
}

TEST(RemoteView, CompactScreenSoftButtonsFollowSideKeys) {
    harmony::app::RemoteView view;
    view.setRemoteProfile("650");
    view.resize(view.sizeHint());
    view.setScreen(harmony::app::RemoteView::Screen::SoftButtons);
    view.setSoftButtons("Watch TV", {"Aspect", "Zoom", "Guide", "Info", "Exit"});
    QList<int> clicked;
    QObject::connect(&view, &harmony::app::RemoteView::softButtonClicked,
                     [&clicked](int position) { clicked.append(position); });
    view.grab();
    click(view, {76, 319});  // SoftLeft1
    click(view, {371, 383}); // SoftRight2
    EXPECT_EQ(clicked, (QList<int>{0, 3}));
    view.flipPage(1);
    view.grab();
    click(view, {76, 319});  // первая ячейка второй страницы
    EXPECT_EQ(clicked, (QList<int>{0, 3, 4}));
}

TEST(RemoteView, Harmony1000PageArrowsFlipSoftButtons) {
    using harmony::app::RemoteView;
    RemoteView view;
    view.setRemoteProfile("1000");
    view.resize(view.sizeHint());
    const QRectF screen = profileScreen("1000");
    const auto at = [&screen](qreal x, qreal y) { // единицы раскладки 320×240 → фото
        return QPointF(screen.x() + x * screen.width() / 320.0, screen.y() + y * screen.height() / 240.0);
    };
    view.selectScreenMode("command");
    view.setSoftButtons("Watch TV", {"A", "B", "C", "D", "E", "F", "G", "H"});
    QList<int> clicked;
    QObject::connect(&view, &RemoteView::softButtonClicked,
                     [&clicked](int position) { clicked.append(position); });
    view.grab();
    click(view, at(94, 67)); // Slot1
    click(view, at(25, 162)); // стрелка вниз «Page»
    view.grab();
    click(view, at(94, 67)); // первая плитка второй страницы
    click(view, at(25, 119)); // стрелка вверх
    view.grab();
    click(view, at(94, 67));
    EXPECT_EQ(clicked, (QList<int>{0, 6, 0}));
}

/**
 * Не тест, а инструмент для глаза: пишет PNG экранов всех моделей и режимов.
 * Запуск: HARMONY_PREVIEW_DIR=<папка> harmony_gui_tests --gtest_filter=RemoteView.RenderPreviews
 */
TEST(RemoteView, RenderPreviews) {
    const QString directory = qEnvironmentVariable("HARMONY_PREVIEW_DIR");
    if (directory.isEmpty()) GTEST_SKIP() << "HARMONY_PREVIEW_DIR is not set";
    ASSERT_TRUE(QDir().mkpath(directory));

    const QList<harmony::app::RemoteView::ActivityItem> activities = {
        {"Watch TV", "VirtualTelevisionN", false},
        {"Watch a DVD", "VirtualDvd", false},
        {"Listen to Music", "VirtualMediaCenterMusic", false},
        {"Watch VCR", "VirtualVcr", false},
        {"Listen to Radio", "VirtualRadioSimple", false},
        {"Play Game", "VirtualGameConsole", false},
    };
    for (const auto* id : {"900", "one", "1000", "1100", "1100i", "600", "650", "665", "700"}) {
        harmony::app::RemoteView view;
        view.setRemoteProfile(QString::fromLatin1(id));
        view.setActivities(activities);
        view.setSoftButtons(QStringLiteral("Watch TV"),
                            {"Live TV", "Guide", "Info", "Exit", "Menu", "Back"});
        for (const QString& mode : view.screenModes()) {
            view.selectScreenMode(mode);
            view.resize(view.sizeHint());
            const QString path = directory + QLatin1Char('/') + QString::fromLatin1(id) +
                                 QLatin1Char('-') + mode + QStringLiteral(".png");
            EXPECT_TRUE(view.grab().save(path)) << id << mode.toStdString();
        }
    }
}

TEST(RemoteView, ScreenPreviewFollowsModeForEveryTouchRemote) {
    for (const auto* id : {"900", "one", "1000", "1100", "1100i"}) {
        SCOPED_TRACE(id);
        harmony::app::RemoteView view;
        view.setRemoteProfile(id);
        view.resize(900, 900);
        view.setActivities({{"TV", "VirtualTelevisionN", false}, {"Music", "VirtualCd", false}, {"Game", "VirtualGameConsole", false}});
        view.setSoftButtons("Commands", {"One", "Two", "Three", "Four", "Five", "Six"});
        view.selectScreenMode("activities");
        const auto activities = view.screenPreview({320, 240}).toImage();
        view.selectScreenMode("command");
        const auto commands = view.screenPreview({320, 240}).toImage();
        EXPECT_FALSE(activities.isNull());
        EXPECT_NE(activities, commands);
    }
}

TEST(RemoteView, FreeActivityKeyHighlightsOnPressAndEditsOnRelease) {
    harmony::app::RemoteView view;
    view.setRemoteProfile("900");
    view.resize(view.sizeHint());
    view.setMode(harmony::app::RemoteView::Mode::Activity);
    view.setActivityKeys({});
    const auto before = view.grab().toImage();
    int clicks = 0;
    QObject::connect(&view, &harmony::app::RemoteView::keyClicked, [&](const QString&) { ++clicks; });
    const QPointF position(85, 750);
    QMouseEvent press(QEvent::MouseButtonPress, position, position, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    EXPECT_EQ(clicks, 0);
    EXPECT_NE(view.grab().toImage(), before);
    QMouseEvent release(QEvent::MouseButtonRelease, position, position, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &release);
    EXPECT_EQ(clicks, 1);
}

TEST(RemoteView, DevicesButtonShowsThreeDevicesPerPageOn900) {
    using harmony::app::RemoteView;
    RemoteView view;
    view.setRemoteProfile("900");
    view.resize(view.sizeHint());
    view.setDeviceNames({"TV", "Receiver", "Player", "Console"});
    ASSERT_TRUE(view.screenModes().contains("devices"));
    click(view, {285, 420}); // Devices
    EXPECT_EQ(view.screen(), RemoteView::Screen::Devices);
    view.grab();
    int device = -1, activity = -1, command = -1;
    QObject::connect(&view, &RemoteView::deviceClicked, [&](int index) { device = index; });
    QObject::connect(&view, &RemoteView::activityClicked, [&](int index) { activity = index; });
    QObject::connect(&view, &RemoteView::softButtonClicked, [&](int index) { command = index; });
    const QRectF screen = profileScreen("900");
    const QPointF first(screen.center().x(), screen.y() + screen.height() * 0.28);
    click(view, first);
    EXPECT_EQ(device, 0);
    click(view, {359, 301}); // следующая страница
    view.grab();
    click(view, first);
    EXPECT_EQ(device, 3);
    EXPECT_EQ(activity, -1);
    EXPECT_EQ(command, -1);
    view.setDeviceNames({"TV"});
    view.grab();
    click(view, first);
    EXPECT_EQ(device, 0); // страница сброшена, когда список стал короче
}

TEST(RemotePanel, PreviewOmitsNotSentEntriesAndKeepsConfigurationIndices) {
    harmony::model::RemoteConfig config;
    harmony::model::Device hidden, visible;
    hidden.id = "hidden"; hidden.label = "Hidden"; hidden.notSent = true;
    visible.id = "visible"; visible.label = "Visible";
    config.devices = {hidden, visible};
    harmony::model::Activity disabled, dependent, sent;
    disabled.id = "disabled"; disabled.label = "Disabled"; disabled.notSent = true;
    dependent.id = "dependent"; dependent.label = "Dependent"; dependent.powerOn = {hidden.id};
    sent.id = "sent"; sent.label = "Sent"; sent.powerOn = {visible.id};
    config.activities = {disabled, dependent, sent};
    harmony::app::RemotePanel panel;
    panel.setConfig(&config, "900");
    auto* view = panel.findChild<harmony::app::RemoteView*>();
    ASSERT_NE(view, nullptr);
    view->resize(view->sizeHint());
    int activity = -1, device = -1;
    QObject::connect(&panel, &harmony::app::RemotePanel::activityClicked, [&](int index) { activity = index; });
    QObject::connect(&panel, &harmony::app::RemotePanel::deviceClicked, [&](int index) { device = index; });
    view->selectScreenMode("activities");
    view->grab();
    // Панель кладёт пульт в QScrollArea: окончательный размер виджета известен после отрисовки.
    const QRectF screen = profileScreen("900");
    const QPointF first = widgetPoint(*view, {screen.center().x(), screen.y() + screen.height() * 0.28});
    const QPointF second = widgetPoint(*view, {screen.center().x(), screen.y() + screen.height() * 0.49});
    click(*view, first);
    EXPECT_EQ(activity, 2);
    activity = -1;
    click(*view, second);
    EXPECT_EQ(activity, -1);
    view->selectScreenMode("devices");
    view->grab();
    click(*view, first);
    EXPECT_EQ(device, 1);
    device = -1;
    click(*view, second);
    EXPECT_EQ(device, -1);
}

TEST(RemotePanel, MissingProfileMessageReplacesPreviewAndDisappearsForAKnownProfile) {
    harmony::model::RemoteConfig config;
    harmony::app::RemotePanel panel;
    panel.resize(500,650);
    panel.setConfig(&config, "missing-profile");
    panel.show();
    QApplication::processEvents();
    auto* missing=panel.findChild<QLabel*>("missingRemoteProfile");
    auto* preview=panel.findChild<QWidget*>("remoteScroll");
    ASSERT_NE(missing,nullptr);
    ASSERT_NE(preview,nullptr);
    EXPECT_TRUE(missing->isVisible());
    EXPECT_FALSE(preview->isVisible());
    panel.setConfig(&config, "900");
    QApplication::processEvents();
    EXPECT_FALSE(missing->isVisible());
    EXPECT_TRUE(preview->isVisible());
}

TEST(ModernShell, ResizesHidesAndRestoresTheRemotePanel) {
    QTemporaryDir settingsDirectory;
    ASSERT_TRUE(settingsDirectory.isValid());
    const QStringList keys{"window/configSplitter","window/remotePanelWidth","window/remotePanelVisible"};
    struct RestoreSettings {
        QString organization=QCoreApplication::organizationName();
        QString application=QCoreApplication::applicationName();
        QSettings::Format format=QSettings::defaultFormat();
        QMap<QString,QVariant> values;
        ~RestoreSettings() {
            QSettings settings;
            for(auto it=values.cbegin();it!=values.cend();++it)
                if(it.value().isValid())settings.setValue(it.key(),it.value());else settings.remove(it.key());
            QCoreApplication::setOrganizationName(organization);
            QCoreApplication::setApplicationName(application);
            QSettings::setDefaultFormat(format);
        }
    } restore;
    QCoreApplication::setOrganizationName("HarmonyGuiTests");
    QCoreApplication::setApplicationName("RemotePanelState");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDirectory.path());
    QSettings settings;
    for(const auto& key:keys){restore.values[key]=settings.value(key);settings.remove(key);}
    harmony::app::ModernShell shell;
    shell.resize(1400,750);
    shell.addPage("test","Test",new QWidget);
    shell.setSidePanel(new QWidget);
    shell.show();
    QApplication::processEvents();
    auto* splitter=shell.findChild<QSplitter*>("mainSplitter");
    auto* toggle=shell.findChild<QToolButton*>("remotePanelToggle");
    ASSERT_NE(splitter,nullptr);
    ASSERT_NE(toggle,nullptr);
    ASSERT_EQ(splitter->count(),3);
    EXPECT_TRUE(shell.isSidePanelVisible());
    splitter->setSizes({250,750,350});
    splitter->splitterMoved(1050,2);
    const int width=splitter->sizes()[2];
    EXPECT_GT(width,180);
    toggle->click();
    EXPECT_FALSE(shell.isSidePanelVisible());
    EXPECT_FALSE(toggle->isChecked());
    toggle->click();
    EXPECT_TRUE(shell.isSidePanelVisible());
    EXPECT_NEAR(splitter->sizes()[2],width,4);
    toggle->click();
    settings.sync();
    EXPECT_EQ(settings.status(),QSettings::NoError);
    harmony::app::ModernShell reopened;
    reopened.resize(1400,750);
    reopened.addPage("test","Test",new QWidget);
    reopened.setSidePanel(new QWidget);
    reopened.show();
    QApplication::processEvents();
    EXPECT_FALSE(reopened.isSidePanelVisible());
    reopened.setSidePanelVisible(true);
    auto* restored=reopened.findChild<QSplitter*>("mainSplitter");
    ASSERT_NE(restored,nullptr);
    EXPECT_NEAR(restored->sizes()[2],width,4);
    restored->setSizes({250,1100,0});
    restored->splitterMoved(restored->width(),2);
    EXPECT_FALSE(reopened.isSidePanelVisible());
    reopened.setSidePanelVisible(true);
    EXPECT_TRUE(reopened.isSidePanelVisible());
}

TEST(Theme, DeviceColoursRepeatAndNoDeviceIsGrey) {
    // Цвет устройства один на пульте, в легенде и в списках: палитра в theme, восемь цветов по кругу.
    namespace theme = harmony::app::theme;
    QSet<QRgb> distinct;
    for (int i = 0; i < 8; ++i) {
        const QColor colour = theme::deviceColor(i);
        ASSERT_TRUE(colour.isValid()) << i;
        distinct.insert(colour.rgb());
    }
    EXPECT_EQ(distinct.size(), 8);
    EXPECT_EQ(theme::deviceColor(8), theme::deviceColor(0));
    EXPECT_EQ(theme::deviceColor(13), theme::deviceColor(5));
    EXPECT_TRUE(theme::deviceColor(-1).isValid());
    EXPECT_FALSE(distinct.contains(theme::deviceColor(-1).rgb()));
}

TEST(Theme, RemoteOverlayColoursAreTranslucent) {
    // Подсветка кнопок ложится на фото пульта: заливки и пунктир полупрозрачные.
    namespace theme = harmony::app::theme;
    for (const auto role : {theme::Color::RemoteDimFill, theme::Color::RemoteDimLine, theme::Color::RemoteBrokenFill,
                            theme::Color::RemoteSelectedGlow, theme::Color::RemoteHoverLine}) {
        const QColor colour = theme::color(role);
        ASSERT_TRUE(colour.isValid());
        EXPECT_GT(colour.alpha(), 0);
        EXPECT_LT(colour.alpha(), 255);
    }
    EXPECT_EQ(theme::color(theme::Color::RemoteBroken).alpha(), 255);
}

TEST(Labels, ModifierNamesHaveNoTrailingPlus) {
    namespace labels = harmony::app::labels;
    for (const auto key : {Qt::ControlModifier, Qt::ShiftModifier, Qt::AltModifier}) {
        const QString text = labels::modifier(key);
        EXPECT_FALSE(text.isEmpty());
        EXPECT_FALSE(text.endsWith('+')) << text.toStdString();
        EXPECT_FALSE(text.endsWith('A')) << text.toStdString();
    }
#ifndef Q_OS_MACOS
    EXPECT_EQ(labels::modifier(Qt::ControlModifier), "Ctrl");
    EXPECT_EQ(labels::modifier(Qt::ShiftModifier), "Shift");
    EXPECT_EQ(labels::modifier(Qt::AltModifier), "Alt");
#endif
}
