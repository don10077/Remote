/**
 * @file zoneEditorZones.cpp
 * @brief Редактор зон: действия над зонами (добавление, копирование, выравнивание) и авто-зоны.
 */

#include "zoneEditorDialog.h"

#include "../labels.h"
#include "../remoteResources.h"
#include "../zoneEditing.h"

#include <QAction>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <algorithm>
#include <utility>

namespace harmony::app {

namespace {

/// Скрипт авто-зон: рядом с исполняемым файлом либо в репозитории (раскладки build/ и dist/).
QString findAutoZonesScript() {
    const QString base = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        base + QStringLiteral("/tools/autoZones.py"),
        base + QStringLiteral("/../../tools/autoZones.py"),
        base + QStringLiteral("/../../../tools/autoZones.py"),
        base + QStringLiteral("/../../../../tools/autoZones.py"), // build/ucrt64/<preset>/{app,tests}
    };
    for (const QString& candidate : candidates)
        if (QFileInfo::exists(QDir::cleanPath(candidate))) return QDir::cleanPath(candidate);
    return {};
}

} // namespace

// ── Действия над зонами ───────────────────────────────────────────────────────

void ZoneEditorDialog::addZone(const std::optional<QRectF>& drawn) {
    if (!hasLayer()) return;
    const Layer& l = layer();
    State after = l.current;
    QRectF rect;
    if (drawn) {
        rect = drawn->normalized();
    } else {
        const QSizeF size(l.ref.width() / 8, l.ref.height() / 12);
        rect = QRectF(QPointF((l.ref.width() - size.width()) / 2, (l.ref.height() - size.height()) / 2), size);
    }
    QJsonObject zone;
    zone.insert(QStringLiteral("id"), uniqueId(l.body() ? QStringLiteral("Zone") : QStringLiteral("Slot"), idsOf(after.zones)));
    zoneEditing::setRect(zone, rect);
    zone.insert(QStringLiteral("radius"), l.body() ? 4.0 : 0.0);
    zone.insert(QStringLiteral("kind"), l.body() ? QStringLiteral("Key") : QStringLiteral("ScreenSlot"));
    after.zones.append(zone);
    edit(tr("Add zone"), after);
    canvas_->setSelection({static_cast<int>(after.zones.size()) - 1});
}

void ZoneEditorDialog::runAutoZones() {
    if (autoProcess_) { // повторное нажатие во время распознавания — отмена
        stopAutoZones();
        note(tr("Auto zones stopped."));
        return;
    }
    if (!hasLayer() || !layer().body()) return;
    const QString photoPath = remoteResources::folder() + QStringLiteral("/remotes/") + model_ + QStringLiteral("/") + remoteResources::kPhotoFile;
    if (!QFileInfo::exists(photoPath)) {
        note(tr("Auto zones needs the photo %1, but the file was not found.").arg(photoPath));
        return;
    }
    const QString script = findAutoZonesScript();
    if (script.isEmpty()) {
        note(tr("The auto-zones script was not found (tools/autoZones.py)."));
        return;
    }
    // Интерпретаторы по очереди: следующий пробуется, если предыдущий не запустился или завершился с ошибкой.
    const QStringList arguments{script, photoPath, QStringLiteral("--json")};
    const QString python = qEnvironmentVariable("HARMONY_AUTO_ZONES_PYTHON");
    autoAttempts_.clear();
    if (python.isEmpty()) {
        autoAttempts_.append({QStringLiteral("python"), arguments});
        autoAttempts_.append({QStringLiteral("py"), QStringList{QStringLiteral("-3")} + arguments});
    } else {
        autoAttempts_.append({python, arguments});
    }
    autoAction_->setText(tr("Stop auto zones"));
    note(tr("Recognizing zones on the photo… The editor stays available; press “Stop auto zones” to cancel."));
    startAutoZones();
}

