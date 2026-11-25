#include "../app/src/remoteResources.h"
#include "profileNames.h"
#include "../app/src/modern/remoteView.h"
#include <QBuffer>
#include <QDir>
#include <QApplication>
#include <QMouseEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include "harmony/core/remoteProfile.h"
#include "harmony/core/configDocument.h"
#include "harmony/core/modelInfo.h"
#include "harmony/core/writePolicy.h"
#include <algorithm>
#include <functional>
#include <ranges>

namespace {
namespace resources = harmony::app::remoteResources;
namespace paths = harmony::testpaths;
/// Каждый тест работает со своими временными папками ресурсов.
class RemoteResourcesTest : public testing::Test {
protected:
    QTemporaryDir dir;
    QTemporaryDir builtin;
    QString root;
    void SetUp() override {
        ASSERT_TRUE(dir.isValid());
        ASSERT_TRUE(builtin.isValid());
        root = builtin.path();
        ASSERT_TRUE(resources::reload(root).isEmpty());
        ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/900"));
    }
    void TearDown() override {
        resources::reload(builtin.path());
    }
    void write(const QString& relative, const QByteArray& bytes) {
        const QString path = dir.path() + "/" + relative;
        ASSERT_TRUE(QDir().mkpath(QFileInfo(path).path()));
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(bytes), bytes.size());
    }
};

/// profile.json встроенной модели как объект.
QJsonObject remoteOf(const QString& model) {
    return QJsonDocument::fromJson(resources::data(paths::builtinProfile(model))).object();
}
QByteArray toBytes(const QJsonObject& object) { return QJsonDocument(object).toJson(); }
/// Профиль 900 под другим id: папка со своей photo.png (размер тот же) и одним файлом профиля.
QJsonObject customRemote(const QString& id) {
    auto remote = remoteOf("900");
    remote["id"] = id;
    remote["name"] = id;
    return remote;
}
/// Зоны корпуса пульта из снимка ресурсов.
QJsonArray zonesOf(const QString& model) {
    return resources::remote(model).value("hardware").toObject().value("zones").toArray();
}

TEST_F(RemoteResourcesTest, OverrideReloadRemovalAndInvalidRollback) {
    const auto original = zonesOf("900");
    auto remote = remoteOf("900");
    const auto originalPhoto = resources::data(paths::builtinPhoto("900"));
    harmony::app::RemoteView view;
    view.setRemoteProfile("900");
    auto zones = original;
    auto zone = zones[0].toObject();
    zone["id"] = "CustomKey";
    zone["kind"] = "Key";
    zone["rect"] = QJsonArray{1, 2, 30, 40};
    zones[0] = zone;
    auto hardware = remote["hardware"].toObject();
    hardware["zones"] = zones;
    remote["hardware"] = hardware;
    write(paths::profileKey("900"), toBytes(remote));
    const auto modified = zones;
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    view.setRemoteProfile("900");
    view.resize(view.sizeHint());
    QString clicked;
    QObject::connect(&view, &harmony::app::RemoteView::keyClicked, [&clicked](const QString& id) { clicked = id; });
    const QPointF point(16, 22);
    QMouseEvent event(QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &event);
    QMouseEvent release(QEvent::MouseButtonRelease, event.position(), event.globalPosition(), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &release);
    EXPECT_EQ(clicked, "CustomKey");
    EXPECT_TRUE(harmony::app::RemoteView::hardKeyIds("900").contains("CustomKey"));
    EXPECT_EQ(resources::data(paths::builtinPhoto("900")), originalPhoto);
    const auto version = resources::revision();
    write(paths::profileKey("900"), "{broken");
    EXPECT_TRUE(resources::reload(root).contains(paths::profileKey("900")));
    EXPECT_EQ(resources::revision(), version);
    EXPECT_EQ(zonesOf("900"), modified);
    zone["rect"] = QJsonArray{0, 0, 9999, 9999};
    zones[0] = zone;
    hardware["zones"] = zones;
    remote["hardware"] = hardware;
    write(paths::profileKey("900"), toBytes(remote));
    EXPECT_TRUE(resources::reload(root).contains("CustomKey"));
    EXPECT_EQ(zonesOf("900"), modified);
    ASSERT_TRUE(QFile::remove(dir.path() + "/" + paths::profileKey("900")));
    ASSERT_TRUE(resources::reload(root).isEmpty());
    view.setRemoteProfile("900");
    EXPECT_EQ(zonesOf("900"), original);
    EXPECT_FALSE(harmony::app::RemoteView::hardKeyIds("900").contains("CustomKey"));
    clicked.clear();
    QApplication::sendEvent(&view, &event);
    QApplication::sendEvent(&view, &release);
    EXPECT_NE(clicked, "CustomKey");
}

