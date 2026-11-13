#include "zoneCanvas.h"
#include "../theme.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace harmony::app {
namespace {

constexpr qreal HANDLE = 7;    ///< сторона маркера, px окна
constexpr qreal EDGE = 4;      ///< захват края без маркера, px окна
constexpr qreal SNAP_PX = 6;   ///< порог прилипания, px окна
constexpr qreal MARGIN = 24;   ///< поля при вписывании слоя
constexpr qreal MIN_ZOOM = 0.1;
constexpr qreal MAX_ZOOM = 16.0;

/// Цвет зоны по виду: назначаемые — синие, экранные/системные — свои.
QColor kindColour(const QString& kind) {
    if (kind == "Key" || kind == "ScreenSlot" || kind == "ScreenKey") return theme::color(theme::Color::Accent);
    return theme::color(theme::Color::Text);
}

/// Цвет рамок и направляющих следует текущей теме.
QColor accentColour() { return theme::color(theme::Color::Accent); }
const QColor ERROR_COLOUR(0xc4, 0x42, 0x42);
const QColor WARNING_COLOUR(0xb0, 0x78, 0x20);

QString number(qreal value) { return QString::number(zoneEditing::round2(value), 'g', 8); }

} // namespace

ZoneCanvas::ZoneCanvas(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 320);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

QRectF ZoneCanvas::rectIn(const QJsonArray& zones, const std::optional<QRectF>& screen, int item) {
    if (item == SCREEN_ITEM) return screen.value_or(QRectF());
    if (item < 0 || item >= zones.size()) return {};
    return zoneEditing::rectOf(zones[item].toObject());
}

void ZoneCanvas::setRectIn(QJsonArray& zones, std::optional<QRectF>& screen, int item, const QRectF& rect) {
    using zoneEditing::round2;
    if (item == SCREEN_ITEM) {
        screen = QRectF(round2(rect.x()), round2(rect.y()), round2(rect.width()), round2(rect.height()));
        return;
    }
    if (item < 0 || item >= zones.size()) return;
    auto zone = zones[item].toObject();
    zoneEditing::setRect(zone, rect);
    zones[item] = zone;
}

// ── Данные ────────────────────────────────────────────────────────────────────

void ZoneCanvas::setBackdrop(const QPixmap& pixmap, const QSizeF& photoSize) {
    backdrop_ = pixmap;
    if (!photoSize.isEmpty()) photoSize_ = photoSize;
    update();
}

void ZoneCanvas::setLayer(const QRectF& frame, const QSizeF& ref, bool screenLayer, bool keepView) {
    frame_ = frame.isValid() ? frame : QRectF(QPointF(0, 0), photoSize_);
    ref_ = ref.isValid() ? ref : frame_.size();
    screenLayer_ = screenLayer;
    cancelDrag();
    if (!keepView) fitView();
    update();
}

void ZoneCanvas::setZones(const QJsonArray& zones, const std::optional<QRectF>& screen) {
    zones_ = zones;
    screen_ = screen;
    const auto before = selection_.size();
    selection_.removeIf([this](int item) { return itemRect(item).isNull(); });
    if (hover_ != NO_ITEM && itemRect(hover_).isNull()) hover_ = NO_ITEM;
    if (selection_.size() != before) emit selectionChanged();
    update();
}

void ZoneCanvas::setContext(const QList<Context>& items) {
    context_ = items;
    update();
}

void ZoneCanvas::setMarks(const QHash<int, zoneEditing::Issue::Level>& marks) {
    marks_ = marks;
    update();
}

void ZoneCanvas::setSelection(const QList<int>& items) {
    QList<int> valid;
    for (int item : items)
        if (!itemRect(item).isNull() && !valid.contains(item)) valid.append(item);
    if (valid == selection_) return;
    selection_ = valid;
    emit selectionChanged();
    update();
}

void ZoneCanvas::setDrawMode(bool on) {
    if (drawMode_ == on) return;
    drawMode_ = on;
    if (!on && drag_ == Drag::Draw) cancelDrag();
    updateCursor(mapFromGlobal(QCursor::pos()));
    emit drawModeChanged(on);
}

