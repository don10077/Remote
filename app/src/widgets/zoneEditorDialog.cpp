/**
 * @file zoneEditorDialog.cpp
 * @brief Редактор зон пультов: zones.json корпуса и раскладки экранов в layouts.json.
 */

#include "zoneEditorDialog.h"

#include "../labels.h"
#include "../modern/remoteView.h"
#include "../remoteResources.h"
#include "../zoneEditing.h"

#include "harmony/core/remoteProfile.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QScrollArea>
#include <QCompleter>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPainter>
#include <QProcess>
#include <QSaveFile>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QVBoxLayout>

#include <algorithm>

namespace harmony::app {
namespace {

/// Команда истории: записать слою состояние.
class StateCommand final : public QUndoCommand {
public:
    StateCommand(ZoneEditorDialog* dialog, const QString& layerKey, const ZoneEditorDialog::State& before,
                 const ZoneEditorDialog::State& after, const QString& text, const QString& mergeKey = {})
        : dialog_(dialog), layerKey_(layerKey), before_(before), after_(after), mergeKey_(mergeKey) {
        setText(text);
    }

    void undo() override { dialog_->applyState(layerKey_, before_); }
    void redo() override { dialog_->applyState(layerKey_, after_); }

    int id() const override { return mergeKey_.isEmpty() ? 0 : 1; }
    bool mergeWith(const QUndoCommand* other) override {
        const auto* cmd = static_cast<const StateCommand*>(other);
        if (cmd->layerKey_ != layerKey_ || cmd->mergeKey_.isEmpty() || cmd->mergeKey_ != mergeKey_) return false;
        after_ = cmd->after_;
        return true;
    }

private:
    ZoneEditorDialog* dialog_;
    QString layerKey_;
    ZoneEditorDialog::State before_;
    ZoneEditorDialog::State after_;
    QString mergeKey_;
};

/// Все модели для выбора в редакторе: проверенные и новые папки внешних ресурсов.
QStringList editableModels() {
    return remoteResources::modelIds();
}

QString modelName(const QString& id) {
    for (const auto& profile : harmony::remoteProfiles) {
        if (profile.id == id.toStdString()) return QString::fromStdString(profile.name);
    }
    // Новая модель могла быть отклонена загрузчиком: имя читаем из её profile.json.
    QFile file(remoteResources::folder() + QStringLiteral("/remotes/") + id + QStringLiteral("/profile.json"));
    if (!file.open(QIODevice::ReadOnly))
        file.setFileName(QStringLiteral(":/res/remotes/") + id + QStringLiteral("/profile.json"));
    if (file.open(QIODevice::ReadOnly)) {
        const auto profile = QJsonDocument::fromJson(file.readAll()).object();
        // При несовпадении id и папки показываем id: так ошибка сразу видна.
        if (profile.value(QStringLiteral("id")).toString() == id) {
            const QString name = profile.value(QStringLiteral("name")).toString().trimmed();
            if (!name.isEmpty()) return name;
        }
    }
    return id;
}

/// Скрипт авто-зон: рядом с исполняемым файлом либо в репозитории (раскладки build/ и dist/).
QString findAutoZonesScript() {
    const QString base = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        base + QStringLiteral("/tools/autoZones.py"),
        base + QStringLiteral("/../../tools/autoZones.py"),
        base + QStringLiteral("/../../../tools/autoZones.py"),
    };
    for (const QString& candidate : candidates)
        if (QFileInfo::exists(QDir::cleanPath(candidate))) return QDir::cleanPath(candidate);
    return {};
}

} // namespace

