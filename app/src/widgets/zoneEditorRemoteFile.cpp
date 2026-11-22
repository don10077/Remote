/**
 * @file zoneEditorRemoteFile.cpp
 * @brief Редактор зон: сохранение и запись полей remote.json, вкладка «Пульт».
 */

#include "zoneEditorDialog.h"
#include "zoneEditorShared.h"

#include "../modern/remoteView.h"
#include "../remoteResources.h"
#include "../zoneEditing.h"

#include "harmony/core/remoteCatalog.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScopeGuard>

#include <utility>

namespace harmony::app {

using namespace zoneEditorDetail;

namespace {

/// Записать значение по пути ключей; Undefined — удалить ключ, опустевшие объекты на пути тоже.
void setValueAt(QJsonObject& object, const QStringList& keys, const QJsonValue& value) {
    if (keys.isEmpty()) return;
    if (keys.size() == 1) {
        if (value.isUndefined()) object.remove(keys.first());
        else object.insert(keys.first(), value);
        return;
    }
    QJsonObject child = object.value(keys.first()).toObject();
    setValueAt(child, keys.mid(1), value);
    if (child.isEmpty()) object.remove(keys.first());
    else object.insert(keys.first(), child);
}

} // namespace

// ── Сохранение ────────────────────────────────────────────────────────────────

bool ZoneEditorDialog::save() {
    if (actionPanel_->isVisible() && actionBox_->currentData() == "key" && commandBox_->currentText().trimmed().isEmpty()) {
        note(tr("Choose a button for this zone."));
        commandBox_->setFocus();
        return false;
    }
    if (!isDirty()) return true;
    for (const Layer& l : layers_) {
        if (l.draft || l.current == l.original) continue;
        for (const auto& issue : issuesOf(l)) {
            if (issue.level != zoneEditing::Issue::Level::Error) continue;
            QMessageBox::warning(this, windowTitle(),
                                 tr("Layer %1 has errors, the program would reject these resources: %2").arg(l.caption, issue.text));
            return false;
        }
    }

    const QString path = remotePath();

    // Внешние изменения нельзя незаметно заменить целым старым массивом зон.
    QHash<QString, QByteArray> disk;
    QStringList conflicts;
    for (auto it = files_.cbegin(); it != files_.cend(); ++it) {
        QFile file(remoteResources::folder() + '/' + it.key());
        if (!file.open(QIODevice::ReadOnly)) conflicts.append(it.key());
        else {
            disk.insert(it.key(), file.readAll());
            if (disk.value(it.key()) != it.value()) conflicts.append(it.key());
        }
    }
    if (!conflicts.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, windowTitle(),
            tr("These files changed outside the editor."), QMessageBox::Cancel, this);
        box.setDetailedText(conflicts.join('\n'));
        box.setInformativeText(tr("Save my edits: the editor writes only the parts it changed into the current file. Reload: discard my edits and take the file from disk."));
        auto* merge = box.addButton(tr("Save my edits on top"), QMessageBox::AcceptRole);
        auto* reload = box.addButton(tr("Reload and discard my edits"), QMessageBox::DestructiveRole);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == reload) reloadModel(hasLayer() ? layer().key : QString(), true);
        if (box.clickedButton() != merge) return false;
    }
    // Файл старой схемы записывается сразу в текущей: текст зон при загрузке взят из того же перевода.
    QByteArray remote = remoteResources::currentSchema(disk.value(path), model_);
    const auto fail = [this](const QString& file) {
        QMessageBox::warning(this, windowTitle(), tr("Could not update %1: the file structure is not recognised.").arg(file));
        return false;
    };

    // Общая зона плиток изменилась: она пишется в tiles.area, а экраны со своей зоной получают grid.area явно.
    const auto common = commonArea(false);
    const bool commonChanged = common != commonArea(true);
    // У каждого пульта свои экраны: правка одного файла не затрагивает другие модели.
    for (const Layer& l : layers_) {
        if (l.draft || (l.current == l.original && !commonChanged)) continue;
        if (l.current.zones != l.original.zones) {
            const auto patched = zoneEditing::patchZones(remote, l.body() ? QString() : l.layout, linesOf(l));
            if (!patched) return fail(path);
            remote = *patched;
        }
        if (!l.body() && (l.current.zones != l.original.zones || l.current.gap != l.original.gap)) {
            // Сетка экрана (cols × rows, зазор) хранится в remote.json рядом с зонами и всегда им соответствует.
            const QSize grid = slotGrid(l.current.zones);
            const auto withGrid = zoneEditing::patchGrid(remote, l.layout, grid.width(), grid.height(), l.current.gap);
            if (!withGrid) return fail(path);
            remote = *withGrid;
        }
        if (!l.body() && l.current.screen &&
            (l.current.screen != l.original.screen || l.current.ownArea != l.original.ownArea || commonChanged)) {
            // Своя зона — в grid.area экрана; у экрана с общей зоной своей нет.
            const QJsonValue area = l.current.ownArea ? QJsonValue(rectArray(*l.current.screen)) : QJsonValue(QJsonValue::Undefined);
            const auto withArea = zoneEditing::patchValue(remote, l.layout, {QStringLiteral("grid"), QStringLiteral("area")}, area);
            if (!withArea) return fail(path);
            remote = *withArea;
        }
        if (l.body() && l.current.screen != l.original.screen) {
            const auto patched = zoneEditing::patchDisplay(remote, l.current.screen);
            if (!patched) return fail(path);
            remote = *patched;
        }
    }
    if (commonChanged && common) {
        const auto withCommon = zoneEditing::patchValue(remote, {}, {QStringLiteral("tiles"), QStringLiteral("area")},
                                                        rectArray(*common));
        if (!withCommon) return fail(path);
        remote = *withCommon;
    }

    // Сначала проверить, что получился корректный JSON, и только потом писать файл.
    QJsonParseError parseError;
    QJsonDocument::fromJson(remote, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, windowTitle(), tr("%1: the result is not valid JSON (%2). Nothing was saved.").arg(path, parseError.errorString()));
        return false;
    }
    QHash<QString, QByteArray> updates;
    updates.insert(path, remote);
    if (const auto error = zoneEditing::saveFiles(remoteResources::folder(), disk, updates); !error.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), error);
        return false;
    }

    reloadModel(hasLayer() ? layer().key : QString(), true); // превью слоёв строится из снимка
    note(tr("Saved"));
    emit resourcesSaved();
    return true;
}