void ZoneCanvas::setShowIds(bool on) {
    showIds_ = on;
    update();
}

QList<int> ZoneCanvas::allItems() const {
    QList<int> items;
    for (int i = 0; i < zones_.size(); ++i) items.append(i);
    if (screen_) items.append(SCREEN_ITEM);
    return items;
}

bool ZoneCanvas::mayLeave(int item) const {
    const QRectF bounds(QPointF(0, 0), ref_);
    if (!bounds.contains(itemRect(item))) return true;
    if (item < 0) return false;
    const auto zone = zones_[item].toObject();
    return zone.value("kind") == "ScreenPanel" && zone.value("role") == "band";
}

// ── Координаты ────────────────────────────────────────────────────────────────

QPointF ZoneCanvas::layerToPhoto(const QPointF& point) const {
    return {frame_.x() + point.x() * frame_.width() / ref_.width(),
            frame_.y() + point.y() * frame_.height() / ref_.height()};
}

QPointF ZoneCanvas::toWidget(const QPointF& point) const { return offset_ + layerToPhoto(point) * zoom_; }

QRectF ZoneCanvas::toWidget(const QRectF& rect) const {
    return {toWidget(rect.topLeft()), toWidget(rect.bottomRight())};
}

QRectF ZoneCanvas::photoToWidget(const QRectF& rect) const {
    return {offset_ + rect.topLeft() * zoom_, rect.size() * zoom_};
}

QPointF ZoneCanvas::toLayer(const QPointF& widget) const {
    const QPointF photo = (widget - offset_) / zoom_;
    return {(photo.x() - frame_.x()) * ref_.width() / frame_.width(),
            (photo.y() - frame_.y()) * ref_.height() / frame_.height()};
}

qreal ZoneCanvas::unitX() const { return zoom_ * frame_.width() / ref_.width(); }
qreal ZoneCanvas::unitY() const { return zoom_ * frame_.height() / ref_.height(); }

qreal ZoneCanvas::radiusOf(int item, const QRectF& widgetRect) const {
    if (item < 0) return 0;
    // Так же, как RemoteView: положительный — в единицах слоя, отрицательный — доля короткой стороны.
    const qreal radius = zones_[item].toObject().value("radius").toDouble();
    if (radius < 0) return std::min(widgetRect.width(), widgetRect.height()) * -radius;
    return std::min(radius * unitX(), std::min(widgetRect.width(), widgetRect.height()) / 2);
}

QRectF ZoneCanvas::visibleLayerRect() const {
    const QRectF visible(toLayer(QPointF(0, 0)), toLayer(QPointF(width(), height())));
    return visible.intersected(QRectF(QPointF(0, 0), ref_));
}

void ZoneCanvas::fitView() {
    const qreal w = std::max<qreal>(1, width() - 2 * MARGIN);
    const qreal h = std::max<qreal>(1, height() - 2 * MARGIN);
    zoom_ = std::clamp(std::min(w / frame_.width(), h / frame_.height()), MIN_ZOOM, MAX_ZOOM);
    offset_ = QPointF(width() / 2.0, height() / 2.0) - frame_.center() * zoom_;
    fitted_ = true;
    update();
}

void ZoneCanvas::zoomTo(qreal zoom) {
    const QPointF centre(width() / 2.0, height() / 2.0);
    const QPointF photo = (centre - offset_) / zoom_;
    zoom_ = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
    offset_ = centre - photo * zoom_;
    fitted_ = false;
    clampView();
    update();
}

void ZoneCanvas::reveal(int item) {
    const QRectF r = toWidget(itemRect(item));
    if (r.isEmpty()) return;
    const QRectF area = QRectF(rect()).adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN);
    if (area.contains(r)) return;
    offset_ += area.center() - r.center();
    fitted_ = false;
    update();
}

void ZoneCanvas::clampView() {
    // Хотя бы край фото остаётся в окне, чтобы его нельзя было «потерять» прокруткой.
    constexpr qreal visible = 60;
    const QRectF photo(offset_, photoSize_ * zoom_);
    if (photo.right() < visible) offset_.rx() += visible - photo.right();
    if (photo.left() > width() - visible) offset_.rx() -= photo.left() - (width() - visible);
    if (photo.bottom() < visible) offset_.ry() += visible - photo.bottom();
    if (photo.top() > height() - visible) offset_.ry() -= photo.top() - (height() - visible);
}