ZoneEditorDialog::ZoneEditorDialog(const QString& model, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Zone Editor"));
    resize(1280, 800);

    // ── Панель инструментов ──
    auto* toolbar = new QToolBar(this);
    toolbar->setIconSize({20, 20});
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto* undoAction = undo_.createUndoAction(this, tr("Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    toolbar->addAction(undoAction);

    auto* redoAction = undo_.createRedoAction(this, tr("Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    toolbar->addAction(redoAction);

    toolbar->addSeparator();

    drawAction_ = toolbar->addAction(tr("Draw Zone"));
    drawAction_->setCheckable(true);
    drawAction_->setShortcut(Qt::Key_D);
    connect(drawAction_, &QAction::toggled, this, [this](bool on) {
        if (canvas_) canvas_->setDrawMode(on);
    });

    duplicateAction_ = toolbar->addAction(tr("Duplicate"));
    duplicateAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    duplicateAction_->setEnabled(false);
    connect(duplicateAction_, &QAction::triggered, this, &ZoneEditorDialog::duplicateZones);

    deleteAction_ = toolbar->addAction(tr("Delete"));
    deleteAction_->setObjectName(QStringLiteral("deleteZones"));
    deleteAction_->setShortcut(QKeySequence::Delete);
    deleteAction_->setEnabled(false);
    connect(deleteAction_, &QAction::triggered, this, &ZoneEditorDialog::removeZones);

    copyAction_ = toolbar->addAction(tr("Copy"));
    copyAction_->setShortcut(QKeySequence::Copy);
    copyAction_->setEnabled(false);
    connect(copyAction_, &QAction::triggered, this, &ZoneEditorDialog::copyZones);

    auto* pasteAction = toolbar->addAction(tr("Paste"));
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, &ZoneEditorDialog::pasteZones);

    selectAllAction_ = toolbar->addAction(tr("Select all"));
    selectAllAction_->setObjectName(QStringLiteral("selectAll"));
    selectAllAction_->setShortcut(QKeySequence::SelectAll);
    selectAllAction_->setEnabled(false);
    connect(selectAllAction_, &QAction::triggered, this, &ZoneEditorDialog::selectAll);

    autoAction_ = toolbar->addAction(tr("Auto zones"));
    autoAction_->setObjectName(QStringLiteral("autoZones"));
    autoAction_->setToolTip(tr("Recognize buttons on the photo (tools/autoZones.py, Python + opencv). "
                               "Boxes overlapping the zones already on the screen are skipped: delete them first "
                               "(Ctrl+A, then Delete) if you want to replace them. "
                               "Rename the added zones and fix or delete what is not needed."));
    autoAction_->setEnabled(false);
    connect(autoAction_, &QAction::triggered, this, &ZoneEditorDialog::runAutoZones);

    toolbar->addSeparator();

    auto* alignMenu = new QMenu(this);
    auto* alignButton = new QToolButton(this);
    alignButton->setText(tr("Align selected"));
    alignButton->setToolTip(tr("Select several zones with Ctrl. The first selected zone is the reference."));
    alignButton->setMenu(alignMenu);
    alignButton->setPopupMode(QToolButton::InstantPopup);
    toolbar->addWidget(alignButton);
    // Выравнивание
    const QStringList alignLabels = {tr("Left"), tr("Right"), tr("Top"),   tr("Bottom"),
                                     tr("H.Center"), tr("V.Center"), tr("Width"), tr("Height")};
    for (int i = 0; i < 8; ++i) {
        auto* act = alignMenu->addAction(alignLabels[i]);
        act->setEnabled(false);
        connect(act, &QAction::triggered, this, [this, i]() { align(static_cast<Align>(i)); });
        alignActions_.append(act);
    }

    // ── Холст слева ──
    canvas_ = new ZoneCanvas(this);
    auto* fit = toolbar->addAction(tr("Fit to window"));
    connect(fit, &QAction::triggered, canvas_, &ZoneCanvas::fitView);
    for (auto* action : {drawAction_, duplicateAction_, deleteAction_, copyAction_, pasteAction, selectAllAction_}) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        canvas_->addAction(action); // ввод в полях не запускает команды над зонами
    }
    connect(canvas_, &ZoneCanvas::geometryEdited, this, &ZoneEditorDialog::onCanvasGeometry);
    connect(canvas_, &ZoneCanvas::zoneDrawn, this, &ZoneEditorDialog::addZone);
    connect(canvas_, &ZoneCanvas::drawModeChanged, drawAction_, &QAction::setChecked);
    connect(canvas_, &ZoneCanvas::selectionChanged, this, [this]() {
        syncListSelection();
        refreshProperties();
        refreshContext();
    });
    connect(canvas_, &ZoneCanvas::itemActivated, this, [this](int item) {
        if (item == ZoneCanvas::SCREEN_ITEM) {
            // Двойной клик на рамке экрана: переключиться на первый слой экрана.
            if (layers_.size() > 1) showLayer(1);
        } else if (item >= 0) {
            // Двойной клик на зоне: перейти к её id в редакторе свойств.
            idEdit_->setFocus();
            idEdit_->selectAll();
        }
    });
    connect(canvas_, &ZoneCanvas::statusChanged, this, &ZoneEditorDialog::note);

    // ── Правая панель ──
    auto* rightPanel = new QWidget(this);
    rightPanel->setObjectName("zoneProperties");
    rightPanel->setAttribute(Qt::WA_StyledBackground);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(12, 12, 12, 12);

    // Выбор модели и слоя
    auto* headerLayout = new QFormLayout;
    headerLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    modelBox_ = new QComboBox(this);
    const QStringList models = editableModels();
    for (const QString& id : models) {
        modelBox_->addItem(modelName(id), id);
    }
    connect(modelBox_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &ZoneEditorDialog::onModelChosen);
    headerLayout->addRow(tr("Remote:"), modelBox_);

    layerBox_ = new QComboBox(this);
    layerBox_->setObjectName("zoneLayer");
    connect(layerBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0 && index < layers_.size()) showLayer(index);
    });
    headerLayout->addRow(tr("Layer:"), layerBox_);

    screenCheck_ = new QCheckBox(tr("This remote has a screen area"), this);
    screenCheck_->setObjectName("zoneScreenArea");
    screenCheck_->setToolTip(tr("Defines where the screen is drawn on the remote. Disabling removes the screen rectangle from the profile; it does not just hide the outline."));
    connect(screenCheck_, &QCheckBox::toggled, this, &ZoneEditorDialog::setScreenArea);
    headerLayout->addRow({}, screenCheck_);

    rightLayout->addLayout(headerLayout);
    selectionHint_ = new QLabel(tr("Select a button on the photo, or click Draw Zone and drag a rectangle."), this);
    selectionHint_->setWordWrap(true);
    rightLayout->addWidget(selectionHint_);

    // Список зон
    auto* zoneLabel = new QLabel(tr("Zones:"), this);
    rightLayout->addWidget(zoneLabel);

    zoneList_ = new QListWidget(this);
    zoneList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(zoneList_, &QListWidget::itemSelectionChanged, this, &ZoneEditorDialog::onListSelection);
    rightLayout->addWidget(zoneList_);

    // Свойства выделенной зоны
    auto* propsGroup = new QGroupBox(tr("Properties"), this);
    auto* propsLayout = new QFormLayout(propsGroup);

    idEdit_ = new QLineEdit(this);
    connect(idEdit_, &QLineEdit::editingFinished, this, &ZoneEditorDialog::applyId);
    propsLayout->addRow(tr("Button ID:"), idEdit_);
    idEdit_->setToolTip(tr("The ID links this zone to a remote button, for example VolumeUp or Play. Renaming it does not move existing command assignments."));

    kindBox_ = new QComboBox(this);
    connect(kindBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, &ZoneEditorDialog::applyKind);
    propsLayout->addRow(tr("Kind:"), kindBox_);

    auto* rectLayout = new QGridLayout;
    xBox_ = new QDoubleSpinBox(this);
    xBox_->setDecimals(2);
    xBox_->setRange(-99999, 99999);
    xBox_->setSingleStep(1);
    connect(xBox_, &QDoubleSpinBox::editingFinished, this, &ZoneEditorDialog::applyRect);
    rectLayout->addWidget(new QLabel("X:", this), 0, 0);
    rectLayout->addWidget(xBox_, 0, 1);

    yBox_ = new QDoubleSpinBox(this);
    yBox_->setDecimals(2);
    yBox_->setRange(-99999, 99999);
    yBox_->setSingleStep(1);
    connect(yBox_, &QDoubleSpinBox::editingFinished, this, &ZoneEditorDialog::applyRect);
    rectLayout->addWidget(new QLabel("Y:", this), 0, 2);
    rectLayout->addWidget(yBox_, 0, 3);

    wBox_ = new QDoubleSpinBox(this);
    wBox_->setDecimals(2);
    wBox_->setRange(0, 99999);
    wBox_->setSingleStep(1);
    connect(wBox_, &QDoubleSpinBox::editingFinished, this, &ZoneEditorDialog::applyRect);
    rectLayout->addWidget(new QLabel(tr("W:"), this), 1, 0);
    rectLayout->addWidget(wBox_, 1, 1);

    hBox_ = new QDoubleSpinBox(this);
    hBox_->setDecimals(2);
    hBox_->setRange(0, 99999);
    hBox_->setSingleStep(1);
    connect(hBox_, &QDoubleSpinBox::editingFinished, this, &ZoneEditorDialog::applyRect);
    rectLayout->addWidget(new QLabel(tr("H:"), this), 1, 2);
    rectLayout->addWidget(hBox_, 1, 3);

    propsLayout->addRow(tr("Position and size:"), rectLayout);

    radiusBox_ = new QDoubleSpinBox(this);
    radiusBox_->setDecimals(2);
    radiusBox_->setRange(-1, 999);
    radiusBox_->setSingleStep(0.5);
    radiusBox_->setToolTip(tr("0: square corners. Positive: radius in pixels. Negative: fraction of the shorter side (-0.5: circle)."));
    connect(radiusBox_, &QDoubleSpinBox::valueChanged, this, &ZoneEditorDialog::applyRadius);
    propsLayout->addRow(tr("Radius:"), radiusBox_);

    actionPanel_ = new QWidget(this);
    auto* actions = new QFormLayout(actionPanel_);
    actions->setContentsMargins(0, 0, 0, 0);
    actionBox_ = new QComboBox(this);
    actionBox_->setObjectName("zoneAction");
    actionBox_->addItem(tr("Display only"), "none");
    actionBox_->addItem(tr("Assign a command"), "key");
    actionBox_->addItem(tr("Open a screen"), "goto");
    actionBox_->addItem(tr("Previous page"), "previous");
    actionBox_->addItem(tr("Next page"), "next");
    actionBox_->addItem(tr("System button"), "system");
    actions->addRow(tr("On click:"), actionBox_);
    commandBox_ = new QComboBox(this);
    commandBox_->setObjectName("zoneCommand");
    commandBox_->setEditable(true);
    actions->addRow(tr("Button:"), commandBox_);
    targetBox_ = new QComboBox(this);
    actions->addRow(tr("Open screen:"), targetBox_);
    buttonBox_ = new QComboBox(this);
    actions->addRow(tr("Physical button:"), buttonBox_);
    buttonBox_->setToolTip(tr("For a side cell, choose the physical button next to the screen that activates it."));
    propsLayout->addRow(actionPanel_);
    for (auto* box : {actionBox_, commandBox_, targetBox_, buttonBox_})
        connect(box, qOverload<int>(&QComboBox::activated), this, [this] { applyAction(); });
    connect(commandBox_->lineEdit(), &QLineEdit::editingFinished, this, &ZoneEditorDialog::applyAction);

    auto* advanced = new QGroupBox(tr("Advanced properties (JSON)"), this);
    advanced->setCheckable(true);
    advanced->setChecked(false);
    auto* advancedLayout = new QVBoxLayout(advanced);
    fieldsEdit_ = new QLineEdit(this);
    fieldsEdit_->setToolTip(tr("Additional JSON properties. Most button actions can be configured using the fields above."));
    fieldsEdit_->hide();
    advancedLayout->addWidget(fieldsEdit_);
    connect(advanced, &QGroupBox::toggled, fieldsEdit_, &QWidget::setVisible);
    connect(fieldsEdit_, &QLineEdit::editingFinished, this, &ZoneEditorDialog::applyFields);
    propsLayout->addRow(advanced);

    rightLayout->addWidget(propsGroup);

    // Замечания проверки
    checkLabel_ = new QLabel(this);
    rightLayout->addWidget(checkLabel_);

    checkList_ = new QListWidget(this);
    rightLayout->addWidget(checkList_);
    connect(checkList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const int zone = item->data(Qt::UserRole).toInt();
        if (zone >= 0) { canvas_->setSelection({zone}); canvas_->reveal(zone); }
    });

    // ── Низ: статус и кнопки ──
    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);

    noteLabel_ = new QLabel(this);
    noteLabel_->setWordWrap(true);
    noteLabel_->setObjectName("pageMeta");

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    saveButton_ = buttons->addButton(tr("Save changes"), QDialogButtonBox::AcceptRole);
    saveButton_->setAutoDefault(false);
    auto* saveShortcut = new QAction(this);
    saveShortcut->setShortcut(QKeySequence::Save);
    addAction(saveShortcut);
    connect(saveShortcut, &QAction::triggered, this, [this] { if (auto* focus = focusWidget()) focus->clearFocus(); save(); });
    connect(saveButton_, &QPushButton::clicked, this, &ZoneEditorDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &ZoneEditorDialog::reject);

    // ── Сборка ──
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(canvas_);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("zonePropertiesScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(410);
    scroll->setWidget(rightPanel);
    splitter->addWidget(scroll);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(toolbar);
    mainLayout->addWidget(splitter, 1);
    mainLayout->addWidget(statusLabel_);
    mainLayout->addWidget(noteLabel_);
    mainLayout->addWidget(buttons);

    for (auto* box : {modelBox_, layerBox_, kindBox_, actionBox_, commandBox_, targetBox_, buttonBox_}) {
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(12);
    }
    const auto refreshUndo = [this, undoAction, redoAction] {
        undoAction->setText(tr("Undo"));
        redoAction->setText(tr("Redo"));
        undoAction->setToolTip(undo_.undoText());
        redoAction->setToolTip(undo_.redoText());
    };
    connect(&undo_, &QUndoStack::undoTextChanged, this, refreshUndo);
    connect(&undo_, &QUndoStack::redoTextChanged, this, refreshUndo);
    splitter->setSizes({780, 450});
    // Загрузить модель
    const QString initialModel = models.contains(model) ? model : (models.isEmpty() ? QString() : models.first());
    if (!initialModel.isEmpty()) {
        const int idx = modelBox_->findData(initialModel);
        if (idx >= 0) modelBox_->setCurrentIndex(idx);
        loadModel(initialModel);
    }
}