void ZoneEditorDialog::refreshRemote() {
    const QList<QObject*> widgets{nameEdit_, driverBox_, touchCheck_, tileColorEdit_, tileBorderEdit_};
    for (QObject* widget : widgets) widget->blockSignals(true);
    nameEdit_->setText(remote_.value(QStringLiteral("name")).toString());

    // W3: `connection.driver` — способ подключения, заданный пользователем; без поля в файле
    // показывается значение каталога модели. Право записи решает canWriteTo(), не этот список.
    const QString driver = remote_.value(QStringLiteral("connection")).toObject()
                               .value(QStringLiteral("driver")).toString();
    const QString shown = driver.isEmpty()
        ? fromView(harmony::linkName(harmony::modelLink(model_.toStdString())))
        : driver;
    // reload() отклоняет неизвестное имя, поэтому shown всегда есть в списке kLinkNames.
    for (int i = 0; i < driverBox_->count(); ++i)
        if (driverBox_->itemData(i).toString() == shown) {
            driverBox_->setCurrentIndex(i);
            break;
        }
    touchCheck_->setChecked(remote_.value(QStringLiteral("capabilities")).toObject().value(QStringLiteral("touch")).toBool());
    const auto tiles = remote_.value(QStringLiteral("tiles")).toObject();
    tileColorEdit_->setText(tiles.value(QStringLiteral("color")).toString());
    tileBorderEdit_->setText(tiles.value(QStringLiteral("border")).toString());
    for (QObject* widget : widgets) widget->blockSignals(false);

    // Экраны, где плитки — картинки: цвет и рамка плиток там не видны.
    QStringList pictured;
    for (const Layer& l : layers_)
        if (!l.body() && !valueAt(l.screen, {QStringLiteral("cell"), QStringLiteral("image")}).toString().isEmpty())
            pictured.append(l.draft ? l.caption : RemoteView::screenModeName(l.key));
    tilesHint_->setText(pictured.isEmpty() ? QString()
        : tr("Tiles are pictures on these screens, so the tile colour and border are not visible there: %1.")
              .arg(pictured.join(QStringLiteral(", "))));
    tilesHint_->setVisible(!pictured.isEmpty());
}

