#pragma once

/**
 * @file remoteViewShared.h
 * @brief Помощники RemoteView, общие для нескольких его файлов
 *        (раньше — анонимное пространство имён remoteView.cpp).
 */

#include <QLinearGradient>
#include <QPainter>
#include <QColor>
#include <QJsonValue>
#include <QPen>
#include <QRectF>

#include <algorithm>

namespace harmony::app::remoteViewDetail {

/** Прямоугольник [x, y, width, height]; некорректные данные дают пустую область. */
inline QRectF readRect(const QJsonValue& value) {
    const auto a = value.toArray();
    if (a.size() != 4 || !std::all_of(a.begin(), a.end(), [](const QJsonValue& v) {
            return v.isDouble();
        })) return {};
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
}

/// Цвет плиток и фона экрана по умолчанию (классический экран 900), если в remote.json цвет не задан.
inline const QColor DEFAULT_TILE_COLOUR("#0b3442");

/** pill with the look of the Harmony LCD buttons */
inline void paintPill(QPainter& p, const QRectF& r, bool selected, bool hover,
               const QColor& base = DEFAULT_TILE_COLOUR, const QColor& border = QColor("#ffffff")) {
    // Цвета по умолчанию (из remote.json) дают классический вид пульта; другой цвет плитки
    // и рамки перекрашивает плитку, сохраняя объём (светлый верх, тёмный низ).
    const bool customFill = base.isValid() && base != DEFAULT_TILE_COLOUR;
    const bool customBorder = border.isValid() && border != QColor("#ffffff");
    QLinearGradient fill(r.topLeft(), r.bottomLeft());
    fill.setColorAt(0.0, customFill ? base.lighter(190) : QColor("#1f5a6c"));
    fill.setColorAt(1.0, customFill ? base.darker(120) : QColor("#0a2631"));
    if (selected) {
        // soft glow around the selected pill
        p.setPen(QPen(QColor(140, 196, 232, 110), 6.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, 6.0, 6.0);
    }
    const QColor line = customBorder ? border : QColor("#bcd3db");
    p.setPen(QPen(selected ? QColor("#ffffff") : (hover ? line.lighter(120) : line),
                  selected ? 2.5 : 2.0));
    p.setBrush(fill);
    p.drawRoundedRect(r, 6.0, 6.0);
}

} // namespace harmony::app::remoteViewDetail