void ZoneEditorDialog::applyState(const QString& layerKey, const State& state) {
    // Найти слой по ключу
    int index = -1;
    for (int i = 0; i < layers_.size(); ++i) {
        if (layers_[i].key == layerKey) {
            index = i;
            break;
        }
    }
    if (index < 0) return;

    // Применить состояние
    layers_[index].current = state;

    // Если это текущий слой — обновить интерфейс
    if (index == current_) {
        refreshAll();
    }
}

void ZoneEditorDialog::reject() {
    if (!confirmDiscard()) return;
    QDialog::reject();
}

// ── Загрузка модели ───────────────────────────────────────────────────────────

QByteArray ZoneEditorDialog::readResource(const QString& relative) {
    QFile file(remoteResources::folder() + QLatin1Char('/') + relative);
    if (file.open(QIODevice::ReadOnly)) return file.readAll();
    return remoteResources::data(QStringLiteral(":/res/") + relative);
}

bool ZoneEditorDialog::loadModel(const QString& model) {
    const QString dir = QStringLiteral("remotes/") + model + QLatin1Char('/');
    const QString profilePath = dir + QStringLiteral("profile.json");
    const QString zonesPath = dir + QStringLiteral("zones.json");
    const QString layoutsPath = QStringLiteral("layouts.json");
    const QByteArray profileText = readResource(profilePath);
    const QByteArray zonesText = readResource(zonesPath);
    const QByteArray layoutsText = readResource(layoutsPath);
    const auto profile = QJsonDocument::fromJson(profileText).object();
    const auto zonesDoc = QJsonDocument::fromJson(zonesText);
    const auto layoutsDoc = QJsonDocument::fromJson(layoutsText);
    QPixmap photo;
    photo.loadFromData(readResource(dir + QStringLiteral("photo.png")));
    if (profile.isEmpty() || !zonesDoc.isArray() || !layoutsDoc.isArray() || photo.isNull()) {
        QMessageBox::warning(this, windowTitle(), tr("Could not read the resources of remote %1.").arg(model));
        return false;
    }

    QList<Layer> layers;
    Layer body;
    body.key = QStringLiteral("body");
    body.caption = tr("Body");
    body.ref = QSizeF(photo.size());
    body.original.zones = zonesDoc.array();
    const QRectF screen = zoneEditing::rectOf(QJsonObject{{QStringLiteral("rect"), profile.value(QStringLiteral("screen"))}});
    if (screen.isValid()) body.original.screen = screen;
    body.originalText = zoneEditing::zoneTexts(zonesText).value_or(QList<QByteArray>());
    if (body.originalText.size() != body.original.zones.size()) body.originalText.clear();
    body.current = body.original;
    layers.append(body);

    QHash<QString, QJsonObject> library;
    QSet<QString> layoutIds;
    for (const auto& item : layoutsDoc.array()) {
        const auto layout = item.toObject();
        const QString id = layout.value(QStringLiteral("id")).toString();
        library.insert(id, layout);
        layoutIds.insert(id);
    }

    // Режимы экрана модели → раскладки; одна раскладка может обслуживать несколько режимов.
    const auto modes = profile.value(QStringLiteral("modes")).toObject();
    QSet<QString> modeNames;
    QStringList order;
    QHash<QString, QStringList> usedBy;
    for (auto it = modes.begin(); it != modes.end(); ++it) {
        modeNames.insert(it.key());
        const QString id = it.value().toString();
        if (!usedBy.contains(id)) order.append(id);
        usedBy[id].append(it.key());
    }
    for (const QString& id : order) {
        if (!library.contains(id)) continue;
        const auto layout = library.value(id);
        const auto ref = layout.value(QStringLiteral("ref")).toArray();
        if (ref.size() != 2) continue;
        Layer l;
        l.key = id;
        l.layout = id;
        l.modes = usedBy.value(id);
        QStringList names;
        for (const auto& mode : l.modes) names.append(RemoteView::screenModeName(mode));
        l.caption = tr("Screen: %1 (%2)").arg(names.join(QStringLiteral(", ")), id);
        l.ref = QSizeF(ref[0].toDouble(), ref[1].toDouble());
        l.original.zones = layout.value(QStringLiteral("zones")).toArray();
        l.originalText = zoneEditing::zoneTexts(layoutsText, id).value_or(QList<QByteArray>());
        if (l.originalText.size() != l.original.zones.size()) l.originalText.clear();
        l.current = l.original;
        layers.append(l);
    }

    auto* completer = new QCompleter(RemoteView::hardKeyIds(model), idEdit_);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    if (auto* old = idEdit_->completer()) old->deleteLater();
    idEdit_->setCompleter(completer);
    model_ = model;
    modelName_ = modelName(model);
    photo_ = photo;
    photoSize_ = QSizeF(photo.size());
    modes_ = modeNames;
    layoutIds_ = layoutIds;
    layers_ = layers;
    current_ = -1;
    files_.clear();
    files_.insert(profilePath, profileText);
    files_.insert(zonesPath, zonesText);
    files_.insert(layoutsPath, layoutsText);
    undo_.clear();
    canvas_->setBackdrop(photo_, photoSize_);

    layerBox_->blockSignals(true);
    layerBox_->clear();
    for (const Layer& l : layers_) layerBox_->addItem(l.caption);
    layerBox_->blockSignals(false);
    previousScreen_.reset();
    showLayer(0);
    return true;
}