// ── Поиск под курсором ────────────────────────────────────────────────────────

int ZoneCanvas::itemAt(const QPointF& pos) const {
    // Из вложенных зон (кнопка на панели экрана) выбирается меньшая.
    int best = NO_ITEM;
    qreal bestArea = 0;
    for (int item : allItems()) {
        const QRectF r = toWidget(itemRect(item)).adjusted(-2, -2, 2, 2);
        if (!r.contains(pos)) continue;
        const qreal area = r.width() * r.height();
        if (best == NO_ITEM || area < bestArea) {
            best = item;
            bestArea = area;
        }
    }
    return best;
}

QList<QRectF> ZoneCanvas::handleRects(const QRectF& r) const {
    const auto at = [](qreal x, qreal y) { return QRectF(x - HANDLE / 2, y - HANDLE / 2, HANDLE, HANDLE); };
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    return {at(r.left(), r.top()),     at(cx, r.top()),        at(r.right(), r.top()), at(r.right(), cy),
            at(r.right(), r.bottom()), at(cx, r.bottom()),     at(r.left(), r.bottom()), at(r.left(), cy)};
}

int ZoneCanvas::edgesAt(const QPointF& pos) const {
    if (selection_.size() != 1 || drawMode_) return 0;
    const QRectF r = toWidget(itemRect(selection_.first()));
    if (r.isEmpty()) return 0;
    static constexpr int handleEdges[] = {Left | Top, Top, Right | Top, Right, Right | Bottom, Bottom, Left | Bottom, Left};
    const auto handles = handleRects(r);
    for (int i = 0; i < handles.size(); ++i)
        if (handles[i].adjusted(-2, -2, 2, 2).contains(pos)) return handleEdges[i];
    if (!r.adjusted(-EDGE, -EDGE, EDGE, EDGE).contains(pos)) return 0;
    int edges = 0;
    if (std::abs(pos.x() - r.left()) <= EDGE) edges |= Left;
    else if (std::abs(pos.x() - r.right()) <= EDGE) edges |= Right;
    if (std::abs(pos.y() - r.top()) <= EDGE) edges |= Top;
    else if (std::abs(pos.y() - r.bottom()) <= EDGE) edges |= Bottom;
    return edges;
}

void ZoneCanvas::setItemRect(int item, const QRectF& rect) { setRectIn(zones_, screen_, item, rect); }

QRectF ZoneCanvas::keepInside(const QRectF& rect, bool move) const {
    const QRectF bounds(QPointF(0, 0), ref_);
    QRectF r = rect;
    if (move) {
        if (r.right() > bounds.right()) r.moveRight(bounds.right());
        if (r.bottom() > bounds.bottom()) r.moveBottom(bounds.bottom());
        if (r.left() < 0) r.moveLeft(0);
        if (r.top() < 0) r.moveTop(0);
        return r;
    }
    r.setLeft(std::max<qreal>(r.left(), 0));
    r.setTop(std::max<qreal>(r.top(), 0));
    r.setRight(std::min(r.right(), bounds.right()));
    r.setBottom(std::min(r.bottom(), bounds.bottom()));
    return r;
}

// ── Прилипание ────────────────────────────────────────────────────────────────

std::optional<qreal> ZoneCanvas::snap(const QList<qreal>& moving, const QList<qreal>& targets, qreal threshold,
                                      qreal& line) {
    std::optional<qreal> best;
    for (qreal m : moving) {
        for (qreal t : targets) {
            const qreal delta = t - m;
            if (std::abs(delta) <= threshold && (!best || std::abs(delta) < std::abs(*best))) {
                best = delta;
                line = t;
            }
        }
    }
    return best;
}