TEST_F(RemoteResourcesTest, PhotoChangesAndInvalidPhotoRollback) {
    const QString path = paths::builtinPhoto("900");
    auto image = QImage::fromData(resources::data(path));
    image.fill(Qt::red);
    ASSERT_TRUE(image.save(dir.path() + "/" + paths::photoKey("900")));
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_EQ(QImage::fromData(resources::data(path)).pixelColor(10, 10), QColor(Qt::red));
    const auto previousImage = resources::data(path);
    write(paths::photoKey("900"), "broken image");
    EXPECT_TRUE(resources::reload(root).contains(paths::photoKey("900")));
    EXPECT_EQ(resources::data(path), previousImage);
    QImage wrong(10, 10, QImage::Format_RGB32);
    wrong.fill(Qt::blue);
    ASSERT_TRUE(wrong.save(dir.path() + "/" + paths::photoKey("900")));
    EXPECT_FALSE(resources::reload(root).isEmpty());
    EXPECT_EQ(resources::data(path), previousImage);
}
TEST_F(RemoteResourcesTest, OldSchemaIsMigratedKeepingUserEdits) {
    // profile.json от прошлой версии программы: старые connection и style, но со своим цветом плиток.
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/600"));
    auto remote = remoteOf("600");
    remote["connection"] = QJsonObject{{"transport", "usb-hid"}, {"driver", "hid"}, {"configFormat", "unsupported"}};
    auto screens = remote["screens"].toArray();
    auto screen = screens[0].toObject();
    screen.remove("compact");
    screen["style"] = "h600";
    screen["background"] = QJsonObject{{"color", "#0b3442"}};
    screens[0] = screen;
    remote["screens"] = screens;
    remote["tiles"] = QJsonObject{{"color", "#aa0000"}, {"border", "#ffffff"}};
    write(paths::profileKey("600"), toBytes(remote));
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    ASSERT_EQ(resources::migratedByReload().size(), 1);
    const auto* profile = harmony::remoteProfile("600");
    ASSERT_NE(profile, nullptr);
    // W3: driver — значение пользователя, оно приоритетнее каталога; в старой схеме имена
    // другие, поэтому `hid` нормализуется. Поля format, configFormat и transport убираются:
    // формат определяется семейством каталога и данными файла, профилем он не задаётся.
    EXPECT_EQ(profile->driver, "usb-hid");

    // Правка пользователя сохранилась, style стал данными.
    auto migrated = remote;
    ASSERT_TRUE(resources::migrateRemote(migrated, "600"));
    const auto connection = migrated["connection"].toObject();
    EXPECT_EQ(connection.value("driver").toString(), "usb-hid");
    EXPECT_FALSE(connection.contains("format"));
    EXPECT_FALSE(connection.contains("configFormat"));
    EXPECT_FALSE(connection.contains("transport"));
    EXPECT_EQ(migrated["tiles"].toObject().value("color").toString(), "#aa0000");
    const auto first = migrated["screens"].toArray()[0].toObject();
    EXPECT_FALSE(first.contains("style"));
    EXPECT_FALSE(first.contains("compact"));
    // Шапка 600 теперь на картинке фона, надписи на ней тёмные.
    EXPECT_EQ(first.value("background").toObject().value("image").toString(), "sprites/screen_600.png");
    EXPECT_EQ(first.value("panel").toObject().value("text").toString(), "#111111");
    EXPECT_FALSE(resources::migrateRemote(migrated, "600")); // второй раз менять нечего
}

TEST_F(RemoteResourcesTest, CompactScreenBecomesBackgroundPicture) {
    // Прошлая версия: compact и картинка фона без шапки; своя картинка пользователя не заменяется.
    auto remote = remoteOf("650");
    auto screens = remote["screens"].toArray();
    for (int i = 0; i < screens.size(); ++i) {
        auto screen = screens[i].toObject();
        screen["compact"] = true;
        screen.remove("panel");
        screen["background"] = QJsonObject{{"image", i == 0 ? "sprites/background_650.png" : "sprites/mine.png"}};
        screens[i] = screen;
    }
    remote["screens"] = screens;
    ASSERT_TRUE(resources::migrateRemote(remote, "650"));
    const auto migrated = remote["screens"].toArray();
    EXPECT_EQ(migrated[0].toObject()["background"].toObject()["image"].toString(), "sprites/screen_650.png");
    EXPECT_EQ(migrated[1].toObject()["background"].toObject()["image"].toString(), "sprites/mine.png");
    for (const auto& value : migrated) EXPECT_FALSE(value.toObject().contains("compact"));
}

TEST_F(RemoteResourcesTest, ScreenTouchMovesToRemoteCapabilities) {
    // Прошлая схема: сенсорность у каждого экрана, у пульта её нет.
    auto remote = remoteOf("900");
    remote.remove("capabilities");
    auto screens = remote["screens"].toArray();
    for (int i = 0; i < screens.size(); ++i) {
        auto screen = screens[i].toObject();
        screen["touch"] = true;
        screens[i] = screen;
    }
    remote["screens"] = screens;
    ASSERT_TRUE(resources::migrateRemote(remote, "900"));
    EXPECT_TRUE(remote["capabilities"].toObject().value("touch").toBool());
    for (const auto& value : remote["screens"].toArray()) EXPECT_FALSE(value.toObject().contains("touch"));
    // Значение пульта главнее: его не перезаписывают экраны.
    auto touchless = remoteOf("600");
    auto list = touchless["screens"].toArray();
    auto first = list[0].toObject();
    first["touch"] = true;
    list[0] = first;
    touchless["screens"] = list;
    ASSERT_TRUE(resources::migrateRemote(touchless, "600"));
    EXPECT_FALSE(touchless["capabilities"].toObject().value("touch").toBool());
    // Штатные файлы уже в новой схеме.
    for (const auto& profile : harmony::bundledRemoteProfiles) {
        auto bundled = remoteOf(QString::fromStdString(profile.id));
        EXPECT_FALSE(resources::migrateRemote(bundled, QString::fromStdString(profile.id))) << profile.id;
    }
}

