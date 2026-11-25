#include "remoteResources.h"
#include "harmony/core/remoteCatalog.h"
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
/// Проверенные описания пультов последнего снимка: id → профиль с достроенными rect ячеек.
QHash<QString, QJsonObject> remotes;
quint64 generation = 0;
/// Пользовательские модели, пропущенные последним reload() из-за ошибок (тексты ошибок).
QStringList skippedModels;
QHash<QString, QString> skippedById;
/// Профили в старой схеме, переведённые последним reload() на новую (пути к файлам).
QStringList migratedFiles;
/// Предупреждения последнего reload(): модель загружена, но данные расходятся.
QStringList warnings;
const QString kPrefix = kResourcePrefix;
const QString kRemotes = kPrefix + kRemotesDir + QLatin1Char('/');
/// Проверить конечные координаты прямоугольника и положительный размер.
QRectF rect(const QJsonValue& value) {
    const auto a = value.toArray();
    if (a.size() != 4) return {};
    for (const auto& n : a) if (!n.isDouble() || !std::isfinite(n.toDouble())) return {};
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
}
/// Путь профиля от корня папки ресурсов: `remotes/<id>/profile.json`.
QString profileKey(const QString& modelId) {
    return kRemotesDir + QLatin1Char('/') + modelId + QLatin1Char('/') + kProfileFile;
}
/// Старое имя профиля от корня папки ресурсов: `remotes/<id>/remote.json`.
QString legacyKey(const QString& modelId) {
    return kRemotesDir + QLatin1Char('/') + modelId + QLatin1Char('/') + kLegacyProfileFile;
}
/// Файл профиля пульта: `remotes/<id>/profile.json` или старое имя, путь от корня папки ресурсов.
bool profileFile(const QString& relative) {
    if (!relative.startsWith(kRemotesDir + QLatin1Char('/'))) return false;
    const QString name = relative.section('/', 2, 2);
    return relative.count(QLatin1Char('/')) == 2 && (name == kProfileFile || name == kLegacyProfileFile);
}
/// Имя файла — старое (`remote.json`), а не `profile.json`.
bool legacyProfile(const QString& relative) {
    return relative.endsWith("/" + kLegacyProfileFile);
}
/// Встроенный профиль пульта: `profile.json`, пока его нет — старый `remote.json`; пусто — нет.
QByteArray builtinProfile(const QString& modelId) {
    for (const QString& name : {kProfileFile, kLegacyProfileFile}) {
        QFile file(kRemotes + modelId + "/" + name);
        if (file.open(QIODevice::ReadOnly)) return file.readAll();
    }
    return {};
}
/**
 * Файл относится к ресурсам пультов: путь от корня папки ресурсов.
 * Из JSON читается только профиль пульта; оставшиеся от старых версий zones.json,
 * screens.json и layouts.json не читаются и не мешают загрузке.
 */
bool managed(const QString& relative) {
    if (relative.endsWith(".json")) return profileFile(relative);
    return (relative.startsWith(kRemotesDir + QLatin1Char('/')) ||
            relative.startsWith(kSpritesDir + QLatin1Char('/'))) &&
           (relative.endsWith(".png") || relative.endsWith(".svg"));
}
}

QString profileFilePath(const QDir& modelDir) {
    if (modelDir.exists(kProfileFile)) return modelDir.filePath(kProfileFile);
    if (modelDir.exists(kLegacyProfileFile)) return modelDir.filePath(kLegacyProfileFile);
    return modelDir.filePath(kProfileFile);
}

QString folder() { return QDir(QCoreApplication::applicationDirPath()).filePath("resources"); }
quint64 revision() { return generation; }
QStringList skipped() { return skippedModels; }
QStringList migratedByReload() { return migratedFiles; }
QStringList profileWarnings() { return warnings; }

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
        const QByteArray bytes = builtinProfile(modelId);
        if (bytes.isEmpty()) return {};
        return normalizeRemote(QJsonDocument::fromJson(bytes).object());
    }
    return remotes.value(modelId);
}