void ZoneCanvas::collectTargets(QList<qreal>& xs, QList<qreal>& ys) const {
    xs = {0, ref_.width() / 2, ref_.width()};
    ys = {0, ref_.height() / 2, ref_.height()};
    for (int item : allItems()) {
        if (startRects_.contains(item)) continue;
        const QRectF r = itemRect(item);
        xs << r.left() << r.center().x() << r.right();
        ys << r.top() << r.center().y() << r.bottom();
    }
}

// ── Жесты ─────────────────────────────────────────────────────────────────────

void ZoneCanvas::dragMove(const QPointF& pos, Qt::KeyboardModifiers modifiers) {
    QPointF d((pos.x() - pressPos_.x()) / unitX(), (pos.y() - pressPos_.y()) / unitY());
    if (modifiers & Qt::ShiftModifier) { // только по одной оси
        if (std::abs(d.x()) > std::abs(d.y())) d.setY(0);
        else d.setX(0);
    }
    // Сдвиг целыми единицами: дробные координаты раскладок (…33, …67) сохраняются.
    d = QPointF(std::round(d.x()), std::round(d.y()));
    QRectF box;
    bool free = false;
    for (auto it = startRects_.cbegin(); it != startRects_.cend(); ++it) {
        box = box.isNull() ? it.value() : box.united(it.value());
        free = free || mayLeave(it.key());
    }
    guideX_.reset();
    guideY_.reset();
    if (snapping_ && !(modifiers & Qt::AltModifier)) {
        QList<qreal> xs;
        QList<qreal> ys;
        collectTargets(xs, ys);
        const QRectF moved = box.translated(d);
        qreal line = 0;
        if (!(modifiers & Qt::ShiftModifier) || d.x() != 0) {
            if (const auto s = snap({moved.left(), moved.center().x(), moved.right()}, xs, SNAP_PX / unitX(), line)) {
                d.rx() += *s;
                guideX_ = line;
            }
        }
        if (!(modifiers & Qt::ShiftModifier) || d.y() != 0) {
            if (const auto s = snap({moved.top(), moved.center().y(), moved.bottom()}, ys, SNAP_PX / unitY(), line)) {
                d.ry() += *s;
                guideY_ = line;
            }
        }
    }
    if (!free) d += keepInside(box.translated(d), true).topLeft() - box.translated(d).topLeft();
    for (auto it = startRects_.cbegin(); it != startRects_.cend(); ++it) setItemRect(it.key(), it.value().translated(d));
}

void ZoneCanvas::dragResize(const QPointF& pos, Qt::KeyboardModifiers modifiers) {
    const QRectF start = startRects_.value(pressItem_);
    const QPointF delta = toLayer(pos) - toLayer(pressPos_);
    const qreal dx = std::round(delta.x());
    const qreal dy = std::round(delta.y());
    qreal l = start.left();
    qreal t = start.top();
    qreal r = start.right();
    qreal b = start.bottom();
    if (resizeEdges_ & Left) l += dx;
    if (resizeEdges_ & Right) r += dx;
    if (resizeEdges_ & Top) t += dy;
    if (resizeEdges_ & Bottom) b += dy;
    guideX_.reset();
    guideY_.reset();
    if (snapping_ && !(modifiers & Qt::AltModifier)) {
        QList<qreal> xs;
        QList<qreal> ys;
        collectTargets(xs, ys);
        qreal line = 0;
        qreal& x = (resizeEdges_ & Left) ? l : r;
        qreal& y = (resizeEdges_ & Top) ? t : b;
        if ((resizeEdges_ & (Left | Right)) != 0) {
            if (const auto s = snap({x}, xs, SNAP_PX / unitX(), line)) {
                x += *s;
                guideX_ = line;
            }
        }
        if ((resizeEdges_ & (Top | Bottom)) != 0) {
            if (const auto s = snap({y}, ys, SNAP_PX / unitY(), line)) {
                y += *s;
                guideY_ = line;
            }
        }
    }
    constexpr qreal minimum = 1;
    if (resizeEdges_ & Left) l = std::min(l, r - minimum);
    if (resizeEdges_ & Right) r = std::max(r, l + minimum);
    if (resizeEdges_ & Top) t = std::min(t, b - minimum);
    if (resizeEdges_ & Bottom) b = std::max(b, t + minimum);
    QRectF rect(QPointF(l, t), QPointF(r, b));
    if (!mayLeave(pressItem_)) rect = keepInside(rect, false);
    setItemRect(pressItem_, rect);
}