QStringList ZoneEditorDialog::layoutUsers(const QString& layout) const {
    QStringList users;
    for (const QString& path : remoteResources::exportFiles({})) {
        if (!path.startsWith(QStringLiteral("remotes/")) || !path.endsWith(QStringLiteral("/profile.json"))) continue;
        const QString id = path.section(QLatin1Char('/'), 1, 1);
        if (id == model_) continue;
        const auto modes = QJsonDocument::fromJson(remoteResources::data(QStringLiteral(":/res/") + path)).object()
                               .value(QStringLiteral("modes")).toObject();
        for (const auto& mode : modes)
            if (mode.toString() == layout) {
                users.append(id);
                break;
            }
    }
    return users;
}

QString ZoneEditorDialog::copyLayoutId(const QString& layout) const {
    const QString base = layout + QLatin1Char('_') + model_;
    return uniqueId(base, layoutIds_);
}

// ── Вспомогательное ───────────────────────────────────────────────────────────

QJsonObject ZoneEditorDialog::zoneAt(int item) const {
    if (!hasLayer() || item < 0 || item >= layer().current.zones.size()) return {};
    return layer().current.zones[item].toObject();
}

QString ZoneEditorDialog::uniqueId(const QString& base, const QSet<QString>& taken) const {
    if (!taken.contains(base)) return base;
    for (int n = 2;; ++n) {
        const QString candidate = base + QString::number(n);
        if (!taken.contains(candidate)) return candidate;
    }
}

