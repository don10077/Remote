/**
 * @file remoteViewPaint.cpp
 * @brief Виджет пульта: отрисовка корпуса, зон и экрана.
 */

#include "remoteView.h"
#include "remoteViewShared.h"
#include "../remoteResources.h"
#include "../theme.h"

#include "activityIcon.h"

#include <QFontMetricsF>
#include <QJsonObject>
#include <QImage>
#include <QLocale>
#include <QSvgRenderer>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTime>
#include <algorithm>
#include <memory>
#include <vector>

using harmony::app::RemoteView;
namespace resources = harmony::app::remoteResources;
namespace theme = harmony::app::theme;
using namespace harmony::app::remoteViewDetail;

namespace {

using harmony::app::activityPixmap;

/**
 * Оформление плиток экрана: `cell` экрана, поверх него общий для пульта блок `tiles`
 * (цвет и рамка плиток у всех экранов пульта одинаковые).
 */
QJsonObject tileStyle(const QJsonObject& screen, const QJsonObject& tiles) {
    QJsonObject style = screen.value("cell").toObject();
    for (auto it = tiles.begin(); it != tiles.end(); ++it) style.insert(it.key(), it.value());
    return style;
}

// LCD size in photo pixels. The LCD content is laid out in these units and
// scaled together with the photo.
constexpr qreal LCD_W = 232.0;
constexpr qreal LCD_H = 290.0;
constexpr qreal LCD_HEAD = 52.0; // black title band
constexpr qreal LCD_FOOT = 24.0; // black Options / Devices band
constexpr qreal LCD_TOP = 58.0;  // first content row
constexpr qreal LCD_ROW_H = 62.0;
constexpr qreal LCD_ROW_GAP = 8.0;
constexpr qreal LCD_PAD = 8.0;

/**
 * Общий фон экрана пульта (радиальный градиент). Единственное место, где он рисуется:
 * классические и созданные в редакторе экраны используют одну и ту же функцию.
 */
void paintLcdBackdrop(QPainter& p, qreal w, qreal h) {
    QRadialGradient bg(QPointF(w / 2.0, h * 0.55), h * 0.62);
    bg.setColorAt(0.0, QColor("#1d7087"));
    bg.setColorAt(0.5, DEFAULT_TILE_COLOUR);
    bg.setColorAt(1.0, QColor("#031017"));
    p.fillRect(QRectF(0, 0, w, h), bg);
}

/** Чёрная шапка: заголовок, батарейка, номер страницы и часы (в единицах LCD_W). */
void paintLcdHeader(QPainter& p, const QFont& base, qreal w, const QString& titleText,
                    const QString& pageText) {
    p.fillRect(QRectF(0, 0, w, LCD_HEAD), Qt::black);
    QFont title = base;
    title.setPixelSize(19);
    title.setBold(true);
    p.setFont(title);
    p.setPen(Qt::white);
    p.drawText(QRectF(8, 2, w - 44, 26), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetricsF(title).elidedText(titleText, Qt::ElideRight, w - 44));
    // батарейка
    p.setPen(QPen(QColor("#d7dde0"), 1.5));
    p.setBrush(QColor("#3ecf5b"));
    p.drawRect(QRectF(w - 30, 9, 20, 10));
    p.fillRect(QRectF(w - 10, 12, 3, 4), QColor("#d7dde0"));

    QFont small = base;
    small.setPixelSize(13);
    small.setBold(true);
    p.setFont(small);
    p.setPen(Qt::white);
    p.drawText(QRectF(8, 28, 120, 20), Qt::AlignLeft | Qt::AlignVCenter, pageText);
    p.drawText(QRectF(w - 80, 28, 72, 20), Qt::AlignRight | Qt::AlignVCenter,
               QTime::currentTime().toString("HH:mm"));
}

/** Нижняя чёрная полоса с подписями белых клавиш Options / Devices. */
void paintLcdFooter(QPainter& p, const QFont& base, qreal w, qreal h, const QString& options,
                    const QString& devices) {
    QFont small = base;
    small.setPixelSize(13);
    small.setBold(true);
    p.fillRect(QRectF(0, h - LCD_FOOT, w, LCD_FOOT), Qt::black);
    p.setFont(small);
    p.setPen(QColor("#9bd46f"));
    p.drawText(QRectF(0, h - LCD_FOOT, w / 2.0, LCD_FOOT), Qt::AlignCenter, options);
    p.setPen(QColor("#f08a4b"));
    p.drawText(QRectF(w / 2.0, h - LCD_FOOT, w / 2.0, LCD_FOOT), Qt::AlignCenter, devices);
}

// ── содержимое экранов: только раскладка и данные конфигурации ─────────────

/** Содержимое одной ячейки экрана. */
struct CellContent {
    QString icon;         ///< id кнопки со значком (рисуется картинкой экрана, не кодом)
    QString activityType; ///< тип активности: значок берётся из прошивки (PNG)
    QString label;
    QString sublabel;
    QColor colour = Qt::white;     ///< цвет подписи
    QColor iconColour = Qt::white; ///< цвет значка
    bool active = false;      ///< текущий экран (угол) или выбранная плитка
    bool accent = false;       ///< выделенная кнопка (Pause)
    bool empty = false;
    bool disabled = false;
};

/**
 * Шрифт подписей на экране. Полужирный легче и ближе к надписям пульта, чем Bold
 * (у Segoe UI есть начертание Semibold).
 */
QFont screenFont(const QPainter& p, bool bold) {
    QFont font = p.font();
    font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    return font;
}

/**
 * Наибольший размер шрифта (в пикселях), при котором текст с переносами помещается в rect.
 * maxPixels > 0 — не крупнее этого размера (общий размер подписей одного экрана).
 */
int fittedPixels(const QFont& base, const QRectF& rect, const QString& text, qreal startFactor,
                 qreal minFactor, int align = Qt::AlignCenter, int maxPixels = 0) {
    QFont font = base;
    int pixels = std::max(6, static_cast<int>(rect.height() * startFactor));
    if (maxPixels > 0) pixels = std::min(pixels, maxPixels);
    const int minimum = std::max(5, static_cast<int>(rect.height() * minFactor));
    for (; pixels > minimum; --pixels) {
        font.setPixelSize(pixels);
        const QFontMetricsF metrics(font);
        const QRectF needed = metrics.boundingRect(rect, align | Qt::TextWordWrap, text);
        if (needed.height() <= rect.height() && needed.width() <= rect.width() + 1.0) break;
    }
    return pixels;
}

/** Подобрать размер шрифта так, чтобы текст с переносами поместился в rect. */
void drawFittedText(QPainter& p, const QRectF& rect, const QString& text, const QColor& colour,
                    bool bold, qreal opacity, qreal startFactor, qreal minFactor,
                    int align = Qt::AlignCenter, int maxPixels = 0) {
    if (text.isEmpty() || rect.isEmpty()) return;
    QFont font = screenFont(p, bold);
    font.setPixelSize(fittedPixels(font, rect, text, startFactor, minFactor, align, maxPixels));
    p.save();
    p.setOpacity(p.opacity() * std::clamp(opacity, 0.0, 1.0));
    p.setFont(font);
    p.setPen(colour);
    p.drawText(rect, align | Qt::TextWordWrap, text);
    p.restore();
}

/** Одна строка с подбором размера и многоточием (стороны, углы, подписи). */
void drawSingleLine(QPainter& p, const QRectF& rect, const QString& text, const QColor& colour,
                    bool bold, qreal opacity, qreal startFactor, int align = Qt::AlignCenter) {
    if (text.isEmpty() || rect.isEmpty()) return;
    QFont font = screenFont(p, bold);
    int pixels = std::max(6, static_cast<int>(rect.height() * startFactor));
    const int minimum = std::max(5, static_cast<int>(rect.height() * 0.18));
    for (; pixels > minimum; --pixels) {
        font.setPixelSize(pixels);
        if (QFontMetricsF(font).horizontalAdvance(text) <= rect.width()) break;
    }
    font.setPixelSize(pixels);
    const QString shown = QFontMetricsF(font).elidedText(text, Qt::ElideRight, rect.width());
    p.save();
    p.setOpacity(p.opacity() * std::clamp(opacity, 0.0, 1.0));
    p.setFont(font);
    p.setPen(colour);
    p.drawText(rect, align, shown);
    p.restore();
}

/**
 * Подпись в узкой ячейке (боковые панели): одна строка, а если при читаемом размере
 * (3/4 от начального) она не помещается и в ней есть пробел — две строки.
 */
void drawPanelLabel(QPainter& p, const QRectF& rect, const QString& text, const QColor& colour,
                    qreal startFactor) {
    if (text.isEmpty() || rect.isEmpty()) return;
    QFont font = screenFont(p, true);
    font.setPixelSize(std::max(5, static_cast<int>(rect.height() * startFactor * 0.75)));
    const bool fits = QFontMetricsF(font).horizontalAdvance(text) <= rect.width();
    if (fits || !text.trimmed().contains(QLatin1Char(' '))) {
        drawSingleLine(p, rect, text, colour, true, 1.0, startFactor);
        return;
    }
    // две строки: каждая не крупнее одной строки и не больше ~40% высоты ячейки
    drawFittedText(p, rect, text, colour, true, 1.0, std::min(startFactor, 0.40), 0.16);
}

/** Значок активности из прошивки (у остальных кнопок значки уже на картинке экрана). */
void drawCellIcon(QPainter& p, const QRectF& rect, const CellContent& cell, qreal opacity) {
    if (rect.isEmpty()) return;
    if (!cell.activityType.isEmpty()) {
        const QPixmap& pixmap = activityPixmap(cell.activityType);
        if (pixmap.isNull()) return;
        const QSizeF scaled = QSizeF(pixmap.size()).scaled(rect.size(), Qt::KeepAspectRatio);
        // у значков прошивки прозрачные поля неравные (слева шире), поэтому по горизонтали
        // центрируется непрозрачная часть, а не вся картинка
        // cacheKey → сдвиг центра в долях ширины; после перечитывания ресурсов значки новые, кэш сбрасывается
        static QHash<qint64, qreal> shifts;
        static quint64 version = 0;
        if (version != resources::revision()) { shifts.clear(); version = resources::revision(); }
        auto shift = shifts.constFind(pixmap.cacheKey());
        if (shift == shifts.cend()) {
            const QImage alpha = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
            int left = alpha.width(), right = -1;
            for (int y = 0; y < alpha.height(); ++y) {
                const auto* line = reinterpret_cast<const QRgb*>(alpha.constScanLine(y));
                for (int x = 0; x < alpha.width(); ++x) {
                    if (qAlpha(line[x]) <= 32) continue;
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
            }
            const qreal value = right < left ? 0.0
                : ((left + right + 1) / 2.0 - alpha.width() / 2.0) / alpha.width();
            shift = shifts.insert(pixmap.cacheKey(), value);
        }
        const QRectF target(rect.center().x() - scaled.width() / 2.0 - *shift * scaled.width(),
                            rect.center().y() - scaled.height() / 2.0,
                            scaled.width(), scaled.height());
        p.save();
        p.setOpacity(p.opacity() * std::clamp(opacity, 0.0, 1.0));
        p.drawPixmap(target, pixmap, QRectF(pixmap.rect()));
        p.restore();
    }
}

/// Цвет подписей на панелях экрана: `panel.text` экрана в remote.json, по умолчанию белый.
QColor panelTextColor(const QJsonObject& layout) {
    const QColor colour(layout.value("panel").toObject().value("text").toString());
    return colour.isValid() ? colour : QColor(Qt::white);
}

/**
 * @brief Нарисовать подложку `ScreenPanel` экрана без спрайтов заливкой `panel.color`.
 *
 * Форма — по `role` ячейки: `band` — прямоугольная полоса, `oval` — кольцо (эллипс без заливки),
 * `side` и остальные — скруглённая панель с радиусом ячейки.
 * @param fallback Цвет, если `panel.color` не задан или некорректен.
 */
void paintScreenPanel(QPainter& p, const QJsonObject& layout, const QJsonObject& zone, const QRectF& rect,
                      const QColor& fallback) {
    QColor colour(layout.value("panel").toObject().value("color").toString());
    if (!colour.isValid()) colour = fallback;
    const QString role = zone.value("role").toString();
    p.save();
    if (role == "oval") {
        const qreal width = std::max<qreal>(1.0, std::min(rect.width(), rect.height()) * 0.08);
        p.setPen(QPen(colour, width));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(rect.adjusted(width / 2, width / 2, -width / 2, -width / 2));
    } else if (role == "band") {
        p.fillRect(rect, colour);
    } else {
        const qreal radius = zone.value("radius").toDouble(6);
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawRoundedRect(rect, radius, radius);
    }
    p.restore();
}

/**
 * @brief Значок батареи в ячейке `Battery`: корпус с контактом и три деления.
 *
 * Значок вписывается по центру ячейки в пропорции 2:1, одним цветом.
 */
void paintBattery(QPainter& p, const QRectF& rect, const QColor& colour) {
    const qreal h = std::min(rect.height() * 0.8, rect.width() / 2.2);
    if (h <= 0) return;
    const qreal w = h * 2.0;
    const QRectF body(rect.center().x() - w / 2, rect.center().y() - h / 2, w * 0.9, h);
    const QRectF tip(body.right(), body.center().y() - h * 0.2, w * 0.1, h * 0.4);
    const qreal pen = std::max<qreal>(0.6, h * 0.1);
    p.save();
    p.setPen(QPen(colour, pen));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body.adjusted(pen / 2, pen / 2, -pen / 2, -pen / 2), h * 0.15, h * 0.15);
    p.fillRect(tip, colour);
    const QRectF inner = body.adjusted(pen * 1.6, pen * 1.6, -pen * 1.6, -pen * 1.6);
    const qreal gap = pen * 0.8;
    const qreal segment = (inner.width() - 2 * gap) / 3;
    for (int i = 0; i < 3 && segment > 0; ++i)
        p.fillRect(QRectF(inner.left() + i * (segment + gap), inner.top(), segment, inner.height()), colour);
    p.restore();
}

/** Текст с подбором размера и, если нужно, тенью под ним. */
void drawStyledText(QPainter& p, const QRectF& rect, const QString& text, const QColor& colour,
                    bool bold, bool shadow, qreal opacity, qreal startFactor, qreal minFactor,
                    int maxPixels = 0) {
    if (shadow) {
        const qreal d = std::max<qreal>(0.6, rect.height() * 0.025);
        drawFittedText(p, rect.translated(d, d), text, QColor(0, 0, 0, 150), bold, opacity,
                       startFactor, minFactor, Qt::AlignCenter, maxPixels);
    }
    drawFittedText(p, rect, text, colour, bold, opacity, startFactor, minFactor,
                   Qt::AlignCenter, maxPixels);
}

/**
 * Плитка-картинка из resources/sprites (`cell.image` экрана в remote.json), одна на все
 * ячейки: на экране избранного это уменьшенная плитка активностей. "shadow": true — мягкая
 * тень вправо-вниз, как у плиток 1000 (числа подогнаны глазами по фотографии экрана 1000;
 * скрипт-генератор compose.py удалён 20.11.2026 — эти значения остались в коде).
 */
struct TileFace {
    QImage image;
    QImage shadow; ///< тень с полями SHADOW_PAD вокруг плитки, в пикселях image
};

constexpr int SHADOW_PAD = 12;          ///< поле картинки тени
constexpr QPointF SHADOW_OFFSET{3, 4};  ///< сдвиг тени относительно плитки
constexpr int SHADOW_BLUR = 3;          ///< радиус каждого из трёх проходов box blur
const QColor SHADOW_COLOUR(0, 20, 50, 150);

/** Размытие альфы тремя проходами скользящего среднего (≈ гауссово). */
void blurAlpha(std::vector<int>& alpha, int width, int height, int radius) {
    std::vector<int> line;
    const auto pass = [&](int count, int length, auto at) {
        for (int i = 0; i < count; ++i) {
            line.assign(length, 0);
            for (int k = 0; k < length; ++k) line[k] = at(i, k);
            int sum = 0;
            for (int k = -radius; k <= radius; ++k) sum += line[std::clamp(k, 0, length - 1)];
            for (int k = 0; k < length; ++k) {
                at(i, k) = sum / (2 * radius + 1);
                sum += line[std::min(k + radius + 1, length - 1)] - line[std::max(k - radius, 0)];
            }
        }
    };
    for (int n = 0; n < 3; ++n) {
        pass(height, width, [&](int y, int x) -> int& { return alpha[std::size_t(y) * width + x]; });
        pass(width, height, [&](int x, int y) -> int& { return alpha[std::size_t(y) * width + x]; });
    }
}

const TileFace* tileFace(const QJsonObject& tile) {
    static QHash<QString, TileFace> cache;
    static quint64 version = 0;
    if (version != resources::revision()) { cache.clear(); version = resources::revision(); }
    const QString file = tile.value("image").toString();
    if (file.isEmpty()) return nullptr;
    const bool withShadow = tile.value("shadow").toBool();
    const QString key = file + (withShadow ? "|shadow" : "");
    auto found = cache.constFind(key);
    if (found == cache.cend()) {
        TileFace face;
        face.image = QImage::fromData(resources::data(":/res/" + file)).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (face.image.isNull()) qWarning("Cannot load tile image: %s", qPrintable(file));
        if (!face.image.isNull() && withShadow) {
            const int w = face.image.width() + 2 * SHADOW_PAD;
            const int h = face.image.height() + 2 * SHADOW_PAD;
            std::vector<int> alpha(std::size_t(w) * h, 0);
            for (int y = 0; y < face.image.height(); ++y)
                for (int x = 0; x < face.image.width(); ++x)
                    alpha[std::size_t(y + SHADOW_PAD) * w + x + SHADOW_PAD] =
                        qAlpha(face.image.pixel(x, y)) * SHADOW_COLOUR.alpha() / 255;
            blurAlpha(alpha, w, h, SHADOW_BLUR);
            face.shadow = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    face.shadow.setPixel(x, y, qPremultiply(qRgba(SHADOW_COLOUR.red(), SHADOW_COLOUR.green(),
                                                                  SHADOW_COLOUR.blue(),
                                                                  alpha[std::size_t(y) * w + x])));
        }
        found = cache.insert(key, face);
    }
    return found->image.isNull() ? nullptr : &found.value();
}

/** Нарисовать плитку-картинку в rect (картинка растягивается, тень — вместе с ней). */
void paintTileFace(QPainter& p, const QRectF& rect, const TileFace& face) {
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!face.shadow.isNull()) {
        const qreal sx = rect.width() / face.image.width();
        const qreal sy = rect.height() / face.image.height();
        const QPointF origin(rect.x() + (SHADOW_OFFSET.x() - SHADOW_PAD) * sx,
                             rect.y() + (SHADOW_OFFSET.y() - SHADOW_PAD) * sy);
        p.drawImage(QRectF(origin, QSizeF(face.shadow.width() * sx, face.shadow.height() * sy)),
                    face.shadow);
    }
    p.drawImage(rect, face.image);
    p.restore();
}