void ZoneCanvas::dragBand(const QPointF& pos) {
    band_ = QRectF(pressPos_, pos).normalized();
    if (drag_ != Drag::Band) return;
    QList<int> selected = bandBase_;
    for (int item : allItems())
        if (!selected.contains(item) && band_.contains(toWidget(itemRect(item)))) selected.append(item);
    if (selected != selection_) {
        selection_ = selected;
        emit selectionChanged();
    }
}

void ZoneCanvas::cancelDrag() {
    if (drag_ == Drag::Move || drag_ == Drag::Resize)
        for (auto it = startRects_.cbegin(); it != startRects_.cend(); ++it) setItemRect(it.key(), it.value());
    drag_ = Drag::None;
    clickPan_ = false;
    panMoved_ = false;
    startRects_.clear();
    band_ = {};
    guideX_.reset();
    guideY_.reset();
    update();
}

void ZoneCanvas::nudge(int dx, int dy, bool resize) {
    if (selection_.isEmpty()) return;
    QStringList ids;
    for (int item : selection_) {
        QRectF r = itemRect(item);
        if (resize) {
            r.setWidth(std::max<qreal>(1, r.width() + dx));
            r.setHeight(std::max<qreal>(1, r.height() + dy));
        } else {
            r.translate(dx, dy);
        }
        if (!mayLeave(item)) r = keepInside(r, !resize);
        setItemRect(item, r);
        ids.append(QString::number(item));
    }
    // Серия нажатий над тем же выделением — один шаг отмены.
    emit geometryEdited(resize ? tr("Resize zones") : tr("Move zones"),
                        (resize ? QStringLiteral("size:") : QStringLiteral("move:")) + ids.join(','));
    update();
}

// ── События ───────────────────────────────────────────────────────────────────

void ZoneCanvas::mousePressEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    pressPos_ = lastPos_ = pos;
    if (event->button() != Qt::LeftButton) {
        event->accept(); // правая и средняя кнопки холст больше не двигают
        return;
    }
    if (drawMode_) {
        drag_ = Drag::Draw;
        band_ = QRectF(pos, pos);
        return;
    }
    if (const int edges = edgesAt(pos); edges != 0) {
        drag_ = Drag::Resize;
        resizeEdges_ = edges;
        pressItem_ = selection_.first();
        startRects_ = {{pressItem_, itemRect(pressItem_)}};
        return;
    }
    const auto modifiers = event->modifiers();
    const int item = itemAt(pos);
    pressItem_ = item;
    if (item == NO_ITEM) {
        if (modifiers & (Qt::ShiftModifier | Qt::ControlModifier)) {
            // Shift/Ctrl + левая кнопка по пустому месту — выделение рамки.
            drag_ = Drag::Band;
            band_ = QRectF(pos, pos);
            bandBase_ = selection_;
            update();
            return;
        }
        // Левая кнопка по пустому месту: перетаскивание двигает холст,
        // щелчок без движения снимает выделение.
        drag_ = Drag::Pan;
        clickPan_ = true;
        panMoved_ = false;
        setCursor(Qt::ClosedHandCursor);
        if (!selection_.isEmpty()) {
            selection_.clear();
            emit selectionChanged();
        }
        update();
        return;
    }
    if (modifiers & Qt::ControlModifier) {
        if (isSelected(item)) selection_.removeAll(item);
        else selection_.append(item);
    } else if (modifiers & Qt::ShiftModifier) {
        if (!isSelected(item)) selection_.append(item);
    } else if (!isSelected(item)) {
        selection_ = {item};
    } else {
        // Щелчок по выделенной зоне делает её опорной для выравнивания.
        selection_.removeAll(item);
        selection_.prepend(item);
    }
    emit selectionChanged();
    if (isSelected(item)) {
        drag_ = Drag::Move;
        startRects_.clear();
        for (int selected : selection_) startRects_.insert(selected, itemRect(selected));
    }
    update();
}