QSet<QString> ZoneEditorDialog::idsOf(const QJsonArray& zones) const {
    QSet<QString> ids;
    for (const auto& zone : zones) ids.insert(zone.toObject().value(QStringLiteral("id")).toString());
    return ids;
}

QList<zoneEditing::Issue> ZoneEditorDialog::issuesOf(const Layer& l) const {
    if (l.body()) return zoneEditing::checkBody(l.current.zones, photoSize_, l.current.screen);
    const QSet<QString> bodyIds = layers_.isEmpty() ? QSet<QString>() : idsOf(layers_.first().current.zones);
    return zoneEditing::checkScreen(l.current.zones, l.ref, bodyIds, modes_);
}

QList<QByteArray> ZoneEditorDialog::linesOf(const Layer& l) {
    // Неизменённая зона переносится исходным текстом; остальные записываются заново.
    QList<QByteArray> lines;
    QList<bool> used(l.original.zones.size(), false);
    for (const auto& value : l.current.zones) {
        const auto zone = value.toObject();
        int match = -1;
        if (!l.originalText.isEmpty()) {
            for (int i = 0; i < l.original.zones.size(); ++i) {
                if (!used[i] && l.original.zones[i].toObject() == zone) {
                    match = i;
                    break;
                }
            }
        }
        if (match >= 0) {
            used[match] = true;
            lines.append(l.originalText[match]);
        } else {
            lines.append(zoneEditing::zoneLine(zone));
        }
    }
    return lines;
}

bool ZoneEditorDialog::isDirty() const {
    for (const Layer& l : layers_)
        if (!(l.current == l.original)) return true;
    return false;
}

// ── Показ слоя и обновление виджетов ──────────────────────────────────────────