TEST_F(RemoteResourcesTest, ReadRemoteForEditingUsesDiskTextAndCurrentSchema) {
    // Нет файла в папке — встроенное описание.
    const auto builtinFile = resources::readRemote("900", dir.path());
    EXPECT_TRUE(builtinFile.error.isEmpty());
    EXPECT_EQ(builtinFile.text, resources::data(paths::builtinProfile("900")));
    EXPECT_EQ(builtinFile.current, builtinFile.text);
    EXPECT_EQ(builtinFile.remote.value("id").toString(), "900");

    // Файл старой схемы: текст как на диске, описание и current — в новой схеме.
    // W3: driver остаётся данными пользователя (приоритет над каталогом), имена прошлой
    // схемы нормализуются, поля format и transport больше нет.
    auto remote = remoteOf("900");
    remote["connection"] = QJsonObject{{"driver", "harmony900"}, {"configFormat", "harmony900"}};
    write(paths::profileKey("900"), toBytes(remote));
    const auto file = resources::readRemote("900", dir.path());
    EXPECT_EQ(file.path, paths::profileKey("900"));
    EXPECT_EQ(file.text, toBytes(remote));
    EXPECT_EQ(file.remote["connection"].toObject().value("driver").toString(), "usb-net");
    EXPECT_FALSE(QJsonDocument::fromJson(file.current).object()["connection"].toObject().contains("format"));
    EXPECT_EQ(resources::currentSchema(file.text, "900"), file.current);

    // Сломанный файл: ошибка с путём, описание пустое.
    write(paths::profileKey("900"), "{broken");
    const auto broken = resources::readRemote("900", dir.path());
    EXPECT_TRUE(broken.remote.isEmpty());
    EXPECT_TRUE(broken.error.contains(paths::profileKey("900")));
}

TEST_F(RemoteResourcesTest, DiscoverNewModelAndPersistProject) {
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/custom-600"));
    auto remote = remoteOf("600");
    remote["id"] = "custom-600";
    remote["name"] = "Custom 600";
    remote["connection"] = QJsonObject{{"driver", "usb-hid"}};
    write(paths::profileKey("custom-600"), toBytes(remote));
    write(paths::photoKey("custom-600"), resources::data(paths::builtinPhoto("600")));
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    const auto* added = harmony::remoteProfile("custom-600");
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(added->driver, "usb-hid");
    harmony::app::RemoteView view;
    view.setRemoteProfile("custom-600");
    EXPECT_FALSE(view.screenModes().isEmpty());
    EXPECT_FALSE(harmony::app::RemoteView::hardKeyIds("custom-600").isEmpty());
    auto document = harmony::ConfigDocument::createNew("Custom remote");
    ASSERT_TRUE(document.setTargetRemote("custom-600"));
    EXPECT_EQ(document.targetRemote(), "custom-600");
    EXPECT_TRUE(document.targetModel().custom);
    EXPECT_TRUE(document.targetModel().hasProfile);
    EXPECT_FALSE(document.targetModel().known);
    EXPECT_EQ(document.targetModel().name, "Custom 600");
    EXPECT_EQ(document.targetModel().link, harmony::LinkKind::UsbHid)
        << "своей модели нет в каталоге: связь из profile.json";
    EXPECT_FALSE(document.toEzHex(true));
    const auto saved = document.toEzHex(false);
    ASSERT_TRUE(saved);
    auto reopened = harmony::ConfigDocument::fromEzHex(*saved, "Reopened");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->targetRemote(), "custom-600");
    remote["connection"] = QJsonObject{{"driver", "unknown-driver"}};
    write(paths::profileKey("custom-600"), toBytes(remote));
    // Ошибка в пользовательской модели не блокирует загрузку: модель пропускается.
    EXPECT_TRUE(resources::reload(root).isEmpty());
    EXPECT_EQ(resources::skipped().size(), 1);
    EXPECT_EQ(harmony::remoteProfile("custom-600"), nullptr);
    EXPECT_NE(harmony::remoteProfile("900"), nullptr);
    root = builtin.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_EQ(harmony::remoteProfile("custom-600"), nullptr);
    EXPECT_EQ(reopened->targetRemote(), "custom-600");
    EXPECT_FALSE(reopened->toEzHex(true));
    EXPECT_FALSE(reopened->targetModel().hasProfile);
    EXPECT_FALSE(reopened->targetModel().custom);
    EXPECT_EQ(reopened->targetModel().name, "custom-600");
}

