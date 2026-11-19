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
#include <string_view>
#include <QSvgRenderer>
#include <cmath>

namespace harmony::app::remoteResources {
namespace {
QHash<QString, QByteArray> snapshot;
/// Проверенные описания пультов последнего снимка: id → remote.json с достроенными rect ячеек.
QHash<QString, QJsonObject> remotes;
quint64 generation = 0;
/// Пользовательские модели, пропущенные последним reload() из-за ошибок (тексты ошибок).
QStringList skippedModels;
QHash<QString, QString> skippedById;
/// remote.json в старой схеме, переведённые последним reload() на новую (пути к файлам).
QStringList migratedFiles;
/// Проверить конечные координаты прямоугольника и положительный размер.
QRectF rect(const QJsonValue& value) {
    const auto a = value.toArray();
    if (a.size() != 4) return {};
    for (const auto& n : a) if (!n.isDouble() || !std::isfinite(n.toDouble())) return {};
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
}
/// Описание пульта: `remotes/<id>/remote.json`, путь от корня папки ресурсов.
bool remoteFile(const QString& relative) {
    static const QRegularExpression pattern(QStringLiteral("^remotes/[^/]+/remote\\.json$"));
    return pattern.match(relative).hasMatch();
}
/**
 * Файл относится к ресурсам пультов: путь от корня папки ресурсов.
 * Из JSON читается только remote.json; оставшиеся от старых версий profile.json, zones.json,
 * screens.json и layouts.json не читаются и не мешают загрузке.
 */
bool managed(const QString& relative) {
    if (relative.endsWith(".json")) return remoteFile(relative);
    return (relative.startsWith("remotes/") || relative.startsWith("sprites/")) &&
           (relative.endsWith(".png") || relative.endsWith(".svg"));
}
const QString kPrefix = ":/res/";
const QString kRemotes = ":/res/remotes/";
}

QString folder() { return QDir(QCoreApplication::applicationDirPath()).filePath("resources"); }
quint64 revision() { return generation; }
QStringList skipped() { return skippedModels; }
QStringList migratedByReload() { return migratedFiles; }

ScreenZones screenZones(const QJsonObject& screen) {
    ScreenZones zones;
    const auto ref = screen.value("ref").toArray();
    const auto background = screen.value("background").toObject();
    if (ref.size() != 2 || !(background.contains("header") || background.contains("footer")))
        return zones;
    const qreal w = ref[0].toDouble();
    const qreal h = ref[1].toDouble();
    const auto header = background.value("header").toObject();
    const auto footer = background.value("footer").toObject();
    const qreal headH = header.value("height").toDouble();
    const qreal footH = footer.value("height").toDouble();
    if (w <= 0 || h <= 0 || headH < 0 || footH < 0 || headH + footH >= h) return zones;
    const auto color = [](const QJsonObject& zone) {
        const QColor c(zone.value("color").toString());
        return c.isValid() ? c : QColor();
    };
    zones.any = true;
    zones.header = QRectF(0, 0, w, headH);
    zones.body = QRectF(0, headH, w, h - headH - footH);
    zones.footer = QRectF(0, h - footH, w, footH);
    zones.headerColor = color(header);
    zones.footerColor = color(footer);
    return zones;
}

QJsonObject normalizeScreen(QJsonObject screen, const QJsonValue& defaultArea) {
    const auto grid = screen.value("grid").toObject();
    const auto ref = screen.value("ref").toArray();
    const int cols = grid.value("cols").toInt();
    const int rows = grid.value("rows").toInt();
    if (cols <= 0 || rows <= 0 || ref.size() != 2) return screen;
    // Ячейки без rect ставятся по сетке: col/row (с нуля), colSpan/rowSpan (по умолчанию 1),
    // внутри зоны плиток grid.area, а без неё — по всему экрану.
    const qreal gap = grid.value("gap").toDouble(4);
    QRectF area(0, 0, ref[0].toDouble(), ref[1].toDouble());
    if (const QRectF given = rect(grid.value("area")); given.isValid()) area = given;
    else if (const QRectF common = rect(defaultArea); common.isValid()) area = common;
    // Плитки живут только в теле фона: между шапкой и подвалом.
    if (const ScreenZones zones = screenZones(screen); zones.any) {
        const QRectF inside = area.intersected(zones.body);
        area = inside.isValid() && !inside.isEmpty() ? inside : zones.body;
    }
    const qreal cw = (area.width() - gap * (cols + 1)) / cols;
    const qreal ch = (area.height() - gap * (rows + 1)) / rows;
    const auto round2 = [](qreal v) { return std::round(v * 100) / 100; };
    QJsonArray cells = screen.value("cells").toArray();
    bool changed = false;
    for (int i = 0; i < cells.size(); ++i) {
        auto cell = cells[i].toObject();
        if (cell.contains("rect") || !cell.contains("col") || !cell.contains("row")) continue;
        const int col = cell.value("col").toInt();
        const int row = cell.value("row").toInt();
        const int cs = std::max(1, cell.value("colSpan").toInt(1));
        const int rs = std::max(1, cell.value("rowSpan").toInt(1));
        cell.insert("rect", QJsonArray{round2(area.x() + gap + col * (cw + gap)), round2(area.y() + gap + row * (ch + gap)),
                                       round2(cw * cs + gap * (cs - 1)), round2(ch * rs + gap * (rs - 1))});
        if (!cell.contains("kind")) cell.insert("kind", "ScreenSlot");
        cells[i] = cell;
        changed = true;
    }
    if (changed) screen.insert("cells", cells);
    return screen;
}

QJsonObject normalizeRemote(QJsonObject remote) {
    QJsonArray screens = remote.value("screens").toArray();
    // Своя зона экрана (grid.area) главнее общей зоны пульта (tiles.area).
    const QJsonValue common = remote.value("tiles").toObject().value("area");
    for (int i = 0; i < screens.size(); ++i) screens[i] = normalizeScreen(screens[i].toObject(), common);
    if (remote.contains("screens")) remote.insert("screens", screens);
    return remote;
}

QJsonObject remote(const QString& modelId) {
    if (generation == 0) {
        // reload() ещё не вызывался (тесты, ранний запуск): встроенное описание пульта.
        QFile file(kRemotes + modelId + "/remote.json");
        if (!file.open(QIODevice::ReadOnly)) return {};
        return normalizeRemote(QJsonDocument::fromJson(file.readAll()).object());
    }
    return remotes.value(modelId);
}

RemoteFile readRemote(const QString& modelId, const QString& root) {
    RemoteFile file;
    file.path = "remotes/" + modelId + "/remote.json";
    QFile input(QDir(root).filePath(file.path));
    if (!input.open(QIODevice::ReadOnly)) input.setFileName(kPrefix + file.path);
    if (!input.isOpen() && !input.open(QIODevice::ReadOnly)) {
        file.error = file.path + ": " + input.errorString();
        return file;
    }
    file.text = input.readAll();
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.text, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        file.error = file.path + ": " +
                     (parseError.error != QJsonParseError::NoError ? parseError.errorString() : QStringLiteral("invalid JSON structure"));
        file.current = file.text;
        return file;
    }
    QJsonObject remote = document.object();
    file.current = migrateRemote(remote, modelId) ? QJsonDocument(remote).toJson(QJsonDocument::Indented) : file.text;
    file.remote = normalizeRemote(remote);
    return file;
}

