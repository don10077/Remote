#include "remoteView.h"
#include "../remoteResources.h"

#include "activityIcon.h"
#include "harmony/core/remoteProfile.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFontMetricsF>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHelpEvent>
#include <QImage>
#include <QLocale>
#include <QMap>
#include <QSvgRenderer>
#include <memory>
#include <QLinearGradient>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTime>
#include <QToolTip>
#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

using harmony::app::RemoteView;
namespace resources = harmony::app::remoteResources;

namespace {

using harmony::app::activityPixmap;

/** Прямоугольник [x, y, width, height]; некорректные данные дают пустую область. */
QRectF readRect(const QJsonValue& value) {
    const auto a = value.toArray();
    if (a.size() != 4 || !std::all_of(a.begin(), a.end(), [](const QJsonValue& v) {
            return v.isDouble();
        })) return {};
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
}

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

// overlay colours
const QColor DIM_FILL(20, 22, 26, 158);
const QColor DIM_LINE(255, 255, 255, 75);
const QColor BROKEN_FILL(229, 72, 77, 64);
const QColor BROKEN_LINE(255, 90, 82);
const QColor SELECT_LINE(138, 184, 255);
const QColor SELECT_GLOW(138, 184, 255, 90);
const QColor HOVER_LINE(255, 255, 255, 140);

/** pill with the look of the Harmony LCD buttons */
void paintPill(QPainter& p, const QRectF& r, bool selected, bool hover,
               const QColor& base = QColor("#0b3442"), const QColor& border = QColor("#ffffff")) {
    // Цвета по умолчанию (из remote.json) дают классический вид пульта; другой цвет плитки
    // и рамки перекрашивает плитку, сохраняя объём (светлый верх, тёмный низ).
    const bool customFill = base.isValid() && base != QColor("#0b3442");
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

/**
 * Общий фон экрана пульта (радиальный градиент). Единственное место, где он рисуется:
 * классические и созданные в редакторе экраны используют одну и ту же функцию.
 */
void paintLcdBackdrop(QPainter& p, qreal w, qreal h) {
    QRadialGradient bg(QPointF(w / 2.0, h * 0.55), h * 0.62);
    bg.setColorAt(0.0, QColor("#1d7087"));
    bg.setColorAt(0.5, QColor("#0b3442"));
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
        static QHash<qint64, qreal> shifts; // cacheKey → сдвиг центра в долях ширины
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
 * тень вправо-вниз, как у плиток 1000 (значения сняты с tools/sprites/compose.py).
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

RemoteView::RemoteView(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setAcceptDrops(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setRemoteProfile(QString());
}

void RemoteView::setRemoteProfile(const QString& id) {
    const QString requested = id.isEmpty() ? QString::fromStdString(harmony::defaultRemoteId()) : id;
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
    const QString id = remoteId.isEmpty() ? QString::fromStdString(harmony::defaultRemoteId()) : remoteId;
    // Списки кнопок принадлежат текущему снимку ресурсов.
    static QHash<QString, QStringList> cache;
    static quint64 version = 0;
    if (version != resources::revision()) { cache.clear(); version = resources::revision(); }
    if (cache.contains(id)) return cache.value(id);
    QStringList keys;
    if (const auto* profile = harmony::remoteProfile(id.toStdString())) {
        QPixmap image;
        image.loadFromData(resources::data(QString::fromUtf8(profile->image.data())));
        const QJsonObject remote = resources::remote(id);
        const auto hardware = remote.value("hardware").toObject();
        const auto size = hardware.value("size").toArray();
        if (!image.isNull() && size.size() == 2 &&
            QSize(size[0].toInt(), size[1].toInt()) == image.size()) {
            const QRectF bounds(QPointF(0, 0), QSizeF(image.size()));
            for (const auto& entry : hardware.value("zones").toArray()) {
                const auto zone = entry.toObject();
                const QString key = zone.value("id").toString();
                const QRectF rect = readRect(zone.value("rect"));
                if (zone.value("kind").toString() == "Key" && !key.isEmpty() &&
                    rect.isValid() && bounds.contains(rect) && zone.value("radius").isDouble() &&
                    !keys.contains(key)) {
                    keys.append(key);
                }
            }
            // Назначаемые кнопки экранов модели (поле "key" ячеек экранов remote.json):
            // транспорт и цифры 1000/1100, Menu/Guide/Info/Exit на боковых панелях.
            // Экраны — по алфавиту id (как раньше), ячейки — в порядке файла: от этого порядка
            // зависит сортировка кнопок в списках программы.
            QMap<QString, QJsonArray> cellsByScreen;
            for (const auto& screen : remote.value("screens").toArray())
                cellsByScreen.insert(screen.toObject().value("id").toString(),
                                     screen.toObject().value("cells").toArray());
            for (const auto& cells : std::as_const(cellsByScreen)) {
                for (const auto& entry : cells) {
                    const QString key = entry.toObject().value("key").toString();
                    if (!key.isEmpty() && !keys.contains(key)) keys.append(key);
                }
            }
        }
    }
    cache.insert(id, keys);
    return keys;
}

QString RemoteView::keyName(const QString& id) {
    static const QHash<QString, const char*> names = {
        {"Off", QT_TR_NOOP("Off")},
        {"Help", QT_TR_NOOP("Help")},
        {"Activities", QT_TR_NOOP("Activities")},
        {"Options", QT_TR_NOOP("Options")},
        {"Devices", QT_TR_NOOP("Devices")},
        {"PageLeft", QT_TR_NOOP("Previous screen page")},
        {"PageRight", QT_TR_NOOP("Next screen page")},
        {"Menu", QT_TR_NOOP("Menu")},
        {"Exit", QT_TR_NOOP("Exit")},
        {"Info", QT_TR_NOOP("Info")},
        {"Guide", QT_TR_NOOP("Guide")},
        {"UpArrow", QT_TR_NOOP("Page up")},
        {"DownArrow", QT_TR_NOOP("Page down")},
        {"Red", QT_TR_NOOP("Red")},
        {"Green", QT_TR_NOOP("Green")},
        {"Yellow", QT_TR_NOOP("Yellow")},
        {"Blue", QT_TR_NOOP("Blue")},
        {"VolumeUp", QT_TR_NOOP("Volume +")},
        {"VolumeDown", QT_TR_NOOP("Volume −")},
        {"VolumeMute", QT_TR_NOOP("Mute")},
        {"ChannelUp", QT_TR_NOOP("Channel +")},
        {"ChannelDown", QT_TR_NOOP("Channel −")},
        {"PrevChannel", QT_TR_NOOP("Previous channel")},
        {"DirectionUp", QT_TR_NOOP("Up")},
        {"DirectionDown", QT_TR_NOOP("Down")},
        {"DirectionLeft", QT_TR_NOOP("Left")},
        {"DirectionRight", QT_TR_NOOP("Right")},
        {"Select", QT_TR_NOOP("OK")},
        {"Rewind", QT_TR_NOOP("Rewind")},
        {"FastForward", QT_TR_NOOP("Fast forward")},
        {"SkipBack", QT_TR_NOOP("Back")},
        {"SkipForward", QT_TR_NOOP("Forward")},
        {"Play", QT_TR_NOOP("Play")},
        {"Pause", QT_TR_NOOP("Pause")},
        {"Stop", QT_TR_NOOP("Stop")},
        {"Record", QT_TR_NOOP("Record")},
        {"Slow", QT_TR_NOOP("Slow")},
        {"NumberStar", QT_TR_NOOP("* (star)")},
        {"NumberHash", QT_TR_NOOP("# (hash)")},
        {"Settings", QT_TR_NOOP("Settings")},
        {"NumberPlus", QT_TR_NOOP("+ (clear)")},
        {"NumberEnter", QT_TR_NOOP("E (enter)")},
    };
    if (id.startsWith("Number") && id.size() == 7) {
        return tr("Digit %1").arg(id.back());
    }
    auto it = names.find(id);
    if (it != names.end()) {
        return tr(it.value());
    }
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

// ── geometry ─────────────────────────────────────────────────────────────────

QRectF RemoteView::screenToPhoto(const QRectF& rect) const {
    const auto ref = modes.value(activeMode).toObject().value("ref").toArray();
    const qreal sx = modelScreen.width() / ref.at(0).toDouble();
    const qreal sy = modelScreen.height() / ref.at(1).toDouble();
    return {modelScreen.x() + rect.x()*sx, modelScreen.y() + rect.y()*sy,
            rect.width()*sx, rect.height()*sy};
}

qreal RemoteView::scale() const {
    if (isLandscape()) return width() / static_cast<qreal>(photoSize.width());
    return std::min(width() / static_cast<qreal>(photoSize.width()),
                    height() / static_cast<qreal>(photoSize.height()));
}

QRectF RemoteView::photoTarget() const {
    const qreal s = scale();
    const qreal w = photoSize.width() * s;
    const qreal h = photoSize.height() * s;
    return {width() - w, (height() - h) / 2.0, w, h};
}

QRectF RemoteView::toWidget(const QRectF& photoRect) const {
    const QRectF target = photoTarget();
    const qreal s = scale();
    return {target.x() + photoRect.x() * s, target.y() + photoRect.y() * s, photoRect.width() * s,
            photoRect.height() * s};
}

qreal RemoteView::zoneRadius(const Zone& zone) const {
    if (zone.radius < 0) {
        const QRectF r = toWidget(zone.rect);
        return std::min(r.width(), r.height()) * (-zone.radius);
    }
    return zone.radius * scale();
}

const RemoteView::Zone* RemoteView::zoneAt(const QPointF& pos) const {
    for (const auto& zone : zones()) {
        if (toWidget(zone.rect).contains(pos)) {
            return &zone;
        }
    }
    return nullptr;
}

const RemoteView::ScreenHit* RemoteView::screenHitAt(const QPointF& pos) const {
    for (const auto& hit : screenHits) {
        if (toWidget(hit.rect).contains(pos)) {
            return &hit;
        }
    }
    return nullptr;
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
        p.setBrush(QColor("#15171a"));
        p.drawRoundedRect(target, 40 * scale(), 40 * scale());
    }

    screenHits.clear();
    if (modelScreen.isValid()) paintScreen(p);
    paintZones(p);
}

void RemoteView::overrideLayout(const QString& mode, const QSizeF& ref, const QJsonArray& zones,
                                const QString& cellColor, const QString& title) {
    QJsonObject screen = modes.value(mode).toObject();
    if (!modes.contains(mode)) {
        // Новый экран наследует фон и оформление ячеек от экранов этого пульта.
        for (auto it = modes.begin(); it != modes.end(); ++it) {
            const QJsonObject donor = it.value().toObject();
            if (!donor.contains("background")) continue;
            for (const char* field : {"background", "cell", "compact", "panel"})
                if (donor.contains(field)) screen.insert(field, donor.value(field));
            break;
        }
        screen.insert("id", mode);
    }
    screen.insert("ref", QJsonArray{ref.width(), ref.height()});
    screen.insert("cells", zones);
    // Заголовок в шапке нужен только черновику экрана в редакторе, в remote.json его нет.
    if (!title.isEmpty()) screen.insert("title", title);
    // Цвет из редактора перекрашивает плитки всего пульта (блок tiles), фон экрана не меняется.
    if (QColor(cellColor).isValid()) tiles.insert("color", cellColor);
    modes.insert(mode, screen);
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
                p.setBrush(DIM_FILL);
                QPen pen(DIM_LINE, 1.0, Qt::DashLine);
                p.setPen(pen);
                p.drawRoundedRect(r, radius, radius);
            } else if (state == KeyState::Broken) {
                p.setBrush(BROKEN_FILL);
                p.setPen(QPen(BROKEN_LINE, 2.0));
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
                p.setBrush(DIM_FILL);
                p.setPen(QPen(DIM_LINE, 1.0, Qt::DashLine));
                p.drawRoundedRect(r, radius, radius);
            }
        }

        if (selected) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(SELECT_GLOW, 6.0));
            p.drawRoundedRect(r, radius, radius);
            p.setPen(QPen(SELECT_LINE, 2.0));
            p.drawRoundedRect(r, radius, radius);
        } else if (zone.id == hoverId) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(HOVER_LINE, 1.5));
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
        p.fillRect(lcd, QColor(138, 184, 255, 60));
        return;
    }
    // Единая схема для всех пультов, без имён раскладок:
    // 1) у экрана есть картинки слоями (compose) — рисуем их;
    // 2) иначе фон и плитки как у 900 по ячейкам экрана; плитка — картинка (cell.image) или paintPill
    //    цветом из "tiles". Оформление определяется данными пульта, а не названием экрана.
    if (paintComposedScreen(p, layout)) return;
    paintGridScreen(p, layout);
}