void ZoneCanvas::mouseMoveEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    switch (drag_) {
    case Drag::Pan:
        if (clickPan_ && !panMoved_) {
            // Порог, чтобы обычный щелчок левой кнопкой не сдвигал холст.
            if ((pos - pressPos_).manhattanLength() < QApplication::startDragDistance()) break;
            panMoved_ = true;
            offset_ += pos - pressPos_;
        } else {
            offset_ += pos - lastPos_;
        }
        fitted_ = false;
        clampView();
        break;
    case Drag::Move:
        dragMove(pos, event->modifiers());
        break;
    case Drag::Resize:
        dragResize(pos, event->modifiers());
        break;
    case Drag::Band:
    case Drag::Draw:
        dragBand(pos);
        break;
    case Drag::None: {
        const int item = itemAt(pos);
        if (item != hover_) {
            hover_ = item;
            update();
        }
        updateCursor(pos);
        break;
    }
    }
    lastPos_ = pos;
    emitStatus(pos);
    if (drag_ != Drag::None) update();
}

void ZoneCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (drag_ == Drag::None) return;
    const Drag drag = std::exchange(drag_, Drag::None);
    if (drag == Drag::Pan) {
        // Выделение уже снято в mousePressEvent: щелчок по пустому месту.
        clickPan_ = false;
        panMoved_ = false;
    }
    guideX_.reset();
    guideY_.reset();
    if (drag == Drag::Move || drag == Drag::Resize) {
        bool changed = false;
        for (auto it = startRects_.cbegin(); it != startRects_.cend(); ++it)
            changed = changed || itemRect(it.key()) != it.value();
        if (changed) {
            emit geometryEdited(drag == Drag::Move ? tr("Move zones") : tr("Resize zone"), {});
        } else if (drag == Drag::Move && selection_.size() > 1 &&
                   !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) && pressItem_ != NO_ITEM) {
            selection_ = {pressItem_}; // щелчок без перетаскивания оставляет одну зону
            emit selectionChanged();
        }
    } else if (drag == Drag::Draw) {
        const QRectF r = QRectF(toLayer(band_.topLeft()), toLayer(band_.bottomRight()))
                             .intersected(QRectF(QPointF(0, 0), ref_));
        const QRectF rounded(QPointF(std::round(r.left()), std::round(r.top())),
                             QPointF(std::round(r.right()), std::round(r.bottom())));
        setDrawMode(false);
        if (rounded.width() >= 2 && rounded.height() >= 2) emit zoneDrawn(rounded);
    }
    startRects_.clear();
    band_ = {};
    updateCursor(event->position());
    update();
}

void ZoneCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    if (const int item = itemAt(event->position()); item != NO_ITEM) emit itemActivated(item);
}

void ZoneCanvas::wheelEvent(QWheelEvent* event) {
    const QPoint angle = event->angleDelta();
    if (event->modifiers() & Qt::ControlModifier) {
        const QPointF anchor = event->position();
        const QPointF photo = (anchor - offset_) / zoom_;
        zoom_ = std::clamp(zoom_ * std::pow(1.0015, angle.y()), MIN_ZOOM, MAX_ZOOM);
        offset_ = anchor - photo * zoom_;
    } else if (event->modifiers() & Qt::ShiftModifier) {
        offset_.rx() += (angle.x() != 0 ? angle.x() : angle.y()) / 2.0;
    } else {
        offset_ += QPointF(angle.x(), angle.y()) / 2.0;
    }
    fitted_ = false;
    clampView();
    emitStatus(event->position());
    update();
    event->accept();
}