void ZoneEditorDialog::showLayer(int index, bool keepView) {
    if (index < 0 || index >= layers_.size()) return;
    current_ = index;
    layerBox_->blockSignals(true);
    layerBox_->setCurrentIndex(index);
    layerBox_->blockSignals(false);
    const Layer& l = layer();
    QRectF frame(QPointF(0, 0), photoSize_);
    if (!l.body()) {
        const auto& screen = layers_.first().current.screen;
        if (screen && screen->isValid()) frame = *screen;
    }
    QPixmap backdrop = photo_;
    if (!l.body()) {
        RemoteView preview;
        preview.setRemoteProfile(model_);
        preview.resize(photoSize_.toSize());
        QList<RemoteView::ActivityItem> activities;
        QStringList commands;
        for (int i = 1; i <= 12; ++i) {
            activities.append({tr("Activity %1").arg(i), "VirtualTelevisionN", false});
            commands.append(tr("Command %1").arg(i));
        }
        preview.setActivities(activities);
        preview.setSoftButtons(tr("Commands"), commands);
        if (!l.modes.isEmpty()) preview.selectScreenMode(l.modes.first());
        QPainter painter(&backdrop);
        painter.drawPixmap(frame.toRect(), preview.screenPreview(frame.size().toSize()));
    }
    canvas_->setBackdrop(backdrop, photoSize_);
    canvas_->setSelection({});
    canvas_->setLayer(frame, l.ref, !l.body(), keepView);
    refreshAll();
    canvas_->setFocus();
}

void ZoneEditorDialog::refreshAll() {
    if (!hasLayer()) return;
    const Layer& l = layer();
    canvas_->setZones(l.current.zones, l.body() ? l.current.screen : std::optional<QRectF>());
    autoAction_->setEnabled(l.body());
    screenCheck_->blockSignals(true);
    screenCheck_->setVisible(l.body());
    screenCheck_->setChecked(l.body() && l.current.screen.has_value());
    screenCheck_->blockSignals(false);
    refreshList();
    refreshProperties();
    refreshContext();
    refreshCheck();
    updateTitle();
    saveButton_->setEnabled(isDirty());
    noteLabel_->setText(tr("Arrows move, Ctrl+arrows resize, Shift ×10, Alt disables snapping, left button on empty space pans (click clears selection), Shift/Ctrl+drag selects several zones, Ctrl+wheel zooms."));
}

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
        else if (id.startsWith("Slot")) caption = tr("Tile %1").arg(id.mid(4));
        else if (zone.value("kind") == "ScreenPanel") caption = tr("Screen decoration");
        auto* item = new QListWidgetItem(caption, zoneList_);
        item->setToolTip(id + " · " + zone.value("kind").toString());
        item->setData(Qt::UserRole, i);
    }
    if (l.body() && l.current.screen) {
        auto* item = new QListWidgetItem(tr("Screen area"), zoneList_);
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
    fieldsEdit_->setEnabled(zone);
    radiusBox_->setEnabled(zone);
    for (auto* box : {xBox_, yBox_, wBox_, hBox_}) box->setEnabled(one);

    const QList<QObject*> widgets{idEdit_, kindBox_, fieldsEdit_, radiusBox_, xBox_, yBox_, wBox_, hBox_, actionBox_, commandBox_, targetBox_, buttonBox_};
    for (QObject* widget : widgets) widget->blockSignals(true);

    idEdit_->clear();
    kindBox_->clear();
    fieldsEdit_->clear();
    radiusBox_->setValue(0);
    const QRectF rect = one ? canvas_->itemRect(item) : QRectF();
    xBox_->setValue(rect.x());
    yBox_->setValue(rect.y());
    wBox_->setValue(rect.width());
    hBox_->setValue(rect.height());
    if (zone) {
        const QJsonObject z = zoneAt(item);
        idEdit_->setText(z.value("id").toString());
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
        for (const auto& key : keys) commandBox_->addItem(RemoteView::keyName(key) + " (" + key + ")", key);
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
            buttonBox_->addItem(RemoteView::keyName(id) + " (" + id + ")", id);
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
        QJsonObject extra = z;
        for (const QString& reservedKey : {QStringLiteral("id"), QStringLiteral("rect"), QStringLiteral("radius"), QStringLiteral("kind")})
            extra.remove(reservedKey);
        if (!extra.isEmpty()) fieldsEdit_->setText(QString::fromUtf8(QJsonDocument(extra).toJson(QJsonDocument::Compact)));
    }

    for (QObject* widget : widgets) widget->blockSignals(false);
}

void ZoneEditorDialog::refreshContext() {
    QList<ZoneCanvas::Context> items;
    if (hasLayer() && !layer().body()) {
        QSet<QString> buttons;
        for (int item : canvas_->selection()) {
            if (item < 0) continue;
            const QString button = zoneAt(item).value("button").toString();
            if (!button.isEmpty()) buttons.insert(button);
        }
        for (const auto& value : layers_.first().current.zones) {
            const auto zone = value.toObject();
            const QRectF r = zoneEditing::rectOf(zone);
            if (!r.isValid()) continue;
            ZoneCanvas::Context context;
            context.rect = r;
            context.highlight = buttons.contains(zone.value("id").toString());
            items.append(context);
        }
    }
    canvas_->setContext(items);
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
    checkLabel_->setText(issues.isEmpty() ? tr("Check: no problems")
                                          : tr("Check: %1 errors, %2 warnings").arg(errors).arg(warnings));
}

void ZoneEditorDialog::updateTitle() {
    QString title = tr("Zone Editor — %1").arg(modelName_);
    if (isDirty()) title += QStringLiteral(" *");
    setWindowTitle(title);
}