void ZoneEditorDialog::startAutoZones() {
    if (autoAttempts_.isEmpty()) {
        resetAutoAction();
        note(tr("Auto zones did not start. Python 3 with the opencv package is required: pip install opencv-python."));
        return;
    }
    const auto [program, arguments] = autoAttempts_.takeFirst();
    auto* process = new QProcess(this);
    process->setObjectName(QStringLiteral("autoZonesProcess"));
    autoProcess_ = process;
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (process != autoProcess_) return; // процесс уже отменён
        autoProcess_ = nullptr;
        process->deleteLater();
        if (status != QProcess::NormalExit || code != 0) {
            startAutoZones();
            return;
        }
        autoAttempts_.clear();
        resetAutoAction();
        applyAutoZones(process->readAllStandardOutput());
    });
    // FailedToStart приходит без finished(): сразу пробуем следующий интерпретатор.
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process != autoProcess_ || error != QProcess::FailedToStart) return;
        autoProcess_ = nullptr;
        process->deleteLater();
        startAutoZones();
    });
    process->start(program, arguments);
}

void ZoneEditorDialog::stopAutoZones() {
    autoAttempts_.clear();
    QProcess* process = std::exchange(autoProcess_, nullptr);
    if (!process) return;
    process->disconnect(this);
    process->kill();
    process->waitForFinished(1000); // забрать завершившийся процесс, чтобы QProcess не предупреждал при удалении
    process->deleteLater();
    resetAutoAction();
}

void ZoneEditorDialog::resetAutoAction() {
    autoAction_->setText(tr("Auto zones"));
    autoAction_->setEnabled(hasLayer() && layer().body());
}

void ZoneEditorDialog::applyAutoZones(const QByteArray& output) {
    if (!hasLayer()) return;
    // Результат относится к корпусу: пока скрипт работал, мог открыться экран.
    if (!layer().body()) showLayer(0);
    const QJsonArray boxes = QJsonDocument::fromJson(output).array();
    State after = layer().current;
    const QJsonArray added = zoneEditing::autoZones(boxes, after.zones, QStringLiteral("Auto"), after.screen);
    if (added.isEmpty()) {
        if (boxes.isEmpty()) {
            note(tr("No zones were recognized on the photo."));
        } else {
            note(tr("All %1 recognized zones overlap the zones already on the screen, so nothing was added. "
                   "Delete the zones you want to replace (%2, then %3) and run Auto zones again.")
                .arg(boxes.size())
                .arg(labels::shortcut(QKeySequence::SelectAll), labels::shortcut(QKeySequence::Delete)));
        }
        return;
    }
    const int first = after.zones.size();
    for (const auto& zone : added) after.zones.append(zone);
    edit(tr("Auto zones"), after);
    QList<int> created;
    for (int i = first; i < after.zones.size(); ++i) created.append(i);
    canvas_->setSelection(created);
    const int skipped = boxes.size() - added.size();
    note(skipped > 0
        ? tr("Recognized %1 zones: %2 added, %3 skipped because they overlap the zones on the screen.")
            .arg(boxes.size()).arg(added.size()).arg(skipped)
        : tr("Recognized %1 zone(s). Rename them and fix or delete what is not needed.").arg(added.size()));
}

void ZoneEditorDialog::duplicateZones() {
    if (!hasLayer()) return;
    State after = layer().current;
    QSet<QString> taken = idsOf(after.zones);
    QList<int> created;
    for (int item : canvas_->selection()) {
        if (item < 0) continue;
        QJsonObject zone = zoneAt(item);
        zone.insert(QStringLiteral("id"), uniqueId(zone.value(QStringLiteral("id")).toString() + QStringLiteral("_copy"), taken));
        taken.insert(zone.value(QStringLiteral("id")).toString());
        zoneEditing::setRect(zone, zoneEditing::rectOf(zone).translated(10, 10));
        after.zones.append(zone);
        created.append(static_cast<int>(after.zones.size()) - 1);
    }
    if (created.isEmpty()) return;
    edit(tr("Duplicate zones"), after);
    canvas_->setSelection(created);
}