void ZoneCanvas::keyPressEvent(QKeyEvent* event) {
    const auto modifiers = event->modifiers();
    const int step = (modifiers & Qt::ShiftModifier) ? 10 : 1;
    const bool resize = modifiers & Qt::ControlModifier;
    switch (event->key()) {
    case Qt::Key_Left: nudge(-step, 0, resize); return;
    case Qt::Key_Right: nudge(step, 0, resize); return;
    case Qt::Key_Up: nudge(0, -step, resize); return;
    case Qt::Key_Down: nudge(0, step, resize); return;
    case Qt::Key_Escape:
        if (drag_ != Drag::None) {
            cancelDrag();
            return;
        }
        if (drawMode_) {
            setDrawMode(false);
            return;
        }
        if (!selection_.isEmpty()) {
            setSelection({});
            return;
        }
        break;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

bool ZoneCanvas::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(event);
        const int item = drag_ == Drag::None ? itemAt(help->pos()) : NO_ITEM;
        if (item == NO_ITEM) {
            QToolTip::hideText();
            event->ignore();
            return true;
        }
        const QRectF r = itemRect(item);
        const QString name = item == SCREEN_ITEM ? tr("Screen area")
                                                 : zones_[item].toObject().value("id").toString() + " · " +
                                                       zones_[item].toObject().value("kind").toString();
        QToolTip::showText(help->globalPos(),
                           QStringLiteral("%1\n%2, %3  %4 × %5")
                               .arg(name, number(r.x()), number(r.y()), number(r.width()), number(r.height())),
                           this);
        return true;
    }
    if (event->type() == QEvent::Leave && hover_ != NO_ITEM) {
        hover_ = NO_ITEM;
        update();
    }
    return QWidget::event(event);
}

void ZoneCanvas::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fitted_) fitView();
    else clampView();
}

void ZoneCanvas::updateCursor(const QPointF& pos) {
    if (drawMode_) {
        setCursor(Qt::CrossCursor);
        return;
    }
    switch (edgesAt(pos)) {
    case Left | Top:
    case Right | Bottom: setCursor(Qt::SizeFDiagCursor); return;
    case Right | Top:
    case Left | Bottom: setCursor(Qt::SizeBDiagCursor); return;
    case Left:
    case Right: setCursor(Qt::SizeHorCursor); return;
    case Top:
    case Bottom: setCursor(Qt::SizeVerCursor); return;
    default: break;
    }
    const int item = itemAt(pos);
    if (item == NO_ITEM) setCursor(Qt::OpenHandCursor); // левая кнопка по пустому месту двигает холст
    else if (isSelected(item)) setCursor(Qt::SizeAllCursor);
    else unsetCursor();
}

void ZoneCanvas::emitStatus(const QPointF& pos) {
    const QPointF layer = toLayer(pos);
    QString text = tr("x %1, y %2").arg(number(layer.x()), number(layer.y()));
    if ((drag_ == Drag::Move || drag_ == Drag::Resize) && pressItem_ != NO_ITEM) {
        const QRectF r = itemRect(pressItem_);
        text += QStringLiteral("  ·  [%1, %2, %3, %4]")
                    .arg(number(r.x()), number(r.y()), number(r.width()), number(r.height()));
    } else if (drag_ == Drag::Draw) {
        const QRectF r(toLayer(band_.topLeft()), toLayer(band_.bottomRight()));
        text += QStringLiteral("  ·  %1 × %2").arg(number(std::round(r.width())), number(std::round(r.height())));
    }
    text += QStringLiteral("  ·  %1%").arg(qRound(zoom_ * 100));
    emit statusChanged(text);
}

// ── Отрисовка ─────────────────────────────────────────────────────────────────

void ZoneCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme::color(theme::Color::Window));
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF photo(offset_, photoSize_ * zoom_);
    if (!backdrop_.isNull()) p.drawPixmap(photo, backdrop_, QRectF(QPointF(0, 0), backdrop_.size()));

    const QRectF layer = toWidget(QRectF(QPointF(0, 0), ref_));
    if (screenLayer_) {
        QPainterPath outside;
        outside.addRect(rect());
        QPainterPath inside;
        inside.addRect(layer);
        p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 120));
        p.setPen(QPen(theme::color(theme::Color::Border), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(layer);
    }

    for (const auto& item : context_) {
        p.setBrush(Qt::NoBrush);
        p.setPen(item.highlight ? QPen(accentColour(), 2) : QPen(theme::color(theme::Color::Border), 1, Qt::DotLine));
        p.drawRoundedRect(photoToWidget(item.rect), 3, 3);
    }

    QFont label = font();
    label.setPixelSize(11);
    const QFontMetricsF metrics(label);
    p.setFont(label);
    const auto drawItem = [&](int item) {
        const QRectF r = toWidget(itemRect(item));
        if (r.isEmpty()) return;
        const bool selected = isSelected(item);
        const bool hovered = item == hover_;
        QString kind;
        QString id;
        if (item >= 0) {
            const auto zone = zones_[item].toObject();
            kind = zone.value("kind").toString();
            id = zone.value("id").toString();
        }
        const bool passive = kind == "ScreenPanel" || kind == "ScreenLabel";
        QColor colour = item == SCREEN_ITEM ? accentColour() : kindColour(kind);
        QColor fill = colour;
        fill.setAlpha(item == SCREEN_ITEM ? (selected ? 40 : 0) : passive ? (selected ? 50 : 10) : (selected ? 80 : hovered ? 60 : 32));
        QPen pen(colour, selected ? 2.0 : hovered ? 1.6 : 1.2,
                 passive || item == SCREEN_ITEM ? Qt::DashLine : Qt::SolidLine);
        if (const auto mark = marks_.constFind(item); mark != marks_.cend()) {
            pen.setColor(mark.value() == zoneEditing::Issue::Level::Error ? ERROR_COLOUR : WARNING_COLOUR);
            pen.setWidthF(std::max<qreal>(pen.widthF(), 2.0));
        }
        p.setPen(pen);
        p.setBrush(fill);
        const qreal radius = radiusOf(item, r);
        p.drawRoundedRect(r, radius, radius);
        if (selected && selection_.size() > 1 && selection_.first() == item) {
            // Опорная зона выравнивания.
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(Qt::white, 1, Qt::DashLine));
            p.drawRoundedRect(r.adjusted(-3, -3, 3, 3), radius + 3, radius + 3);
        }
        const QString text = item == SCREEN_ITEM ? tr("Screen area") : id;
        if (showIds_ && r.width() > 16 && r.height() > 9 && !text.isEmpty()) {
            const QString shown = metrics.elidedText(text, Qt::ElideRight, r.width() - 4);
            if (shown.size() > 1) {
                const QRectF box = item == SCREEN_ITEM ? r.adjusted(4, 2, 0, 0) : r;
                const auto align = item == SCREEN_ITEM ? Qt::AlignLeft | Qt::AlignTop : Qt::AlignCenter;
                const QRectF labelBox = metrics.boundingRect(box, align, shown).adjusted(-3, -1, 3, 1);
                p.fillRect(labelBox, theme::color(theme::Color::Surface));
                p.setPen(theme::color(theme::Color::Text));
                p.drawText(box, align, shown);
            }
        }
    };
    // Подложки и подписи экрана — снизу, затем остальные; выделенные — поверх всех.
    QList<int> order = allItems();
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        const auto rank = [this](int item) {
            if (isSelected(item)) return 2;
            if (item < 0) return 0;
            const QString kind = zones_[item].toObject().value("kind").toString();
            return kind == "ScreenPanel" ? 0 : 1;
        };
        return rank(a) < rank(b);
    });
    for (int item : order) drawItem(item);

    if (selection_.size() == 1 && drag_ != Drag::Band && !drawMode_) {
        p.setPen(QPen(QColor(0x20, 0x24, 0x28), 1));
        p.setBrush(Qt::white);
        for (const QRectF& handle : handleRects(toWidget(itemRect(selection_.first())))) p.drawRect(handle);
    }

    p.setPen(QPen(accentColour(), 1));
    if (guideX_) {
        const qreal x = toWidget(QPointF(*guideX_, 0)).x();
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
    }
    if (guideY_) {
        const qreal y = toWidget(QPointF(0, *guideY_)).y();
        p.drawLine(QPointF(0, y), QPointF(width(), y));
    }
    if (!band_.isNull()) {
        p.setPen(QPen(QColor(0x50, 0xa0, 0xff), 1, Qt::DashLine));
        p.setBrush(QColor(0x50, 0xa0, 0xff, 40));
        p.drawRect(band_);
    }
}

} // namespace harmony::app
