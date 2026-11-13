#include "remoteResources.h"
#include "harmony/core/remoteProfile.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRectF>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>
#include <QSvgRenderer>
#include <cmath>

namespace harmony::app::remoteResources {
namespace {
QHash<QString, QByteArray> snapshot;
quint64 generation = 0;
/// Проверить конечные координаты прямоугольника и положительный размер.
QRectF rect(const QJsonValue& value) {
    const auto a = value.toArray();
    if (a.size() != 4) return {};
    for (const auto& n : a) if (!n.isDouble() || !std::isfinite(n.toDouble())) return {};
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
}
/// Файл относится к ресурсам пультов: путь от корня папки ресурсов.
bool managed(const QString& relative) {
    return (relative == "layouts.json" || relative.startsWith("remotes/") || relative.startsWith("sprites/")) &&
           (relative.endsWith(".json") || relative.endsWith(".png") || relative.endsWith(".svg"));
}
const QString kPrefix = ":/res/";
}

QString folder() { return QDir(QCoreApplication::applicationDirPath()).filePath("resources"); }
quint64 revision() { return generation; }
QByteArray data(const QString& path) {
    const auto it = snapshot.constFind(path);
    if (it != snapshot.cend()) return *it;
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString reload(const QString& root) {
    if (const QString error = prepareFolder(root); !error.isEmpty()) return error;
    QHash<QString, QByteArray> candidate;
    QHash<QString, QString> sources;
    for (const QString& base : {root}) {
        if (base.isEmpty()) continue;
        QDirIterator files(base, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString file = files.next();
            const QString relative = QDir(base).relativeFilePath(file);
            if (!managed(relative)) continue;
            QFile input(file);
            if (!input.open(QIODevice::ReadOnly)) return file + ": " + input.errorString();
            const QString key = ":/res/" + relative;
            candidate.insert(key, input.readAll());
            sources.insert(key, file);
        }
    }
    QHash<QString, QJsonDocument> json;
    for (auto it = candidate.cbegin(); it != candidate.cend(); ++it) {
        if (it.key().endsWith(".json")) {
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(it.value(), &error);
            if (error.error != QJsonParseError::NoError)
                return sources[it.key()] + ": " + error.errorString();
            const bool array = it.key().endsWith("/zones.json") || it.key().endsWith("/layouts.json");
            if (array ? !doc.isArray() : !doc.isObject()) return sources[it.key()] + ": invalid JSON structure";
            json.insert(it.key(), doc);
        } else if (it.key().endsWith(".png")) {
            if (QImage::fromData(it.value()).isNull()) return sources[it.key()] + ": invalid image";
        } else if (!QSvgRenderer(it.value()).isValid()) return sources[it.key()] + ": invalid SVG";
    }
    const QString layoutPath = ":/res/layouts.json";
    QSet<QString> layouts;
    for (const auto& entry : json[layoutPath].array()) {
        const auto layout = entry.toObject();
        const auto id = layout.value("id").toString();
        const auto ref = layout.value("ref").toArray();
        if (id.isEmpty() || layouts.contains(id) || ref.size() != 2 ||
            ref[0].toDouble() <= 0 || ref[1].toDouble() <= 0 || !layout.value("zones").isArray())
            return sources[layoutPath] + ": invalid layout " + id;
        layouts.insert(id);
        for (const auto& z : layout.value("zones").toArray())
            if (!rect(z.toObject().value("rect")).isValid()) return sources[layoutPath] + ": invalid zone in " + id;
        const auto tile = layout.value("tile").toObject().value("image").toString();
        if (!tile.isEmpty() && !candidate.contains(":/res/" + tile)) return sources[layoutPath] + ": missing " + tile;
    }
    std::vector<harmony::RemoteProfile> models;
    auto paths = json.keys();
    paths.sort();
    for (const auto& path : paths) {
        if (!path.startsWith(":/res/remotes/") || !path.endsWith("/profile.json")) continue;
        const QString dir = path.left(path.size() - QString("/profile.json").size());
        const QString modelId = dir.mid(QString(":/res/remotes/").size());
        static const QRegularExpression validId("^[a-z0-9][a-z0-9_-]*$");
        if (!validId.match(modelId).hasMatch()) return sources[path] + ": invalid model id";
        const auto profile = json[dir + "/profile.json"].object();
        const QString name = profile.value("name").toString().trimmed();
        if (name.isEmpty() || !candidate.contains(dir + "/zones.json"))
            return sources[path] + ": name and zones.json are required";
        const harmony::RemoteProfile* bundled = nullptr;
        for (const auto& item : harmony::bundledRemoteProfiles)
            if (item.id == modelId.toStdString()) bundled = &item;
        for (const auto* field : {"driver", "configFormat"})
            if (profile.contains(field) && !profile.value(field).isString())
                return sources[path] + ": invalid " + field;
        const QString driver = profile.value("driver").toString(bundled ? QString::fromStdString(bundled->driver) : "none");
        const QString format = profile.value("configFormat").toString(bundled ? QString::fromStdString(bundled->configFormat) : "unsupported");
        if (!QSet<QString>{"none", "hid", "harmony900"}.contains(driver) ||
            !QSet<QString>{"unsupported", "harmony900"}.contains(format))
            return sources[path] + ": unknown driver or configFormat";
        const bool writable = driver == "harmony900" && format == "harmony900";
        models.push_back({modelId.toStdString(), name.toStdString(), (dir + "/photo.png").toStdString(),
                          writable, driver.toStdString(), format.toStdString()});
        const auto size = profile.value("size").toArray();
        const auto image = QImage::fromData(candidate[dir + "/photo.png"]);
        if (size.size() != 2 || QSize(size[0].toInt(), size[1].toInt()) != image.size() || image.isNull() ||
            profile.value("id").toString() != modelId || profile.value("image") != "photo.png")
            return sources[dir + "/profile.json"] + ": profile/photo mismatch";
        const QRectF bounds(image.rect());
        const auto screen = profile.value("screen");
        if (!screen.isNull() && !screen.isUndefined() && (!rect(screen).isValid() || !bounds.contains(rect(screen))))
            return sources[dir + "/profile.json"] + ": invalid screen rectangle";
        const auto modes = profile.value("modes").toObject();
        for (const auto& mode : modes) if (!layouts.contains(mode.toString()))
            return sources[dir + "/profile.json"] + ": unknown layout " + mode.toString();
        QSet<QString> ids;
        const QSet<QString> kinds{"Key", "System", "PagePrev", "PageNext", "ScreenActs", "ScreenSoft"};
        for (const auto& entry : json[dir + "/zones.json"].array()) {
            const auto zone = entry.toObject();
            const auto id = zone.value("id").toString();
            const auto r = rect(zone.value("rect"));
            if (id.isEmpty() || ids.contains(id) || !r.isValid() || !bounds.contains(r) ||
                !zone.value("radius").isDouble() || !kinds.contains(zone.value("kind").toString()))
                return sources[dir + "/zones.json"] + ": invalid zone " + id;
            ids.insert(id);
        }
        const auto screens = json.value(dir + "/screens.json").object();
        const auto images = screens.value("images").toObject();
        for (const auto& imageDef : images) {
            const auto file = imageDef.toObject().value("file").toString();
            if (file.isEmpty() || !candidate.contains(":/res/" + file))
                return sources[dir + "/screens.json"] + ": missing image " + file;
        }
        const auto modesImages = screens.value("screens").toObject();
        for (const auto& mode : modesImages) {
            if (!mode.isArray()) return sources[dir + "/screens.json"] + ": invalid screen";
            for (const auto& entry : mode.toArray()) {
                const auto item = entry.toObject();
                if (!rect(item.value("rect")).isValid() || !images.contains(item.value("image").toString()))
                    return sources[dir + "/screens.json"] + ": invalid image placement";
            }
        }
    }
    // Сначала штатные модели в привычном порядке, затем новые по id.
    std::stable_sort(models.begin(), models.end(), [](const auto& a, const auto& b) {
        const auto rank = [](const auto& id) {
            for (std::size_t i = 0; i < harmony::bundledRemoteProfiles.size(); ++i)
                if (harmony::bundledRemoteProfiles[i].id == id) return i;
            return harmony::bundledRemoteProfiles.size();
        };
        return rank(a.id) < rank(b.id);
    });
    snapshot.swap(candidate);
    harmony::remoteProfiles.swap(models);
    ++generation;
    return {};
}

QStringList modelIds(const QString& root) {
    QStringList result;
    for (const auto& profile : harmony::remoteProfiles) result.append(QString::fromStdString(profile.id));
    const QDir remotes(QDir(root).filePath(QStringLiteral("remotes")));
    for (const QString& id : remotes.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!result.contains(id) && QFileInfo::exists(remotes.filePath(id + QStringLiteral("/profile.json"))))
            result.append(id);
    }
    return result;
}

QStringList exportFiles(const QString& modelId) {
    const QString model = kPrefix + "remotes/" + modelId + "/";
    QStringList files;
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it)
        if (modelId.isEmpty() || it.key().startsWith(model)) files.append(it.key().mid(kPrefix.size()));
    files.sort();
    return files;
}

