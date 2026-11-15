#pragma once

/**
 * @file zoneEditorShared.h
 * @brief Помощники редактора зон, общие для нескольких файлов ZoneEditorDialog
 *        (раньше — анонимное пространство имён zoneEditorDialog.cpp).
 */

#include "../zoneEditing.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace harmony::app::zoneEditorDetail {

/// Роль экрана, которую можно добавить: роль в remote.json и id экрана по умолчанию.
struct ScreenRole {
    const char* role;
    const char* id;
};

inline constexpr ScreenRole kScreenRoles[] = {{"activity", "activities"}, {"favorites", "favorites"},
                                       {"devices", "devices"},     {"command", "command"},
                                       {"media", "transport"},     {"numpad", "keypad"}};

/// Служебный элемент экрана: ячейка `ScreenLabel` с одним из этих id; первый id — для новой ячейки.
struct ServiceItem {
    const char* title;            ///< подпись флажка (переводится в ZoneEditorDialog)
    QStringList ids;              ///< id ячеек, которые считаются этим элементом
    QRectF place;                 ///< место новой ячейки в долях `ref`
};

inline const QList<ServiceItem>& serviceItems() {
    static const QList<ServiceItem> items{
        {QT_TRANSLATE_NOOP("ZoneEditorDialog", "Title"), {"Title", "Caption"}, {0.02, 0.01, 0.55, 0.08}},
        {QT_TRANSLATE_NOOP("ZoneEditorDialog", "Page number"), {"Page", "PageLabel"}, {0.02, 0.91, 0.25, 0.07}},
        {QT_TRANSLATE_NOOP("ZoneEditorDialog", "Clock"), {"Clock"}, {0.78, 0.01, 0.20, 0.07}},
        {QT_TRANSLATE_NOOP("ZoneEditorDialog", "Battery"), {"Battery"}, {0.64, 0.015, 0.12, 0.05}},
    };
    return items;
}

/// Значение по пути ключей; Undefined — ключа нет.
inline QJsonValue valueAt(const QJsonObject& object, const QStringList& keys) {
    QJsonValue value = object;
    for (const QString& key : keys) value = value.toObject().value(key);
    return value;
}

/// Значение из списка констант ядра как QString.
inline QString fromView(std::string_view value) { return QString::fromLatin1(value.data(), static_cast<qsizetype>(value.size())); }

/// Зона — плитка экрана (а не заголовок, боковая кнопка или панель).
inline bool isSlot(const QJsonObject& zone) {
    return zone.value(QStringLiteral("kind")).toString(QStringLiteral("ScreenSlot")) == QLatin1String("ScreenSlot");
}

/**
 * @brief Зона плиток по самим плиткам: прямоугольник, который они занимают, плюс зазор вокруг.
 * @return nullopt — у экрана нет плиток.
 */
inline std::optional<QRectF> tilesBounds(const QJsonArray& zones, qreal gap) {
    QRectF area;
    for (const auto& value : zones) {
        const QJsonObject zone = value.toObject();
        if (isSlot(zone)) area = area.isNull() ? zoneEditing::rectOf(zone) : area.united(zoneEditing::rectOf(zone));
    }
    if (!area.isValid()) return std::nullopt;
    return area.adjusted(-gap, -gap, gap, gap);
}

/// `[x, y, w, h]` с округлением до сотых, как пишутся прямоугольники в remote.json.
inline QJsonArray rectArray(const QRectF& r) {
    using zoneEditing::round2;
    return {round2(r.x()), round2(r.y()), round2(r.width()), round2(r.height())};
}

/**
 * @brief Сетка плиток экрана: колонки и ряды по числу различных x и y левых верхних углов плиток.
 * @return Размер cols × rows, каждое значение от 1 до 12.
 */
inline QSize slotGrid(const QJsonArray& zones) {
    QSet<int> xs;
    QSet<int> ys;
    for (const auto& value : zones) {
        const QJsonObject zone = value.toObject();
        if (!isSlot(zone)) continue;
        const QRectF r = zoneEditing::rectOf(zone);
        xs.insert(static_cast<int>(std::lround(r.x())));
        ys.insert(static_cast<int>(std::lround(r.y())));
    }
    return {std::clamp(static_cast<int>(xs.size()), 1, 12), std::clamp(static_cast<int>(ys.size()), 1, 12)};
}

} // namespace harmony::app::zoneEditorDetail