// ── Запись полей remote.json ──────────────────────────────────────────────────

void ZoneEditorDialog::reloadModel(QString target, bool keepDrafts) {
    // target по значению: ссылка на ключ слоя устарела бы, когда loadModel() заменит layers_.
    QList<Layer> drafts;
    if (keepDrafts)
        for (const Layer& l : layers_)
            if (l.draft) drafts.append(l);
    remoteResources::reload(); // превью экранов строится из снимка: сначала он, потом слои
    if (!loadModel(model_)) return;
    if (!drafts.isEmpty()) {
        QSignalBlocker block(layerBox_);
        for (const Layer& d : drafts) {
            layers_.append(d);
            layerBox_->addItem(d.caption);
        }
    }
    for (int i = 0; i < layers_.size(); ++i)
        if (layers_[i].key == target) {
            showLayer(i);
            return;
        }
    refreshAll(); // loadModel() уже показал корпус; черновики попали в списки
}

bool ZoneEditorDialog::modifyRemote(const TextChange& change, QString target, bool keepDrafts) {
    if (writing_) return false;
    writing_ = true;
    const auto unlock = qScopeGuard([this] { writing_ = false; });
    if (!confirmDiscard()) return false;
    const QString path = remotePath();
    QByteArray disk;
    {
        // Файл закрывается сразу: на Windows открытый файл нельзя заменить при записи (QSaveFile).
        QFile file(QDir(remoteResources::folder()).filePath(path));
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, windowTitle(), tr("Could not read %1.").arg(path));
            return false;
        }
        disk = file.readAll();
    }
    // Правка точечная и ложится на текущий файл: внешние изменения в остальных местах сохраняются.
    const auto text = change(remoteResources::currentSchema(disk, model_));
    if (!text) {
        QMessageBox::warning(this, windowTitle(), tr("Could not update %1: the file structure is not recognised.").arg(path));
        return false;
    }
    if (*text == disk) return true;
    QJsonParseError parseError;
    QJsonDocument::fromJson(*text, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, windowTitle(), tr("%1: the result is not valid JSON (%2). Nothing was saved.").arg(path, parseError.errorString()));
        return false;
    }
    if (const auto error = zoneEditing::saveFiles(remoteResources::folder(), {{path, disk}}, {{path, *text}}); !error.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), error);
        return false;
    }
    reloadModel(target, keepDrafts);
    note(tr("Saved"));
    emit resourcesSaved();
    return true;
}

bool ZoneEditorDialog::writeField(const QString& screenId, const QStringList& keys, const QJsonValue& value,
                                  QString target) {
    return modifyRemote([screenId, keys, value](const QByteArray& text) {
        return zoneEditing::patchValue(text, screenId, keys, value);
    }, std::move(target));
}

void ZoneEditorDialog::setRemoteField(const QStringList& keys, const QJsonValue& value) {
    if (writing_ || model_.isEmpty() || valueAt(remote_, keys) == value) return;
    if (!writeField(QString(), keys, value, hasLayer() ? layer().key : QString())) refreshAll(); // вернуть поля к файлу
}

void ZoneEditorDialog::setScreenField(const QStringList& keys, const QJsonValue& value) {
    if (writing_ || !hasLayer() || layer().body() || valueAt(layer().screen, keys) == value) return;
    Layer& l = layer();
    if (l.draft) {
        // Черновик живёт только в редакторе: поле меняет его вид, файл не трогаем.
        setValueAt(l.screen, keys, value);
        liveScreen_.reset();
        updateScreenBackdrop();
        refreshScreens();
        refreshRemote();
        return;
    }
    const QString screenId = l.layout; // копии: l указывает в layers_, которые запись перечитает
    if (!writeField(screenId, keys, value, l.key)) refreshAll();
}