/** Подсветка плитки-картинки: выбранная — белое свечение, под мышью — светлая рамка. */
void paintTileHighlight(QPainter& p, const QRectF& rect, qreal radius, bool active, bool hover) {
    if (!active && !hover) return;
    if (active) {
        p.setPen(QPen(QColor(255, 255, 255, 110), 5.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect, radius, radius);
    }
    p.setPen(QPen(Qt::white, active ? 2.0 : 1.2));
    p.setBrush(QColor(255, 255, 255, active ? 40 : 25));
    p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

/** Картинка-спрайт: растр (PNG) или вектор (SVG, рисуется в нужный размер). */
struct Sprite {
    QImage image;
    std::shared_ptr<QSvgRenderer> svg;

    bool isNull() const { return image.isNull() && !svg; }
    void paint(QPainter& p, const QRectF& rect) const {
        if (svg) svg->render(&p, rect);
        else p.drawImage(rect, image);
    }
};

/**
 * Картинка, положенная на экран: rect в единицах экрана (ref), layer — порядок (больше = выше).
 * label/sublabel — надписи поверх картинки (цифра и буквы клавиши): одна пустая клавиша
 * используется для всех цифр.
 */
struct SpriteItem {
    QString image;
    QRectF rect;
    int layer = 0;
    QString label;
    QString sublabel;
    QColor labelColour;
    QColor tint;           ///< перекрасить картинку (значок текущего экрана в углу)
    bool centered = false; ///< label по центру, иначе цифра слева сверху, буквы справа снизу
};

/**
 * Картинки собранных экранов пульта: `layers` экранов remote.json. Каждая картинка — файл
 * из resources/sprites, готовые фото экранов не используются. Формат — в remotes/SCHEMA.md.
 */
struct SpriteSet {
    QHash<QString, Sprite> images;             ///< путь от папки ресурсов → картинка
    QHash<QString, QList<SpriteItem>> screens; ///< экран → картинки по возрастанию слоя
};

const SpriteSet* spriteSet(const QString& modelId) {
    static QHash<QString, SpriteSet> cache;
    static quint64 version = 0;
    if (version != resources::revision()) { cache.clear(); version = resources::revision(); }
    if (modelId.isEmpty()) return nullptr;
    auto found = cache.constFind(modelId);
    if (found == cache.cend()) {
        SpriteSet set;
        for (const auto& screenValue : resources::remote(modelId).value("screens").toArray()) {
            const auto screen = screenValue.toObject();
            if (!screen.contains("layers")) continue;
            QList<SpriteItem> items;
            for (const auto& value : screen.value("layers").toArray()) {
                const auto item = value.toObject();
                SpriteItem entry;
                entry.image = item.value("image").toString();
                if (!set.images.contains(entry.image)) {
                    // путь внутри resources/, общий для всех пультов: картинки можно переиспользовать
                    const QString file = ":/res/" + entry.image;
                    Sprite sprite;
                    if (file.endsWith(".svg", Qt::CaseInsensitive)) {
                        auto renderer = std::make_shared<QSvgRenderer>(resources::data(file));
                        if (renderer->isValid()) sprite.svg = renderer;
                    } else {
                        sprite.image = QImage::fromData(resources::data(file))
                                           .convertToFormat(QImage::Format_ARGB32_Premultiplied);
                    }
                    if (sprite.isNull()) qWarning("Cannot load sprite: %s", qPrintable(file));
                    else set.images.insert(entry.image, sprite);
                }
                entry.rect = readRect(item.value("rect"));
                entry.layer = item.value("layer").toInt();
                entry.label = item.value("label").toString();
                entry.sublabel = item.value("sublabel").toString();
                entry.labelColour = QColor(item.value("labelColor").toString());
                if (!entry.labelColour.isValid()) entry.labelColour = QColor("#26292c");
                entry.centered = item.value("labelAlign").toString() == "center";
                entry.tint = QColor(item.value("tint").toString());
                items.append(entry);
            }
            // одинаковый слой — в порядке файла
            std::stable_sort(items.begin(), items.end(),
                             [](const SpriteItem& a, const SpriteItem& b) { return a.layer < b.layer; });
            set.screens.insert(screen.value("id").toString(), items);
        }
        found = cache.insert(modelId, set);
    }
    return found->images.isEmpty() ? nullptr : &found.value();
}

} // namespace

// ── painting ─────────────────────────────────────────────────────────────────

void RemoteView::resizeEvent(QResizeEvent* event) {
    if (isLandscape()) {
        const int photoHeight = heightForWidth(width());
        if (minimumHeight() != photoHeight) setMinimumHeight(photoHeight);
    }
    scaledPhoto = QPixmap(); // rebuilt lazily in paintEvent
    QWidget::resizeEvent(event);
}

void RemoteView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::TextAntialiasing);

    const QRectF target = photoTarget();
    if (!photo.isNull()) {
        // pre-scale once per size: drawing a 1500 px photo scaled on every
        // hover repaint is noticeably slower and softer
        const qreal dpr = devicePixelRatioF();
        const QSize wanted = (target.size() * dpr).toSize();
        if (scaledPhoto.isNull() || scaledPhoto.size() != wanted) {
            scaledPhoto = photo.scaled(wanted, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            scaledPhoto.setDevicePixelRatio(dpr);
        }
        p.drawPixmap(target.topLeft(), scaledPhoto);
    } else {
        // resource missing: keep the widget usable with a plain body
        p.setPen(Qt::NoPen);
        p.setBrush(theme::color(theme::Color::RemoteBody));
        p.drawRoundedRect(target, 40 * scale(), 40 * scale());
    }

    screenHits.clear();
    if (modelScreen.isValid()) paintScreen(p);
    paintZones(p);
}