// W3 (решено владельцем 22.11): способ подключения — значение из profile.json, если оно задано;
// без поля — каталог. Право записи по-прежнему решает каталог (OD9).
TEST_F(RemoteResourcesTest, ConnectionFieldDecidesTransportAndNotWriting) {
    // Своя модель вне каталога: каталог её не знает, работает поле из файла.
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/my-650"));
    auto custom = remoteOf("650");
    custom["id"] = "my-650";
    custom["name"] = "My 650";
    custom["connection"] = QJsonObject{{"driver", "usb-net"}};
    write(paths::profileKey("my-650"), toBytes(custom));
    write(paths::photoKey("my-650"), resources::data(paths::builtinPhoto("650")));
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    ASSERT_NE(harmony::remoteProfile("my-650"), nullptr);
    EXPECT_EQ(harmony::remoteProfile("my-650")->driver, "usb-net");
    EXPECT_TRUE(harmony::usesUsbNet("my-650")) << "модели нет в каталоге: значение из файла";
    EXPECT_EQ(harmony::canWriteTo(harmony::modelInfo("my-650")).verdict, harmony::WriteVerdict::UnknownModel)
        << "файл не может разрешить запись модели, которой нет в каталоге";

    // Модель каталога: заданное в файле значение важнее каталога, запись решает каталог.
    auto catalog = remoteOf("650");
    catalog["connection"] = QJsonObject{{"driver", "usb-net"}};
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/650"));
    write(paths::profileKey("650"), toBytes(catalog));
    write(paths::photoKey("650"), resources::data(paths::builtinPhoto("650")));
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_TRUE(harmony::usesUsbNet("650")) << "значение файла важнее каталога";
    EXPECT_EQ(harmony::canWriteTo(harmony::modelInfo("650")).verdict, harmony::WriteVerdict::Experimental)
        << "650 не подтверждена записью, профиль на запись не влияет";
    // Значение штатного profile.json — тоже данные пользователя: 700 задаёт связь своим файлом.
    EXPECT_EQ(harmony::remoteProfile("700")->driver, "usb-hid");
    // Модель без своего profile.json: связь только из каталога.
    EXPECT_EQ(harmony::remoteProfile("525"), nullptr);
    EXPECT_FALSE(harmony::usesUsbNet("525")) << "525 — USB HID из каталога";
}

TEST_F(RemoteResourcesTest, ModelInfoUsesOnlyItsOwnProfileAfterReload) {
    EXPECT_TRUE(harmony::modelInfo("600").hasProfile);
    EXPECT_FALSE(harmony::modelInfo("600i").hasProfile);
    EXPECT_FALSE(harmony::modelInfo("525").hasProfile);
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/600i"));
    auto remote = remoteOf("600");
    remote["id"] = "600i";
    remote["name"] = "Copied profile";
    write(paths::profileKey("600i"), toBytes(remote));
    write(paths::photoKey("600i"), resources::data(paths::builtinPhoto("600")));
    ASSERT_TRUE(resources::reload(dir.path()).isEmpty());
    const auto info = harmony::modelInfo("600i");
    EXPECT_TRUE(info.hasProfile);
    EXPECT_TRUE(info.known);
    EXPECT_FALSE(info.custom);
    EXPECT_EQ(info.name, "Harmony 600i");
    EXPECT_EQ(harmony::profileFor("600i").folder, "remotes/600i");
    ASSERT_TRUE(resources::reload(builtin.path()).isEmpty());
    EXPECT_FALSE(harmony::modelInfo("600i").hasProfile);
}

TEST_F(RemoteResourcesTest, GridScreenCellsGetRectsFromColRowAndSpans) {
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/gridmodel"));
    auto remote = remoteOf("600");
    remote["id"] = "gridmodel";
    remote["name"] = "Grid model";
    remote["screens"] = QJsonArray{QJsonObject{
        {"id", "Menu"}, {"role", "command"}, {"ref", QJsonArray{100, 100}},
        {"grid", QJsonObject{{"cols", 2}, {"rows", 2}, {"gap", 0}}},
        {"cells", QJsonArray{QJsonObject{{"id", "A"}, {"col", 0}, {"row", 0}, {"colSpan", 2}},
                             QJsonObject{{"id", "B"}, {"col", 1}, {"row", 1}}}}}};
    write(paths::profileKey("gridmodel"), toBytes(remote));
    write(paths::photoKey("gridmodel"), resources::data(paths::builtinPhoto("600")));
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    const auto screens = resources::remote("gridmodel").value("screens").toArray();
    ASSERT_EQ(screens.size(), 1);
    const auto cells = screens[0].toObject()["cells"].toArray();
    ASSERT_EQ(cells.size(), 2);
    EXPECT_EQ(cells[0].toObject()["rect"].toArray(), (QJsonArray{0, 0, 100, 50}));
    EXPECT_EQ(cells[1].toObject()["rect"].toArray(), (QJsonArray{50, 50, 50, 50}));
    EXPECT_EQ(cells[1].toObject()["kind"].toString(), "ScreenSlot");
    // На диске файл не меняется: rect достраивается только в памяти.
    const auto onDisk = QJsonDocument::fromJson(resources::data(paths::builtinProfile("gridmodel"))).object();
    EXPECT_FALSE(onDisk.value("screens").toArray()[0].toObject().value("cells").toArray()[0].toObject()
                     .contains("rect"));
}

TEST(RemoteResources, GridCellsAreLaidOutInsideTileArea) {
    // Зона плиток 80×60 со сдвигом (10, 20), зазор 0: плитки 40×30 внутри неё, а не на весь экран.
    const QJsonObject screen{{"ref", QJsonArray{100, 100}},
                             {"grid", QJsonObject{{"cols", 2}, {"rows", 2}, {"gap", 0}, {"area", QJsonArray{10, 20, 80, 60}}}},
                             {"cells", QJsonArray{QJsonObject{{"id", "A"}, {"col", 0}, {"row", 0}},
                                                  QJsonObject{{"id", "B"}, {"col", 1}, {"row", 1}}}}};
    const auto cells = resources::normalizeScreen(screen)["cells"].toArray();
    EXPECT_EQ(cells[0].toObject()["rect"].toArray(), (QJsonArray{10, 20, 40, 30}));
    EXPECT_EQ(cells[1].toObject()["rect"].toArray(), (QJsonArray{50, 50, 40, 30}));
}