QByteArray currentSchema(const QByteArray& text, const QString& modelId) {
    const auto document = QJsonDocument::fromJson(text);
    if (!document.isObject()) return text;
    QJsonObject remote = document.object();
    return migrateRemote(remote, modelId) ? QJsonDocument(remote).toJson(QJsonDocument::Indented) : text;
}

bool migrateRemote(QJsonObject& remote, const QString& modelId) {
    bool changed = false;
    // Встроенное описание того же пульта — образец новых полей; у своих пультов его нет.
    QJsonObject builtin;
    if (QFile file(":/res/remotes/" + modelId + "/remote.json"); file.open(QIODevice::ReadOnly))
        builtin = QJsonDocument::fromJson(file.readAll()).object();

    const auto connection = remote.value("connection").toObject();
    const QString driver = connection.value("driver").toString();
    if (connection.contains("configFormat") || connection.contains("transport") || driver == "hid" ||
        driver == "harmony900") {
        const auto fresh = builtin.value("connection").toObject();
        const QString newDriver = driver == "harmony900" ? QStringLiteral("usb-net")
                                  : driver == "hid"      ? QStringLiteral("usb-hid")
                                                         : QStringLiteral("none");
        const QString newFormat = connection.value("configFormat").toString() == "harmony900"
                                      ? QStringLiteral("harmony-xml") : QStringLiteral("none");
        remote.insert("connection", QJsonObject{
            {"driver", fresh.contains("driver") ? fresh.value("driver") : QJsonValue(newDriver)},
            {"format", fresh.contains("format") ? fresh.value("format") : QJsonValue(newFormat)}});
        changed = true;
    }

    // Поле touch экранов: сенсорность — свойство пульта (capabilities.touch), а не экрана.
    {
        QJsonArray screens = remote.value("screens").toArray();
        bool touch = false;
        bool found = false;
        for (int i = 0; i < screens.size(); ++i) {
            QJsonObject screen = screens[i].toObject();
            if (!screen.contains("touch")) continue;
            // take() отдельно: в `touch || take()` поле не убиралось бы после первого `true`.
            const bool screenTouch = screen.take("touch").toBool();
            touch = touch || screenTouch;
            found = true;
            screens[i] = screen;
        }
        if (found) {
            remote.insert("screens", screens);
            QJsonObject capabilities = remote.value("capabilities").toObject();
            if (!capabilities.contains("touch")) {
                capabilities.insert("touch", touch);
                remote.insert("capabilities", capabilities);
            }
            changed = true;
        }
    }

    // Поля style и compact экранов: вид 600-серии (белая шапка, вкладка Devices) теперь нарисован на картинке
    // фона, цвет надписей — panel.text. Картинка и цвет берутся у встроенного экрана; своя картинка остаётся.
    QHash<QString, QJsonObject> builtinScreens;
    for (const auto& value : builtin.value("screens").toArray())
        builtinScreens.insert(value.toObject().value("id").toString(), value.toObject());
    QJsonArray screens = remote.value("screens").toArray();
    bool screensChanged = false;
    for (int i = 0; i < screens.size(); ++i) {
        QJsonObject screen = screens[i].toObject();
        if (!screen.contains("style") && !screen.contains("compact")) continue;
        const bool h600 = screen.take("style").toString() == "h600";
        const bool compactFlag = screen.take("compact").toBool();
        const bool compact = h600 || compactFlag;
        const QJsonObject fresh = builtinScreens.value(screen.value("id").toString());
        if (compact) {
            QJsonObject background = screen.value("background").toObject();
            const QString image = background.value("image").toString();
            const auto freshImage = fresh.value("background").toObject().value("image");
            // Прежние картинки фона 600–700 были без шапки: их заменяет картинка с шапкой.
            const bool oldImage = image.isEmpty() || image == "sprites/background_600.png" ||
                                  image == "sprites/background_650.png";
            if (oldImage && freshImage.isString()) background.insert("image", freshImage);
            screen.insert("background", background);
            QJsonObject panel = screen.value("panel").toObject();
            if (!panel.contains("text")) panel.insert("text", QStringLiteral("#111111")); // тёмные надписи на белой шапке
            screen.insert("panel", panel);
        }
        if (fresh.contains("panel") && !screen.contains("panel")) screen.insert("panel", fresh.value("panel"));
        screens[i] = screen;
        screensChanged = true;
    }
    if (screensChanged) {
        remote.insert("screens", screens);
        changed = true;
    }
    return changed;
}
QString modelError(const QString& id) { return skippedById.value(id); }
QStringList skippedIds() {
    auto ids = skippedById.keys();
    ids.sort();
    return ids;
}
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
    // remote.json прошлой версии программы (старые connection и style) переводится на новую схему
    // в памяти; правки пользователя (зоны, цвета, экраны) сохраняются. Файл на диске не меняется,
    // редактор зон запишет его в новой схеме при следующем сохранении.
    QStringList migrated;
    for (auto it = candidate.begin(); it != candidate.end(); ++it) {
        const QString& key = it.key();
        if (!key.endsWith("/remote.json")) continue;
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(it.value(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) continue; // ошибка — ниже
        QJsonObject remote = document.object();
        const QString modelId = key.mid(kRemotes.size()).section('/', 0, 0);
        if (!migrateRemote(remote, modelId)) continue;
        it.value() = QJsonDocument(remote).toJson(QJsonDocument::Compact);
        migrated.append(sources.value(key));
    }
    QHash<QString, QJsonObject> parsed; // id пульта → remote.json
    // Ошибки пультов, которые не входят в штатный набор: пульт пропускается, остальные работают.
    QHash<QString, QString> failed;
    QString fatal;
    const auto isBundled = [](const QString& id) {
        return std::ranges::any_of(harmony::bundledRemoteProfiles,
                                   [&](const auto& item) { return item.id == id.toStdString(); });
    };
    const auto modelOf = [](const QString& key) -> QString {
        return key.startsWith(kRemotes) ? key.mid(kRemotes.size()).section('/', 0, 0) : QString();
    };
    const auto reject = [&](const QString& key, const QString& message) {
        const QString id = modelOf(key);
        if (id.isEmpty() || isBundled(id)) {
            if (fatal.isEmpty()) fatal = message;
        } else if (!failed.contains(id)) {
            failed.insert(id, message);
        }
    };
    for (auto it = candidate.cbegin(); it != candidate.cend(); ++it) {
        if (it.key().endsWith(".json")) {
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(it.value(), &error);
            if (error.error != QJsonParseError::NoError) {
                reject(it.key(), sources[it.key()] + ": " + error.errorString());
            } else if (!doc.isObject()) {
                reject(it.key(), sources[it.key()] + ": invalid JSON structure");
            } else {
                parsed.insert(modelOf(it.key()), normalizeRemote(doc.object()));
            }
        } else if (it.key().endsWith(".png")) {
            if (QImage::fromData(it.value()).isNull()) reject(it.key(), sources[it.key()] + ": invalid image");
        } else if (!QSvgRenderer(it.value()).isValid()) reject(it.key(), sources[it.key()] + ": invalid SVG");
    }
    if (!fatal.isEmpty()) return fatal;

    std::vector<harmony::RemoteProfile> models;
    // Проверить один пульт; при ошибке вернуть текст (пульт в каталог не попадает).
    const auto checkModel = [&](const QString& modelId, const QJsonObject& remote) -> QString {
        const QString dir = kRemotes + modelId;
        const QString source = sources.value(dir + "/remote.json");
        static const QRegularExpression validId("^[a-z0-9][a-z0-9_-]*$");
        if (!validId.match(modelId).hasMatch()) return source + ": invalid model id";
        const QString name = remote.value("name").toString().trimmed();
        const auto hardware = remote.value("hardware").toObject();
        // Как и раньше, без зон корпуса пульт не загружается (пустой массив допустим).
        if (name.isEmpty() || !hardware.value("zones").isArray())
            return source + ": name and hardware.zones are required";
        const harmony::RemoteProfile* bundled = nullptr;
        for (const auto& item : harmony::bundledRemoteProfiles)
            if (item.id == modelId.toStdString()) bundled = &item;
        const auto connection = remote.value("connection").toObject();
        for (const auto* field : {"driver", "format"})
            if (connection.contains(field) && !connection.value(field).isString())
                return source + ": invalid connection." + field;
        const QString driver = connection.value("driver").toString(
            bundled ? QString::fromStdString(bundled->driver)
                    : QString::fromLatin1(harmony::driver::kNone.data(), harmony::driver::kNone.size()));
        const QString format = connection.value("format").toString(
            bundled ? QString::fromStdString(bundled->format)
                    : QString::fromLatin1(harmony::configFormat::kNone.data(), harmony::configFormat::kNone.size()));
        const auto known = [](const auto& list, const QString& value) {
            return std::ranges::any_of(list, [&](std::string_view item) {
                return value == QLatin1String(item.data(), static_cast<qsizetype>(item.size()));
            });
        };
        if (!known(harmony::driver::kAll, driver) || !known(harmony::configFormat::kAll, format))
            return source + ": unknown connection driver or format";

        const auto size = hardware.value("size").toArray();
        const auto image = QImage::fromData(candidate.value(dir + "/photo.png"));
        if (size.size() != 2 || QSize(size[0].toInt(), size[1].toInt()) != image.size() || image.isNull() ||
            remote.value("id").toString() != modelId || hardware.value("image") != "photo.png")
            return source + ": id, hardware.image or hardware.size mismatch";
        const QRectF bounds(image.rect());
        const auto screen = remote.value("display").toObject().value("rect");
        if (!screen.isNull() && !screen.isUndefined() && (!rect(screen).isValid() || !bounds.contains(rect(screen))))
            return source + ": invalid display.rect";

        QSet<QString> ids;
        const QSet<QString> kinds{"Key", "System", "PagePrev", "PageNext", "ScreenActs", "ScreenSoft"};
        for (const auto& entry : hardware.value("zones").toArray()) {
            const auto zone = entry.toObject();
            const auto id = zone.value("id").toString();
            const auto r = rect(zone.value("rect"));
            if (id.isEmpty() || ids.contains(id) || !r.isValid() || !bounds.contains(r) ||
                !zone.value("radius").isDouble() || !kinds.contains(zone.value("kind").toString()))
                return source + ": invalid zone " + id;
            ids.insert(id);
        }

        // Экраны: id, единицы ref, ячейки с rect (после сетки), картинки есть в ресурсах.
        const auto exists = [&](const QString& file) { return candidate.contains(kPrefix + file); };
        QSet<QString> screenIds;
        for (const auto& value : remote.value("screens").toArray()) {
            const auto screenObject = value.toObject();
            const QString id = screenObject.value("id").toString();
            const auto ref = screenObject.value("ref").toArray();
            if (id.isEmpty() || screenIds.contains(id) || ref.size() != 2 || ref[0].toDouble() <= 0 ||
                ref[1].toDouble() <= 0 || !screenObject.value("cells").isArray())
                return source + ": invalid screen " + id;
            screenIds.insert(id);
            // Зоны фона: высоты не отрицательные, цвета разбираются, шапка и подвал помещаются в экран.
            const auto background = screenObject.value("background").toObject();
            for (const auto* zoneName : {"header", "footer"}) {
                if (!background.contains(zoneName)) continue;
                const auto zone = background.value(zoneName).toObject();
                if (!background.value(zoneName).isObject() ||
                    (zone.contains("color") && !QColor(zone.value("color").toString()).isValid()) ||
                    !zone.value("height").isDouble() || zone.value("height").toDouble() <= 0)
                    return source + ": invalid background." + zoneName + " in screen " + id;
            }
            if (background.contains("header") || background.contains("footer"))
                if (!screenZones(screenObject).any) return source + ": invalid background zones in screen " + id;
            for (const auto& cell : screenObject.value("cells").toArray())
                if (!rect(cell.toObject().value("rect")).isValid()) return source + ": invalid cell in screen " + id;
            // Плитка-картинка обязательна (без неё плитки не рисуются); фон без картинки
            // заливается цветом, поэтому его картинку не проверяем.
            const QString tile = screenObject.value("cell").toObject().value("image").toString();
            if (!tile.isEmpty() && !exists(tile)) return source + ": missing image " + tile;
            for (const auto& entry : screenObject.value("layers").toArray()) {
                const auto layer = entry.toObject();
                const QString file = layer.value("image").toString();
                if (file.isEmpty() || !exists(file)) return source + ": missing image " + file;
                if (!rect(layer.value("rect")).isValid()) return source + ": invalid layer in screen " + id;
            }
        }
        const bool writable = harmony::writableCombination(driver.toStdString(), format.toStdString());
        models.push_back({modelId.toStdString(), name.toStdString(), (dir + "/photo.png").toStdString(), writable,
                          driver.toStdString(), format.toStdString()});
        return {};
    };
    QStringList ids = parsed.keys();
    ids.sort();
    for (const QString& modelId : ids) {
        if (failed.contains(modelId)) continue;
        const QString error = checkModel(modelId, parsed.value(modelId));
        if (error.isEmpty()) continue;
        // Ошибка в штатном пульте останавливает загрузку; чужой пульт просто пропускается.
        if (isBundled(modelId)) return error;
        failed.insert(modelId, error);
    }
    // Файлы пропущенных пультов не попадают в снимок.
    for (auto bad = failed.cbegin(); bad != failed.cend(); ++bad) {
        const QString prefix = kRemotes + bad.key() + "/";
        for (auto it = candidate.begin(); it != candidate.end();)
            it = it.key().startsWith(prefix) ? candidate.erase(it) : std::next(it);
        parsed.remove(bad.key());
    }
    skippedModels = failed.values();
    skippedById = failed;
    migratedFiles = migrated;
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
    remotes.swap(parsed);
    harmony::remoteProfiles.swap(models);
    ++generation;
    return {};
}