void RemoteView::overrideScreen(const QJsonObject& screen) {
    const QString mode = screen.value("id").toString();
    if (mode.isEmpty()) return;
    modes.insert(mode, resources::normalizeScreen(screen, tiles.value("area")));
}

QPixmap RemoteView::screenPreview(const QSize& size) {
    const QRectF source = toWidget(modelScreen);
    if (!source.isValid() || size.isEmpty()) return {};
    QPixmap image(size);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size.width() / source.width(), size.height() / source.height());
    painter.translate(-source.topLeft());
    paintScreen(painter);
    return image;
}

void RemoteView::paintZones(QPainter& p) const {
    for (const auto& zone : zones()) {
        const QRectF r = toWidget(zone.rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = zoneRadius(zone);
        const bool isKey = (zone.kind == ZoneKind::Key);
        const bool selected = isKey && (zone.id == selectedKey || selectedKeys.contains(zone.id));

        if (isKey && currentMode == Mode::Device) {
            const KeyState state = deviceKeys.value(zone.id, KeyState::Free);
            if (state == KeyState::Free) {
                // free key: darken the photo and outline dashed
                p.setBrush(theme::color(theme::Color::RemoteDimFill));
                QPen pen(theme::color(theme::Color::RemoteDimLine), 1.0, Qt::DashLine);
                p.setPen(pen);
                p.drawRoundedRect(r, radius, radius);
            } else if (state == KeyState::Broken) {
                p.setBrush(theme::color(theme::Color::RemoteBrokenFill));
                p.setPen(QPen(theme::color(theme::Color::RemoteBroken), 2.0));
                p.drawRoundedRect(r, radius, radius);
            }
        } else if (isKey && currentMode == Mode::Activity) {
            if (activityKeys.contains(zone.id)) {
                // key belongs to a device: tint + outline in the device colour
                QColor colour = activityKeys.value(zone.id);
                QColor fill = colour;
                fill.setAlpha(46);
                p.setBrush(fill);
                p.setPen(QPen(colour, 2.0));
                p.drawRoundedRect(r, radius, radius);
            } else {
                p.setBrush(theme::color(theme::Color::RemoteDimFill));
                p.setPen(QPen(theme::color(theme::Color::RemoteDimLine), 1.0, Qt::DashLine));
                p.drawRoundedRect(r, radius, radius);
            }
        }

        if (selected) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(theme::color(theme::Color::RemoteSelectedGlow), 6.0));
            p.drawRoundedRect(r, radius, radius);
            p.setPen(QPen(theme::color(theme::Color::RemoteSelected), 2.0));
            p.drawRoundedRect(r, radius, radius);
        } else if (zone.id == hoverId) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(theme::color(theme::Color::RemoteHoverLine), 1.5));
            p.drawRoundedRect(r, radius, radius);
        }
    }
}