void ZoneEditorDialog::setBackgroundZoneHeight(const QString& zone, qreal height) {
    const QStringList whole{QStringLiteral("background"), zone};
    if (height <= 0) {
        setScreenField(whole, QJsonValue(QJsonValue::Undefined)); // без высоты зона недействительна: убираем всю
        return;
    }
    setScreenField({QStringLiteral("background"), zone, QStringLiteral("height")}, zoneEditing::round2(height));
}

void ZoneEditorDialog::previewScreenField(const QStringList& keys, const QString& text) {
    if (!hasLayer() || layer().body()) return;
    const QString colour = text.trimmed();
    if (!colour.isEmpty() && !QColor::isValidColorName(colour)) return; // недописанный цвет не показываем
    QJsonObject screen = liveScreen_.value_or(layer().screen);
    setValueAt(screen, keys, colour.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(colour));
    liveScreen_ = screen;
    updateScreenBackdrop();
}

void ZoneEditorDialog::previewTiles(const QString& key, const QString& text) {
    const QString colour = text.trimmed();
    if (!colour.isEmpty() && !QColor::isValidColorName(colour)) return;
    QJsonObject tiles = liveTiles_.value_or(remote_.value(QStringLiteral("tiles")).toObject());
    if (colour.isEmpty()) tiles.remove(key);
    else tiles.insert(key, colour);
    liveTiles_ = tiles;
    updateScreenBackdrop(); // на корпусе экрана нет: цвет будет виден при переходе на экран
}

std::optional<QJsonValue> ZoneEditorDialog::colourValue(QLineEdit* input) {
    const QString text = input->text().trimmed();
    if (text.isEmpty()) return QJsonValue(QJsonValue::Undefined);
    if (QColor::isValidColorName(text)) return QJsonValue(text);
    note(tr("Not a valid colour: %1").arg(text));
    liveScreen_.reset();
    liveTiles_.reset();
    refreshScreens();
    refreshRemote();
    updateScreenBackdrop();
    return std::nullopt;
}

std::optional<QJsonValue> ZoneEditorDialog::imageValue(QLineEdit* input) {
    const QString text = QDir::fromNativeSeparators(input->text().trimmed());
    if (text.isEmpty()) return QJsonValue(QJsonValue::Undefined);
    // Загрузчик читает картинки только из sprites/ и папок пультов (.png, .svg).
    const bool managed = (text.startsWith(QLatin1String("sprites/")) || text.startsWith(QLatin1String("remotes/"))) &&
                         (text.endsWith(QLatin1String(".png")) || text.endsWith(QLatin1String(".svg"))) &&
                         !text.contains(QLatin1String(".."));
    const bool exists = QFileInfo::exists(QDir(remoteResources::folder()).filePath(text)) ||
                        !remoteResources::data(QStringLiteral(":/res/") + text).isEmpty();
    if (managed && exists) return QJsonValue(text);
    note(managed ? tr("Picture %1 was not found in the resources folder.").arg(text)
                 : tr("A picture must be a .png or .svg file in the sprites folder or in the folder of a remote: %1").arg(text));
    refreshScreens();
    return std::nullopt;
}

bool ZoneEditorDialog::browseImage(QLineEdit* input) {
    const QDir root(remoteResources::folder());
    const QString current = input->text().trimmed();
    const QString start = current.isEmpty() ? root.filePath(QStringLiteral("sprites")) : root.filePath(current);
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose a picture"), start, tr("Pictures (*.png *.svg)"));
    if (file.isEmpty()) return false;
    const QString relative = root.relativeFilePath(file);
    if (relative.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(relative)) {
        note(tr("Copy the picture into the sprites folder of the resources folder first, then choose it there."));
        return false;
    }
    input->setText(relative);
    return true;
}

bool ZoneEditorDialog::confirmDiscard() {
    if (!isDirty()) return true;
    QMessageBox ask(QMessageBox::Question, windowTitle(), tr("The zones have unsaved changes."),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    ask.setDefaultButton(QMessageBox::Save);
    switch (ask.exec()) {
    case QMessageBox::Save: return save();
    case QMessageBox::Discard: return true;
    default: return false;
    }
}

} // namespace harmony::app