TEST_F(RemoteResourcesTest, LeftoverFilesOfOldSchemaAreIgnored) {
    // Файлы прошлых версий (zones.json, screens.json, layouts.json) не читаются:
    // даже испорченные, они не мешают загрузке и не попадают в выгрузку.
    write("layouts.json", "{broken");
    write("remotes/900/zones.json", "[]");
    write("remotes/900/screens.json", "{}");
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_FALSE(zonesOf("900").isEmpty());
    EXPECT_TRUE(resources::data(":/res/remotes/900/zones.json").isEmpty());
    for (const auto& file : resources::exportFiles({}))
        if (file.endsWith(".json")) { EXPECT_TRUE(file.endsWith("/" + resources::kProfileFile)) << file.toStdString(); }
}

TEST_F(RemoteResourcesTest, FolderWithOnlyPhotoIsListedAndGetsStub) {
    // Папка с одним photo.png: редактор зон показывает её и создаёт заготовку profile.json.
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/fresh"));
    QImage photo(120, 300, QImage::Format_ARGB32);
    photo.fill(Qt::transparent);
    QByteArray png;
    QBuffer buffer(&png);
    ASSERT_TRUE(buffer.open(QIODevice::WriteOnly));
    ASSERT_TRUE(photo.save(&buffer, "PNG"));
    write(paths::photoKey("fresh"), png);
    root = dir.path();
    EXPECT_TRUE(resources::modelIds(root).contains("fresh"));
    EXPECT_TRUE(resources::createStub("fresh", root).isEmpty());
    ASSERT_TRUE(resources::reload(root).isEmpty());
    const auto remote = resources::remote("fresh");
    EXPECT_EQ(remote.value("id").toString(), "fresh");
    EXPECT_EQ(remote.value("hardware").toObject().value("size").toArray().at(0).toInt(), 120);
    EXPECT_TRUE(remote.value("hardware").toObject().value("zones").toArray().isEmpty());
    EXPECT_EQ(remote.value("screens").toArray().size(), 1);
    EXPECT_NE(harmony::remoteProfile("fresh"), nullptr);
    // Существующий profile.json заготовка не трогает; папка без photo.png — ошибка.
    const auto before = resources::readRemote("fresh", root).text;
    EXPECT_TRUE(resources::createStub("fresh", root).isEmpty());
    EXPECT_EQ(resources::readRemote("fresh", root).text, before);
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/empty"));
    EXPECT_FALSE(resources::createStub("empty", root).isEmpty());
}

/// Экран 100×200 с сеткой 1×2 и зонами фона из background.
QJsonObject zonedScreen(const QJsonObject& background, const QJsonObject& grid = {{"cols", 1}, {"rows", 2}, {"gap", 0}}) {
    return {{"id", "devices"}, {"ref", QJsonArray{100, 200}}, {"grid", grid}, {"background", background},
            {"cells", QJsonArray{QJsonObject{{"col", 0}, {"row", 0}}, QJsonObject{{"col", 0}, {"row", 1}}}}};
}

TEST(RemoteResources, ScreenZonesParseHeaderBodyFooter) {
    const auto none = resources::screenZones(zonedScreen({{"color", "#000000"}}));
    EXPECT_FALSE(none.any);
    const auto zones = resources::screenZones(zonedScreen(
        {{"header", QJsonObject{{"height", 30}, {"color", "#112233"}}},
         {"footer", QJsonObject{{"height", 20}}}}));
    ASSERT_TRUE(zones.any);
    EXPECT_EQ(zones.header, QRectF(0, 0, 100, 30));
    EXPECT_EQ(zones.body, QRectF(0, 30, 100, 150));
    EXPECT_EQ(zones.footer, QRectF(0, 180, 100, 20));
    EXPECT_EQ(zones.headerColor, QColor("#112233"));
    EXPECT_FALSE(zones.footerColor.isValid()); // без color зона прозрачна
    // Только шапка: тело — всё остальное.
    const auto headerOnly = resources::screenZones(zonedScreen({{"header", QJsonObject{{"height", 50}}}}));
    ASSERT_TRUE(headerOnly.any);
    EXPECT_EQ(headerOnly.body, QRectF(0, 50, 100, 150));
    // Шапка и подвал не помещаются: зон нет.
    EXPECT_FALSE(resources::screenZones(zonedScreen(
        {{"header", QJsonObject{{"height", 120}}}, {"footer", QJsonObject{{"height", 80}}}})).any);
}