void RemoteView::paintScreen(QPainter& p) {
    if (activeMode.isEmpty()) return;
    // Единая схема: все экраны идут через paintModernScreen (спрайт, иначе фон и плитки как у 900).
    paintModernScreen(p);
}

void RemoteView::paintModernScreen(QPainter& p) {
    const QJsonObject layout = modes.value(activeMode).toObject();
    const auto ref = layout.value("ref").toArray();
    const QRectF lcd = toWidget(screenRect());
    if (ref.size() != 2 || ref[0].toDouble() <= 0 || ref[1].toDouble() <= 0) {
        QColor place = theme::color(theme::Color::RemoteSelected); // экран без размеров: только его место
        place.setAlpha(60);
        p.fillRect(lcd, place);
        return;
    }
    // Единая схема для всех пультов, без имён раскладок:
    // 1) у экрана есть картинки слоями (compose) — рисуем их;
    // 2) иначе фон и плитки как у 900 по ячейкам экрана; плитка — картинка (cell.image) или paintPill
    //    цветом из "tiles". Оформление определяется данными пульта, а не названием экрана.
    if (paintComposedScreen(p, layout)) return;
    paintGridScreen(p, layout);
}

QString RemoteView::serviceText(const QString& id, const QJsonObject& layout) const {
    const bool activityScreen = isListScreen();
    if (id == "Caption" || id == "Title") {
        const QString title = layout.value("title").toString();
        return !title.isEmpty() ? title : currentScreen == Screen::Devices ? tr("Devices") : softTitle;
    }
    if (id == "Heading")
        return activityScreen ? (currentScreen == Screen::Devices ? tr("Devices") : tr("What would you like to do?")) : QString();
    if (id == "Page" || id == "PageLabel")
        return activityScreen || currentScreen == Screen::SoftButtons
                   ? tr("%1 of %2").arg(currentPage() + 1).arg(pageCount()) : QString();
    if (id == "Clock") return QLocale::system().toString(QTime::currentTime(), QLocale::ShortFormat);
    return {};
}

