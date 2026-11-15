/**
 * @file remoteViewInput.cpp
 * @brief Виджет пульта: геометрия и поиск зоны под курсором, мышь, перетаскивание, подсказки.
 */

#include "remoteView.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHelpEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QToolTip>
#include <algorithm>
#include <utility>

using harmony::app::RemoteView;

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
