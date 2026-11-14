#include "../app/src/remoteResources.h"
#include "../app/src/modern/remoteView.h"
#include <QDir>
#include <QApplication>
#include <QMouseEvent>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include "harmony/core/remoteProfile.h"
#include "harmony/core/configDocument.h"
#include <algorithm>

namespace {
namespace resources = harmony::app::remoteResources;
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
        QFile file(dir.path() + "/" + relative);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(bytes), bytes.size());
    }
};

/// remote.json встроенной модели как объект.
QJsonObject remoteOf(const QString& model) {
    return QJsonDocument::fromJson(resources::data(":/res/remotes/" + model + "/remote.json")).object();
}
QByteArray toBytes(const QJsonObject& object) { return QJsonDocument(object).toJson(); }

TEST_F(RemoteResourcesTest, OverrideReloadRemovalAndInvalidRollback) {
    const QString path = ":/res/remotes/900/zones.json";
    const auto original = resources::data(path);
    auto remote = remoteOf("900");
    const auto originalPhoto = resources::data(":/res/remotes/900/photo.png");
    harmony::app::RemoteView view;
    view.setRemoteProfile("900");
    auto zones = QJsonDocument::fromJson(original).array();
    auto zone = zones[0].toObject();
    zone["id"] = "CustomKey";
    zone["kind"] = "Key";
    zone["rect"] = QJsonArray{1, 2, 30, 40};
    zones[0] = zone;
    auto hardware = remote["hardware"].toObject();
    hardware["zones"] = zones;
    remote["hardware"] = hardware;
    write("remotes/900/remote.json", toBytes(remote));
    const auto modified = QJsonDocument(zones).toJson(QJsonDocument::Compact);
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
    EXPECT_EQ(resources::data(":/res/remotes/900/photo.png"), originalPhoto);
    const auto version = resources::revision();
    write("remotes/900/remote.json", "{broken");
    EXPECT_TRUE(resources::reload(root).contains("remote.json"));
    EXPECT_EQ(resources::revision(), version);
    EXPECT_EQ(resources::data(path), modified);
    zone["rect"] = QJsonArray{0, 0, 9999, 9999};
    zones[0] = zone;
    hardware["zones"] = zones;
    remote["hardware"] = hardware;
    write("remotes/900/remote.json", toBytes(remote));
    EXPECT_TRUE(resources::reload(root).contains("CustomKey"));
    EXPECT_EQ(resources::data(path), modified);
    ASSERT_TRUE(QFile::remove(dir.path() + "/remotes/900/remote.json"));
    ASSERT_TRUE(resources::reload(root).isEmpty());
    view.setRemoteProfile("900");
    EXPECT_EQ(resources::data(path), original);
    EXPECT_FALSE(harmony::app::RemoteView::hardKeyIds("900").contains("CustomKey"));
    clicked.clear();
    QApplication::sendEvent(&view, &event);
    QApplication::sendEvent(&view, &release);
    EXPECT_NE(clicked, "CustomKey");
}

TEST_F(RemoteResourcesTest, PhotoChangesAndInvalidPhotoRollback) {
    const QString path = ":/res/remotes/900/photo.png";
    auto image = QImage::fromData(resources::data(path));
    image.fill(Qt::red);
    ASSERT_TRUE(image.save(dir.path() + "/remotes/900/photo.png"));
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_EQ(QImage::fromData(resources::data(path)).pixelColor(10, 10), QColor(Qt::red));
    const auto previousImage = resources::data(path);
    write("remotes/900/photo.png", "broken image");
    EXPECT_TRUE(resources::reload(root).contains("photo.png"));
    EXPECT_EQ(resources::data(path), previousImage);
    QImage wrong(10, 10, QImage::Format_RGB32);
    wrong.fill(Qt::blue);
    ASSERT_TRUE(wrong.save(dir.path() + "/remotes/900/photo.png"));
    EXPECT_FALSE(resources::reload(root).isEmpty());
    EXPECT_EQ(resources::data(path), previousImage);
}
TEST_F(RemoteResourcesTest, DiscoverNewModelAndPersistProject) {
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/custom-600"));
    auto remote = remoteOf("600");
    remote["id"] = "custom-600";
    remote["name"] = "Custom 600";
    auto connection = remote["connection"].toObject();
    connection["driver"] = "hid";
    connection["configFormat"] = "unsupported";
    remote["connection"] = connection;
    write("remotes/custom-600/remote.json", toBytes(remote));
    write("remotes/custom-600/photo.png", resources::data(":/res/remotes/600/photo.png"));
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    const auto* added = harmony::remoteProfile("custom-600");
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(added->driver, "hid");
    EXPECT_FALSE(added->writable);
    harmony::app::RemoteView view;
    view.setRemoteProfile("custom-600");
    EXPECT_FALSE(view.screenModes().isEmpty());
    EXPECT_FALSE(harmony::app::RemoteView::hardKeyIds("custom-600").isEmpty());
    auto document = harmony::ConfigDocument::createNew("Custom remote");
    ASSERT_TRUE(document.setTargetRemote("custom-600"));
    EXPECT_EQ(document.targetRemote(), "custom-600");
    EXPECT_FALSE(document.toEzHex(true));
    const auto saved = document.toEzHex(false);
    ASSERT_TRUE(saved);
    auto reopened = harmony::ConfigDocument::fromEzHex(*saved, "Reopened");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->targetRemote(), "custom-600");
    connection["driver"] = "unknown-driver";
    remote["connection"] = connection;
    write("remotes/custom-600/remote.json", toBytes(remote));
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
    write("remotes/gridmodel/remote.json", toBytes(remote));
    write("remotes/gridmodel/photo.png", resources::data(":/res/remotes/600/photo.png"));
    root = dir.path();
    ASSERT_TRUE(resources::reload(root).isEmpty());
    const auto library = QJsonDocument::fromJson(resources::data(":/res/layouts.json")).array();
    bool found = false;
    for (const auto& entry : library) {
        const auto layout = entry.toObject();
        if (layout["id"].toString() != "gridmodel_Menu") continue;
        found = true;
        const auto cells = layout["zones"].toArray();
        ASSERT_EQ(cells.size(), 2);
        EXPECT_EQ(cells[0].toObject()["rect"].toArray(), (QJsonArray{0, 0, 100, 50}));
        EXPECT_EQ(cells[1].toObject()["rect"].toArray(), (QJsonArray{50, 50, 50, 50}));
    }
    EXPECT_TRUE(found);
}