void RemoteView::paintGridScreen(QPainter& p, const QJsonObject& layout) {
    // Единый рендерер экранов remote.json: фон — картинка или заливка цветом,
    // плитки — картинка или «таблетка» цветом с рамкой (по умолчанию DEFAULT_TILE_COLOUR и белая).
    const auto ref = layout.value("ref").toArray();
    const qreal refW = ref[0].toDouble();
    const qreal refH = ref[1].toDouble();
    const QRectF lcd = toWidget(screenRect());
    const auto background = layout.value("background").toObject();
    const auto cellStyle = tileStyle(layout, tiles);
    const QColor bgColor(background.value("color").toString(DEFAULT_TILE_COLOUR.name()));
    const QColor cellColor(cellStyle.value("color").toString(DEFAULT_TILE_COLOUR.name()));
    const QColor borderColor(cellStyle.value("border").toString("#ffffff"));
    const auto loadImage = [](const QString& file) {
        return file.isEmpty() ? QImage() : QImage::fromData(resources::data(":/res/" + file));
    };
    static QHash<QString, QImage> images;
    static quint64 version = 0;
    if (version != resources::revision()) { images.clear(); version = resources::revision(); }
    const auto image = [&](const QString& file) -> const QImage& {
        auto it = images.find(file);
        if (it == images.end()) it = images.insert(file, loadImage(file));
        return it.value();
    };

    p.save();
    QPainterPath clip;
    clip.addRoundedRect(lcd, lcd.width() * 0.02, lcd.height() * 0.02);
    p.setClipPath(clip);
    p.translate(lcd.topLeft());
    p.scale(lcd.width() / refW, lcd.height() / refH);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QImage& bgImage = image(background.value("image").toString());
    // Как на реальном пульте 900: «стеклянный» фон с подсветкой в центре, если нет картинки.
    // Явное background.glass главнее; без него стекло — только у экрана без картинки и без зон фона
    // (готовые экраны 900 не меняются, новый экран с зонами или с glass:false — обычная заливка).
    const resources::ScreenZones zones = resources::screenZones(layout);
    const bool glass = background.contains("glass") ? background.value("glass").toBool()
                                                    : !background.contains("image") && !zones.any;
    if (!bgImage.isNull()) p.drawImage(QRectF(0, 0, refW, refH), bgImage);
    else if (glass) {
        if (bgColor == DEFAULT_TILE_COLOUR) paintLcdBackdrop(p, refW, refH);
        else {
            QRadialGradient glow(QPointF(refW / 2.0, refH * 0.55), refH * 0.62);
            glow.setColorAt(0.0, bgColor.lighter(190));
            glow.setColorAt(0.5, bgColor);
            glow.setColorAt(1.0, bgColor.darker(300));
            p.fillRect(QRectF(0, 0, refW, refH), glow);
        }
    } else {
        // Без картинки и без цвета фон — чёрный прямоугольник (под прозрачными зонами тоже).
        const bool plain = !background.contains("color") && !background.contains("image");
        p.fillRect(QRectF(0, 0, refW, refH), plain ? QColor(Qt::black) : bgColor);
    }
    // Зоны фона поверх картинки или заливки; без color зона прозрачна.
    if (zones.any) {
        if (zones.headerColor.isValid()) p.fillRect(zones.header, zones.headerColor);
        if (zones.footerColor.isValid() && !zones.footer.isEmpty()) p.fillRect(zones.footer, zones.footerColor);
    }

    const int capacity = std::max(1, slotsPerPage(activeMode));
    const int first = currentPage() * capacity;
    const bool activityScreen = isListScreen();
    const bool softScreen = currentScreen == Screen::SoftButtons;
    int slot = 0;
    const auto gridZones = layout.value("cells").toArray();
    // Подложки ScreenPanel — сразу над фоном, под шапкой и плитками: заливка panel.color
    // (по умолчанию темнее фона) по форме роли. На собранных экранах они нарисованы на спрайтах.
    for (const auto& value : gridZones) {
        const auto zone = value.toObject();
        const QRectF rect = readRect(zone.value("rect"));
        if (!rect.isEmpty() && zone.value("kind").toString() == "ScreenPanel")
            paintScreenPanel(p, layout, zone, rect, bgColor.darker(160));
    }
    if (glass) {
        // Стеклянный фон 900: общие шапка и подвал экрана.
        const QString layoutTitle = layout.value("title").toString();
        const QString titleText = !layoutTitle.isEmpty() ? layoutTitle
                                  : currentScreen == Screen::Devices ? tr("Devices") : softTitle;
        paintLcdHeader(p, font(), refW, titleText, tr("p. %1/%2").arg(currentPage() + 1).arg(pageCount()));
        paintLcdFooter(p, font(), refW, refH, tr("Options"), tr("Devices"));
    }
    for (const auto& value : gridZones) {
        const auto zone = value.toObject();
        const QRectF rect = readRect(zone.value("rect"));
        if (rect.isEmpty()) continue;
        const QString zoneKind = zone.value("kind").toString("ScreenSlot");
        if (zoneKind == "ScreenPanel") continue; // подложки уже нарисованы под шапкой и плитками
        if (glass && (zoneKind == "ScreenLabel" || zoneKind == "ScreenSide")) {
            // Служебные зоны уже нарисованы общей шапкой и подвалом.
            continue;
        }
        if (zoneKind == "ScreenLabel" || zoneKind == "ScreenSide") {
            // Надписи поверх фона (шапка, вкладки и панели — на картинке фона), а не плитки:
            // служебная подпись (заголовок, страница, часы), значок батареи или название кнопки у экрана.
            const QString id = zone.value("id").toString();
            const QColor ink = panelTextColor(layout);
            if (id == "Battery") {
                paintBattery(p, rect, ink);
                continue;
            }
            QString text;
            int align = Qt::AlignCenter;
            QRectF box = rect;
            if (zoneKind == "ScreenSide") {
                text = keyName(zone.value("key").toString(id));
            } else {
                text = serviceText(id, layout);
                if (id == "Title" || id == "Caption") {
                    align = Qt::AlignLeft | Qt::AlignVCenter;
                    box.adjust(4, 0, 0, 0);
                } else if (id == "Page" || id == "PageLabel") {
                    align = Qt::AlignRight | Qt::AlignVCenter;
                    box.adjust(0, 0, -4, 0);
                }
            }
            // Тень — только у светлых надписей: под тёмными на светлой шапке она пачкает.
            if (ink.lightness() > 140) {
                const qreal d = std::max<qreal>(0.6, box.height() * 0.06);
                drawSingleLine(p, box.translated(d, d), text, QColor(0, 0, 0, 160), true, 1.0, 0.72, align);
            }
            drawSingleLine(p, box, text, ink, true, 1.0, 0.72, align);
            continue;
        }
        const qreal radius = zone.value("radius").toDouble(cellStyle.value("radius").toDouble(6));
        const bool slotCell = zone.value("kind").toString("ScreenSlot") == "ScreenSlot";
        const bool hover = hoverScreenHit == screenHits.size();
        QString label = zone.value("label").toString();
        QString itemType;
        bool active = false;
        int index = -1;
        if (slotCell) {
            index = first + slot++;
            if (activityScreen) {
                // Как у команд: плитка рисуется и без данных, просто пустая.
                if (index < listItems().size()) {
                    label = listItems()[index].label;
                    itemType = listItems()[index].type;
                    active = listItems()[index].selected;
                }
            } else if (softScreen) {
                label = softLabels.value(index);
                active = selectedSoft == index;
            }
        }
        const TileFace* sprite = slotCell ? tileFace(cellStyle) : nullptr;
        if (sprite != nullptr) {
            // Плитки заданы спрайтом (как у 1000): рисуем его, цвет не меняется.
            paintTileFace(p, rect, *sprite);
            paintTileHighlight(p, rect, radius, active, hover);
        } else {
            // Плитка ровно такая же, как у 900; перекрашивается цветом плиток пульта.
            // Кнопка экрана (ScreenKey/ScreenSide) может иметь свои цвета: поля color и border ячейки.
            const QColor own(zone.value("color").toString());
            const QColor ownBorder(zone.value("border").toString());
            paintPill(p, rect, active, hover, own.isValid() ? own : cellColor, ownBorder.isValid() ? ownBorder : borderColor);
        }
        if (!label.isEmpty()) {
            // Те же размеры шрифта и расположение, что у классических списков и команд.
            // Подпись со значком слева — только на широкой плитке списка (как у 900); на узкой — по центру.
            const bool rows = activityScreen && rect.width() >= refW * 0.6;
            QFont tileFont = font();
            tileFont.setPixelSize(rows ? 17 : std::clamp(static_cast<int>(std::lround(rect.width() * 0.14)), 10, 14));
            tileFont.setBold(true);
            p.setFont(tileFont);
            p.setPen(Qt::white);
            if (rows) {
                QRectF text = rect.adjusted(10, 0, -10, 0);
                if (currentScreen != Screen::Devices) {
                    const QRectF icon(rect.x() + 4, rect.y() + (rect.height() - 41.0) / 2.0, 48, 41);
                    const QPixmap& pix = activityPixmap(itemType);
                    if (!pix.isNull()) {
                        p.save();
                        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
                        p.drawPixmap(icon, pix, QRectF(pix.rect()));
                        p.restore();
                    }
                    text = QRectF(icon.right() + 6, rect.y(), rect.right() - icon.right() - 12, rect.height());
                }
                p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                           QFontMetricsF(tileFont).elidedText(label, Qt::ElideRight, text.width()));
            } else {
                p.drawText(rect.adjusted(4, 2, -4, -2), Qt::AlignCenter | Qt::TextWordWrap, label);
            }
        }
        // У пустой ячейки списка (данных нет) нажатия нет, хотя плитка нарисована.
        const bool hasData = !activityScreen || index < listItems().size();
        if (slotCell && index >= 0 && hasData) {
            const QString button = zone.value("button").toString();
            if (!button.isEmpty()) {
                for (const auto& key : modelZones)
                    if (key.id == button) screenHits.append({key.rect, index});
            }
            if (touchScreen) screenHits.append({screenToPhoto(rect), index});
        }
    }
    p.restore();
}

