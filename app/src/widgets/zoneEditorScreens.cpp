/**
 * @file zoneEditorScreens.cpp
 * @brief Редактор зон: экраны модели — список, сетка плиток, служебные элементы, новый экран.
 */

#include "zoneEditorDialog.h"
#include "zoneEditorShared.h"

#include "../modern/remoteView.h"
#include "../zoneEditing.h"

#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>

#include <algorithm>
#include <cmath>

namespace harmony::app {

using namespace zoneEditorDetail;

namespace {

/**
 * @brief Прямоугольник зоны номер `index` в раскладке cols×rows (по рядам, слева направо) внутри области `area`.
 * @param gap Зазор между зонами и от краёв области, в единицах `ref`.
 */
QRectF gridCell(int index, int cols, int rows, const QRectF& area, qreal gap) {
    const qreal width = (area.width() - gap * (cols + 1)) / cols;
    const qreal height = (area.height() - gap * (rows + 1)) / rows;
    return {area.left() + gap + (index % cols) * (width + gap),
            area.top() + gap + (index / cols) * (height + gap), width, height};
}

/// Служебный элемент `item` есть среди ячеек экрана.
bool hasService(const QJsonArray& zones, const ServiceItem& item) {
    return std::any_of(zones.begin(), zones.end(), [&item](const QJsonValue& value) {
        const QJsonObject zone = value.toObject();
        return zone.value(QStringLiteral("kind")).toString() == QLatin1String("ScreenLabel") &&
               item.ids.contains(zone.value(QStringLiteral("id")).toString());
    });
}

} // namespace

// ── Экраны модели ─────────────────────────────────────────────────────────────

void ZoneEditorDialog::refreshScreens() {
    const bool onScreen = hasLayer() && !layer().body();
    {
        QSignalBlocker block(screenList_);
        screenList_->clear();
        for (int i = 1; i < layers_.size(); ++i) {
            const Layer& l = layers_[i];
            auto* item = new QListWidgetItem(
                l.draft ? tr("New screen (not created yet)")
                        : tr("%1 — zones: %2").arg(RemoteView::screenModeName(l.key)).arg(l.current.zones.size()),
                screenList_);
            item->setToolTip(l.key);
        }
        screenList_->setCurrentRow(onScreen ? current_ - 1 : -1); // на корпусе строка не выбрана: щелчок вернёт к экрану
        // Список ровно по числу экранов (не больше шести строк, дальше своя прокрутка).
        const int rowHeight = screenList_->count() > 0 ? screenList_->sizeHintForRow(0) : screenList_->fontMetrics().height() + 8;
        const int rows = std::clamp(static_cast<int>(screenList_->count()), 1, 6);
        screenList_->setFixedHeight(rows * rowHeight + 2 * screenList_->frameWidth() + 4);
    }
    // Настройки экрана доступны, пока на холсте экран, а не корпус.
    if (onScreen) {
        const QSize grid = slotGrid(layer().current.zones);
        QSignalBlocker blockCols(arrangeColsBox_);
        QSignalBlocker blockRows(arrangeRowsBox_);
        QSignalBlocker blockGap(gapBox_);
        arrangeColsBox_->setValue(grid.width());
        arrangeRowsBox_->setValue(grid.height());
        gapBox_->setValue(layer().current.gap);
    }
    for (QWidget* w : screenWidgets_) w->setEnabled(onScreen);
    {
        // Зона плиток есть только у экрана с плитками.
        QSignalBlocker block(ownAreaCheck_);
        const bool hasArea = onScreen && layer().current.screen.has_value();
        ownAreaCheck_->setEnabled(hasArea);
        ownAreaCheck_->setChecked(hasArea && layer().current.ownArea);
    }
    const bool draft = onScreen && layer().draft;
    bool hasDraft = false;
    for (const Layer& l : layers_) hasDraft = hasDraft || l.draft;
    updateRoleChoices();
    removeScreenButton_->setEnabled(onScreen && !draft);
    // Черновик один: сначала создать или отменить его. Все экраны уже есть: добавлять нечего.
    addScreenButton_->setEnabled(!hasDraft && roleBox_->count() > 0);
    roleBox_->setEnabled(draft);
    draftPanel_->setVisible(draft);
    {
        QSignalBlocker blockRole(roleBox_);
        const int role = onScreen ? roleBox_->findData(layer().role) : -1;
        if (role >= 0) roleBox_->setCurrentIndex(role);
    }

    // Поля оформления открытого экрана — как в remote.json; пустое поле — значение по умолчанию.
    const QJsonObject screen = onScreen ? layer().screen : QJsonObject();
    const auto fill = [&screen](QLineEdit* field, const QStringList& keys) {
        QSignalBlocker block(field);
        field->setText(valueAt(screen, keys).toString());
    };
    fill(backgroundColorEdit_, {QStringLiteral("background"), QStringLiteral("color")});
    fill(backgroundImageEdit_, {QStringLiteral("background"), QStringLiteral("image")});
    for (const auto& [field, zone] : {std::pair{headerColorEdit_, "header"}, std::pair{footerColorEdit_, "footer"}})
        fill(field, {QStringLiteral("background"), QLatin1String(zone), QStringLiteral("color")});
    for (const auto& [box, zone] : {std::pair{headerHeightBox_, "header"}, std::pair{footerHeightBox_, "footer"}}) {
        QSignalBlocker block(box);
        box->setValue(valueAt(screen, {QStringLiteral("background"), QLatin1String(zone), QStringLiteral("height")}).toDouble());
    }
    // Линии шапки и подвала на холсте — только на экране.
    canvas_->setBackgroundBands(
        onScreen, valueAt(screen, {QStringLiteral("background"), QStringLiteral("header"), QStringLiteral("height")}).toDouble(),
        valueAt(screen, {QStringLiteral("background"), QStringLiteral("footer"), QStringLiteral("height")}).toDouble());
    fill(cellImageEdit_, {QStringLiteral("cell"), QStringLiteral("image")});
    fill(panelColorEdit_, {QStringLiteral("panel"), QStringLiteral("color")});
    fill(panelTextEdit_, {QStringLiteral("panel"), QStringLiteral("text")});
    {
        QSignalBlocker block(centerLastRowCheck_);
        centerLastRowCheck_->setChecked(screen.value(QStringLiteral("centerLastRow")).toBool());
    }
    for (int i = 0; i < serviceChecks_.size(); ++i) {
        QSignalBlocker block(serviceChecks_[i]);
        serviceChecks_[i]->setChecked(onScreen && hasService(layer().current.zones, serviceItems()[i]));
    }

    // Что из оформления перекрыто картинками: поле есть, но на экране его не видно.
    QStringList hints;
    // Те же правила, что у отрисовки: явное background.glass, иначе — нет картинки и нет зон фона.
    const QJsonObject background = screen.value(QStringLiteral("background")).toObject();
    const bool hasZones = background.contains(QStringLiteral("header")) || background.contains(QStringLiteral("footer"));
    const bool glass = onScreen && !screen.value(QStringLiteral("compose")).toBool() &&
                       (background.contains(QStringLiteral("glass"))
                            ? background.value(QStringLiteral("glass")).toBool()
                            : !background.contains(QStringLiteral("image")) && !hasZones);
    if (glass)
        hints.append(tr("Without a background picture the screen has the Harmony 900 header and footer: "
                        "the title, page, clock and battery are always drawn there."));
    if (screen.value(QStringLiteral("compose")).toBool()) {
        hints.append(tr("This screen is built from pictures: its background, panels and buttons are drawn on them, "
                        "so the background colour, background picture and panel colour are not visible."));
    } else if (!valueAt(screen, {QStringLiteral("background"), QStringLiteral("image")}).toString().isEmpty()) {
        hints.append(tr("The background picture covers the background colour."));
    }
    if (!valueAt(screen, {QStringLiteral("cell"), QStringLiteral("image")}).toString().isEmpty())
        hints.append(tr("The tile picture replaces the tile colour and border of the remote."));
    screenHint_->setText(hints.join(QLatin1Char(' ')));
    screenHint_->setVisible(!hints.isEmpty());
}

void ZoneEditorDialog::applyGrid() {
    if (!hasLayer() || layer().body()) return;
    const int cols = arrangeColsBox_->value();
    const int rows = arrangeRowsBox_->value();
    const qreal gap = zoneEditing::round2(gapBox_->value());
    // editingFinished приходит и без правки (уход фокуса): тогда плитки не трогаем.
    const QSize grid = slotGrid(layer().current.zones);
    if (cols == grid.width() && rows == grid.height() && gap == layer().current.gap) return;
    arrange(cols, rows, gap, gap != layer().current.gap ? tr("Change tile gap") : tr("Change tile grid"),
            QStringLiteral("grid"));
}

void ZoneEditorDialog::arrange(int cols, int rows, qreal gap, const QString& text, const QString& mergeKey) {
    if (!hasLayer() || layer().body()) return;
    const State& now = layer().current;
    // Плитки ложатся в зону плиток; без неё — туда, где они сейчас, а у экрана без плиток — на весь экран.
    const QRectF area = now.screen.value_or(tilesBounds(now.zones, now.gap).value_or(QRectF(QPointF(0, 0), layer().ref)));
    edit(text, laidOut(now, cols, rows, gap, area), mergeKey);
    refreshScreens();
}

ZoneEditorDialog::State ZoneEditorDialog::laidOut(const State& state, int cols, int rows, qreal gap,
                                                  const QRectF& area) const {
    State after = state;
    const int count = cols * rows;
    // Плитки в порядке следования; лишние убираются с конца, недостающие добавляются. Служебные зоны не трогаем.
    QList<int> tiles;
    for (int i = 0; i < after.zones.size(); ++i)
        if (isSlot(after.zones[i].toObject())) tiles.append(i);
    for (int k = static_cast<int>(tiles.size()) - 1; k >= count; --k) after.zones.removeAt(tiles[k]);
    if (tiles.size() > count) tiles.resize(count);
    for (int k = 0; k < count; ++k) {
        QJsonObject zone;
        if (k < tiles.size()) {
            zone = after.zones[tiles[k]].toObject(); // id, действие и прочие свойства сохраняются
        } else {
            zone.insert(QStringLiteral("id"), uniqueId(QStringLiteral("Slot%1").arg(k + 1), idsOf(after.zones)));
            zone.insert(QStringLiteral("kind"), QStringLiteral("ScreenSlot"));
            zone.insert(QStringLiteral("radius"), 6);
        }
        zoneEditing::setRect(zone, gridCell(k, cols, rows, area, gap));
        if (k < tiles.size()) after.zones[tiles[k]] = zone; else after.zones.append(zone);
    }
    after.gap = gap;
    return after;
}

std::optional<QRectF> ZoneEditorDialog::commonArea(bool original, int skip) const {
    for (int i = 1; i < layers_.size(); ++i) {
        const Layer& l = layers_[i];
        if (i == skip || l.draft) continue;
        const State& state = original ? l.original : l.current;
        if (!state.ownArea && state.screen) return state.screen;
    }
    return std::nullopt;
}

void ZoneEditorDialog::setOwnArea(bool on) {
    if (!hasLayer() || layer().body() || !layer().current.screen) return;
    State after = layer().current;
    if (after.ownArea == on) return;
    after.ownArea = on;
    // Снова общая: экран берёт зону остальных экранов (плитки ложатся в неё); других нет — его зона и есть общая.
    if (!on)
        if (const auto shared = commonArea(false, current_)) after.screen = shared;
    edit(on ? tr("Own tile area for this screen") : tr("Common tile area"), after);
}

void ZoneEditorDialog::setService(int index, bool on) {
    if (!hasLayer() || layer().body() || index < 0 || index >= serviceItems().size()) return;
    const ServiceItem& item = serviceItems()[index];
    State after = layer().current;
    if (on == hasService(after.zones, item)) return;
    if (on) {
        const QSizeF ref = layer().ref;
        QJsonObject zone;
        zone.insert(QStringLiteral("id"), item.ids.first());
        zoneEditing::setRect(zone, QRectF(item.place.x() * ref.width(), item.place.y() * ref.height(),
                                          item.place.width() * ref.width(), item.place.height() * ref.height()));
        zone.insert(QStringLiteral("radius"), 0);
        zone.insert(QStringLiteral("kind"), QStringLiteral("ScreenLabel"));
        after.zones.append(zone);
        edit(tr("Show %1").arg(tr(item.title)), after);
        canvas_->setSelection({static_cast<int>(after.zones.size()) - 1});
        note(tr("Move %1 on the screen to where it should be shown.").arg(tr(item.title)));
    } else {
        for (qsizetype i = after.zones.size() - 1; i >= 0; --i) {
            const QJsonObject zone = after.zones[i].toObject();
            if (zone.value(QStringLiteral("kind")).toString() == QLatin1String("ScreenLabel") &&
                item.ids.contains(zone.value(QStringLiteral("id")).toString()))
                after.zones.removeAt(i);
        }
        canvas_->setSelection({});
        edit(tr("Hide %1").arg(tr(item.title)), after);
    }
}

void ZoneEditorDialog::updateRoleChoices() {
    QSet<QString> existing;
    for (const Layer& l : layers_)
        if (!l.draft && !l.body()) existing.insert(l.role);
    const QString selected = roleBox_->currentData().toString();
    QSignalBlocker block(roleBox_);
    roleBox_->clear();
    for (const ScreenRole& r : kScreenRoles) {
        if (existing.contains(QString::fromLatin1(r.role))) continue;
        roleBox_->addItem(RemoteView::screenModeName(QString::fromLatin1(r.id)), QString::fromLatin1(r.role));
    }
    const int keep = roleBox_->findData(selected);
    if (keep >= 0) roleBox_->setCurrentIndex(keep);
}

void ZoneEditorDialog::beginNewScreen() {
    if (layers_.isEmpty()) return;
    for (int i = 1; i < layers_.size(); ++i)
        if (layers_[i].draft) {
            showLayer(i);
            return;
        }
    updateRoleChoices();
    if (roleBox_->count() == 0) return; // у пульта уже есть все экраны
    // Размер экрана — по пропорциям области экрана корпуса.
    QSizeF size(320, 240);
    const auto display = layers_.first().current.screen;
    if (display && display->isValid()) size = QSizeF(320, std::round(320.0 * display->height() / display->width()));

    Layer l;
    l.key = QStringLiteral("draft:%1").arg(++draftSerial_); // уникален: старые записи истории не заденут новый черновик
    l.layout = l.key;
    l.modes = {l.key};
    l.draft = true;
    l.caption = tr("New screen");
    l.role = roleBox_->currentData().toString();
    // Оформление берётся у экрана этого пульта той же роли, иначе у первого: у всех экранов один вид.
    // Описание пульта уже прочитано при загрузке модели: файл заново не разбирается.
    QJsonObject donor;
    for (const auto& value : remote_.value(QStringLiteral("screens")).toArray()) {
        const QJsonObject screen = value.toObject();
        if (donor.isEmpty() || screen.value(QStringLiteral("role")).toString() == l.role) donor = screen;
        if (screen.value(QStringLiteral("role")).toString() == l.role) break;
    }
    // Картинки слоями (compose, layers) привязаны к id экрана и черновику не подходят.
    for (const char* field : {"background", "cell", "panel", "centerLastRow"})
        if (donor.contains(QLatin1String(field))) l.screen.insert(QLatin1String(field), donor.value(QLatin1String(field)));
    // Цвет и рамка плиток общие для пульта (блок "tiles"): у экрана их не остаётся.
    QJsonObject cell = l.screen.value(QStringLiteral("cell")).toObject();
    cell.remove(QStringLiteral("color"));
    cell.remove(QStringLiteral("border"));
    if (cell.isEmpty()) l.screen.remove(QStringLiteral("cell"));
    else l.screen.insert(QStringLiteral("cell"), cell);
    l.current.gap = donor.value(QStringLiteral("grid")).toObject().value(QStringLiteral("gap")).toDouble(zoneEditing::kDefaultGap);
    // Служебные зоны (шапка, часы, нижние кнопки) и размер — как у образца: экран выглядит как на пульте.
    const auto donorRef = donor.value(QStringLiteral("ref")).toArray();
    if (donorRef.size() == 2 && donorRef[0].toDouble() > 0 && donorRef[1].toDouble() > 0)
        size = QSizeF(donorRef[0].toDouble(), donorRef[1].toDouble());
    for (const auto& value : donor.value(QStringLiteral("cells")).toArray())
        if (!isSlot(value.toObject())) l.current.zones.append(value);
    // Плитки 2×2 (на узком экране длинного пульта три колонки не помещаются) — в области между служебными зонами.
    QRectF area(QPointF(0, 0), size);
    qreal top = 0;
    qreal bottom = size.height();
    for (const auto& value : l.current.zones) {
        const QRectF r = zoneEditing::rectOf(value.toObject());
        if (r.center().y() < size.height() / 3) top = std::max(top, r.bottom());
        else if (r.center().y() > size.height() * 0.7) bottom = std::min(bottom, r.top());
    }
    if (bottom - top > 40) area = QRectF(0, top, size.width(), bottom - top);
    // Новый экран берёт общую зону плиток пульта; без неё зона — своя, между служебными зонами.
    if (const auto shared = commonArea(false)) area = *shared;
    else l.current.ownArea = true;
    for (int i = 0; i < 4; ++i) {
        QJsonObject tile;
        tile.insert(QStringLiteral("id"), QStringLiteral("Slot%1").arg(i + 1));
        tile.insert(QStringLiteral("kind"), QStringLiteral("ScreenSlot"));
        zoneEditing::setRect(tile, gridCell(i, 2, 2, area, l.current.gap));
        tile.insert(QStringLiteral("radius"), 6);
        l.current.zones.append(tile);
    }
    l.current.screen = area;
    l.original.gap = l.current.gap;
    l.original.screen = l.current.screen;
    l.original.ownArea = l.current.ownArea;
    l.ref = size;
    layers_.append(l);
    {
        QSignalBlocker block(layerBox_);
        layerBox_->addItem(l.caption);
    }
    showLayer(static_cast<int>(layers_.size()) - 1);
}

void ZoneEditorDialog::cancelNewScreen() {
    if (!hasLayer() || !layer().draft) return;
    const int index = current_;
    layers_.removeAt(index);
    {
        QSignalBlocker block(layerBox_);
        layerBox_->removeItem(index);
    }
    current_ = -1;
    showLayer(index - 1);
}

void ZoneEditorDialog::createNewScreen() {
    if (!hasLayer() || !layer().draft) return;
    const Layer draft = layer(); // confirmDiscard() может перечитать модель

    QString base = draft.role;
    for (const ScreenRole& r : kScreenRoles)
        if (draft.role == QLatin1String(r.role)) base = QString::fromLatin1(r.id);
    QSet<QString> taken;
    for (const auto& value : remote_.value(QStringLiteral("screens")).toArray())
        taken.insert(value.toObject().value(QStringLiteral("id")).toString());
    const QString id = uniqueId(base, taken);

    QJsonObject screen = draft.screen; // оформление, правленное в черновике
    screen.insert(QStringLiteral("id"), id);
    screen.insert(QStringLiteral("role"), draft.role);
    screen.insert(QStringLiteral("ref"), QJsonArray{draft.ref.width(), draft.ref.height()});
    // Сколько плиток рисовать: колонки и ряды; координаты зон лежат в cells.
    const QSize grid = slotGrid(draft.current.zones);
    QJsonObject gridObject{{QStringLiteral("cols"), grid.width()}, {QStringLiteral("rows"), grid.height()}};
    if (draft.current.gap != zoneEditing::kDefaultGap) gridObject.insert(QStringLiteral("gap"), draft.current.gap);
    // Общая зона, которой нет в файле (её считали по плиткам), пишется экрану явно.
    const bool commonInFile = zoneEditing::rectOf(QJsonObject{
        {QStringLiteral("rect"), valueAt(remote_, {QStringLiteral("tiles"), QStringLiteral("area")})}}).isValid();
    if (draft.current.screen && (draft.current.ownArea || !commonInFile))
        gridObject.insert(QStringLiteral("area"), rectArray(*draft.current.screen));
    screen.insert(QStringLiteral("grid"), gridObject);
    screen.insert(QStringLiteral("cells"), draft.current.zones); // зоны ровно такие, как на холсте
    if (modifyRemote([screen](const QByteArray& text) { return zoneEditing::appendScreen(text, screen); }, id, false))
        note(tr("Screen created"));
}

void ZoneEditorDialog::removeScreen() {
    if (!hasLayer() || layer().body() || layer().draft) return;
    const QString id = layer().layout;
    if (QMessageBox::question(this, windowTitle(),
                              tr("Remove screen «%1»? Its zones will be deleted.").arg(RemoteView::screenModeName(id))) !=
        QMessageBox::Yes)
        return;
    if (modifyRemote([id](const QByteArray& text) { return zoneEditing::removeScreen(text, id); }, QString()))
        note(tr("Screen removed"));
}

} // namespace harmony::app
