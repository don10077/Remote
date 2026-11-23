/**
 * @file remoteView.cpp
 * @brief Виджет пульта: загрузка модели, открытый интерфейс, листание страниц экрана.
 */

#include "remoteView.h"
#include "remoteViewShared.h"
#include "../modelKeysQt.h"
#include "../remoteResources.h"

#include "harmony/core/keyVocabulary.h"
#include "harmony/core/remoteProfile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QFontMetrics>
#include <QPainter>
#include <algorithm>
#include <utility>

using harmony::app::RemoteView;
namespace resources = harmony::app::remoteResources;
using namespace harmony::app::remoteViewDetail;

RemoteView::RemoteView(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setAcceptDrops(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setRemoteProfile(QString());
}

void RemoteView::setRemoteProfile(const QString& id) {
    // U2: модели по умолчанию нет — пустой id значит, что модель не выбрана (Q11).
    const QString requested = id;
    if (profileId == requested && resourceRevision == resources::revision()) return;
    resourceRevision = resources::revision();
    profileId = requested;
    photo = QPixmap();
    photoSize = QSize(PHOTO_WIDTH, PHOTO_HEIGHT);
    modelScreen = {};
    screenTint = QColor();
    modelZones.clear();
    allZones.clear();
    modes = {};
    tiles = {};
    touchScreen = false;
    activityPage = softPage = otherPage = 0;
    screenHits.clear();
    hoverId.clear();
    hoverScreenHit = -1;
    pressKey.clear();
    pressSoft = -1;
    dragFlipZone.clear();
    unsetCursor();
    if (const auto* profile = harmony::remoteProfile(requested.toStdString())) {
        photo.loadFromData(resources::data(QString::fromUtf8(profile->image.data())));
        const QJsonObject remote = resources::remote(requested);
        const auto hardware = remote.value("hardware").toObject();
        const auto size = hardware.value("size").toArray();
        const QSize declaredSize(size.size() == 2 ? size[0].toInt() : 0,
                                 size.size() == 2 ? size[1].toInt() : 0);
        if (!photo.isNull()) photoSize = photo.size();
        if (!photo.isNull() && declaredSize == photoSize) {
            const QRectF bounds(QPointF(0, 0), QSizeF(photoSize));
            for (const auto& value : remote.value("screens").toArray()) {
                const auto screen = value.toObject();
                if (const QString screenId = screen.value("id").toString(); !screenId.isEmpty())
                    modes.insert(screenId, screen);
            }
            // Пульт без экрана устройств показывает их на экране активностей (или команд).
            if (!modes.contains("devices") && !modes.isEmpty())
                modes.insert("devices", modes.value(modes.contains("activities") ? "activities" : "command"));
            tiles = remote.value("tiles").toObject();
            touchScreen = remote.value("capabilities").toObject().value("touch").toBool();
            const auto display = remote.value("display").toObject();
            screenTint = QColor(display.value("tint").toString()); // пусто: цвет по умолчанию
            const QRectF screen = readRect(display.value("rect"));
            if (screen.isValid() && bounds.contains(screen)) modelScreen = screen;
            const QHash<QString, ZoneKind> kinds = {
                {"Key", ZoneKind::Key}, {"System", ZoneKind::System},
                {"ScreenActs", ZoneKind::ScreenActs}, {"ScreenSoft", ZoneKind::ScreenSoft},
                {"PagePrev", ZoneKind::PagePrev}, {"PageNext", ZoneKind::PageNext}};
            for (const auto& entry : hardware.value("zones").toArray()) {
                const auto zone = entry.toObject();
                const auto rect = readRect(zone.value("rect"));
                const auto key = zone.value("id").toString();
                const auto kind = kinds.constFind(zone.value("kind").toString());
                if (key.isEmpty() || !rect.isValid() || !bounds.contains(rect) ||
                    kind == kinds.cend() || !zone.value("radius").isDouble()) {
                    qWarning("Invalid zone in remote %s", qPrintable(requested));
                    continue;
                }
                modelZones.append({key, rect, zone.value("radius").toDouble(), kind.value()});
            }
        } else {
            qWarning("Remote photo size mismatch: %s", qPrintable(requested));
        }
    }
    auto policy = sizePolicy();
    policy.setHorizontalPolicy(isLandscape() ? QSizePolicy::Ignored : QSizePolicy::Preferred);
    policy.setHeightForWidth(isLandscape());
    setSizePolicy(policy);
    setMinimumHeight(0);
    selectScreenMode(activeMode);
    scaledPhoto = QPixmap();
    updateGeometry();
    update();
}

QByteArray RemoteView::commandPayload(const QString& deviceId, const QString& command) {
    return deviceId.toUtf8() + '\n' + command.toUtf8();
}

bool RemoteView::parseCommandPayload(const QByteArray& data, QString& deviceId, QString& command) {
    const qsizetype nl = data.indexOf('\n');
    if (nl <= 0) {
        return false;
    }
    deviceId = QString::fromUtf8(data.left(nl));
    command = QString::fromUtf8(data.mid(nl + 1));
    return !command.isEmpty();
}

QPixmap RemoteView::dragPixmap(const QString& text, QFont font) {
    font.setBold(true);
    const QFontMetrics fm(font);
    QPixmap pixmap(fm.horizontalAdvance(text) + 20, fm.height() + 10);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    paintPill(p, QRectF(pixmap.rect()).adjusted(1, 1, -1, -1), true, false);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(pixmap.rect(), Qt::AlignCenter, text);
    return pixmap;
}

// ── public API ───────────────────────────────────────────────────────────────

void RemoteView::setMode(Mode mode) {
    if (currentMode == mode) {
        return;
    }
    currentMode = mode;
    update();
}

void RemoteView::setDeviceKeys(const QHash<QString, KeyState>& states) {
    deviceKeys = states;
    update();
}

void RemoteView::setActivityKeys(const QHash<QString, QColor>& colours) {
    activityKeys = colours;
    update();
}

void RemoteView::setSelectedKey(const QString& id) {
    selectedKeys.clear();
    if (selectedKey == id) {
        update();
        return;
    }
    selectedKey = id;
    update();
}

void RemoteView::setSelectedKeys(const QSet<QString>& ids) {
    selectedKey.clear();
    selectedKeys = ids;
    update();
}

void RemoteView::setDeviceNames(const QStringList& names) {
    deviceItems.clear();
    for (const auto& name : names) deviceItems.append({name, {}, false});
    if (currentScreen == Screen::Devices) otherPage = std::clamp(otherPage, 0, pageCount() - 1);
    update();
}

void RemoteView::setScreen(Screen screen) {
    selectScreenMode(screen == Screen::Devices ? "devices" : screen == Screen::Activities ? "activities" : "command");
}

void RemoteView::selectScreenMode(const QString& mode) {
    // Неизвестный режим: activities, затем command (у 600-серии нет экрана активностей),
    // затем первый размеченный режим; пусто, только если у профиля нет экрана.
    if (modes.contains(mode)) activeMode = mode;
    else if (modes.contains("activities")) activeMode = QStringLiteral("activities");
    else if (modes.contains("command")) activeMode = QStringLiteral("command");
    else activeMode = modes.isEmpty() ? QString() : modes.keys().constFirst();
    // Избранное по умолчанию показывает те же экранные кнопки, что и «Команды».
    currentScreen = activeMode == "devices" ? Screen::Devices : activeMode == "activities" ? Screen::Activities :
                    (activeMode == "command" || activeMode == "favorites") ? Screen::SoftButtons
                                                                           : Screen::Other;
    screenHits.clear();
    hoverScreenHit = -1;
    hoverId.clear();
    otherPage = 0;
    rebuildScreenZones();
    update();
}

void RemoteView::rebuildScreenZones() {
    allZones = modelZones;
    if (activeMode.isEmpty() || !modelScreen.isValid()) return;
    const auto layout = modes.value(activeMode).toObject();
    const auto ref = layout.value("ref").toArray();
    if (ref.size() != 2 || ref[0].toDouble() <= 0 || ref[1].toDouble() <= 0) return;
    const qreal unit = modelScreen.width() / ref[0].toDouble();
    // Кнопки экрана — первыми: при поиске под курсором они важнее зон фото.
    QList<Zone> screen;
    for (const auto& value : layout.value("cells").toArray()) {
        const auto zone = value.toObject();
        const QRectF rect = readRect(zone.value("rect"));
        if (rect.isEmpty()) continue;
        const qreal r = zone.value("radius").toDouble();
        const qreal radius = r < 0 ? r : r * unit;
        const QRectF photoRect = screenToPhoto(rect);
        const QString id = zone.value("id").toString();
        if (const QString key = zone.value("key").toString(); !key.isEmpty()) {
            screen.append({key, photoRect, radius, ZoneKind::Key});
            // ячейка у физической кнопки (1100): та же кнопка и по нажатию на корпусе
            if (const QString button = zone.value("button").toString(); !button.isEmpty()) {
                for (const auto& hard : modelZones)
                    if (hard.id == button) screen.append({key, hard.rect, hard.radius, ZoneKind::Key});
            }
        } else if (const QString mode = zone.value("goto").toString(); !mode.isEmpty()) {
            // экрана нет у модели (не размечен) — кнопка остаётся картинкой
            if (modes.contains(mode)) screen.append({id, photoRect, radius, ZoneKind::ScreenMode, mode});
        } else if (zone.value("page").isDouble()) {
            screen.append({id, photoRect, radius,
                           zone.value("page").toInt() < 0 ? ZoneKind::PagePrev : ZoneKind::PageNext});
        } else if (zone.value("system").toBool()) {
            screen.append({id, photoRect, radius, ZoneKind::System});
        }
    }
    allZones = screen + modelZones;
}

QString RemoteView::screenModeName(const QString& mode) {
    // Литералы, чтобы lupdate нашёл строки; id режимов в файлах не меняются.
    static const QHash<QString, const char*> names = {
        {"activities", QT_TR_NOOP("Activities")},
        {"command", QT_TR_NOOP("Commands")},
        {"favorites", QT_TR_NOOP("Favorites")},
        {"transport", QT_TR_NOOP("Media")},
        {"keypad", QT_TR_NOOP("Keypad")},
        {"devices", QT_TR_NOOP("Devices")},
        {"options", QT_TR_NOOP("Options")},
    };
    const auto found = names.constFind(mode);
    if (found != names.cend()) return tr(found.value());
    // Добавленные экраны: «command2» — «Команды 2».
    int end = static_cast<int>(mode.size());
    while (end > 0 && mode[end - 1].isDigit()) --end;
    if (end > 0 && end < mode.size()) {
        const auto base = names.constFind(mode.left(end));
        if (base != names.cend()) return tr(base.value()) + QLatin1Char(' ') + mode.mid(end);
    }
    return mode;
}

void RemoteView::setActivities(const QList<ActivityItem>& items) {
    activities = items;
    const int pages = std::max(
        1, static_cast<int>((activities.size() + slotsPerPage("activities") - 1) / slotsPerPage("activities")));
    activityPage = std::clamp(activityPage, 0, pages - 1);
    update();
}

void RemoteView::setSoftButtons(const QString& title, const QStringList& labels) {
    softTitle = title;
    softLabels = labels;
    const int capacity = slotsPerPage("command");
    const int pages = std::max(1, (int(softLabels.size()) + capacity - 1) / capacity);
    softPage = std::clamp(softPage, 0, pages - 1);
    update();
}

void RemoteView::setSelectedSoft(int position) {
    if (selectedSoft == position) {
        return;
    }
    selectedSoft = position;
    update();
}

void RemoteView::showActivityPage(int index) {
    if (index < 0) {
        return;
    }
    activityPage = index / slotsPerPage("activities");
    update();
}

void RemoteView::showSoftPage(int position) {
    if (position < 0) {
        return;
    }
    softPage = position / slotsPerPage("command");
    update();
}

QStringList RemoteView::hardKeyIds(const QString& remoteId) {
    // K5: решение о списке клавиш принимает `modelKeyIds()` — без фото и без модели по
    // умолчанию: пустой id значит, что модель не выбрана (Q3). Прежний кеш по id модели и
    // ревизии ресурсов снят: список зависит ещё и от конфигурации документа (вопрос 2 отчёта).
    return modelKeyIds(remoteId);
}

QStringList RemoteView::sharedKeyIds() {
    // Имена словаря, допустимые для вида `Key`, в порядке словаря. Список не зависит ни от
    // профиля модели, ни от скан-кода: это список имён, а не список клавиш какой-либо модели.
    return dictionaryIdsForKind(harmony::PhysicalKind::Key);
}

QString RemoteView::keyName(const QString& id) {
    // Имя кнопки показывается как есть: id зоны — всегда английское имя из словаря
    // (harmony/core/keyVocabulary.h), и переводить его нельзя, иначе оно перестанет совпадать
    // с именами в конфигурациях других пультов.
    return id;
}

QSize RemoteView::sizeHint() const {
    return photoSize;
}

QSize RemoteView::minimumSizeHint() const {
    if (!isLandscape()) return {0, 0};
    return {240, heightForWidth(240)};
}

int RemoteView::heightForWidth(int width) const {
    return (width * photoSize.height()) / photoSize.width();
}

// ── paging ───────────────────────────────────────────────────────────────────

int RemoteView::slotsPerPage(const QString& mode) const {
    const auto zones = modes.value(mode).toObject().value("cells").toArray();
    return std::max(1, int(std::count_if(zones.begin(), zones.end(), [](const QJsonValue& zone) {
        return zone.toObject().value("kind") == "ScreenSlot";
    })));
}

int RemoteView::pageCount() const {
    const int count = currentScreen == Screen::Activities ? activities.size() :
                      currentScreen == Screen::SoftButtons ? softLabels.size() :
                      activeMode == "devices" ? deviceItems.size() : 0;
    const int capacity = slotsPerPage(activeMode);
    return std::max(1, (count + capacity - 1) / capacity);
}

int RemoteView::currentPage() const {
    if (currentScreen == Screen::Other || currentScreen == Screen::Devices) return otherPage;
    return currentScreen == Screen::Activities ? activityPage : softPage;
}

void RemoteView::flipPage(int delta) {
    const int page = std::clamp(currentPage() + delta, 0, pageCount() - 1);
    if (currentScreen == Screen::Activities) {
        activityPage = page;
    } else {
        if (currentScreen == Screen::Other || currentScreen == Screen::Devices) otherPage = page;
        else softPage = page;
    }
    hoverScreenHit = -1;
    update();
}