bool RemoteView::paintComposedScreen(QPainter& p, const QJsonObject& layout) {
    // "compose": экран собирается из картинок `layers` экрана
    if (!layout.value("compose").toBool()) return false;
    const SpriteSet* sprites = spriteSet(profileId);
    if (sprites == nullptr) return false;
    const auto ref = layout.value("ref").toArray();
    const qreal refW = ref[0].toDouble();
    const qreal refH = ref[1].toDouble();
    const auto zones = layout.value("cells").toArray();
    const QRectF lcd = toWidget(screenRect());
    const QJsonObject tile = tileStyle(layout, tiles);

    p.save();
    QPainterPath clip;
    clip.addRoundedRect(lcd, lcd.width() * 0.02, lcd.height() * 0.02);
    p.setClipPath(clip);
    p.translate(lcd.topLeft());
    p.scale(lcd.width() / refW, lcd.height() / refH);
    if (sprites != nullptr) {
        // Собранный экран: картинки `layers` по слоям (фон — тоже картинка, слой 0).
        // Плитки, подписи и подсветка из конфигурации рисуются поверх всех слоёв.
        for (const SpriteItem& item : sprites->screens.value(layout.value("id").toString())) {
            const auto image = sprites->images.constFind(item.image);
            if (image == sprites->images.cend()) {
                qWarning("Unknown screen sprite: %s", qPrintable(item.image));
                continue;
            }
            if (item.tint.isValid()) {
                // значок рисуется в отдельный слой своего размера на экране и заливается цветом
                const QSize size = p.transform().mapRect(item.rect).size().toSize().expandedTo({1, 1});
                QImage layer(size, QImage::Format_ARGB32_Premultiplied);
                layer.fill(Qt::transparent);
                QPainter lp(&layer);
                lp.setRenderHint(QPainter::SmoothPixmapTransform);
                image->paint(lp, QRectF(QPointF(), QSizeF(size)));
                lp.setCompositionMode(QPainter::CompositionMode_SourceIn);
                lp.fillRect(layer.rect(), item.tint);
                lp.end();
                p.drawImage(item.rect, layer);
            } else {
                image->paint(p, item.rect);
            }
            const QRectF r = item.rect;
            if (item.centered) {
                drawSingleLine(p, r, item.label, item.labelColour, true, 1.0, 0.62);
                continue;
            }
            // клавиша: цифра слева сверху, буквы справа снизу
            drawSingleLine(p, QRectF(r.x() + r.width() * 0.13, r.y() + r.height() * 0.08,
                                     r.width() * 0.45, r.height() * 0.50),
                           item.label, item.labelColour, true, 1.0, 0.92,
                           int(Qt::AlignLeft) | int(Qt::AlignVCenter));
            drawSingleLine(p, QRectF(r.x() + r.width() * 0.30, r.y() + r.height() * 0.58,
                                     r.width() * 0.60, r.height() * 0.34),
                           item.sublabel, item.labelColour, true, 1.0, 0.90,
                           int(Qt::AlignRight) | int(Qt::AlignVCenter));
        }
    }

    const int capacity = std::max(1, slotsPerPage(activeMode));
    const int first = currentPage() * capacity;
    const bool activityScreen = isListScreen();
    const bool softScreen = currentScreen == Screen::SoftButtons;
    const int visible = std::clamp(static_cast<int>(listItems().size()) - first, 0, capacity);
    QList<QRectF> occupied;
    if (layout.value("centerLastRow").toBool() && activityScreen) {
        for (const auto& value : zones) {
            const auto zone = value.toObject();
            if (zone.value("kind") == "ScreenSlot" && occupied.size() < visible)
                occupied.append(readRect(zone.value("rect")));
        }
    }
    const QColor shadow(0, 0, 0, 160);
    // цвет подписи на плитке (`cell.text`): у светлых плиток 1000 тёмный, без тени
    const QColor tileText(tile.value("text").toString());

    // Подписи плиток — одним размером на экране (доля высоты плитки). Не помещается
    // в строку — переносится на вторую; уменьшается только то, что не влезает и в две
    // строки (длинное слово), и только на своей плитке.
    const QFont tileFont = screenFont(p, true);
    auto tileContent = [](const QRectF& rect) {
        const qreal pad = std::min(rect.width(), rect.height()) * 0.08;
        return rect.adjusted(pad, pad, -pad, -pad);
    };
    auto tileLabelPixels = [](const QRectF& content, bool activity) {
        return std::max(6, static_cast<int>(content.height() * (activity ? 0.17 : 0.22)));
    };
    // сколько места нужно подписи на плитке при общем размере (с переносами)
    auto labelHeight = [&tileFont](const QString& text, qreal width, int pixels) {
        QFont font = tileFont;
        font.setPixelSize(pixels);
        return QFontMetricsF(font)
            .boundingRect(QRectF(0, 0, width, 1e4), Qt::AlignCenter | Qt::TextWordWrap, text)
            .height();
    };

    int slot = 0;
    for (const auto& value : zones) {
        const auto zone = value.toObject();
        const auto kind = zone.value("kind").toString();
        const auto id = zone.value("id").toString();
        QRectF rect = readRect(zone.value("rect"));
        if (rect.isEmpty()) continue;
        const qreal radius = std::clamp(zone.value("radius").toDouble(), 0.0,
                                        std::min(rect.width(), rect.height()) / 2.0);
        const bool hover = hoverScreenHit == screenHits.size();

        if (kind == "ScreenSlot") {
            const int index = first + slot++;
            CellContent cell;
            if (activityScreen) {
                if (index >= listItems().size()) continue; // на пульте лишних плиток нет
                cell.activityType = listItems()[index].type;
                cell.label = listItems()[index].label;
                cell.active = listItems()[index].selected;
            } else if (softScreen) {
                cell.label = softLabels.value(index);
                cell.active = selectedSoft == index;
                cell.empty = cell.label.isEmpty();
            } else {
                continue;
            }
            if (!occupied.isEmpty() && visible < capacity && rect.y() == occupied.last().y()) {
                QRectF row = rect;
                for (const QRectF& other : occupied)
                    if (other.y() == rect.y()) row = row.united(other);
                rect.translate(refW / 2.0 - row.center().x(), 0);
            }
            if (const TileFace* face = tileFace(tile)) {
                paintTileFace(p, rect, *face); // уменьшенная плитка активностей (избранное)
            } else if (tile.contains("image")) {
                // картинка плитки не загрузилась: ровная заливка цветом плиток
                const QColor border(tile.value("border").toString());
                p.setPen(border.isValid() ? QPen(border, 1.0) : QPen(Qt::NoPen));
                p.setBrush(QColor(tile.value("color").toString()));
                p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
            }
            paintTileHighlight(p, rect, radius, cell.active, hover);
            const QRectF content = tileContent(rect);
            if (cell.empty) {
                drawSingleLine(p, content, QStringLiteral("+"),
                               tileText.isValid() ? tileText : QColor(Qt::white), false,
                               hover ? 0.9 : 0.45, 0.55);
            } else if (!cell.activityType.isEmpty()) {
                // значок из прошивки и подпись под ним; подписи в две строки место
                // уступает значок
                const int pixels = tileLabelPixels(content, true);
                const qreal oneLine = content.height() * 0.40;
                const qreal labelH =
                    labelHeight(cell.label, content.width(), pixels) > oneLine ? content.height() * 0.52
                                                                               : oneLine;
                const qreal iconH = content.height() - labelH + content.height() * 0.02;
                drawCellIcon(p, QRectF(content.left() + content.width() * 0.05, content.top(),
                                       content.width() * 0.90, iconH),
                             cell, 1.0);
                const QRectF box(content.left(), content.bottom() - labelH, content.width(), labelH);
                drawStyledText(p, box, cell.label, tileText.isValid() ? tileText : QColor(Qt::white),
                               true, !tileText.isValid(), 1.0, (pixels + 1.0) / labelH, 0.20, pixels);
            } else {
                const int pixels = tileLabelPixels(content, false);
                drawStyledText(p, content, cell.label, tileText.isValid() ? tileText : QColor(Qt::white),
                               true, !tileText.isValid(), 1.0, (pixels + 1.0) / content.height(), 0.12, pixels);
            }
            const QString button = zone.value("button").toString();
            if (!button.isEmpty()) {
                for (const auto& key : modelZones)
                    if (key.id == button) screenHits.append({key.rect, index});
            }
            if (touchScreen) screenHits.append({screenToPhoto(rect), index});
        } else if (kind == "ScreenLabel") {
            // Подписи из конфигурации и заголовок экрана активностей рисуются поверх картинок.
            if (id == "Battery") {
                paintBattery(p, rect, Qt::white);
                continue;
            }
            const QString text = serviceText(id, layout);
            if (id == "PageWord") {
                // надпись на стрелке листания 1000 (кнопки UpArrow/DownArrow), цвет панели
                drawSingleLine(p, rect, tr("Page"), panelTextColor(layout), true, 1.0, 0.80);
                continue;
            }
            if (text.isEmpty()) continue;
            const qreal d = std::max<qreal>(0.6, rect.height() * 0.06);
            drawSingleLine(p, rect.translated(d, d), text, shadow, true, 1.0, 0.80);
            drawSingleLine(p, rect, text, Qt::white, true, 1.0, 0.80);
        } else if (kind == "ScreenSide") {
            // название назначаемой кнопки
            const QString key = zone.value("key").toString();
            if (key.isEmpty()) continue;
            drawPanelLabel(p, rect.adjusted(6, 0, -6, 0), keyName(key), panelTextColor(layout), 0.34);
        }
        // ScreenKey, ScreenPanel: всё нужное уже есть на картинках экрана
    }
    p.restore();
    return true;
}
