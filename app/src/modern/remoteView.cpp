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
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHelpEvent>
#include <QImage>
#include <QLocale>
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

/** Прочитать встроенный JSON; при ошибке вернуть пустой документ и диагностику. */
QJsonDocument readJson(const QString& path) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(resources::data(path), &error);
    if (error.error != QJsonParseError::NoError) qWarning("Invalid remote JSON: %s", qPrintable(path));
    return document;
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
void paintPill(QPainter& p, const QRectF& r, bool selected, bool hover) {
    QLinearGradient fill(r.topLeft(), r.bottomLeft());
    fill.setColorAt(0.0, QColor("#1f5a6c"));
    fill.setColorAt(1.0, QColor("#0a2631"));
    if (selected) {
        // soft glow around the selected pill
        p.setPen(QPen(QColor(140, 196, 232, 110), 6.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, 6.0, 6.0);
    }
    p.setPen(QPen(selected ? QColor("#ffffff") : (hover ? QColor("#e4f0f4") : QColor("#bcd3db")),
                  selected ? 2.5 : 2.0));
    p.setBrush(fill);
    p.drawRoundedRect(r, 6.0, 6.0);
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

/** Буквы под цифрами телефонной клавиатуры — как на самих пультах. */
QString keypadLetters(int digit) {
    static const char* const letters[] = {"", "", "ABC", "DEF", "GHI", "JKL",
                                          "MNO", "PQRS", "TUV", "WXYZ"};
    return digit >= 0 && digit <= 9 ? QString::fromLatin1(letters[digit]) : QString();
}

/** Угловая кнопка открывает один и тот же экран на всех моделях. */
struct CornerKey {
    const char* icon;
    const char* mode;
};

/**
 * Содержимое экранной кнопки по её id из layouts.json.
 * Значок совпадает с id; цифры и служебные подписи задаются здесь.
 */
CellContent keyContent(const QString& id, const QString& mode) {
    static const QHash<QString, CornerKey> corners = {
        {"CornerTL", {"Transport", "transport"}},
        {"CornerTR", {"Favorites", "favorites"}},
        {"CornerBL", {"Numeric", "keypad"}},
        {"CornerBR", {"Custom", "command"}}, // звезда открывает экранные кнопки активности
    };
    CellContent cell;
    if (const auto corner = corners.constFind(id); corner != corners.cend()) {
        cell.icon = QString::fromLatin1(corner->icon);
        cell.active = mode == QLatin1String(corner->mode);
        return cell;
    }
    if (id == "Help") {
        cell.label = QCoreApplication::translate("RemoteView", "Help");
    } else if (id == "Pause") {
        cell.icon = id;
        cell.accent = true; // центральная кнопка выделена, как на пульте
    } else if (id == "Record") {
        cell.icon = id;
        cell.label = QCoreApplication::translate("RemoteView", "Rec");
    } else if (id == "Slow") {
        cell.icon = id;
        cell.label = QCoreApplication::translate("RemoteView", "Slow");
    } else if (id.startsWith("Number") && id != "NumberStar" && id != "NumberHash") {
        const int digit = id.mid(6).toInt();
        cell.label = QString::number(digit);
        cell.sublabel = keypadLetters(digit);
    } else {
        cell.icon = id; // Play, Stop, NumberStar, NumberHash, PageUp/PageDown…
    }
    return cell;
}

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

/**
 * Палитра экрана. Выбирается полем "style" раскладки в layouts.json:
 * h900 (по умолчанию, вид 900), t1000 (синий глянец, светлые плитки),
 * t1100 (тёмно-бирюзовый фон, чёрные плитки и панели). Цвета сняты с фото экранов пультов.
 */
struct ScreenStyle {
    QColor bgCenter, bgMid, bgEdge;
    bool gloss = false; ///< диагональные блики на фоне (1000)
    bool frame = false; ///< тонкая светлая рамка по краю LCD (900)

    QColor tileTop, tileBottom, tileActiveTop, tileActiveBottom, tileBorder, tileText;
    qreal tileBorderWidth = 2.0;
    bool tileBold = false;
    QColor selectBorder, selectGlow;

    QColor keyTop, keyBottom, keyBorder, keyText; ///< круглые кнопки транспорта и цифры
    bool keyGloss = false;                        ///< светлый блик в верхней половине кнопки
    QColor cornerTop, cornerBottom, cornerBorder, cornerText;
    QColor helpTop, helpBottom, helpBorder, helpText;
    QColor activeBorder, activeText; ///< угловая кнопка текущего экрана
    QColor accentIcon;               ///< значок на оранжевой Pause
    QColor homeIcon;

    QColor panelTop, panelBottom, panelBorder, panelDivider, panelText;
    QColor band;
    QColor ovalCenter, ovalEdge, ovalBorder;

    QColor labelText;
    bool labelBold = false;
    bool textShadow = false; ///< тёмная тень под белым текстом (1000)
};

const ScreenStyle& screenStyle(const QString& name) {
    static const ScreenStyle h900 = [] {
        ScreenStyle s;
        s.bgCenter = QColor("#1d7087"); s.bgMid = QColor("#0b3442"); s.bgEdge = QColor("#031017");
        s.frame = true;
        s.tileTop = QColor("#1f5a6c"); s.tileBottom = QColor("#0a2631");
        s.tileActiveTop = QColor("#247d95"); s.tileActiveBottom = QColor("#0a2b38");
        s.tileBorder = QColor("#bcd3db"); s.tileText = Qt::white;
        s.selectBorder = Qt::white; s.selectGlow = QColor(140, 196, 232, 110);
        s.keyTop = QColor("#1a4d5e"); s.keyBottom = QColor("#082330");
        s.keyBorder = QColor("#9bc0ce"); s.keyText = Qt::white;
        s.cornerTop = s.helpTop = s.keyTop; s.cornerBottom = s.helpBottom = s.keyBottom;
        s.cornerBorder = s.helpBorder = s.keyBorder; s.cornerText = s.helpText = Qt::white;
        s.activeBorder = s.activeText = QColor("#f0a050");
        s.accentIcon = s.homeIcon = Qt::white;
        s.panelTop = QColor("#1a4d5e"); s.panelBottom = QColor("#082330");
        s.panelBorder = QColor("#9bc0ce"); s.panelDivider = QColor(155, 192, 206, 120);
        s.panelText = Qt::white;
        s.band = Qt::black;
        s.ovalCenter = QColor(255, 255, 255, 30); s.ovalEdge = QColor(255, 255, 255, 10);
        s.ovalBorder = QColor(255, 255, 255, 60);
        s.labelText = QColor("#d7e6ec");
        return s;
    }();
    static const ScreenStyle t1000 = [] {
        ScreenStyle s;
        s.bgCenter = QColor("#3d8fdc"); s.bgMid = QColor("#1c5fb3"); s.bgEdge = QColor("#0a2e6b");
        s.gloss = true;
        s.tileTop = QColor(214, 226, 234, 215); s.tileBottom = QColor(146, 168, 186, 215);
        s.tileActiveTop = QColor(238, 243, 247, 235); s.tileActiveBottom = QColor(176, 197, 213, 235);
        s.tileBorder = QColor(236, 243, 247, 230); s.tileText = Qt::white;
        s.tileBorderWidth = 1.2; s.tileBold = true;
        s.selectBorder = Qt::white; s.selectGlow = QColor(255, 255, 255, 120);
        // серебристые кнопки транспорта с тёмными значками
        s.keyTop = QColor("#f6f7f8"); s.keyBottom = QColor("#a7acb1");
        s.keyBorder = QColor("#e4e8eb"); s.keyText = QColor("#26292c"); s.keyGloss = true;
        // углы: значки прямо на чёрной кромке, без подложки
        s.cornerTop = s.cornerBottom = s.cornerBorder = Qt::transparent;
        s.cornerText = QColor("#e9eef2");
        s.helpTop = QColor("#6a6e72"); s.helpBottom = QColor("#2b2e31");
        s.helpBorder = QColor("#8e9398"); s.helpText = Qt::white;
        s.activeBorder = s.activeText = QColor("#f0a050");
        s.accentIcon = QColor("#8c1111"); s.homeIcon = QColor("#7cc242");
        s.panelTop = QColor("#f1f2f3"); s.panelBottom = QColor("#c2c6c9");
        s.panelBorder = QColor(255, 255, 255, 200); s.panelDivider = QColor(110, 116, 122, 150);
        s.panelText = QColor("#2a2d30");
        s.band = QColor("#050607");
        s.ovalCenter = QColor(130, 185, 240, 110); s.ovalEdge = QColor(70, 130, 210, 70);
        s.ovalBorder = QColor(175, 212, 250, 140);
        s.labelText = Qt::white; s.labelBold = true; s.textShadow = true;
        return s;
    }();
    static const ScreenStyle t1100 = [] {
        ScreenStyle s;
        s.bgCenter = QColor("#3b93a6"); s.bgMid = QColor("#1d5f6f"); s.bgEdge = QColor("#0a2a33");
        s.tileTop = QColor("#262829"); s.tileBottom = QColor("#050606");
        s.tileActiveTop = QColor("#3b4648"); s.tileActiveBottom = QColor("#101616");
        s.tileBorder = QColor("#8fc3cf"); s.tileText = Qt::white;
        s.tileBorderWidth = 1.2; s.tileBold = true;
        s.selectBorder = Qt::white; s.selectGlow = QColor(143, 195, 207, 130);
        s.keyTop = QColor("#34474f"); s.keyBottom = QColor("#0b1418");
        s.keyBorder = QColor("#8fb3bf"); s.keyText = QColor("#e8eef0"); s.keyGloss = true;
        s.cornerTop = QColor("#1b1b1b"); s.cornerBottom = Qt::black;
        s.cornerBorder = QColor("#5a5f62"); s.cornerText = Qt::white;
        s.helpTop = QColor("#2b2b2b"); s.helpBottom = Qt::black;
        s.helpBorder = QColor("#8a9094"); s.helpText = Qt::white;
        s.activeBorder = QColor("#f0a050"); s.activeText = QColor("#f5a623");
        s.accentIcon = QColor("#8c1111"); s.homeIcon = QColor("#f5a623");
        s.panelTop = QColor("#2b2d2e"); s.panelBottom = QColor("#050505");
        s.panelBorder = QColor("#4a5054"); s.panelDivider = QColor(140, 190, 200, 130);
        s.panelText = Qt::white;
        s.band = Qt::black;
        s.ovalCenter = QColor(160, 210, 222, 70); s.ovalEdge = QColor(120, 180, 195, 40);
        s.ovalBorder = QColor(170, 215, 225, 120);
        s.labelText = Qt::white; s.labelBold = true;
        return s;
    }();
    if (name == QLatin1String("t1000")) return t1000;
    if (name == QLatin1String("t1100")) return t1100;
    return h900;
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
 * Плитка-картинка из resources/sprites (поле "tile": {"image": ...} раскладки), одна на все
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

const TileFace* tileFace(const QJsonObject& layout) {
    static QHash<QString, TileFace> cache;
    static quint64 version = 0;
    if (version != resources::revision()) { cache.clear(); version = resources::revision(); }
    const auto tile = layout.value("tile").toObject();
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

/** Плитка активности / канала / команды. */
void paintTile(QPainter& p, const QRectF& r, qreal radius, const CellContent& cell,
               bool selected, bool hover, const ScreenStyle& s, const TileFace* face = nullptr) {
    if (r.isEmpty()) return;
    const qreal rounding = std::clamp(radius, 0.0, std::min(r.width(), r.height()) / 2.0);
    if (face != nullptr) {
        // плитка-картинка; свободная ячейка — та же плитка с «+», как на экране с фото
        paintTileFace(p, r, *face);
        paintTileHighlight(p, r, rounding, selected, hover);
        if (cell.empty) {
            drawSingleLine(p, r, QStringLiteral("+"), s.tileText, false, hover ? 0.9 : 0.45, 0.55);
            return;
        }
    } else if (cell.empty) {
        // Свободная экранная ячейка: пунктирная рамка и «+», как на 900.
        QColor line = s.tileBorder;
        line.setAlpha(hover ? 230 : 120);
        p.setPen(QPen(line, 1.5, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, rounding, rounding);
        drawSingleLine(p, r, QStringLiteral("+"), s.tileText, false, hover ? 1.0 : 0.6, 0.60);
        return;
    } else {
        QLinearGradient fill(r.topLeft(), r.bottomLeft());
        if (cell.accent) {
            fill.setColorAt(0.0, QColor("#f0a24a"));
            fill.setColorAt(1.0, QColor("#a34f12"));
        } else if (cell.active) {
            fill.setColorAt(0.0, s.tileActiveTop);
            fill.setColorAt(1.0, s.tileActiveBottom);
        } else {
            fill.setColorAt(0.0, s.tileTop);
            fill.setColorAt(1.0, s.tileBottom);
        }
        if (selected) {
            // мягкое свечение вокруг выбранной плитки, как у полос 900
            p.setPen(QPen(s.selectGlow, 6.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, rounding, rounding);
        }
        const QColor border = cell.accent ? QColor("#ffc281") : selected ? s.selectBorder :
                              hover ? s.tileBorder.lighter(125) : s.tileBorder;
        p.setPen(QPen(border, selected ? 2.5 : hover ? s.tileBorderWidth + 0.6 : s.tileBorderWidth));
        p.setBrush(fill);
        p.drawRoundedRect(r, rounding, rounding);
    }

    const QColor colour = cell.colour;
    const qreal opacity = (cell.disabled ? 0.45 : 1.0);
    const bool hasIcon = !cell.icon.isEmpty() || !cell.activityType.isEmpty();
    const bool hasLabel = !cell.label.isEmpty();
    const bool hasSublabel = !cell.sublabel.isEmpty();
    const qreal pad = std::min(r.width(), r.height()) * 0.09;
    const QRectF content = r.adjusted(pad, pad, -pad, -pad);
    if (content.isEmpty()) return;

    if (hasIcon && (hasLabel || hasSublabel)) {
        const QRectF iconRect(content.left() + content.width() * 0.06, content.top(),
                              content.width() * 0.88, content.height() * 0.60);
        drawCellIcon(p, iconRect, cell, opacity);
        const QRectF textRect(content.left(), content.top() + content.height() * 0.58,
                              content.width(), content.height() * 0.42);
        drawStyledText(p, textRect, cell.label, colour, s.tileBold, s.textShadow, opacity, 0.70, 0.30);
    } else if (hasIcon) {
        const qreal side = std::min(content.width(), content.height()) * 0.92;
        drawCellIcon(p, QRectF(content.center().x() - side / 2.0, content.center().y() - side / 2.0,
                               side, side),
                     cell, opacity);
    } else if (hasLabel || hasSublabel) {
        if (hasSublabel) {
            const QRectF main(content.left(), content.top(), content.width(), content.height() * 0.60);
            const QRectF sub(content.left(), content.top() + content.height() * 0.56,
                             content.width(), content.height() * 0.44);
            drawStyledText(p, main, cell.label, colour, true, s.textShadow, opacity, 0.85, 0.40);
            drawStyledText(p, sub, cell.sublabel, colour, false, s.textShadow, opacity * 0.9, 0.75, 0.35);
        } else {
            drawStyledText(p, content, cell.label, colour, s.tileBold, s.textShadow, opacity, 0.48, 0.22);
        }
    }
}

/** Вид экранной кнопки: от него зависят подложка и цвет значка. */
enum class KeyRole {
    Round,  ///< транспорт, цифры
    Corner, ///< углы, Home/Settings, листание
    Help,   ///< вкладка Help сверху
    Panel   ///< кнопка внутри боковой панели: только значок
};

KeyRole keyRole(const QString& id, bool insidePanel) {
    if (insidePanel) return KeyRole::Panel;
    if (id == QLatin1String("Help")) return KeyRole::Help;
    if (id.startsWith(QLatin1String("Corner")) || id == QLatin1String("Home") ||
        id == QLatin1String("Settings") || id == QLatin1String("PageUp") ||
        id == QLatin1String("PageDown"))
        return KeyRole::Corner;
    return KeyRole::Round;
}

/** Кнопка на экране: угловая, боковая или транспортная. */
void paintKey(QPainter& p, const QRectF& r, qreal radius, const CellContent& source, bool hover,
              const ScreenStyle& s, KeyRole role) {
    if (r.isEmpty()) return;
    CellContent cell = source;
    const qreal rounding = std::clamp(radius, 0.0, std::min(r.width(), r.height()) / 2.0);
    QColor top = s.keyTop, bottom = s.keyBottom, edge = s.keyBorder, text = s.keyText;
    if (role == KeyRole::Corner) {
        top = s.cornerTop; bottom = s.cornerBottom; edge = s.cornerBorder; text = s.cornerText;
    } else if (role == KeyRole::Help) {
        top = s.helpTop; bottom = s.helpBottom; edge = s.helpBorder; text = s.helpText;
    } else if (role == KeyRole::Panel) {
        top = bottom = edge = Qt::transparent; text = s.panelText;
    }
    if (cell.accent) {
        // Pause: оранжевый круг, как на всех пультах с экраном
        top = QColor("#f8cf4a"); bottom = QColor("#e08a00"); edge = QColor("#ffd98a");
        text = s.accentIcon;
    } else if (cell.active) {
        top = top.alpha() ? top.lighter(130) : top;
        bottom = bottom.alpha() ? bottom.lighter(130) : bottom;
        edge = s.activeBorder;
        text = s.activeText;
    } else if (hover && edge.alpha()) {
        edge = edge.lighter(125);
    }
    if (source.icon == QLatin1String("Home") && !cell.active) text = s.homeIcon;

    QLinearGradient fill(r.topLeft(), r.bottomLeft());
    fill.setColorAt(0.0, top);
    fill.setColorAt(1.0, bottom);
    p.setPen(edge.alpha() ? QPen(edge, (cell.active || cell.accent || hover) ? 2.0 : 1.4)
                          : QPen(Qt::NoPen));
    p.setBrush(fill);
    p.drawRoundedRect(r, rounding, rounding);
    if (s.keyGloss && role == KeyRole::Round && !cell.accent) {
        // блик в верхней половине, как у выпуклых кнопок 1000/1100
        QLinearGradient shine(r.topLeft(), QPointF(r.left(), r.center().y()));
        shine.setColorAt(0.0, QColor(255, 255, 255, 90));
        shine.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(shine);
        p.drawRoundedRect(r.adjusted(1, 1, -1, -r.height() / 2.0), rounding, rounding);
    }
    if (cell.empty) return;

    cell.iconColour = text;
    const QColor colour = text;
    const qreal opacity = (cell.disabled ? 0.45 : 1.0);
    const bool hasIcon = !cell.icon.isEmpty();
    const bool hasLabel = !cell.label.isEmpty();
    const bool hasSublabel = !cell.sublabel.isEmpty();
    const qreal pad = std::min(r.width(), r.height()) * 0.10;
    const QRectF content = r.adjusted(pad, pad, -pad, -pad);
    if (content.isEmpty()) return;

    if (hasIcon && hasLabel) {
        const QRectF iconRect(content.left() + content.width() * 0.15, content.top(),
                              content.width() * 0.70, content.height() * 0.58);
        drawCellIcon(p, iconRect, cell, opacity);
        drawSingleLine(p, QRectF(content.left(), content.top() + content.height() * 0.56,
                                 content.width(), content.height() * 0.44),
                       cell.label, colour, false, opacity, 0.80);
    } else if (hasIcon) {
        const qreal side = std::min(content.width(), content.height()) * 0.82;
        drawCellIcon(p, QRectF(content.center().x() - side / 2.0, content.center().y() - side / 2.0,
                               side, side),
                     cell, opacity);
    } else if (hasSublabel) {
        // цифры клавиатуры: крупная цифра и мелкие буквы под ней
        drawSingleLine(p, QRectF(content.left(), content.top(), content.width(),
                                 content.height() * 0.60),
                       cell.label, colour, true, opacity, 0.90);
        drawSingleLine(p, QRectF(content.left(), content.top() + content.height() * 0.58,
                                 content.width(), content.height() * 0.42),
                       cell.sublabel, colour, false, opacity * 0.9, 0.78);
    } else if (hasLabel) {
        drawSingleLine(p, content, cell.label, colour, true, opacity, 0.62);
    }
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
 * Экраны модели из remotes/<id>/screens.json: какие картинки из resources/sprites где
 * лежат и на каком слое. Готовые фото экранов не используются. Формат — в resources/README.md.
 */
struct SpriteSet {
    QHash<QString, Sprite> images;
    QHash<QString, QList<SpriteItem>> screens; ///< режим → картинки по возрастанию слоя
};

const SpriteSet* spriteSet(const QString& directory) {
    static QHash<QString, SpriteSet> cache;
    static quint64 version = 0;
    if (version != resources::revision()) { cache.clear(); version = resources::revision(); }
    if (directory.isEmpty()) return nullptr;
    auto found = cache.constFind(directory);
    if (found == cache.cend()) {
        SpriteSet set;
        const auto document = readJson(directory + "/screens.json").object();
        const auto images = document.value("images").toObject();
        for (auto it = images.begin(); it != images.end(); ++it) {
            const auto def = it.value().toObject();
            // путь внутри resources/, общий для всех пультов: картинки можно переиспользовать
            const QString file = ":/res/" + def.value("file").toString();
            Sprite sprite;
            if (file.endsWith(".svg", Qt::CaseInsensitive)) {
                auto renderer = std::make_shared<QSvgRenderer>(resources::data(file));
                if (renderer->isValid()) sprite.svg = renderer;
            } else {
                sprite.image = QImage::fromData(resources::data(file)).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            }
            if (sprite.isNull()) {
                qWarning("Cannot load sprite: %s", qPrintable(file));
                continue;
            }
            set.images.insert(it.key(), sprite);
        }
        const auto screens = document.value("screens").toObject();
        for (auto it = screens.begin(); it != screens.end(); ++it) {
            QList<SpriteItem> items;
            for (const auto& value : it.value().toArray()) {
                const auto item = value.toObject();
                SpriteItem entry;
                entry.image = item.value("image").toString();
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
            set.screens.insert(it.key(), items);
        }
        found = cache.insert(directory, set);
    }
    return found->images.isEmpty() ? nullptr : &found.value();
}

} // namespace

RemoteView::RemoteView(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setAcceptDrops(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setRemoteProfile(QStringLiteral("900"));
}

void RemoteView::setRemoteProfile(const QString& id) {
    const QString requested = id.isEmpty() ? QStringLiteral("900") : id;
    if (profileId == requested && resourceRevision == resources::revision()) return;
    resourceRevision = resources::revision();
    profileId = requested;
    photo = QPixmap();
    photoSize = QSize(PHOTO_WIDTH, PHOTO_HEIGHT);
    modelScreen = {};
    screenTint = QColor();
    profileDirectory.clear();
    modelZones.clear();
    allZones.clear();
    modes = {};
    layouts.clear();
    activityPage = softPage = otherPage = 0;
    screenHits.clear();
    hoverId.clear();
    hoverScreenHit = -1;
    pressKey.clear();
    pressSoft = -1;
    dragFlipZone.clear();
    unsetCursor();
    if (const auto* profile = harmony::remoteProfile(requested.toStdString())) {
        const QString imagePath = QString::fromUtf8(profile->image.data());
        const QString directory = QFileInfo(imagePath).path();
        profileDirectory = directory;
        photo.loadFromData(resources::data(imagePath));
        const auto metadata = readJson(directory + "/profile.json").object();
        const auto size = metadata.value("size").toArray();
        const QSize declaredSize(size.size() == 2 ? size[0].toInt() : 0,
                                 size.size() == 2 ? size[1].toInt() : 0);
        if (!photo.isNull()) photoSize = photo.size();
        if (!photo.isNull() && declaredSize == photoSize) {
            const QRectF bounds(QPointF(0, 0), QSizeF(photoSize));
            modes = metadata.value("modes").toObject();
            // Старые и пользовательские профили тоже получают список устройств.
            if (!modes.contains("devices") && !modes.isEmpty())
                modes.insert("devices", modes.value(modes.contains("activities") ? "activities" : "command"));
            screenTint = QColor(metadata.value("screenTint").toString()); // пусто: цвет по умолчанию
            const auto library = readJson(":/res/layouts.json").array();
            for (const auto& item : library) {
                const auto layout = item.toObject();
                layouts.insert(layout.value("id").toString(), layout);
            }
            const QRectF screen = readRect(metadata.value("screen"));
            if (screen.isValid() && bounds.contains(screen)) modelScreen = screen;
            const QHash<QString, ZoneKind> kinds = {
                {"Key", ZoneKind::Key}, {"System", ZoneKind::System},
                {"ScreenActs", ZoneKind::ScreenActs}, {"ScreenSoft", ZoneKind::ScreenSoft},
                {"PagePrev", ZoneKind::PagePrev}, {"PageNext", ZoneKind::PageNext}};
            const auto document = readJson(directory + "/zones.json");
            for (const auto& entry : document.array()) {
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
    const auto layout = layouts.value(modes.value(activeMode).toString());
    const auto ref = layout.value("ref").toArray();
    if (ref.size() != 2 || ref[0].toDouble() <= 0 || ref[1].toDouble() <= 0) return;
    const qreal unit = modelScreen.width() / ref[0].toDouble();
    // Кнопки экрана — первыми: при поиске под курсором они важнее зон фото.
    QList<Zone> screen;
    for (const auto& value : layout.value("zones").toArray()) {
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
    return found != names.cend() ? tr(found.value()) : mode;
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
    const QString id = remoteId.isEmpty() ? QStringLiteral("900") : remoteId;
    // Списки кнопок принадлежат текущему снимку ресурсов.
    static QHash<QString, QStringList> cache;
    static quint64 version = 0;
    if (version != resources::revision()) { cache.clear(); version = resources::revision(); }
    if (cache.contains(id)) return cache.value(id);
    QStringList keys;
    if (const auto* profile = harmony::remoteProfile(id.toStdString())) {
        const QString imagePath = QString::fromUtf8(profile->image.data());
        const QString directory = QFileInfo(imagePath).path();
        QPixmap image;
        image.loadFromData(resources::data(imagePath));
        const auto size = readJson(directory + "/profile.json").object().value("size").toArray();
        if (!image.isNull() && size.size() == 2 &&
            QSize(size[0].toInt(), size[1].toInt()) == image.size()) {
            const QRectF bounds(QPointF(0, 0), QSizeF(image.size()));
            const auto document = readJson(directory + "/zones.json");
            for (const auto& entry : document.array()) {
                const auto zone = entry.toObject();
                const QString key = zone.value("id").toString();
                const QRectF rect = readRect(zone.value("rect"));
                if (zone.value("kind").toString() == "Key" && !key.isEmpty() &&
                    rect.isValid() && bounds.contains(rect) && zone.value("radius").isDouble() &&
                    !keys.contains(key)) {
                    keys.append(key);
                }
            }
            // Назначаемые кнопки экранов модели (поле "key" зон в layouts.json):
            // транспорт и цифры 1000/1100, Menu/Guide/Info/Exit на боковых панелях.
            const auto library = readJson(":/res/layouts.json").array();
            const auto profileModes =
                readJson(directory + "/profile.json").object().value("modes").toObject();
            for (const auto& layoutId : profileModes) {
                for (const auto& item : library) {
                    const auto layout = item.toObject();
                    if (layout.value("id") != layoutId) continue;
                    for (const auto& entry : layout.value("zones").toArray()) {
                        const QString key = entry.toObject().value("key").toString();
                        if (!key.isEmpty() && !keys.contains(key)) keys.append(key);
                    }
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
    const auto ref = layouts.value(modes.value(activeMode).toString()).value("ref").toArray();
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
    const auto zones = layouts.value(modes.value(mode).toString()).value("zones").toArray();
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

void RemoteView::paintClassicScreen(QPainter& p) {
    const QRectF lcd = toWidget(screenRect());

    p.save();
    // everything below is drawn in LCD units (LCD_W x LCD_H), scaled with the
    // photo, so text and pills keep the proportions of the real screen
    p.translate(lcd.topLeft());
    p.scale(lcd.width() / LCD_W, lcd.height() / LCD_H);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, LCD_W, LCD_H), 5, 5);
    p.setClipPath(clip);

    QRadialGradient bg(QPointF(LCD_W / 2.0, LCD_H * 0.55), LCD_H * 0.62);
    bg.setColorAt(0.0, QColor("#1d7087"));
    bg.setColorAt(0.5, QColor("#0b3442"));
    bg.setColorAt(1.0, QColor("#031017"));
    p.fillRect(QRectF(0, 0, LCD_W, LCD_H), bg);

    // title band
    p.fillRect(QRectF(0, 0, LCD_W, LCD_HEAD), Qt::black);
    QFont title = font();
    title.setPixelSize(19);
    title.setBold(true);
    p.setFont(title);
    p.setPen(Qt::white);
    const QString titleText =
        (isListScreen()) ? (currentScreen == Screen::Devices ? tr("Devices") : tr("My activities")) : softTitle;
    p.drawText(QRectF(8, 2, LCD_W - 44, 26), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetricsF(title).elidedText(titleText, Qt::ElideRight, LCD_W - 44));
    // battery
    p.setPen(QPen(QColor("#d7dde0"), 1.5));
    p.setBrush(QColor("#3ecf5b"));
    p.drawRect(QRectF(LCD_W - 30, 9, 20, 10));
    p.fillRect(QRectF(LCD_W - 10, 12, 3, 4), QColor("#d7dde0"));

    QFont small = font();
    small.setPixelSize(13);
    small.setBold(true);
    p.setFont(small);
    p.setPen(Qt::white);
    p.drawText(QRectF(8, 28, 120, 20), Qt::AlignLeft | Qt::AlignVCenter,
               tr("p. %1/%2").arg(currentPage() + 1).arg(pageCount()));
    p.drawText(QRectF(LCD_W - 80, 28, 72, 20), Qt::AlignRight | Qt::AlignVCenter,
               QTime::currentTime().toString("HH:mm"));

    screenHits.clear();
    if (isListScreen()) {
        paintActivityScreen(p);
    } else {
        paintSoftScreen(p);
    }

    // Options / Devices band, labels of the two white keys below the LCD
    p.fillRect(QRectF(0, LCD_H - LCD_FOOT, LCD_W, LCD_FOOT), Qt::black);
    p.setFont(small);
    p.setPen(QColor("#9bd46f"));
    p.drawText(QRectF(0, LCD_H - LCD_FOOT, LCD_W / 2.0, LCD_FOOT), Qt::AlignCenter, tr("Options"));
    p.setPen(QColor("#f08a4b"));
    p.drawText(QRectF(LCD_W / 2.0, LCD_H - LCD_FOOT, LCD_W / 2.0, LCD_FOOT), Qt::AlignCenter,
               tr("Devices"));

    p.restore();
}

void RemoteView::paintActivityScreen(QPainter& p) {
    QFont label = font();
    label.setPixelSize(17);
    label.setBold(true);
    const QFontMetricsF fm(label);

    const int first = currentPage() * ACTIVITIES_PER_PAGE;
    for (int slot = 0; slot < ACTIVITIES_PER_PAGE; ++slot) {
        const int index = first + slot;
        if (index >= listItems().size()) {
            break;
        }
        const auto& item = listItems()[index];
        const QRectF row(LCD_PAD, LCD_TOP + slot * (LCD_ROW_H + LCD_ROW_GAP), LCD_W - 2 * LCD_PAD,
                         LCD_ROW_H);
        const bool hover = (hoverScreenHit == screenHits.size());
        paintPill(p, row, item.selected, hover);

        // значок активности как на пульте (80x68 px, вписываем с сохранением пропорций)
        const QRectF icon(row.x() + 4, row.y() + (row.height() - 41.0) / 2.0, 48, 41);
        const QPixmap& pix = activityPixmap(item.type);
        if (currentScreen != Screen::Devices && !pix.isNull()) {
            p.save();
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawPixmap(icon, pix, QRectF(pix.rect()));
            p.restore();
        }

        p.setFont(label);
        p.setPen(Qt::white);
        const QRectF text = currentScreen == Screen::Devices ? row.adjusted(10, 0, -10, 0)
            : QRectF(icon.right() + 6, row.y(), row.right() - icon.right() - 12, row.height());
        p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(item.label, Qt::ElideRight, text.width()));

        screenHits.append(ScreenHit{screenToPhoto(row), index});
    }
}

void RemoteView::paintSoftScreen(QPainter& p) {
    QFont label = font();
    label.setPixelSize(14);
    label.setBold(true);
    p.setFont(label);

    const qreal colW = (LCD_W - 2 * LCD_PAD - LCD_ROW_GAP) / 2.0;
    const int first = softPage * SOFT_PER_PAGE;
    for (int slot = 0; slot < SOFT_PER_PAGE; ++slot) {
        const int position = first + slot;
        const int col = slot % 2;
        const int row = slot / 2;
        const QRectF r(LCD_PAD + col * (colW + LCD_ROW_GAP),
                       LCD_TOP + row * (LCD_ROW_H + LCD_ROW_GAP), colW, LCD_ROW_H);
        const QString text = softLabels.value(position);
        const bool hover = (hoverScreenHit == screenHits.size());

        if (text.isEmpty()) {
            // free slot
            QPen pen(hover ? QColor("#8fb3bf") : QColor("#4a7582"), 1.5, Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, 6, 6);
            p.setPen(QColor("#8fb3bf"));
            p.drawText(r, Qt::AlignCenter, QStringLiteral("+"));
        } else {
            paintPill(p, r, position == selectedSoft, hover);
            p.setPen(Qt::white);
            p.drawText(r.adjusted(4, 2, -4, -2), Qt::AlignCenter | Qt::TextWordWrap, text);
        }
        screenHits.append(ScreenHit{screenToPhoto(r), position});
    }
}

void RemoteView::paintScreen(QPainter& p) {
    if (activeMode.isEmpty()) return;
    const QString layoutId = modes.value(activeMode).toString();
    if ((isListScreen() && layoutId == "list_3") ||
        (currentScreen == Screen::SoftButtons && layoutId == "grid_2x3")) {
        paintClassicScreen(p);
        return;
    }
    paintModernScreen(p);
}

void RemoteView::paintModernScreen(QPainter& p) {
    const QString layoutId = modes.value(activeMode).toString();
    const auto layout = layouts.value(layoutId);
    const auto ref = layout.value("ref").toArray();
    const QRectF lcd = toWidget(screenRect());
    if (ref.size() != 2 || ref[0].toDouble() <= 0 || ref[1].toDouble() <= 0) {
        p.fillRect(lcd, QColor(138, 184, 255, 60));
        return;
    }
    if (layout.value("style").toString() == "h600") {
        paintCompactScreen(p, layout);
        return;
    }
    if (paintComposedScreen(p, layout)) return; // экран из картинок screens.json модели
    const qreal refW = ref[0].toDouble();
    const qreal refH = ref[1].toDouble();
    const auto zones = layout.value("zones").toArray();
    const ScreenStyle& style = screenStyle(layout.value("style").toString());

    p.save();
    QPainterPath clip;
    clip.addRoundedRect(lcd, lcd.width() * 0.035, lcd.height() * 0.035);
    p.setClipPath(clip);

    // «Стеклянный» ЖК: светлое пятно в центре, к краям темнее.
    QRadialGradient background(QPointF(lcd.center().x(), lcd.top() + lcd.height() * 0.55),
                               lcd.height() * 0.62);
    background.setColorAt(0.0, style.bgCenter);
    background.setColorAt(0.5, style.bgMid);
    background.setColorAt(1.0, style.bgEdge);
    p.fillRect(lcd, background);

    p.translate(lcd.topLeft());
    p.scale(lcd.width() / refW, lcd.height() / refH);
    if (style.gloss) {
        // широкие диагональные блики, как на синем фоне 1000
        p.setPen(Qt::NoPen);
        for (const qreal x : {refW * 0.05, refW * 0.30}) {
            QPainterPath streak;
            streak.moveTo(x, 0);
            streak.lineTo(x + refW * 0.16, 0);
            streak.lineTo(x + refW * 0.16 - refH * 0.9, refH);
            streak.lineTo(x - refH * 0.9, refH);
            streak.closeSubpath();
            p.setBrush(QColor(255, 255, 255, 14));
            p.drawPath(streak);
        }
    }
    if (style.frame) {
        p.fillRect(QRectF(0, 0, refW, 1.5), QColor(255, 255, 255, 22));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 38), 0.7));
        p.drawRoundedRect(QRectF(0, 0, refW, refH), 5, 5);
    }

    // Подложки (ScreenPanel): чёрные кромки, овал транспорта, боковые панели.
    // Рисуются до ячеек, поэтому порядок в layouts.json не важен.
    QList<QRectF> sidePanels;
    for (const auto& value : zones) {
        const auto zone = value.toObject();
        if (zone.value("kind") != "ScreenPanel") continue;
        const QRectF rect = readRect(zone.value("rect"));
        if (rect.isEmpty()) continue;
        const QString role = zone.value("role").toString();
        const qreal radius = zone.value("radius").toDouble();
        if (role == "band") {
            p.setPen(Qt::NoPen);
            p.setBrush(style.band);
            p.drawRoundedRect(rect, radius, radius);
        } else if (role == "oval") {
            QRadialGradient glow(rect.center(), std::max(rect.width(), rect.height()) / 2.0);
            glow.setColorAt(0.0, style.ovalCenter);
            glow.setColorAt(1.0, style.ovalEdge);
            p.setPen(QPen(style.ovalBorder, 1.0));
            p.setBrush(glow);
            p.drawEllipse(rect);
        } else if (role == "side") {
            sidePanels.append(rect);
            QLinearGradient fill(rect.topLeft(), rect.bottomLeft());
            fill.setColorAt(0.0, style.panelTop);
            fill.setColorAt(1.0, style.panelBottom);
            p.setPen(QPen(style.panelBorder, 1.0));
            p.setBrush(fill);
            p.drawRoundedRect(rect, radius, radius);
            // разделители между ячейками панели (между двумя кнопками листания их нет)
            QList<QPair<QRectF, bool>> cells; // прямоугольник, это ScreenSide
            for (const auto& other : zones) {
                const auto item = other.toObject();
                const QString kind = item.value("kind").toString();
                const QRectF cell = readRect(item.value("rect"));
                if ((kind == "ScreenSide" || kind == "ScreenKey") && rect.contains(cell.center()))
                    cells.append({cell, kind == "ScreenSide"});
            }
            std::sort(cells.begin(), cells.end(),
                      [](const auto& a, const auto& b) { return a.first.top() < b.first.top(); });
            for (int i = 1; i < cells.size(); ++i) {
                if (!cells[i - 1].second && !cells[i].second) continue;
                const qreal y = (cells[i - 1].first.bottom() + cells[i].first.top()) / 2.0;
                const QPointF a(rect.left() + 6, y), b(rect.right() - 6, y);
                QLinearGradient line(a, b);
                QColor clear = style.panelDivider;
                clear.setAlpha(0);
                line.setColorAt(0.0, clear);
                line.setColorAt(0.5, style.panelDivider);
                line.setColorAt(1.0, clear);
                p.setPen(QPen(QBrush(line), 1.0));
                p.drawLine(a, b);
            }
        }
    }
    const auto insidePanel = [&sidePanels](const QRectF& rect) {
        return std::any_of(sidePanels.begin(), sidePanels.end(),
                           [&rect](const QRectF& panel) { return panel.contains(rect.center()); });
    };

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

    int slot = 0;
    for (const auto& value : zones) {
        const auto zone = value.toObject();
        const auto kind = zone.value("kind").toString();
        const auto id = zone.value("id").toString();
        QRectF rect = readRect(zone.value("rect"));
        if (rect.isEmpty()) continue;
        const qreal radius = zone.value("radius").toDouble();
        const bool hover = hoverScreenHit == screenHits.size();

        if (kind == "ScreenSlot") {
            const int index = first + slot++;
            CellContent content;
            if (activityScreen) {
                if (index >= listItems().size()) continue; // пустые ячейки остаются фоном
                content.activityType = listItems()[index].type;
                content.label = listItems()[index].label;
                content.active = listItems()[index].selected;
            } else if (softScreen) {
                // Свободная ячейка на пульте выглядит пустой рамкой с «+».
                content.label = softLabels.value(index);
                content.active = selectedSoft == index;
                content.empty = content.label.isEmpty();
            } else {
                continue; // у этого режима нет содержимого на экране
            }
            if (!occupied.isEmpty() && visible < capacity && rect.y() == occupied.last().y()) {
                QRectF row = rect;
                for (const QRectF& other : occupied)
                    if (other.y() == rect.y()) row = row.united(other);
                rect.translate(refW / 2.0 - row.center().x(), 0);
            }
            content.colour = style.tileText;
            paintTile(p, rect, radius, content, content.active, hover, style, tileFace(layout));
            const QString button = zone.value("button").toString();
            if (!button.isEmpty()) {
                for (const auto& key : modelZones)
                    if (key.id == button) screenHits.append({key.rect, index});
            }
            if (layout.value("touch").toBool()) screenHits.append({screenToPhoto(rect), index});
        } else if (kind == "ScreenKey") {
            paintKey(p, rect, radius, keyContent(id, activeMode), hover, style,
                     keyRole(id, insidePanel(rect)));
        } else if (kind == "ScreenSide") {
            // Ячейка с назначаемой кнопкой (Menu, Guide…): её название.
            if (const QString key = zone.value("key").toString(); !key.isEmpty()) {
                drawPanelLabel(p, rect.adjusted(3, 0, -3, 0), keyName(key), style.panelText, 0.42);
                continue;
            }
            // Остальные боковые ячейки — физические кнопки: рисуем только подписи
            // системных Devices/Options, остальные оставляем конфигурации.
            if (id != "Devices" && id != "Options") continue;
            const bool devices = id == "Devices";
            drawSingleLine(p, rect, devices ? tr("Devices") : tr("Options"),
                           devices ? QColor("#9bd46f") : QColor("#f08a4b"), true, 1.0, 0.72);
        } else if (kind == "ScreenLabel") {
            QString text;
            const bool bold = id == "Title" || id == "Heading";
            if (id == "Title") {
                text = currentScreen == Screen::Devices ? tr("Devices") : softTitle;
            } else if (id == "Heading") {
                text = currentScreen == Screen::Devices ? tr("Devices") : activityScreen ? tr("What would you like to do?") : softTitle;
            } else if (id == "Page" || id == "PageLabel") {
                if (activityScreen || softScreen)
                    text = QStringLiteral("%1/%2").arg(currentPage() + 1).arg(pageCount());
            } else if (id == "Clock") {
                text = QTime::currentTime().toString(QStringLiteral("HH:mm"));
            } else if (id == "Caption") {
                text = currentScreen == Screen::Devices ? tr("Devices") : softTitle;
            }
            if (text.isEmpty()) continue;
            if (id == "Title" || id == "Heading") {
                drawStyledText(p, rect, text, Qt::white, bold, style.textShadow, 1.0, 0.80, 0.35);
            } else {
                if (style.textShadow) {
                    const qreal d = std::max<qreal>(0.6, rect.height() * 0.06);
                    drawSingleLine(p, rect.translated(d, d), text, QColor(0, 0, 0, 150),
                                   style.labelBold, 1.0, 0.62);
                }
                drawSingleLine(p, rect, text, style.labelText, style.labelBold, 1.0, 0.62);
            }
        }
    }
    p.restore();
}

bool RemoteView::paintComposedScreen(QPainter& p, const QJsonObject& layout) {
    // "compose": экран собирается из картинок screens.json модели
    if (!layout.value("compose").toBool()) return false;
    const SpriteSet* sprites = spriteSet(profileDirectory);
    if (sprites == nullptr) return false;
    const auto ref = layout.value("ref").toArray();
    const qreal refW = ref[0].toDouble();
    const qreal refH = ref[1].toDouble();
    const auto zones = layout.value("zones").toArray();
    const QRectF lcd = toWidget(screenRect());

    p.save();
    QPainterPath clip;
    clip.addRoundedRect(lcd, lcd.width() * 0.02, lcd.height() * 0.02);
    p.setClipPath(clip);
    p.translate(lcd.topLeft());
    p.scale(lcd.width() / refW, lcd.height() / refH);
    if (sprites != nullptr) {
        // Собранный экран: картинки из screens.json по слоям (фон — тоже картинка, слой 0).
        // Плитки, подписи и подсветка из конфигурации рисуются поверх всех слоёв.
        for (const SpriteItem& item : sprites->screens.value(activeMode)) {
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
    // цвет подписи на плитке ("tile": {"text": ...}): у светлых плиток 1000 тёмный, без тени
    const QColor tileText(layout.value("tile").toObject().value("text").toString());

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
            if (const TileFace* face = tileFace(layout)) {
                paintTileFace(p, rect, *face); // уменьшенная плитка активностей (избранное)
            } else if (const auto tile = layout.value("tile").toObject(); tile.contains("color")) {
                // ровная заливка, если картинки плитки нет
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
                const ScreenStyle& style = screenStyle(layout.value("style").toString());
                drawSingleLine(p, rect, tr("Page"), style.panelText, true, 1.0, 0.80);
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
            const ScreenStyle& style = screenStyle(layout.value("style").toString());
            drawPanelLabel(p, rect.adjusted(6, 0, -6, 0), keyName(key), style.panelText, 0.34);
        }
        // ScreenKey, ScreenPanel: всё нужное уже есть на картинках экрана
    }
    p.restore();
    return true;
}

void RemoteView::paintCompactScreen(QPainter& p, const QJsonObject& layout) {
    // Экран 600/650/665/700: белая шапка, четыре команды у боковых кнопок,
    // крест-разделитель и белая вкладка Devices. Цвет фона — screenTint профиля.
    const auto ref = layout.value("ref").toArray();
    const qreal refW = ref[0].toDouble();
    const qreal refH = ref[1].toDouble();
    const auto zones = layout.value("zones").toArray();
    const QRectF lcd = toWidget(screenRect());
    const bool softScreen = currentScreen == Screen::SoftButtons;
    // Список активностей на 600-серии выглядит так же: по одной у каждой боковой кнопки.
    const bool activityScreen = isListScreen();
    const QColor tint = screenTint.isValid() ? screenTint : QColor("#7d7f82");

    QList<QRectF> slotRects;
    QRectF titleBand;
    for (const auto& value : zones) {
        const auto zone = value.toObject();
        const QRectF rect = readRect(zone.value("rect"));
        if (zone.value("kind") == "ScreenSlot") slotRects.append(rect);
        else if (zone.value("kind") == "ScreenLabel") titleBand = titleBand.united(rect);
    }

    p.save();
    QPainterPath clip;
    clip.addRoundedRect(lcd, lcd.width() * 0.02, lcd.height() * 0.02);
    p.setClipPath(clip);
    p.translate(lcd.topLeft());
    p.scale(lcd.width() / refW, lcd.height() / refH);

    // светлое пятно слева сверху, к краям темнее — как на фото пультов
    QRadialGradient background(QPointF(refW * 0.38, refH * 0.42), refW * 0.85);
    background.setColorAt(0.0, tint.lighter(135));
    background.setColorAt(0.55, tint);
    background.setColorAt(1.0, tint.darker(150));
    p.fillRect(QRectF(0, 0, refW, refH), background);
    if (!titleBand.isEmpty()) p.fillRect(QRectF(0, 0, refW, titleBand.bottom()), Qt::white);

    if (slotRects.size() == 4) {
        // тонкий крест между ячейками, гаснущий к концам
        const auto fade = [](const QPointF& a, const QPointF& b) {
            QLinearGradient line(a, b);
            line.setColorAt(0.0, QColor(255, 255, 255, 0));
            line.setColorAt(0.5, QColor(255, 255, 255, 170));
            line.setColorAt(1.0, QColor(255, 255, 255, 0));
            return QPen(QBrush(line), 1.0);
        };
        const qreal x = (slotRects[0].right() + slotRects[1].left()) / 2.0;
        const qreal y = (slotRects[0].bottom() + slotRects[2].top()) / 2.0;
        const QPointF top(x, slotRects[0].top() + 4), bottom(x, slotRects[2].bottom() - 4);
        const QPointF left(slotRects[0].left() + 4, y), right(slotRects[1].right() - 4, y);
        p.setPen(fade(top, bottom));
        p.drawLine(top, bottom);
        p.setPen(fade(left, right));
        p.drawLine(left, right);
    }

    const int capacity = std::max(1, slotsPerPage(activeMode));
    const int first = currentPage() * capacity;
    int slot = 0;
    for (const auto& value : zones) {
        const auto zone = value.toObject();
        const auto kind = zone.value("kind").toString();
        const auto id = zone.value("id").toString();
        const QRectF rect = readRect(zone.value("rect"));
        if (rect.isEmpty()) continue;
        const qreal radius = zone.value("radius").toDouble();
        const bool hover = hoverScreenHit == screenHits.size();

        if (kind == "ScreenSlot") {
            const int index = first + slot++;
            if (!softScreen && !activityScreen) continue;
            // на экране активностей свободных мест нет: лишние ячейки пустые и не кликаются
            if (activityScreen && index >= listItems().size()) continue;
            const QString text = softScreen ? softLabels.value(index) : listItems()[index].label;
            const bool selected = softScreen ? selectedSoft == index : listItems()[index].selected;
            const QString button = zone.value("button").toString();
            // подпись прижата к своей физической кнопке, как на пульте
            const bool leftSide = button.startsWith("SoftLeft") ||
                                  (!button.startsWith("SoftRight") && rect.center().x() < refW / 2.0);
            if (selected || hover) {
                p.setPen(selected ? QPen(QColor(255, 255, 255, 220), 1.2) : QPen(Qt::NoPen));
                p.setBrush(QColor(255, 255, 255, selected ? 60 : 35));
                p.drawRoundedRect(rect.adjusted(2, 2, -2, -2), radius, radius);
            }
            if (text.isEmpty()) {
                // свободное место: едва заметный «+»
                drawSingleLine(p, QRectF(rect.x(), rect.center().y() - 10, rect.width(), 20),
                               QStringLiteral("+"), Qt::white, false, hover ? 0.8 : 0.45, 0.9);
            } else {
                int align = Qt::AlignVCenter;
                align |= leftSide ? Qt::AlignLeft : Qt::AlignRight;
                const QRectF box = rect.adjusted(5, 4, -5, -4);
                drawFittedText(p, box.translated(0.8, 0.8), text, QColor(0, 0, 0, 110), true, 1.0,
                               0.22, 0.12, align);
                drawFittedText(p, box, text, Qt::white, true, 1.0, 0.22, 0.12, align);
            }
            if (!button.isEmpty()) {
                for (const auto& key : modelZones)
                    if (key.id == button) screenHits.append({key.rect, index});
            }
            if (layout.value("touch").toBool()) screenHits.append({screenToPhoto(rect), index});
        } else if (kind == "ScreenLabel") {
            QString text;
            int align = Qt::AlignVCenter;
            QRectF box = rect;
            if (id == "Title") {
                text = currentScreen == Screen::Devices ? tr("Devices") : softScreen ? softTitle : activityScreen ? tr("Activities") : QString();
                align |= Qt::AlignLeft;
                box.adjust(4, 0, 0, 0);
            } else if (id == "Page") {
                if (softScreen || activityScreen) text =QStringLiteral("%1/%2").arg(currentPage() + 1).arg(pageCount());
                align |= Qt::AlignRight;
                box.adjust(0, 0, -4, 0);
            }
            drawSingleLine(p, box, text, QColor("#111111"), true, 1.0, 0.68, align);
        } else if (kind == "ScreenSide" && id == "Devices") {
            // белая вкладка, уходящая за нижний край экрана
            QPainterPath tab;
            tab.addRoundedRect(rect.adjusted(0, 0, 0, rect.height()), 6, 6);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawPath(tab);
            drawSingleLine(p, rect, tr("Devices"), QColor("#111111"), true, 1.0, 0.72);
        }
    }
    p.restore();
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