TEST(RemoteResources, TilesDefaultToBodyZone) {
    const QJsonObject background{{"header", QJsonObject{{"height", 40}}}, {"footer", QJsonObject{{"height", 20}}}};
    const auto screen = resources::normalizeScreen(zonedScreen(background), {});
    const auto cells = screen.value("cells").toArray();
    // Тело 100×140 от y=40, сетка 1×2 без зазоров: плитки по 70 высотой.
    EXPECT_EQ(cells.at(0).toObject().value("rect").toArray().at(1).toDouble(), 40);
    EXPECT_EQ(cells.at(1).toObject().value("rect").toArray().at(1).toDouble(), 110);
    // Своя grid.area главнее общей, но не выходит за тело: (0,0,100,100) обрезается до (0,40,100,60).
    const auto own = resources::normalizeScreen(
        zonedScreen(background, {{"cols", 1}, {"rows", 2}, {"gap", 0}, {"area", QJsonArray{0, 0, 100, 100}}}), {});
    EXPECT_EQ(own.value("cells").toArray().at(0).toObject().value("rect").toArray().at(1).toDouble(), 40);
    EXPECT_EQ(own.value("cells").toArray().at(1).toObject().value("rect").toArray().at(1).toDouble(), 70);
    // Общая tiles.area тоже обрезается телом.
    const auto common = resources::normalizeScreen(zonedScreen(background), QJsonArray{0, 0, 100, 200});
    EXPECT_EQ(common.value("cells").toArray().at(1).toObject().value("rect").toArray().at(1).toDouble(), 110);
}

TEST_F(RemoteResourcesTest, InvalidBackgroundZonesRejectModel) {
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/zoned"));
    auto remote = remoteOf("600");
    remote["id"] = "zoned";
    auto screens = remote["screens"].toArray();
    auto screen = screens.at(0).toObject();
    screen["background"] = QJsonObject{{"header", QJsonObject{{"height", -5}}}};
    screens[0] = screen;
    remote["screens"] = screens;
    write(paths::profileKey("zoned"), toBytes(remote));
    write(paths::photoKey("zoned"), resources::data(paths::builtinPhoto("600")));
    root = dir.path();
    EXPECT_TRUE(resources::reload(root).isEmpty());
    EXPECT_EQ(harmony::remoteProfile("zoned"), nullptr);
    ASSERT_EQ(resources::skipped().size(), 1);
    EXPECT_TRUE(resources::skipped().first().contains("background"));
}

TEST_F(RemoteResourcesTest, ZoneEditorListsRejectedModelFolders) {
    // Новая папка с чужим id в profile.json: загрузчик отклоняет её целиком,
    // но редактор зон должен показать модель, чтобы ошибку можно было исправить.
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/880"));
    auto remote = remoteOf("700");
    remote["id"] = "700"; // папка 880, а внутри остался id скопированной модели
    write(paths::profileKey("880"), toBytes(remote));
    write(paths::photoKey("880"), resources::data(paths::builtinPhoto("700")));
    root = dir.path();
    EXPECT_TRUE(resources::reload(root).isEmpty()); // чужая модель пропущена, остальные работают
    ASSERT_EQ(resources::skipped().size(), 1);
    EXPECT_TRUE(resources::skipped().first().contains("mismatch"));
    EXPECT_EQ(harmony::remoteProfile("880"), nullptr);
    EXPECT_TRUE(resources::modelIds(root).contains("880"));
}

TEST_F(RemoteResourcesTest, ExportModelAndAllRoundTrip) {
    const auto model = resources::exportFiles("900");
    EXPECT_TRUE(model.contains(paths::profileKey("900")));
    EXPECT_EQ(std::count_if(model.cbegin(), model.cend(), [](const QString& f) { return f.endsWith(".json"); }), 1);
    EXPECT_TRUE(model.contains(paths::photoKey("900")));
    for (const auto& file : model) EXPECT_TRUE(file.startsWith("remotes/900/")) << file.toStdString();
    EXPECT_TRUE(resources::exportFiles("no-such-model").isEmpty());
    const auto all = resources::exportFiles({});
    EXPECT_TRUE(all.contains(paths::profileKey("600")));
    EXPECT_TRUE(std::any_of(all.cbegin(), all.cend(), [](const QString& f) { return f.startsWith(resources::kSpritesDir + QLatin1Char('/')); }));

    QTemporaryDir out;
    ASSERT_TRUE(out.isValid());
    const QString target = out.path() + "/exported";
    ASSERT_TRUE(resources::exportTo(target, model).isEmpty());
    for (const auto& file : model) {
        QFile written(target + "/" + file);
        ASSERT_TRUE(written.open(QIODevice::ReadOnly)) << file.toStdString();
        EXPECT_EQ(written.readAll(), resources::data(":/res/" + file)) << file.toStdString();
    }
    EXPECT_FALSE(resources::exportTo(target, {"remotes/900/missing.json"}).isEmpty());
    // Выгруженная папка без правок читается как папка ресурсов и ничего не меняет.
    const auto zones = zonesOf("900");
    root = target;
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_EQ(zonesOf("900"), zones);
    ASSERT_TRUE(resources::exportTo(target, all).isEmpty());
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_EQ(resources::exportFiles({}), all);
}

TEST_F(RemoteResourcesTest, WatchPathsOnlyResourceFiles) {
    EXPECT_TRUE(resources::watchPaths(dir.path() + "/missing").isEmpty());
    write(paths::profileKey("900"), resources::data(paths::builtinProfile("900")));
    write("notes.txt", "not a resource");
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/theme"));
    write("theme/check.png", resources::data(paths::builtinPhoto("900")));
    root = dir.path();
    const auto paths = resources::watchPaths(root);
    EXPECT_TRUE(paths.contains(dir.path()));
    EXPECT_TRUE(paths.contains(dir.path() + "/remotes"));
    EXPECT_TRUE(paths.contains(dir.path() + "/remotes/900"));
    EXPECT_TRUE(paths.contains(dir.path() + "/" + paths::profileKey("900")));
    EXPECT_FALSE(paths.contains(dir.path() + "/notes.txt"));
    EXPECT_FALSE(paths.contains(dir.path() + "/theme"));
    EXPECT_FALSE(paths.contains(dir.path() + "/theme/check.png"));
}