QString exportTo(const QString& target, const QStringList& files) {
    const QDir root(target);
    if (target.isEmpty() || !QDir().mkpath(target)) return target + ": cannot create folder";
    for (const QString& relative : files) {
        const auto it = snapshot.constFind(kPrefix + relative);
        // Только пути из снимка: они уже прошли managed() и не выходят за корень.
        if (it == snapshot.cend()) return relative + ": not in the loaded resources";
        const QString path = root.filePath(relative);
        if (!QDir().mkpath(QFileInfo(path).path())) return QFileInfo(path).path() + ": cannot create folder";
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(*it) != it->size() || !out.commit())
            return path + ": " + out.errorString();
    }
    return {};
}

QString prepareFolder(const QString& root) {
    if (root.isEmpty() || !QDir().mkpath(root)) return root + ": cannot create folder";
    const QDir base(root);
    const QDir builtin(":/res");
    QDirIterator files(":/res", QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString file = files.next();
        const QString relative = builtin.relativeFilePath(file);
        if (!managed(relative) || base.exists(relative)) continue;
        QFile input(file);
        if (!input.open(QIODevice::ReadOnly)) return file + ": " + input.errorString();
        const QString path = base.filePath(relative);
        if (!QDir().mkpath(QFileInfo(path).path())) return QFileInfo(path).path() + ": cannot create folder";
        const QByteArray bytes = input.readAll();
        if (input.error() != QFileDevice::NoError) return file + ": " + input.errorString();
        // NewOnly защищает даже файл, созданный другим процессом после проверки exists().
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (QFileInfo::exists(path)) continue;
            return path + ": " + out.errorString();
        }
        if (out.write(bytes) != bytes.size() || !out.flush()) {
            const QString error = path + ": " + out.errorString();
            out.remove(); // удаляем только незавершённый файл, созданный этим вызовом
            return error;
        }
    }
    return {};
}

QStringList watchPaths(const QString& root) {
    if (root.isEmpty() || !QDir(root).exists()) return {};
    const QDir base(root);
    QStringList paths{root};
    QDirIterator entries(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        const QString path = entries.next();
        const QString relative = base.relativeFilePath(path);
        if (entries.fileInfo().isDir() ? relative == "remotes" || relative == "sprites" ||
                                             relative.startsWith("remotes/") || relative.startsWith("sprites/")
                                       : managed(relative))
            paths.append(path);
    }
    return paths;
}
}