void RemoteView::paintGridScreen(QPainter& p, const QJsonObject& layout) {
    // Единый рендерер экранов remote.json: фон — картинка или заливка цветом,
    // плитки — картинка или «таблетка» цветом с рамкой (по умолчанию #0b3442 и белая).
    const auto ref = layout.value("ref").toArray();
    const qreal refW = ref[0].toDouble();
    const qreal refH = ref[1].toDouble();
    const QRectF lcd = toWidget(screenRect());
    const auto background = layout.value("background").toObject();
    const auto cellStyle = tileStyle(layout, tiles);
    const QColor bgColor(background.value("color").toString("#0b3442"));
    const QColor cellColor(cellStyle.value("color").toString("#0b3442"));
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
    const bool glass = !background.contains("image") && !layout.value("compact").toBool();
    if (!bgImage.isNull()) p.drawImage(QRectF(0, 0, refW, refH), bgImage);
    else if (glass) {
        if (bgColor == QColor("#0b3442")) paintLcdBackdrop(p, refW, refH);
        else {
            QRadialGradient glow(QPointF(refW / 2.0, refH * 0.55), refH * 0.62);
            glow.setColorAt(0.0, bgColor.lighter(190));
            glow.setColorAt(0.5, bgColor);
            glow.setColorAt(1.0, bgColor.darker(300));
            p.fillRect(QRectF(0, 0, refW, refH), glow);
        }
    } else p.fillRect(QRectF(0, 0, refW, refH), bgColor);

    const int capacity = std::max(1, slotsPerPage(activeMode));
    const int first = currentPage() * capacity;
    const bool activityScreen = isListScreen();
    const bool softScreen = currentScreen == Screen::SoftButtons;
    int slot = 0;
    const auto gridZones = layout.value("cells").toArray();
    if (glass) {
        // Стеклянный фон 900: общие шапка и подвал экрана.
        const QString layoutTitle = layout.value("title").toString();
        const QString titleText = !layoutTitle.isEmpty() ? layoutTitle
                                  : currentScreen == Screen::Devices ? tr("Devices") : softTitle;
        paintLcdHeader(p, font(), refW, titleText, tr("p. %1/%2").arg(currentPage() + 1).arg(pageCount()));
        paintLcdFooter(p, font(), refW, refH, tr("Options"), tr("Devices"));
    }
    // Экран 600–700: спрайт фона модели, белая шапка и вкладка Devices; плитки как у 900.
    const bool compact = layout.value("compact").toBool();
    if (compact) {
        QRectF band;
        for (const auto& value : gridZones) {
            const auto zone = value.toObject();
            if (zone.value("kind").toString() == "ScreenLabel") band = band.united(readRect(zone.value("rect")));
        }
        if (!band.isEmpty()) p.fillRect(QRectF(0, 0, refW, band.bottom()), Qt::white);
    }
    for (const auto& value : gridZones) {
        const auto zone = value.toObject();
        const QRectF rect = readRect(zone.value("rect"));
        if (rect.isEmpty()) continue;
        const QString zoneKind = zone.value("kind").toString("ScreenSlot");
        if (glass && (zoneKind == "ScreenLabel" || zoneKind == "ScreenSide")) {
            // Служебные зоны уже нарисованы общей шапкой и подвалом.
            continue;
        }
        if (compact && zoneKind != "ScreenSlot") {
            const QString id = zone.value("id").toString();
            if (zoneKind == "ScreenLabel") {
                QString text;
                int align = Qt::AlignVCenter;
                QRectF box = rect;
                if (id == "Title") {
                    text = !layout.value("title").toString().isEmpty() ? layout.value("title").toString()
                           : currentScreen == Screen::Devices ? tr("Devices") : softTitle;
                    align |= Qt::AlignLeft;
                    box.adjust(4, 0, 0, 0);
                } else if (id == "Page") {
                    text = QStringLiteral("%1/%2").arg(currentPage() + 1).arg(pageCount());
                    align |= Qt::AlignRight;
                    box.adjust(0, 0, -4, 0);
                }
                drawSingleLine(p, box, text, QColor("#111111"), true, 1.0, 0.68, align);
            } else if (zoneKind == "ScreenSide" && id == "Devices") {
                QPainterPath tab;
                tab.addRoundedRect(rect.adjusted(0, 0, 0, rect.height()), 6, 6);
                p.setPen(Qt::NoPen);
                p.setBrush(Qt::white);
                p.drawPath(tab);
                drawSingleLine(p, rect, tr("Devices"), QColor("#111111"), true, 1.0, 0.72);
            }
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
            paintPill(p, rect, active, hover, cellColor, borderColor);
        }
        if (!label.isEmpty()) {
            // Те же размеры шрифта и расположение, что у классических списков и команд.
            const bool rows = activityScreen && !compact; // на узком экране 600–700 подписи по центру
            QFont tileFont = font();
            tileFont.setPixelSize(rows ? 17 : compact ? 12 : 14);
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
            if (layout.value("touch").toBool()) screenHits.append({screenToPhoto(rect), index});
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
            if (layout.value("touch").toBool()) screenHits.append({screenToPhoto(rect), index});
        } else if (kind == "ScreenLabel") {
            // Подписи из конфигурации и заголовок экрана активностей рисуются поверх картинок.
            QString text;
            if (id == "Caption" || id == "Title") {
                text = currentScreen == Screen::Devices ? tr("Devices") : softTitle;
            } else if (id == "Heading") {
                if (activityScreen) text = currentScreen == Screen::Devices ? tr("Devices") : tr("What would you like to do?");
            } else if (id == "Page" || id == "PageLabel") {
                if (activityScreen || softScreen)
                    text = tr("%1 of %2").arg(currentPage() + 1).arg(pageCount());
            } else if (id == "Clock") {
                text = QLocale::system().toString(QTime::currentTime(), QLocale::ShortFormat);
            } else if (id == "PageWord") {
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

// ── interaction ──────────────────────────────────────────────────────────────

void RemoteView::mouseMoveEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    if (event->buttons() & Qt::LeftButton) {
        startDragIfMoved(pos);
    }
    const Zone* zone = zoneAt(pos);
    const ScreenHit* hit = screenHitAt(pos);

    const QString newHover = (zone != nullptr) ? zone->id : QString();
    const int newScreenHover =
        (hit != nullptr) ? static_cast<int>(hit - screenHits.constData()) : -1;

    if (newHover != hoverId || newScreenHover != hoverScreenHit) {
        hoverId = newHover;
        hoverScreenHit = newScreenHover;
        setCursor((zone != nullptr || hit != nullptr) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void RemoteView::leaveEvent(QEvent* event) {
    hoverId.clear();
    hoverScreenHit = -1;
    unsetCursor();
    update();
    QWidget::leaveEvent(event);
}

void RemoteView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPointF pos = event->position();

    // откуда может начаться перетаскивание: назначенная кнопка / экранная кнопка
    pressPos = pos;
    clickKey.clear();
    clickSoft = -1;
    pressKey.clear();
    pressSoft = -1;
    if (const ScreenHit* hit = screenHitAt(pos)) {
        if ((currentScreen == Screen::SoftButtons) && !softLabels.value(hit->value).isEmpty()) {
            pressSoft = hit->value;
        }
    } else if (const Zone* zone = zoneAt(pos)) {
        const bool assigned =
            (zone->kind == ZoneKind::Key) &&
            (((currentMode == Mode::Device) &&
              (deviceKeys.value(zone->id, KeyState::Free) != KeyState::Free)) ||
             ((currentMode == Mode::Activity) && activityKeys.contains(zone->id)));
        if (assigned) {
            pressKey = zone->id;
        }
    }

    if (const ScreenHit* hit = screenHitAt(pos)) {
        if (currentScreen == Screen::Devices) {
            emit deviceClicked(hit->value);
        } else if (currentScreen == Screen::Activities) {
            emit activityClicked(hit->value);
        } else {
            clickSoft = hit->value;
            setSelectedSoft(clickSoft);
        }
        return;
    }

    const Zone* zone = zoneAt(pos);
    if (zone == nullptr) {
        QWidget::mousePressEvent(event);
        return;
    }

    switch (zone->kind) {
    case ZoneKind::Key:
        clickKey = zone->id;
        setSelectedKey(clickKey);
        break;
    case ZoneKind::System:
        emit systemKeyClicked(zone->id);
        break;
    case ZoneKind::ScreenActs:
        setScreen(Screen::Activities);
        emit screenChanged(currentScreen);
        break;
    case ZoneKind::ScreenSoft:
        if (zone->id == "Devices" && modes.contains("devices"))
            selectScreenMode("devices");
        else if (zone->id == "Options" && modes.contains("options"))
            selectScreenMode("options");
        else if (!zone->id.startsWith("Soft"))
            setScreen(Screen::SoftButtons);
        emit screenChanged(currentScreen);
        break;
    case ZoneKind::PagePrev:
        flipPage(-1);
        break;
    case ZoneKind::PageNext:
        flipPage(1);
        break;
    case ZoneKind::ScreenMode: {
        const QString target = zone->target; // zone указывает в allZones, который пересоберётся
        selectScreenMode(target);
        emit screenChanged(currentScreen);
        break;
    }
    }
}

void RemoteView::mouseReleaseEvent(QMouseEvent* event) {
    const QString key = std::exchange(clickKey, {});
    const int soft = std::exchange(clickSoft, -1);
    pressKey.clear();
    pressSoft = -1;
    if (event->button() == Qt::LeftButton &&
        (event->position() - pressPos).manhattanLength() < QApplication::startDragDistance()) {
        if (!key.isEmpty()) emit keyClicked(key);
        else if (soft >= 0) emit softButtonClicked(soft);
    }
    QWidget::mouseReleaseEvent(event);
}

void RemoteView::startDragIfMoved(const QPointF& pos) {
    if ((pressKey.isEmpty() && (pressSoft < 0)) ||
        ((pos - pressPos).manhattanLength() < QApplication::startDragDistance())) {
        return;
    }
    clickKey.clear();
    clickSoft = -1;
    auto* mime = new QMimeData();
    QString text;
    if (!pressKey.isEmpty()) {
        mime->setData(MIME_KEY, pressKey.toUtf8());
        text = keyName(pressKey);
    } else {
        mime->setData(MIME_SOFT, QByteArray::number(pressSoft));
        text = softLabels.value(pressSoft);
    }
    pressKey.clear();
    pressSoft = -1;

    const QPixmap pixmap = dragPixmap(text, font());
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(pixmap.width() / 2, pixmap.height() / 2));
    drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
    hoverId.clear();
    hoverScreenHit = -1;
    update();
}

bool RemoteView::updateDropTarget(const QMimeData* mime, const QPointF& pos) {
    QString key;
    int screen = -1;
    const bool command = mime->hasFormat(MIME_COMMAND);
    if (mime->hasFormat(MIME_KEY) || command) {
        const Zone* zone = zoneAt(pos);
        if ((zone != nullptr) && (zone->kind == ZoneKind::Key) && (currentMode != Mode::Neutral)) {
            key = zone->id;
        }
    }
    // над стрелками листания экрана: перелистнуть страницу
    const Zone* flip = zoneAt(pos);
    const bool canFlip =
        (flip != nullptr) &&
        ((flip->kind == ZoneKind::PagePrev) || (flip->kind == ZoneKind::PageNext)) &&
        (currentScreen == Screen::SoftButtons) && (mime->hasFormat(MIME_SOFT) || command);
    const QString flipId = canFlip ? flip->id : QString();
    if (flipId != dragFlipZone) {
        dragFlipZone = flipId;
        if (canFlip) {
            flipPage((flip->kind == ZoneKind::PagePrev) ? -1 : 1);
        }
    }
    if ((mime->hasFormat(MIME_SOFT) || command) && (currentScreen == Screen::SoftButtons) &&
        (currentMode != Mode::Neutral)) {
        if (const ScreenHit* hit = screenHitAt(pos)) {
            screen = static_cast<int>(hit - screenHits.constData());
        }
    }
    if ((key != hoverId) || (screen != hoverScreenHit)) {
        hoverId = key;
        hoverScreenHit = screen;
        update();
    }
    return !key.isEmpty() || (screen >= 0);
}

void RemoteView::dragEnterEvent(QDragEnterEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (mime->hasFormat(MIME_KEY) || mime->hasFormat(MIME_SOFT) || mime->hasFormat(MIME_COMMAND)) {
        event->acceptProposedAction();
        updateDropTarget(mime, event->position());
    }
}

void RemoteView::dragMoveEvent(QDragMoveEvent* event) {
    if (updateDropTarget(event->mimeData(), event->position())) {
        // Ctrl: копия кнопки вместо переноса
        const bool copy = event->modifiers() & Qt::ControlModifier;
        event->setDropAction((copy || event->mimeData()->hasFormat(MIME_COMMAND)) ? Qt::CopyAction
                                                                                  : Qt::MoveAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void RemoteView::dragLeaveEvent(QDragLeaveEvent* event) {
    dragFlipZone.clear();
    hoverId.clear();
    hoverScreenHit = -1;
    update();
    QWidget::dragLeaveEvent(event);
}

void RemoteView::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (!updateDropTarget(mime, event->position())) {
        event->ignore();
        return;
    }
    const QString key = hoverId;
    const int position = (hoverScreenHit >= 0) ? screenHits.value(hoverScreenHit).value : -1;
    dragFlipZone.clear();
    hoverId.clear();
    hoverScreenHit = -1;
    update();
    event->acceptProposedAction();

    QString deviceId;
    QString command;
    if (mime->hasFormat(MIME_COMMAND) &&
        parseCommandPayload(mime->data(MIME_COMMAND), deviceId, command)) {
        if (!key.isEmpty()) {
            emit commandDropped(key, deviceId, command);
        } else if (position >= 0) {
            emit commandDroppedOnSoft(position, deviceId, command);
        }
    } else if (mime->hasFormat(MIME_KEY) && !key.isEmpty()) {
        const QString from = QString::fromUtf8(mime->data(MIME_KEY));
        if (from != key) {
            emit keyDropped(from, key, event->modifiers() & Qt::ControlModifier);
        }
    } else if (mime->hasFormat(MIME_SOFT) && (position >= 0)) {
        const int from = mime->data(MIME_SOFT).toInt();
        if (from != position) {
            emit softDropped(from, position);
        }
    }
}

bool RemoteView::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const Zone* zone = zoneAt(help->pos());
        if (zone == nullptr) {
            QToolTip::hideText();
            event->ignore();
            return true;
        }
        QString text = keyName(zone->id);
        if (zone->kind == ZoneKind::Key && currentMode == Mode::Device) {
            switch (deviceKeys.value(zone->id, KeyState::Free)) {
            case KeyState::Free:
                text = tr("%1 — free").arg(text);
                break;
            case KeyState::Broken:
                text = tr("%1 — no such command").arg(text);
                break;
            case KeyState::Assigned:
                break;
            }
        } else if (zone->kind == ZoneKind::ScreenMode) {
            text = tr("Screen: %1").arg(screenModeName(zone->target));
        } else if (zone->kind == ZoneKind::PagePrev || zone->kind == ZoneKind::PageNext) {
            text = keyName(zone->kind == ZoneKind::PagePrev ? "PageLeft" : "PageRight");
        } else if (zone->kind != ZoneKind::Key) {
            text = tr("%1 (system button)").arg(text);
        }
        QToolTip::showText(help->globalPos(), text, this);
        return true;
    }
    return QWidget::event(event);
}