RemoteFile readRemote(const QString& modelId, const QString& root) {
    RemoteFile file;
    file.path = profileKey(modelId);
    const QDir dir(root);
    // На диске: profile.json, пока его нет — старый remote.json; иначе встроенный ресурс.
    QString source = file.path;
    if (!QFileInfo::exists(dir.filePath(source)) && QFileInfo::exists(dir.filePath(legacyKey(modelId))))
        source = legacyKey(modelId);
    QFile input(dir.filePath(source));
    if (input.open(QIODevice::ReadOnly)) {
        file.sourcePath = source;
    } else {
        input.setFileName(kPrefix + file.path);
        if (!input.open(QIODevice::ReadOnly)) input.setFileName(kPrefix + legacyKey(modelId));
        if (!input.isOpen() && !input.open(QIODevice::ReadOnly)) {
            file.error = file.path + ": " + input.errorString();
            return file;
        }
    }
    const QString named = file.sourcePath.isEmpty() ? file.path : file.sourcePath;
    file.text = input.readAll();
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.text, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        file.error = named + ": " +
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
    if (const QByteArray bytes = builtinProfile(modelId); !bytes.isEmpty())
        builtin = QJsonDocument::fromJson(bytes).object();

    // W3: `connection.driver` — способ подключения модели, заданный пользователем; если поля
    // нет, связь берётся из каталога. Поля format, configFormat и transport из прошлой схемы
    // больше не участвуют: они убираются, имена драйвера переводятся на текущие.
    const auto connection = remote.value("connection").toObject();
    const QString driver = connection.value("driver").toString();
    const QString normalized = driver == "harmony900" ? QStringLiteral("usb-net")
                               : driver == "hid"      ? QStringLiteral("usb-hid")
                                                      : driver;
    const bool oldFields = connection.contains("format") || connection.contains("configFormat")
                        || connection.contains("transport");
    if (oldFields || driver != normalized) {
        if (normalized.isEmpty()) remote.remove("connection");
        else remote.insert("connection", QJsonObject{{"driver", normalized}});
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
    warnings.clear();
    if (const QString error = prepareFolder(root); !error.isEmpty()) return error;
    QHash<QString, QByteArray> candidate;
    QHash<QString, QString> sources;
    // У пульта один профиль. Старый remote.json читается, пока profile.json нет, но ключ снимка
    // всегда profile.json: иначе описание пульта попало бы дважды. Оба файла — предупреждение.
    QStringList both;
    for (const QString& base : {root}) {
        if (base.isEmpty()) continue;
        QDirIterator files(base, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString file = files.next();
            const QString relative = QDir(base).relativeFilePath(file);
            if (!managed(relative)) continue;
            QString key = ":/res/" + relative;
            if (profileFile(relative)) {
                const QString modelId = relative.section('/', 1, 1);
                const QString canonical = QDir(base).filePath(profileKey(modelId));
                const QString legacy = QDir(base).filePath(legacyKey(modelId));
                if (legacyProfile(relative)) {
                    if (QFileInfo::exists(canonical)) {
                        if (!both.contains(modelId)) both.append(modelId);
                        continue; // profile.json прочитается своим файлом
                    }
                    key = ":/res/" + profileKey(modelId);
                } else if (QFileInfo::exists(legacy) && !both.contains(modelId)) {
                    both.append(modelId);
                }
            }
            QFile input(file);
            if (!input.open(QIODevice::ReadOnly)) return file + ": " + input.errorString();
            candidate.insert(key, input.readAll());
            sources.insert(key, file);
        }
    }
    warnings.clear();
    for (const QString& modelId : both)
        warnings.append(QStringLiteral("remotes/") + modelId + QStringLiteral(": both ") + kProfileFile +
                        QStringLiteral(" and ") + kLegacyProfileFile + QStringLiteral(" exist; ") +
                        kProfileFile + QStringLiteral(" is used"));
    // Профиль прошлой версии программы переводится на новую схему в памяти: лишний connection
    // (связь теперь в каталоге модели) и старые style и compact экранов. Правки пользователя
    // (зоны, цвета, экраны) сохраняются. Файл на диске не меняется,
    // редактор зон запишет его в новой схеме при следующем сохранении.
    QStringList migrated;
    for (auto it = candidate.begin(); it != candidate.end(); ++it) {
        const QString& key = it.key();
        if (!key.endsWith("/" + kProfileFile)) continue;
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(it.value(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) continue; // ошибка — ниже
        QJsonObject remote = document.object();
        const QString modelId = key.mid(kRemotes.size()).section('/', 0, 0);
        if (!migrateRemote(remote, modelId)) continue;
        it.value() = QJsonDocument(remote).toJson(QJsonDocument::Compact);
        migrated.append(sources.value(key));
    }
    QHash<QString, QJsonObject> parsed; // id пульта → профиль
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
        const QString source = sources.value(dir + "/" + kProfileFile);
        static const QRegularExpression validId("^[a-z0-9][a-z0-9_-]*$");
        if (!validId.match(modelId).hasMatch()) return source + ": invalid model id";
        const QString name = remote.value("name").toString().trimmed();
        const auto hardware = remote.value("hardware").toObject();
        // Как и раньше, без зон корпуса пульт не загружается (пустой массив допустим).
        if (name.isEmpty() || !hardware.value("zones").isArray())
            return source + ": name and hardware.zones are required";

        // W3: проверяется только driver: это значение пользователя, задающее способ связи.
        // Пустое поле или поля нет — способ связи берётся из каталога модели.
        const auto connection = remote.value("connection").toObject();
        if (connection.contains("driver") && !connection.value("driver").isString())
            return source + ": invalid connection.driver";
        const QString driver = connection.value("driver").toString();
        if (!driver.isEmpty() && !harmony::linkFromName(driver.toStdString()))
            return source + ": unknown connection driver";

        const auto size = hardware.value("size").toArray();
        const auto image = QImage::fromData(candidate.value(dir + "/" + kPhotoFile));
        if (size.size() != 2 || QSize(size[0].toInt(), size[1].toInt()) != image.size() || image.isNull() ||
            remote.value("id").toString() != modelId || hardware.value("image") != kPhotoFile)
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
        models.push_back({modelId.toStdString(), name.toStdString(), (dir + "/" + kPhotoFile).toStdString(), driver.toStdString()});
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
    const QDir remotesDir(QDir(root).filePath(QStringLiteral("remotes")));
    for (const QString& id : remotesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        // Папка с одним photo.png тоже модель: для неё редактор создаст заготовку (createStub).
        const QDir dir(remotesDir.filePath(id));
        const bool hasProfile = QFileInfo::exists(dir.filePath(kProfileFile)) ||
                                QFileInfo::exists(dir.filePath(kLegacyProfileFile));
        if (!result.contains(id) && (hasProfile || QFileInfo::exists(dir.filePath(kPhotoFile))))
            result.append(id);
    }
    return result;
}

QString createStub(const QString& modelId, const QString& root) {
    const QDir dir(QDir(root).filePath("remotes/" + modelId));
    const QString target = dir.filePath(kProfileFile);
    // Старый файл профиля тоже считается существующим: заготовка рядом с ним только мешала бы.
    if (QFileInfo::exists(target) || QFileInfo::exists(dir.filePath(kLegacyProfileFile))) return {};
    const QString photo = dir.filePath(kPhotoFile);
    const QImage image(photo);
    if (image.isNull()) return photo + ": no valid " + kPhotoFile;
    const QJsonObject screen{
        {"id", "devices"}, {"role", "devices"},
        {"ref", QJsonArray{232, 290}},
        {"background", QJsonObject{{"color", "#000000"}, {"glass", false}}},
        {"cells", QJsonArray{}}};
    const QJsonObject remote{
        {"schemaVersion", 1}, {"id", modelId}, {"name", modelId},
        {"capabilities", QJsonObject{{"touch", false}}},
        {"hardware", QJsonObject{{"image", kPhotoFile},
                                 {"size", QJsonArray{image.width(), image.height()}},
                                 {"zones", QJsonArray{}}}},
        {"screens", QJsonArray{screen}}};
    QSaveFile out(target);
    const QByteArray bytes = QJsonDocument(remote).toJson(QJsonDocument::Indented);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
        return target + ": " + out.errorString();
    return {};
}

QString archiveLegacyProfile(const QString& modelId, const QString& root) {
    const QDir dir(QDir(root).filePath("remotes/" + modelId));
    const QString legacy = dir.filePath(kLegacyProfileFile);
    // Убирать нечего или профиль ещё не записан под новым именем — файл не трогаем.
    if (!QFileInfo::exists(legacy) || !QFileInfo::exists(dir.filePath(kProfileFile))) return {};
    const QString backup = dir.filePath(kLegacyProfileBackup);
    if (QFileInfo::exists(backup) && !QFile::remove(backup)) return backup + ": cannot replace the backup";
    QFile file(legacy);
    if (!file.rename(backup)) return legacy + ": " + file.errorString();
    return {};
}

QString adoptLegacyProfile(const QString& modelId, const QString& root) {
    const QDir dir(QDir(root).filePath("remotes/" + modelId));
    const QString legacy = dir.filePath(kLegacyProfileFile);
    const QString target = dir.filePath(kProfileFile);
    if (!QFileInfo::exists(legacy) || QFileInfo::exists(target)) return {};
    QFile input(legacy);
    if (!input.open(QIODevice::ReadOnly)) return legacy + ": " + input.errorString();
    const QByteArray bytes = input.readAll();
    if (input.error() != QFileDevice::NoError) return legacy + ": " + input.errorString();
    input.close();
    // Сначала profile.json с тем же текстом, потом старый файл в .bak: если второй шаг не удался,
    // в папке остаются оба файла (читается profile.json), а не потерянный профиль.
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
        return target + ": " + out.errorString();
    return archiveLegacyProfile(modelId, root);
}

QStringList exportFiles(const QString& modelId) {
    const QString model = kRemotes + modelId + "/";
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
        // У пульта, у которого уже есть старый remote.json, встроенный profile.json не кладём:
        // иначе правки пользователя оказались бы под чужим файлом с тем же именем пульта.
        if (!legacyProfile(relative) && profileFile(relative)) {
            const QDir modelDir = QFileInfo(base.filePath(relative)).dir();
            if (modelDir.exists(kLegacyProfileFile)) continue;
        }
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
        if (entries.fileInfo().isDir() ? relative == kRemotesDir || relative == kSpritesDir ||
                                             relative.startsWith(kRemotesDir + QLatin1Char('/')) || relative.startsWith(kSpritesDir + QLatin1Char('/'))
                                       : managed(relative))
            paths.append(path);
    }
    return paths;
}
}