TEST_F(RemoteResourcesTest, ZoneEditorListsRejectedModelFolders) {
    // Новая папка с чужим id в remote.json: загрузчик отклоняет её целиком,
    // но редактор зон должен показать модель, чтобы ошибку можно было исправить.
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/remotes/880"));
    auto remote = remoteOf("700");
    remote["id"] = "700"; // папка 880, а внутри остался id скопированной модели
    write("remotes/880/remote.json", toBytes(remote));
    write("remotes/880/photo.png", resources::data(":/res/remotes/700/photo.png"));
    root = dir.path();
    EXPECT_TRUE(resources::reload(root).isEmpty()); // чужая модель пропущена, остальные работают
    ASSERT_EQ(resources::skipped().size(), 1);
    EXPECT_TRUE(resources::skipped().first().contains("mismatch"));
    EXPECT_EQ(harmony::remoteProfile("880"), nullptr);
    EXPECT_TRUE(resources::modelIds(root).contains("880"));
}

TEST_F(RemoteResourcesTest, ExportModelAndAllRoundTrip) {
    const auto model = resources::exportFiles("900");
    EXPECT_TRUE(model.contains("remotes/900/remote.json"));
    EXPECT_FALSE(model.contains("remotes/900/zones.json")); // достраивается из remote.json
    EXPECT_TRUE(model.contains("remotes/900/photo.png"));
    for (const auto& file : model) EXPECT_TRUE(file.startsWith("remotes/900/")) << file.toStdString();
    EXPECT_TRUE(resources::exportFiles("no-such-model").isEmpty());
    const auto all = resources::exportFiles({});
    EXPECT_TRUE(all.contains("remotes/600/remote.json"));
    EXPECT_TRUE(std::any_of(all.cbegin(), all.cend(), [](const QString& f) { return f.startsWith("sprites/"); }));

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
    const auto zones = resources::data(":/res/remotes/900/zones.json");
    root = target;
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_EQ(resources::data(":/res/remotes/900/zones.json"), zones);
    ASSERT_TRUE(resources::exportTo(target, all).isEmpty());
    ASSERT_TRUE(resources::reload(root).isEmpty());
    EXPECT_EQ(resources::exportFiles({}), all);
}

TEST_F(RemoteResourcesTest, WatchPathsOnlyResourceFiles) {
    EXPECT_TRUE(resources::watchPaths(dir.path() + "/missing").isEmpty());
    write("remotes/900/remote.json", resources::data(":/res/remotes/900/remote.json"));
    write("notes.txt", "not a resource");
    ASSERT_TRUE(QDir().mkpath(dir.path() + "/theme"));
    write("theme/check.png", resources::data(":/res/remotes/900/photo.png"));
    root = dir.path();
    const auto paths = resources::watchPaths(root);
    EXPECT_TRUE(paths.contains(dir.path()));
    EXPECT_TRUE(paths.contains(dir.path() + "/remotes"));
    EXPECT_TRUE(paths.contains(dir.path() + "/remotes/900"));
    EXPECT_TRUE(paths.contains(dir.path() + "/remotes/900/remote.json"));
    EXPECT_FALSE(paths.contains(dir.path() + "/notes.txt"));
    EXPECT_FALSE(paths.contains(dir.path() + "/theme"));
    EXPECT_FALSE(paths.contains(dir.path() + "/theme/check.png"));
}

TEST_F(RemoteResourcesTest, PrepareFolderAddsMissingFilesAndKeepsExisting) {
    // Пользовательский файл не перезаписывается; остальное докладывается из встроенных ресурсов.
    const QByteArray custom = "{\"custom\":true}";
    write("remotes/900/remote.json", custom);
    root = dir.path();
    ASSERT_TRUE(resources::prepareFolder(root).isEmpty());
    QFile kept(dir.path() + "/remotes/900/remote.json");
    ASSERT_TRUE(kept.open(QIODevice::ReadOnly));
    EXPECT_EQ(kept.readAll(), custom);
    for (const QString& file : {QString("remotes/600/remote.json"), QString("remotes/900/photo.png")}) {
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
    const QString zonesPath = root + "/remotes/900/remote.json";
    const QString photoPath = root + "/remotes/900/photo.png";
    const auto originalPhoto = resources::data(":/res/remotes/900/photo.png");
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
    EXPECT_EQ(resources::data(":/res/remotes/900/remote.json"), changed);
    // Препятствие на месте корня даёт ошибку, а не другую скрытую папку.
    QFile blocked(dir.path() + "/blocked");
    ASSERT_TRUE(blocked.open(QIODevice::WriteOnly));
    blocked.close();
    EXPECT_FALSE(resources::prepareFolder(blocked.fileName()).isEmpty());
}

TEST(RemoteResources, WorkingFolderIsBesideExecutable) {
    EXPECT_EQ(resources::folder(), QDir(QCoreApplication::applicationDirPath()).filePath("resources"));
}

}