QStringList modelIds(const QString& root) {
    QStringList result;
    for (const auto& profile : harmony::remoteProfiles) result.append(QString::fromStdString(profile.id));
    const QDir remotes(QDir(root).filePath(QStringLiteral("remotes")));
    for (const QString& id : remotes.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        // Папка с одним photo.png тоже модель: для неё редактор создаст заготовку (createStub).
        if (!result.contains(id) && (QFileInfo::exists(remotes.filePath(id + QStringLiteral("/remote.json"))) ||
                                     QFileInfo::exists(remotes.filePath(id + QStringLiteral("/photo.png")))))
            result.append(id);
    }
    return result;
}

QString createStub(const QString& modelId, const QString& root) {
    const QDir dir(QDir(root).filePath("remotes/" + modelId));
    const QString target = dir.filePath("remote.json");
    if (QFileInfo::exists(target)) return {};
    const QString photo = dir.filePath("photo.png");
    const QImage image(photo);
    if (image.isNull()) return photo + ": no valid photo.png";
    const QJsonObject screen{
        {"id", "devices"}, {"role", "devices"},
        {"ref", QJsonArray{232, 290}},
        {"background", QJsonObject{{"color", "#000000"}, {"glass", false}}},
        {"cells", QJsonArray{}}};
    const QJsonObject remote{
        {"schemaVersion", 1}, {"id", modelId}, {"name", modelId},
        {"capabilities", QJsonObject{{"touch", false}}},
        {"connection", QJsonObject{{"driver", "none"}, {"format", "none"}}},
        {"hardware", QJsonObject{{"image", "photo.png"},
                                 {"size", QJsonArray{image.width(), image.height()}},
                                 {"zones", QJsonArray{}}}},
        {"screens", QJsonArray{screen}}};
    QSaveFile out(target);
    const QByteArray bytes = QJsonDocument(remote).toJson(QJsonDocument::Indented);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
        return target + ": " + out.errorString();
    return {};
}

QStringList exportFiles(const QString& modelId) {
    const QString model = kPrefix + "remotes/" + modelId + "/";
    QStringList files;
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        if (modelId.isEmpty() || it.key().startsWith(model)) files.append(it.key().mid(kPrefix.size()));
    }
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