TEST_F(RemoteResourcesTest, PrepareFolderAddsMissingFilesAndKeepsExisting) {
    // Пользовательский файл не перезаписывается; остальное докладывается из встроенных ресурсов.
    const QByteArray custom = "{\"custom\":true}";
    write(paths::profileKey("900"), custom);
    root = dir.path();
    ASSERT_TRUE(resources::prepareFolder(root).isEmpty());
    QFile kept(dir.path() + "/" + paths::profileKey("900"));
    ASSERT_TRUE(kept.open(QIODevice::ReadOnly));
    EXPECT_EQ(kept.readAll(), custom);
    for (const QString& file : {paths::profileKey("600"), paths::photoKey("900")}) {
        QFile added(dir.path() + "/" + file);
        ASSERT_TRUE(added.open(QIODevice::ReadOnly)) << file.toStdString();
        EXPECT_EQ(added.readAll(), resources::data(":/res/" + file)) << file.toStdString();
    }
    // Повторный запуск ничего не ломает.
    EXPECT_TRUE(resources::prepareFolder(root).isEmpty());
}

TEST_F(RemoteResourcesTest, CreateRestoreAndNeverRewriteExistingFiles) {
    root = dir.path() + "/resources";
    ASSERT_FALSE(QDir(root).exists());
    ASSERT_TRUE(resources::reload(root).isEmpty());
    const QString zonesPath = root + "/" + paths::profileKey("900");
    const QString photoPath = root + "/" + paths::photoKey("900");
    const auto originalPhoto = resources::data(paths::builtinPhoto("900"));
    QFile zones(zonesPath);
    ASSERT_TRUE(zones.open(QIODevice::ReadWrite));
    const auto bytes = zones.readAll();
    auto remote = QJsonDocument::fromJson(bytes).object();
    auto hardware = remote["hardware"].toObject();
    auto definitions = hardware["zones"].toArray();
    auto first = definitions[0].toObject();
    first["rect"] = QJsonArray{1, 2, 30, 40};
    definitions[0] = first;
    hardware["zones"] = definitions;
    remote["hardware"] = hardware;
    const auto changed = toBytes(remote);
    ASSERT_TRUE(zones.resize(0));
    ASSERT_TRUE(zones.seek(0));
    ASSERT_EQ(zones.write(changed), changed.size());
    // Удерживаем существующий файл открытым: подготовка не должна даже пытаться заменять его.
    ASSERT_TRUE(zones.flush());
    ASSERT_TRUE(QFile::remove(photoPath));
    ASSERT_TRUE(resources::prepareFolder(root).isEmpty());
    ASSERT_TRUE(zones.seek(0));
    EXPECT_EQ(zones.readAll(), changed);
    zones.close();
    QFile restored(photoPath);
    ASSERT_TRUE(restored.open(QIODevice::ReadOnly));
    EXPECT_EQ(restored.readAll(), originalPhoto);
    restored.close();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_EQ(resources::data(paths::builtinProfile("900")), changed);
    // Препятствие на месте корня даёт ошибку, а не другую скрытую папку.
    QFile blocked(dir.path() + "/blocked");
    ASSERT_TRUE(blocked.open(QIODevice::WriteOnly));
    blocked.close();
    EXPECT_FALSE(resources::prepareFolder(blocked.fileName()).isEmpty());
}

// U1: порядок списка моделей — по id. 900 больше не первый: он не модель по умолчанию,
// и порядок списка задаёт сортировка по id.
TEST_F(RemoteResourcesTest, ModelListIsOrderedById) {
    const auto ids = resources::modelIds(root);
    ASSERT_FALSE(ids.isEmpty());
    EXPECT_TRUE(std::ranges::is_sorted(ids)) << "порядок списка моделей";
    EXPECT_TRUE(ids.contains("900")) << "900 в списке моделей";
    EXPECT_NE(ids.first(), "900") << "900 не первый: модели по умолчанию нет";
}

// V0: профиль пульта читается из profile.json — нового имени файла.
TEST_F(RemoteResourcesTest, ProfileFileIsReadUnderNewName) {
    const QString id = "profile-only";
    write(paths::photoKey(id), resources::data(paths::builtinPhoto("900")));
    write(paths::profileKey(id), toBytes(customRemote(id)));
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_FALSE(resources::remote(id).isEmpty());
    const auto file = resources::readRemote(id, root);
    EXPECT_EQ(file.path, paths::profileKey(id));
    EXPECT_EQ(file.sourcePath, file.path);
    EXPECT_TRUE(resources::profileWarnings().isEmpty());
    EXPECT_TRUE(resources::exportFiles(id).contains(paths::profileKey(id)));
}