void ZoneEditorDialog::removeZones() {
    if (!hasLayer()) return;
    QList<int> items = canvas_->selection();
    if (items.isEmpty()) return;
    State after = layer().current;
    std::sort(items.begin(), items.end(), std::greater<int>());
    for (int item : items) {
        if (item == ZoneCanvas::SCREEN_ITEM) after.screen.reset();
        else if (item >= 0 && item < after.zones.size()) after.zones.removeAt(item);
    }
    canvas_->setSelection({});
    edit(tr("Delete zones"), after);
}

void ZoneEditorDialog::selectAll() {
    if (!hasLayer()) return;
    QList<int> items;
    for (int i = 0; i < layer().current.zones.size(); ++i) items.append(i);
    if (!items.isEmpty()) canvas_->setSelection(items);
}

void ZoneEditorDialog::copyZones() {
    QByteArrayList lines;
    for (int item : canvas_->selection())
        if (item >= 0) lines.append(zoneEditing::zoneLine(zoneAt(item)));
    if (lines.isEmpty()) return;
    const QByteArray separator = QByteArray(",") + char(10);
    QGuiApplication::clipboard()->setText(QString::fromUtf8(lines.join(separator)));
    note(tr("Copied %1 zone(s)").arg(lines.size()));
}

void ZoneEditorDialog::pasteZones() {
    if (!hasLayer()) return;
    const QJsonArray pasted = zoneEditing::parseZones(QGuiApplication::clipboard()->text().toUtf8());
    if (pasted.isEmpty()) {
        note(tr("The clipboard has no zones"));
        return;
    }
    State after = layer().current;
    QSet<QString> taken = idsOf(after.zones);
    QList<int> created;
    for (const auto& value : pasted) {
        QJsonObject zone = value.toObject();
        const QString id = uniqueId(zone.value(QStringLiteral("id")).toString(), taken);
        zone.insert(QStringLiteral("id"), id);
        taken.insert(id);
        after.zones.append(zone);
        created.append(static_cast<int>(after.zones.size()) - 1);
    }
    edit(tr("Paste zones"), after);
    canvas_->setSelection(created);
}

void ZoneEditorDialog::align(Align how) {
    const auto selection = canvas_->selection();
    if (!hasLayer() || selection.size() < 2) return;
    const QRectF base = canvas_->itemRect(selection.first());
    State after = layer().current;
    for (int i = 1; i < selection.size(); ++i) {
        QRectF r = canvas_->itemRect(selection[i]);
        switch (how) {
        case Align::Left: r.moveLeft(base.left()); break;
        case Align::Right: r.moveRight(base.right()); break;
        case Align::Top: r.moveTop(base.top()); break;
        case Align::Bottom: r.moveBottom(base.bottom()); break;
        case Align::CentreX: r.moveCenter(QPointF(base.center().x(), r.center().y())); break;
        case Align::CentreY: r.moveCenter(QPointF(r.center().x(), base.center().y())); break;
        case Align::Width: r.setWidth(base.width()); break;
        case Align::Height: r.setHeight(base.height()); break;
        }
        ZoneCanvas::setRectIn(after.zones, after.screen, selection[i], r);
    }
    edit(tr("Align zones"), after);
}

void ZoneEditorDialog::setScreenArea(bool on) {
    if (!hasLayer() || !layer().body()) return;
    State after = layer().current;
    if (on == after.screen.has_value()) return;
    if (on) {
        const QSizeF size = photoSize_ * 0.4;
        after.screen = previousScreen_.value_or(QRectF(QPointF((photoSize_.width() - size.width()) / 2, photoSize_.height() * 0.1), size));
    } else {
        previousScreen_ = after.screen;
        after.screen.reset();
    }
    edit(on ? tr("Add screen area") : tr("Remove screen area"), after);
    if (on) { canvas_->setSelection({ZoneCanvas::SCREEN_ITEM}); canvas_->reveal(ZoneCanvas::SCREEN_ITEM); }
}

} // namespace harmony::app