void ZoneEditorDialog::note(const QString& text) {
    statusLabel_->setText(text);
}

// ── История правок и обработчики ──────────────────────────────────────────────

void ZoneEditorDialog::edit(const QString& text, const State& after, const QString& mergeKey) {
    if (!hasLayer() || after == layer().current) return;
    undo_.push(new StateCommand(this, layer().key, layer().current, after, text, mergeKey));
}

void ZoneEditorDialog::onCanvasGeometry(const QString& text, const QString& mergeKey) {
    if (!hasLayer()) return;
    State after = layer().current;
    after.zones = canvas_->zones();
    if (layer().body()) after.screen = canvas_->screen();
    edit(text, after, mergeKey);
}

void ZoneEditorDialog::onListSelection() {
    QList<int> items;
    for (int row = 0; row < zoneList_->count(); ++row)
        if (zoneList_->item(row)->isSelected()) items.append(zoneList_->item(row)->data(Qt::UserRole).toInt());
    canvas_->setSelection(items);
    refreshProperties();
    refreshContext();
    if (!items.isEmpty()) canvas_->reveal(items.first());
}

void ZoneEditorDialog::onModelChosen(int index) {
    if (index < 0) return;
    const QString id = modelBox_->itemData(index).toString();
    if (id == model_) return;
    if (confirmDiscard() && loadModel(id)) return;
    QSignalBlocker block(modelBox_);
    modelBox_->setCurrentIndex(modelBox_->findData(model_));
}

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
    if (!hasLayer() || !layer().body()) return;
    const QString photoPath = remoteResources::folder() + QStringLiteral("/remotes/") + model_ + QStringLiteral("/photo.png");
    if (!QFileInfo::exists(photoPath)) {
        note(tr("Auto zones needs the photo %1, but the file was not found.").arg(photoPath));
        return;
    }
    const QString script = findAutoZonesScript();
    if (script.isEmpty()) {
        note(tr("The auto-zones script was not found (tools/autoZones.py)."));
        return;
    }
    const auto run = [](const QString& program, const QStringList& arguments, QByteArray& out, QByteArray& err) {
        QProcess process;
        process.start(program, arguments);
        if (!process.waitForStarted(5000)) return false;
        if (!process.waitForFinished(20000)) {
            process.kill();
            return false;
        }
        out = process.readAllStandardOutput();
        err = process.readAllStandardError();
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    note(tr("Recognizing zones on the photo…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QByteArray out, err;
    const bool ok = run(QStringLiteral("python"), {script, photoPath, QStringLiteral("--json")}, out, err)
        || run(QStringLiteral("py"), {QStringLiteral("-3"), script, photoPath, QStringLiteral("--json")}, out, err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        note(tr("Auto zones did not start. Python 3 with the opencv package is required: pip install opencv-python."));
        return;
    }
    const QJsonArray boxes = QJsonDocument::fromJson(out).array();
    State after = layer().current;
    const QJsonArray added = zoneEditing::autoZones(boxes, after.zones, QStringLiteral("Auto"), after.screen);
    if (added.isEmpty()) {
        if (boxes.isEmpty()) {
            note(tr("No zones were recognized on the photo."));
        } else {
            note(tr("All %1 recognized zones overlap the zones already on the screen, so nothing was added. "
                   "Delete the zones you want to replace (Ctrl+A, then Delete) and run Auto zones again.")
                .arg(boxes.size()));
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

void ZoneEditorDialog::applyFields() {
    const auto selection = canvas_->selection();
    if (!hasLayer() || selection.size() != 1 || selection.first() < 0) return;
    const int item = selection.first();
    const QString text = fieldsEdit_->text().trimmed();
    QJsonObject extra;
    if (!text.isEmpty()) {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(text.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            note(tr("Fields must be a JSON object: %1").arg(error.errorString()));
            refreshProperties();
            return;
        }
        extra = doc.object();
    }
    const QStringList reserved{QStringLiteral("id"), QStringLiteral("rect"), QStringLiteral("radius"), QStringLiteral("kind")};
    const QJsonObject zone = zoneAt(item);
    QJsonObject result;
    for (const QString& key : reserved)
        if (zone.contains(key)) result.insert(key, zone.value(key));
    for (auto it = extra.begin(); it != extra.end(); ++it)
        if (!reserved.contains(it.key())) result.insert(it.key(), it.value());
    if (result == zone) return;
    State after = layer().current;
    after.zones[item] = result;
    edit(tr("Edit fields"), after);
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

// ── Сохранение ────────────────────────────────────────────────────────────────

bool ZoneEditorDialog::save() {
    if (actionPanel_->isVisible() && actionBox_->currentData() == "key" && commandBox_->currentText().trimmed().isEmpty()) {
        note(tr("Choose a button for this zone."));
        commandBox_->setFocus();
        return false;
    }
    if (!isDirty()) return true;
    for (const Layer& l : layers_) {
        if (l.current == l.original) continue;
        for (const auto& issue : issuesOf(l)) {
            if (issue.level != zoneEditing::Issue::Level::Error) continue;
            QMessageBox::warning(this, windowTitle(),
                                 tr("Layer %1 has errors, the program would reject these resources: %2").arg(l.caption, issue.text));
            return false;
        }
    }

    const QString dir = QStringLiteral("remotes/") + model_ + QLatin1Char('/');
    const QString profilePath = dir + QStringLiteral("profile.json");
    const QString zonesPath = dir + QStringLiteral("zones.json");
    const QString layoutsPath = QStringLiteral("layouts.json");

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
        // Какие файлы запишет сохранение: остальные внешние правки не пострадают.
        QSet<QString> willWrite;
        for (const Layer& l : layers_) {
            if (l.current == l.original) continue;
            if (l.body()) {
                if (l.current.zones != l.original.zones) willWrite.insert(zonesPath);
                if (l.current.screen != l.original.screen) willWrite.insert(profilePath);
            } else if (l.current.zones != l.original.zones) {
                willWrite.insert(layoutsPath);
                if (!layoutUsers(l.layout).isEmpty()) willWrite.insert(profilePath); // возможна копия раскладки
            }
        }
        QStringList overwritten;
        for (const QString& path : conflicts)
            if (willWrite.contains(path)) overwritten.append(path);
        QMessageBox box(QMessageBox::Warning, windowTitle(),
            tr("These files changed outside the editor."), QMessageBox::Cancel, this);
        box.setDetailedText(conflicts.join('\n'));
        QString info = tr("Save my edits: the editor writes only the parts it changed into the current files. Reload: discard my edits and take the files from disk.");
        if (!overwritten.isEmpty())
            info += QStringLiteral("\n\n") +
                    tr("The editor also changes these externally modified files: %1. Its edits will replace the current content there.").arg(overwritten.join(QStringLiteral(", ")));
        box.setInformativeText(info);
        auto* merge = box.addButton(tr("Save my edits on top"), QMessageBox::AcceptRole);
        auto* reload = box.addButton(tr("Reload and discard my edits"), QMessageBox::DestructiveRole);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == reload) loadModel(model_);
        if (box.clickedButton() != merge) return false;
    }
    QByteArray profile = disk.value(profilePath);
    QByteArray zones = disk.value(zonesPath);
    QByteArray layouts = disk.value(layoutsPath);
    QSet<QString> touched;
    QSet<QString> taken = layoutIds_;
    const auto fail = [this](const QString& file) {
        QMessageBox::warning(this, windowTitle(), tr("Could not update %1: the file structure is not recognised.").arg(file));
        return false;
    };

    for (const Layer& l : layers_) {
        if (l.current == l.original) continue;
        const QList<QByteArray> lines = linesOf(l);
        if (l.body()) {
            if (l.current.zones != l.original.zones) {
                const auto patched = zoneEditing::patchZonesFile(zones, lines);
                if (!patched) return fail(zonesPath);
                zones = *patched;
                touched.insert(zonesPath);
            }
            if (l.current.screen != l.original.screen) {
                const auto patched = zoneEditing::patchScreen(profile, l.current.screen);
                if (!patched) return fail(profilePath);
                profile = *patched;
                touched.insert(profilePath);
            }
            continue;
        }
        if (l.current.zones == l.original.zones) continue;
        bool copy = false;
        const QStringList users = layoutUsers(l.layout);
        if (!users.isEmpty()) {
            QMessageBox ask(QMessageBox::Question, windowTitle(),
                            tr("Layout %1 is also used by: %2.").arg(l.layout, users.join(QStringLiteral(", "))),
                            QMessageBox::Cancel, this);
            ask.setInformativeText(tr("Change it for all of these remotes, or make a copy for %1 only?").arg(modelName_));
            auto* copyButton = ask.addButton(tr("Copy for this remote"), QMessageBox::AcceptRole);
            auto* allButton = ask.addButton(tr("Change for all"), QMessageBox::DestructiveRole);
            ask.setDefaultButton(copyButton);
            ask.exec();
            if (ask.clickedButton() == copyButton) copy = true;
            else if (ask.clickedButton() != allButton) return false;
        }
        if (copy) {
            const QString newId = uniqueId(l.layout + QLatin1Char('_') + model_, taken);
            taken.insert(newId);
            auto patched = zoneEditing::appendLayoutCopy(layouts, l.layout, newId, lines);
            if (!patched) return fail(layoutsPath);
            layouts = *patched;
            for (const QString& mode : l.modes) {
                patched = zoneEditing::patchMode(profile, mode, newId);
                if (!patched) return fail(profilePath);
                profile = *patched;
            }
            touched.insert(layoutsPath);
            touched.insert(profilePath);
        } else {
            const auto patched = zoneEditing::patchLayoutZones(layouts, l.layout, lines);
            if (!patched) return fail(layoutsPath);
            layouts = *patched;
            touched.insert(layoutsPath);
        }
    }

    // Сначала проверить, что получился корректный JSON, и только потом писать файлы.
    const auto contentOf = [&](const QString& path) -> const QByteArray& {
        return path == zonesPath ? zones : path == profilePath ? profile : layouts;
    };
    for (const QString& path : touched) {
        QJsonParseError error;
        QJsonDocument::fromJson(contentOf(path), &error);
        if (error.error != QJsonParseError::NoError) {
            QMessageBox::warning(this, windowTitle(), tr("%1: the result is not valid JSON (%2). Nothing was saved.").arg(path, error.errorString()));
            return false;
        }
    }
    QHash<QString, QByteArray> updates;
    for (const auto& path : touched) updates.insert(path, contentOf(path));
    if (const auto error = zoneEditing::saveFiles(remoteResources::folder(), disk, updates); !error.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), error);
        return false;
    }

    const int index = current_;
    if (loadModel(model_)) showLayer(qMin(index, static_cast<int>(layers_.size()) - 1));
    note(tr("Saved"));
    emit resourcesSaved();
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