// V0: старое имя remote.json читается, пока profile.json нет (совместимость).
TEST_F(RemoteResourcesTest, LegacyRemoteFileIsReadWhenProfileIsMissing) {
    const QString id = "legacy-only";
    write(paths::photoKey(id), resources::data(paths::builtinPhoto("900")));
    write(paths::legacyKey(id), toBytes(customRemote(id)));
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_FALSE(resources::remote(id).isEmpty());
    const auto file = resources::readRemote(id, root);
    EXPECT_EQ(file.path, paths::profileKey(id)) << "писать всегда в новое имя";
    EXPECT_EQ(file.sourcePath, paths::legacyKey(id)) << "прочитан старый файл";
    EXPECT_TRUE(resources::exportFiles(id).contains(paths::profileKey(id)));
    EXPECT_FALSE(resources::exportFiles(id).contains(paths::legacyKey(id)));
    const auto warnings = resources::profileWarnings();
    EXPECT_TRUE(warnings.isEmpty()) << warnings.join(", ").toStdString();
}

// V0: оба файла — берётся profile.json; предупреждение называет оба файла (правило 18).
TEST_F(RemoteResourcesTest, BothProfileFilesPreferNewNameAndWarn) {
    const QString id = "both-files";
    write(paths::photoKey(id), resources::data(paths::builtinPhoto("900")));
    auto modern = customRemote(id);
    modern["name"] = "New profile";
    auto legacy = customRemote(id);
    legacy["name"] = "Old profile";
    write(paths::profileKey(id), toBytes(modern));
    write(paths::legacyKey(id), toBytes(legacy));
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_EQ(resources::remote(id).value("name").toString(), "New profile");
    EXPECT_EQ(resources::readRemote(id, root).sourcePath, paths::profileKey(id));
    const auto warnings = resources::profileWarnings();
    ASSERT_EQ(warnings.size(), 1);
    EXPECT_TRUE(warnings.first().contains(resources::kProfileFile));
    EXPECT_TRUE(warnings.first().contains(resources::kLegacyProfileFile));
}

// V0: перед первой записью старый файл переводится на новое имя, прежний текст остаётся в .bak.
TEST_F(RemoteResourcesTest, FirstSaveAdoptsLegacyFile) {
    const QString id = "adopt-me";
    write(paths::photoKey(id), resources::data(paths::builtinPhoto("900")));
    const QByteArray legacyText = toBytes(customRemote(id));
    write(paths::legacyKey(id), legacyText);
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    const auto before = resources::remote(id);
    ASSERT_FALSE(before.isEmpty());

    EXPECT_TRUE(resources::adoptLegacyProfile(id, root).isEmpty());
    const QDir modelDir(dir.path() + "/remotes/" + id);
    EXPECT_TRUE(modelDir.exists(resources::kProfileFile));
    EXPECT_TRUE(modelDir.exists(resources::kLegacyProfileBackup));
    EXPECT_FALSE(modelDir.exists(resources::kLegacyProfileFile));
    QFile backup(modelDir.filePath(resources::kLegacyProfileBackup));
    ASSERT_TRUE(backup.open(QIODevice::ReadOnly));
    EXPECT_EQ(backup.readAll(), legacyText) << "старый файл не потерян";
    backup.close();

    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_EQ(resources::remote(id), before) << "повторная загрузка даёт ту же модель";
    EXPECT_TRUE(resources::profileWarnings().isEmpty());
}

// V0: снятый файл *.remote.json (P2) — не профиль: он не загружается и в выгрузку не попадает.
TEST_F(RemoteResourcesTest, CapturedRemoteFileIsNotAProfile) {
    const QString id = "capture-only";
    const QString capture = "Harmony-" + id + "-20261125-1200.remote.json";
    write(paths::photoKey(id), resources::data(paths::builtinPhoto("900")));
    write("remotes/" + id + "/" + capture, toBytes(customRemote(id)));
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_TRUE(resources::remote(id).isEmpty()) << "снятый файл не описывает пульт";
    EXPECT_TRUE(resources::data(":/res/remotes/" + id + "/" + capture).isEmpty());
    const auto files = resources::exportFiles(id);
    EXPECT_FALSE(files.contains("remotes/" + id + "/" + capture));
    EXPECT_FALSE(files.contains(paths::profileKey(id)));
}

// V0: имя файла для записи — profile.json; старое имя существует только для чтения.
// Единственное место, где имена записаны литералами: здесь проверяется само решение
// о названиях. Остальные тесты берут их из remoteResources::kProfileFile через profileNames.h.
TEST(RemoteResources, ProfileFilePathPrefersNewName) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QDir modelDir(dir.path());
    EXPECT_EQ(resources::profileFilePath(modelDir), modelDir.filePath("profile.json"));
    QFile legacy(modelDir.filePath("remote.json"));
    ASSERT_TRUE(legacy.open(QIODevice::WriteOnly));
    legacy.close();
    EXPECT_EQ(resources::profileFilePath(modelDir), modelDir.filePath("remote.json"));
    QFile modern(modelDir.filePath("profile.json"));
    ASSERT_TRUE(modern.open(QIODevice::WriteOnly));
    modern.close();
    EXPECT_EQ(resources::profileFilePath(modelDir), modelDir.filePath("profile.json"));
}

TEST(RemoteResources, WorkingFolderIsBesideExecutable) {
    EXPECT_EQ(resources::folder(), QDir(QCoreApplication::applicationDirPath()).filePath("resources"));
}

}
