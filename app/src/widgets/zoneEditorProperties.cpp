/**
 * @file zoneEditorProperties.cpp
 * @brief Редактор зон: список зон, свойства выделенной зоны и проверка.
 */

#include "zoneEditorDialog.h"

#include "../modern/remoteView.h"
#include "../remoteResources.h"
#include "../zoneEditing.h"

#include <QAction>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>

#include <algorithm>

namespace harmony::app {

void ZoneEditorDialog::refreshList() {
    const Layer& l = layer();
    zoneList_->blockSignals(true);
    zoneList_->clear();
    for (int i = 0; i < l.current.zones.size(); ++i) {
        const auto zone = l.current.zones[i].toObject();
        const QString id = zone.value("id").toString();
        QString caption = id.isEmpty() ? tr("(no id)") : RemoteView::keyName(id);
        if (id == "Home") caption = tr("Home");
        else if (id == "Back") caption = tr("Back");
        else if (id == "Clock") caption = tr("Clock");
        else if (id == "Battery") caption = tr("Battery");
        else if ((id == "Page" || id == "PageLabel") && zone.value("kind") == "ScreenLabel") caption = tr("Page number");
        else if ((id == "Title" || id == "Caption") && zone.value("kind") == "ScreenLabel") caption = tr("Title");
        else if (id.startsWith("Slot")) caption = tr("Tile %1").arg(id.mid(4));
        else if (zone.value("kind") == "ScreenPanel") caption = tr("Screen decoration");
        auto* item = new QListWidgetItem(caption, zoneList_);
        item->setToolTip(id + " · " + zone.value("kind").toString());
        item->setData(Qt::UserRole, i);
    }
    if (l.current.screen) {
        auto* item = new QListWidgetItem(l.body() ? tr("Screen area")
                                         : l.current.ownArea ? tr("Tile area of this screen") : tr("Tile area of all screens"),
                                         zoneList_);
        item->setData(Qt::UserRole, ZoneCanvas::SCREEN_ITEM);
    }
    zoneList_->blockSignals(false);
    syncListSelection();
}

void ZoneEditorDialog::syncListSelection() {
    const auto selection = canvas_->selection();
    zoneList_->blockSignals(true);
    zoneList_->clearSelection();
    for (int row = 0; row < zoneList_->count(); ++row) {
        auto* item = zoneList_->item(row);
        item->setSelected(selection.contains(item->data(Qt::UserRole).toInt()));
    }
    zoneList_->blockSignals(false);
}

void ZoneEditorDialog::refreshProperties() {
    const auto selection = canvas_->selection();
    const bool one = selection.size() == 1;
    const int item = one ? selection.first() : ZoneCanvas::NO_ITEM;
    const bool zone = one && item >= 0;
    const bool anyZone = std::any_of(selection.cbegin(), selection.cend(), [](int i) { return i >= 0; });

    duplicateAction_->setEnabled(anyZone);
    deleteAction_->setEnabled(!selection.isEmpty());
    copyAction_->setEnabled(anyZone);
    selectAllAction_->setEnabled(hasLayer() && !layer().current.zones.isEmpty());
    for (QAction* action : alignActions_) action->setEnabled(selection.size() >= 2);

    selectionHint_->setText(selection.isEmpty()
        ? tr("Select a button on the photo, or click Draw Zone and drag a rectangle.")
        : one ? tr("Drag the zone to move it. Drag its corner handles to resize it. Changes are applied when you save.")
              : tr("Several zones selected. Move them together or use Align selected."));
    actionPanel_->setVisible(zone && !layer().body() &&
        (zoneAt(item).value("kind") == "ScreenKey" || zoneAt(item).value("kind") == "ScreenSide"));
    idEdit_->setEnabled(zone);
    kindBox_->setEnabled(zone);
    radiusBox_->setEnabled(zone);
    propsForm_->setRowVisible(panelRoleBox_, zone && zoneAt(item).value("kind") == "ScreenPanel");
    for (auto* box : {xBox_, yBox_, wBox_, hBox_}) box->setEnabled(one);
    {
        const QString kind = zone ? zoneAt(item).value("kind").toString("ScreenSlot") : QString();
        propsForm_->setRowVisible(zoneColorEdit_, zone && !layer().body() &&
            (kind == "ScreenKey" || kind == "ScreenSide" || kind == "ScreenSlot"));
    }

    const QList<QObject*> widgets{zoneColorEdit_, idEdit_, kindBox_, panelRoleBox_, radiusBox_, xBox_, yBox_, wBox_, hBox_, actionBox_, commandBox_, targetBox_, buttonBox_};
    for (QObject* widget : widgets) widget->blockSignals(true);

    idEdit_->clear();
    zoneColorEdit_->clear();
    kindBox_->clear();
    radiusBox_->setValue(0);
    const QRectF rect = one ? canvas_->itemRect(item) : QRectF();
    xBox_->setValue(rect.x());
    yBox_->setValue(rect.y());
    wBox_->setValue(rect.width());
    hBox_->setValue(rect.height());
    if (zone) {
        const QJsonObject z = zoneAt(item);
        idEdit_->setText(z.value("id").toString());
        zoneColorEdit_->setText(z.value("color").toString());
        QStringList kinds = layer().body() ? zoneEditing::bodyKinds() : zoneEditing::screenKinds();
        const QString kind = z.value("kind").toString();
        if (!kinds.contains(kind)) kinds.append(kind);
        const QHash<QString, QString> captions{
            {"Key", tr("Assignable button")}, {"System", tr("System button")},
            {"ScreenActs", tr("Activities screen")}, {"ScreenSoft", tr("Commands screen")},
            {"PagePrev", tr("Previous page")}, {"PageNext", tr("Next page")},
            {"ScreenSlot", tr("Screen tile")}, {"ScreenKey", tr("Screen button")},
            {"ScreenSide", tr("Side cell")}, {"ScreenLabel", tr("Screen label")},
            {"ScreenPanel", tr("Screen panel")}};
        for (const auto& value : kinds)
            kindBox_->addItem(captions.value(value, value), value);
        kindBox_->setCurrentIndex(kinds.indexOf(kind));
        radiusBox_->setValue(z.value("radius").toDouble());
        commandBox_->clear();
        auto keys = RemoteView::hardKeyIds(model_);
        if (keys.isEmpty()) keys = RemoteView::sharedKeyIds();
        for (const auto& key : keys) commandBox_->addItem(key, key);
        const QString key = z.value("key").toString();
        commandBox_->setCurrentIndex(commandBox_->findData(key));
        if (commandBox_->currentIndex() < 0) commandBox_->setEditText(key);
        targetBox_->clear();
        auto modes = modes_.values();
        modes.sort();
        for (const auto& mode : modes) targetBox_->addItem(RemoteView::screenModeName(mode), mode);
        const QString target = z.value("goto").toString();
        if (!target.isEmpty() && targetBox_->findData(target) < 0) targetBox_->addItem(target, target);
        targetBox_->setCurrentIndex(target.isEmpty() ? 0 : targetBox_->findData(target));
        buttonBox_->clear();
        buttonBox_->addItem(tr("None (touch screen)"), "");
        for (const auto& value : layers_.first().current.zones) {
            const QString id = value.toObject().value("id").toString();
            buttonBox_->addItem(id, id);
        }
        const QString button = z.value("button").toString();
        if (buttonBox_->findData(button) < 0) buttonBox_->addItem(button, button);
        buttonBox_->setCurrentIndex(buttonBox_->findData(button));
        buttonBox_->setEnabled(z.value("kind") == "ScreenSide");
        const QString action = !key.isEmpty() ? "key" : !target.isEmpty() ? "goto" :
            z.value("page").isDouble() ? (z.value("page").toInt() < 0 ? "previous" : "next") :
            z.value("system").toBool() ? "system" : "none";
        actionBox_->setCurrentIndex(actionBox_->findData(action));
        commandBox_->setEnabled(action == "key");
        targetBox_->setEnabled(action == "goto");
        auto* form = qobject_cast<QFormLayout*>(actionPanel_->layout());
        form->setRowVisible(commandBox_, action == "key");
        form->setRowVisible(targetBox_, action == "goto");
        form->setRowVisible(buttonBox_, z.value("kind") == "ScreenSide");
        // Неизвестная роль подложки показывается как есть, чтобы её не потерять незаметно.
        const QString panelRole = z.value("role").toString();
        if (!panelRole.isEmpty() && panelRoleBox_->findData(panelRole) < 0) panelRoleBox_->addItem(panelRole, panelRole);
        panelRoleBox_->setCurrentIndex(panelRole.isEmpty() ? -1 : panelRoleBox_->findData(panelRole));
    }

    for (QObject* widget : widgets) widget->blockSignals(false);
}

void ZoneEditorDialog::refreshContext() {
    // Контур кнопок корпуса нужен только поверх фото; на экране фото нет — контекста нет.
    canvas_->setContext({});
}

void ZoneEditorDialog::refreshCheck() {
    const auto issues = issuesOf(layer());
    checkList_->clear();
    QHash<int, zoneEditing::Issue::Level> marks;
    int errors = 0;
    int warnings = 0;
    for (const auto& issue : issues) {
        const bool error = issue.level == zoneEditing::Issue::Level::Error;
        if (error) ++errors;
        else ++warnings;
        auto* item = new QListWidgetItem((error ? tr("Error: ") : tr("Warning: ")) + issue.text, checkList_);
        item->setForeground(error ? QColor(0xff, 0x4d, 0x4f) : QColor(0xc0, 0x80, 0x00));
        item->setData(Qt::UserRole, issue.zone);
        if (issue.zone >= 0 && (error || !marks.contains(issue.zone))) marks.insert(issue.zone, issue.level);
    }
    canvas_->setMarks(marks);
    QString text = issues.isEmpty() ? tr("Check: no problems")
                                    : tr("Check: %1 errors, %2 warnings").arg(errors).arg(warnings);
    // Ошибка, из-за которой загрузчик пропустил этот пульт.
    if (const QString problem = remoteResources::modelError(model_); !problem.isEmpty())
        text += QStringLiteral("\n\n") + tr("Configuration error of this remote: %1").arg(problem);
    checkLabel_->setText(text);
}

// ── Правка свойств ────────────────────────────────────────────────────────────

void ZoneEditorDialog::applyRect() {
    const auto selection = canvas_->selection();
    if (!hasLayer() || selection.size() != 1) return;
    const int item = selection.first();
    const QRectF rect(xBox_->value(), yBox_->value(), wBox_->value(), hBox_->value());
    if (!rect.isValid() || rect == canvas_->itemRect(item)) {
        refreshProperties();
        return;
    }
    State after = layer().current;
    ZoneCanvas::setRectIn(after.zones, after.screen, item, rect);
    edit(tr("Edit rectangle"), after, QStringLiteral("rect:") + QString::number(item));
}

void ZoneEditorDialog::applyId() {
    const auto selection = canvas_->selection();
    if (!hasLayer() || selection.size() != 1 || selection.first() < 0) return;
    const int item = selection.first();
    const QString id = idEdit_->text().trimmed();
    QJsonObject zone = zoneAt(item);
    if (id == zone.value(QStringLiteral("id")).toString()) return;
    zone.insert(QStringLiteral("id"), id);
    State after = layer().current;
    after.zones[item] = zone;
    edit(tr("Change id"), after);
}

void ZoneEditorDialog::applyKind(int index) {
    const auto selection = canvas_->selection();
    if (!hasLayer() || index < 0 || selection.size() != 1 || selection.first() < 0) return;
    const int item = selection.first();
    const QString kind = kindBox_->itemData(index).toString();
    QJsonObject zone = zoneAt(item);
    if (kind == zone.value(QStringLiteral("kind")).toString()) return;
    zone.insert(QStringLiteral("kind"), kind);
    State after = layer().current;
    after.zones[item] = zone;
    edit(tr("Change kind"), after);
}

void ZoneEditorDialog::applyRadius(double value) {
    const auto selection = canvas_->selection();
    if (!hasLayer() || selection.size() != 1 || selection.first() < 0) return;
    const int item = selection.first();
    QJsonObject zone = zoneAt(item);
    const qreal radius = zoneEditing::round2(value);
    if (zone.value(QStringLiteral("radius")).toDouble() == radius && zone.value(QStringLiteral("radius")).isDouble()) return;
    zone.insert(QStringLiteral("radius"), radius);
    State after = layer().current;
    after.zones[item] = zone;
    edit(tr("Change corner radius"), after, QStringLiteral("radius:") + QString::number(item));
}

void ZoneEditorDialog::applyPanelRole(int index) {
    const auto selection = canvas_->selection();
    if (!hasLayer() || layer().body() || index < 0 || selection.size() != 1 || selection.first() < 0) return;
    const int item = selection.first();
    QJsonObject zone = zoneAt(item);
    if (zone.value(QStringLiteral("kind")).toString() != QLatin1String("ScreenPanel")) return;
    const QString role = panelRoleBox_->itemData(index).toString();
    if (role == zone.value(QStringLiteral("role")).toString()) return;
    zone.insert(QStringLiteral("role"), role);
    State after = layer().current;
    after.zones[item] = zone;
    edit(tr("Change panel shape"), after);
}

void ZoneEditorDialog::applyZoneColor() {
    const auto selection = canvas_->selection();
    if (!hasLayer() || layer().body() || selection.size() != 1 || selection.first() < 0) return;
    const int item = selection.first();
    QJsonObject zone = zoneAt(item);
    const QString kind = zone.value(QStringLiteral("kind")).toString(QStringLiteral("ScreenSlot"));
    if (kind != QLatin1String("ScreenKey") && kind != QLatin1String("ScreenSide") && kind != QLatin1String("ScreenSlot")) return;
    const QString colour = zoneColorEdit_->text().trimmed();
    if (!colour.isEmpty() && !QColor::isValidColorName(colour)) {
        note(tr("Unknown colour. Use a name or #rrggbb."));
        zoneColorEdit_->setText(zone.value(QStringLiteral("color")).toString());
        return;
    }
    if (colour == zone.value(QStringLiteral("color")).toString()) return;
    if (colour.isEmpty()) zone.remove(QStringLiteral("color"));
    else zone.insert(QStringLiteral("color"), colour);
    State after = layer().current;
    after.zones[item] = zone;
    edit(tr("Change button colour"), after);
}

void ZoneEditorDialog::applyAction() {
    if (!hasLayer() || layer().body() || canvas_->selection().size() != 1) return;
    const int item = canvas_->selection().first();
    if (item < 0) return;
    auto zone = zoneAt(item);
    if (zone.value("kind") != "ScreenKey" && zone.value("kind") != "ScreenSide") return;
    const auto action = actionBox_->currentData().toString();
    QString key = commandBox_->currentText().trimmed();
    if (commandBox_->currentIndex() >= 0 && key == commandBox_->itemText(commandBox_->currentIndex()))
        key = commandBox_->currentData().toString();
    if (action == "key" && key.isEmpty()) {
        commandBox_->setEnabled(true);
        qobject_cast<QFormLayout*>(actionPanel_->layout())->setRowVisible(commandBox_, true);
        commandBox_->setFocus();
        note(tr("Choose a button for this zone."));
        return;
    }
    if (action == "goto" && targetBox_->currentData().toString().isEmpty()) {
        note(tr("This remote has no screen to open."));
        return;
    }
    for (const auto* field : {"key", "goto", "page", "system"}) zone.remove(field);
    if (action == "key") zone["key"] = key;
    else if (action == "goto") zone["goto"] = targetBox_->currentData().toString();
    else if (action == "previous" || action == "next") zone["page"] = action == "previous" ? -1 : 1;
    else if (action == "system") zone["system"] = true;
    if (zone.value("kind") == "ScreenSide") {
        const QString button = buttonBox_->currentData().toString();
        if (button.isEmpty()) zone.remove("button"); else zone["button"] = button;
    }
    State after = layer().current;
    after.zones[item] = zone;
    edit(tr("Change button action"), after);
}

} // namespace harmony::app
