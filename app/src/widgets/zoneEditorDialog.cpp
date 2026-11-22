/**
 * @file zoneEditorDialog.cpp
 * @brief Редактор зон пультов в remote.json: зоны корпуса и ячейки экранов.
 */

#include "zoneEditorDialog.h"
#include "zoneEditorShared.h"

#include "../icons.h"
#include "../labels.h"
#include "../modern/remoteView.h"
#include "../remoteResources.h"
#include "../zoneEditing.h"

#include "harmony/core/remoteProfile.h"

#include <QAction>
#include <QMenu>
#include <QCompleter>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QVBoxLayout>

#include <algorithm>

namespace harmony::app {

using namespace zoneEditorDetail;

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
    // Новая модель могла быть отклонена загрузчиком: имя читаем из её remote.json.
    const QJsonObject profile = remoteResources::readRemote(id).remote;
    // При несовпадении id и папки показываем id: так ошибка сразу видна.
    if (profile.value(QStringLiteral("id")).toString() == id) {
        const QString name = profile.value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty()) return name;
    }
    return id;
}

/// Заголовок экрана выбранной роли (как в списке экранов).
QString roleTitle(const QString& role) {
    for (const ScreenRole& r : kScreenRoles)
        if (role == QLatin1String(r.role)) return RemoteView::screenModeName(QString::fromLatin1(r.id));
    return role;
}

/// Цвет фона экрана и плиток, если в remote.json он не задан.
constexpr const char* kDefaultBackground = "#0b3442";

/// Цвет рамки плиток, если в remote.json он не задан.
constexpr const char* kDefaultBorder = "#ffffff";

/// Название способа подключения (`connection.driver`).
QString driverTitle(const QString& id) {
    if (id == QLatin1String("usb-net")) return QCoreApplication::translate("ZoneEditorDialog", "USB network (900, 1000, 1100)");
    if (id == QLatin1String("usb-hid")) return QCoreApplication::translate("ZoneEditorDialog", "USB HID (One, 600–700)");
    if (id == QLatin1String("hub")) return QCoreApplication::translate("ZoneEditorDialog", "Harmony Hub over the network");
    if (id == QLatin1String("mh")) return QCoreApplication::translate("ZoneEditorDialog", "MyHarmony over the network");
    if (id == QLatin1String("none")) return QCoreApplication::translate("ZoneEditorDialog", "No connection");
    return id;
}

} // namespace

// ── Подпись с сокращением ─────────────────────────────────────────────────────

ElidedLabel::ElidedLabel(QWidget* parent) : QLabel(parent) {
    // Ignored по ширине: подпись не растягивает окно под длинный текст.
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
}

void ElidedLabel::setFullText(const QString& text) {
    if (full_ == text) return;
    full_ = text;
    setToolTip(text);
    elide();
}

void ElidedLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    elide();
}

void ElidedLabel::elide() {
    constexpr int kMinimumWidth = 32; // при совсем узкой подписи показываем хотя бы многоточие
    QLabel::setText(fontMetrics().elidedText(full_, Qt::ElideRight, std::max(kMinimumWidth, width())));
}

ZoneEditorDialog::ZoneEditorDialog(const QString& model, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Zone Editor"));
    resize(1280, 800);

    // ── Панель инструментов ──
    auto* toolbar = new QToolBar(this);
    toolbar->setIconSize({18, 18});
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);

    auto* undoAction = undo_.createUndoAction(this, tr("Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    undoAction->setIcon(icons::make(icons::Icon::Undo));
    toolbar->addAction(undoAction);

    auto* redoAction = undo_.createRedoAction(this, tr("Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    redoAction->setIcon(icons::make(icons::Icon::Redo));
    toolbar->addAction(redoAction);

    toolbar->addSeparator();

    drawAction_ = toolbar->addAction(tr("Draw Zone"));
    drawAction_->setCheckable(true);
    drawAction_->setShortcut(Qt::Key_D);
    drawAction_->setIcon(icons::make(icons::Icon::Plus));
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

    toolbar->addSeparator();

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
                               "(%1, then %2) if you want to replace them. "
                               "Rename the added zones and fix or delete what is not needed. "
                               "Press the button again to stop recognition.")
                                .arg(labels::shortcut(QKeySequence::SelectAll), labels::shortcut(QKeySequence::Delete)));
    autoAction_->setEnabled(false);
    connect(autoAction_, &QAction::triggered, this, &ZoneEditorDialog::runAutoZones);

    toolbar->addSeparator();

    // Выравнивание выделенных зон: меню, чтобы восемь команд не занимали всю панель.
    auto* alignMenu = new QMenu(this);
    auto* alignButton = new QToolButton(this);
    alignButton->setText(tr("Align selected"));
    alignButton->setToolTip(tr("Select several zones with %1. The first selected zone is the reference.")
                                .arg(labels::modifier(Qt::ControlModifier)));
    alignButton->setMenu(alignMenu);
    alignButton->setPopupMode(QToolButton::InstantPopup);
    toolbar->addWidget(alignButton);
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
    // Распорка прижимает «Вписать в окно» к правому краю панели.
    auto* toolbarSpacer = new QWidget(this);
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(toolbarSpacer);
    auto* fit = toolbar->addAction(tr("Fit to window"));
    connect(fit, &QAction::triggered, canvas_, &ZoneCanvas::fitView);
    for (auto* action : {drawAction_, duplicateAction_, deleteAction_, copyAction_, pasteAction, selectAllAction_}) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        canvas_->addAction(action); // ввод в полях не запускает команды над зонами
    }
    connect(canvas_, &ZoneCanvas::geometryEdited, this, &ZoneEditorDialog::onCanvasGeometry);
    connect(canvas_, &ZoneCanvas::zoneDrawn, this, &ZoneEditorDialog::addZone);
    // Линии шапки и подвала, перетянутые мышью, пишутся так же, как поля «Header»/«Footer».
    connect(canvas_, &ZoneCanvas::backgroundBandChanged, this,
            [this](const QString& zone, qreal height) { setBackgroundZoneHeight(zone, height); });
    connect(canvas_, &ZoneCanvas::drawModeChanged, drawAction_, &QAction::setChecked);
    connect(canvas_, &ZoneCanvas::selectionChanged, this, [this]() {
        syncListSelection();
        refreshProperties();
        refreshContext();
    });
    connect(canvas_, &ZoneCanvas::itemActivated, this, [this](int item) {
        if (item == ZoneCanvas::SCREEN_ITEM) {
            // Двойной клик на рамке экрана корпуса: переключиться на первый слой экрана.
            if (!hasLayer() || !layer().body()) return;
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
    rightLayout->setSpacing(10);

    // Выбор модели и слоя
    auto* headerLayout = new QFormLayout;
    headerLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    headerLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setHorizontalSpacing(8);
    headerLayout->setVerticalSpacing(6);

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
    headerLayout->addRow(tr("Editing:"), layerBox_);
    rightLayout->addLayout(headerLayout);

    // Вкладки: у каждой только свои настройки.
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName("zoneTabs");
    tabs->setDocumentMode(true);
    rightLayout->addWidget(tabs, 1);

    // Вкладка «Зоны»
    auto* zonesPage = new QWidget(this);
    auto* zonesLayout = new QVBoxLayout(zonesPage);
    zonesLayout->setContentsMargins(0, 12, 0, 0);
    zonesLayout->setSpacing(10);
    tabs->addTab(zonesPage, tr("Zones"));

    selectionHint_ = new QLabel(tr("Select a button on the photo, or click Draw Zone and drag a rectangle."), this);
    selectionHint_->setWordWrap(true);
    selectionHint_->setObjectName("pageMeta");
    zonesLayout->addWidget(selectionHint_);

    // Только для корпуса: есть ли у пульта экран. Включение создаёт зону экрана на фото,
    // дальше она правится как любая зона; выключение удаляет её.
    screenCheck_ = new QCheckBox(tr("The remote has a screen"), this);
    screenCheck_->setObjectName("zoneScreenArea");
    screenCheck_->setToolTip(tr("When on, a screen zone is created on the photo: move and resize it like any other zone."));
    connect(screenCheck_, &QCheckBox::toggled, this, &ZoneEditorDialog::setScreenArea);
    zonesLayout->addWidget(screenCheck_);

    zoneList_ = new QListWidget(this);
    zoneList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    zoneList_->setMinimumHeight(90);
    connect(zoneList_, &QListWidget::itemSelectionChanged, this, &ZoneEditorDialog::onListSelection);
    zonesLayout->addWidget(zoneList_, 1);

    // Свойства выделенной зоны
    auto* propsGroup = new QGroupBox(tr("Properties"), this);
    auto* propsLayout = new QFormLayout(propsGroup);
    propsLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    propsLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    propsLayout->setHorizontalSpacing(8);
    propsLayout->setVerticalSpacing(6);

    idEdit_ = new QLineEdit(this);
    connect(idEdit_, &QLineEdit::editingFinished, this, &ZoneEditorDialog::applyId);
    propsLayout->addRow(tr("Button ID:"), idEdit_);
    idEdit_->setToolTip(tr("The ID links this zone to a remote button, for example VolumeUp or Play. Renaming it does not move existing command assignments."));

    kindBox_ = new QComboBox(this);
    connect(kindBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, &ZoneEditorDialog::applyKind);
    propsLayout->addRow(tr("Kind:"), kindBox_);

    // Две пары полей в сетке 2×2: подписи узкие, поля тянутся поровну.
    auto* rectLayout = new QGridLayout;
    rectLayout->setContentsMargins(0, 0, 0, 0);
    rectLayout->setHorizontalSpacing(6);
    rectLayout->setVerticalSpacing(4);
    const auto addRectBox = [&](const QString& caption, qreal minimum, int row, int column) {
        auto* box = new QDoubleSpinBox(this);
        box->setObjectName(QStringLiteral("zoneRect%1").arg(row * 2 + column)); // 0 — X, 1 — Y, 2 — W, 3 — H
        box->setDecimals(2);
        box->setRange(minimum, 99999);
        box->setSingleStep(1);
        box->setMinimumWidth(78);
        box->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(box, &QDoubleSpinBox::editingFinished, this, &ZoneEditorDialog::applyRect);
        auto* label = new QLabel(caption, this);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rectLayout->addWidget(label, row, column * 2);
        rectLayout->addWidget(box, row, column * 2 + 1);
        return box;
    };
    xBox_ = addRectBox(QStringLiteral("X:"), -99999, 0, 0);
    yBox_ = addRectBox(QStringLiteral("Y:"), -99999, 0, 1);
    wBox_ = addRectBox(tr("W:"), 0, 1, 0);
    hBox_ = addRectBox(tr("H:"), 0, 1, 1);
    rectLayout->setColumnStretch(1, 1);
    rectLayout->setColumnStretch(3, 1);

    propsLayout->addRow(tr("Position and size:"), rectLayout);

    radiusBox_ = new QDoubleSpinBox(this);
    radiusBox_->setDecimals(2);
    radiusBox_->setRange(-1, 999);
    radiusBox_->setSingleStep(0.5);
    radiusBox_->setMinimumWidth(78);
    radiusBox_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    radiusBox_->setToolTip(tr("0: square corners. Positive: radius in pixels. Negative: fraction of the shorter side (-0.5: circle)."));
    connect(radiusBox_, &QDoubleSpinBox::valueChanged, this, &ZoneEditorDialog::applyRadius);
    propsLayout->addRow(tr("Radius:"), radiusBox_);

    // Роль подложки ScreenPanel: форма, которой она рисуется на экране без картинок.
    panelRoleBox_ = new QComboBox(this);
    panelRoleBox_->setObjectName("zonePanelRole");
    panelRoleBox_->addItem(tr("Band (may go beyond the screen)"), "band");
    panelRoleBox_->addItem(tr("Side panel"), "side");
    panelRoleBox_->addItem(tr("Ring around buttons"), "oval");
    panelRoleBox_->setToolTip(tr("How the screen panel is drawn when the screen has no pictures; its colour is set on the Screens tab."));
    connect(panelRoleBox_, qOverload<int>(&QComboBox::activated), this, &ZoneEditorDialog::applyPanelRole);
    propsLayout->addRow(tr("Panel shape:"), panelRoleBox_);
    propsForm_ = propsLayout;
    zoneColorEdit_ = new QLineEdit(this);
    zoneColorEdit_->setPlaceholderText(tr("Screen default"));
    zoneColorEdit_->setToolTip(tr("Fill colour of this button or tile, like #2a6fdb. Empty: the colour common to the screen."));
    connect(zoneColorEdit_, &QLineEdit::editingFinished, this, &ZoneEditorDialog::applyZoneColor);
    propsLayout->addRow(tr("Colour:"), zoneColorEdit_);

    actionPanel_ = new QWidget(this);
    auto* actions = new QFormLayout(actionPanel_);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    actions->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    actions->setHorizontalSpacing(8);
    actions->setVerticalSpacing(6);
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

    zonesLayout->addWidget(propsGroup);

    // Вкладка «Экраны»
    auto* screensPage = new QWidget(this);
    auto* screensLayout = new QVBoxLayout(screensPage);
    screensLayout->setContentsMargins(0, 12, 0, 0);
    screensLayout->setSpacing(10);
    tabs->addTab(screensPage, tr("Screens"));
    connect(tabs, &QTabWidget::currentChanged, this, [this, screensPage, tabs](int index) {
        // Вкладка «Экраны» сразу показывает экран на холсте вместо фото пульта.
        if (tabs->widget(index) == screensPage && hasLayer() && layer().body() && layers_.size() > 1) showLayer(1);
    });

    auto* screensHint = new QLabel(tr("Pick a screen: it is shown on the left instead of the remote photo, and its zones can be moved and resized right there. Every zone is independent."), this);
    screensHint->setWordWrap(true);
    screensHint->setObjectName("pageMeta");
    screensLayout->addWidget(screensHint);

    screenList_ = new QListWidget(this);
    screenList_->setObjectName("zoneScreenList");
    screenList_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); // высоту задаёт число экранов, см. refreshScreens()
    connect(screenList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row + 1 < layers_.size() && row + 1 != current_) showLayer(row + 1);
    });
    screensLayout->addWidget(screenList_);

    // Кнопки под списком: новый экран (черновик с живым просмотром) и удаление выбранного.
    addScreenButton_ = new QPushButton(tr("Add screen"), this);
    addScreenButton_->setObjectName("zoneAddScreen");
    addScreenButton_->setIcon(icons::make(icons::Icon::Plus));
    addScreenButton_->setToolTip(tr("Shows a new screen right away; its parameters below change it live. It is written to the remote only after “Create screen”."));
    connect(addScreenButton_, &QPushButton::clicked, this, &ZoneEditorDialog::beginNewScreen);
    removeScreenButton_ = new QPushButton(tr("Remove"), this);
    removeScreenButton_->setObjectName("zoneRemoveScreen");
    connect(removeScreenButton_, &QPushButton::clicked, this, &ZoneEditorDialog::removeScreen);
    auto* listButtons = new QHBoxLayout;
    listButtons->setSpacing(6);
    listButtons->addWidget(addScreenButton_);
    listButtons->addWidget(removeScreenButton_);
    listButtons->addStretch(1);
    screensLayout->addLayout(listButtons);

    // Настройки открытого экрана доступны, только когда на холсте экран, а не корпус.
    auto* selectedGroup = new QGroupBox(tr("Screen parameters"), this);
    auto* selectedForm = new QFormLayout(selectedGroup);
    selectedForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    selectedForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    selectedForm->setHorizontalSpacing(8);
    selectedForm->setVerticalSpacing(6);
    roleBox_ = new QComboBox(this);
    roleBox_->setObjectName("zoneScreenRole");
    // Названия те же, что у экранов в списке (по id экрана по умолчанию).
    for (const ScreenRole& r : kScreenRoles) roleBox_->addItem(RemoteView::screenModeName(QString::fromLatin1(r.id)), QString::fromLatin1(r.role));
    roleBox_->setToolTip(tr("The type of a screen is chosen when it is created."));
    connect(roleBox_, qOverload<int>(&QComboBox::activated), this, [this] {
        if (!hasLayer() || !layer().draft) return;
        layer().role = roleBox_->currentData().toString();
        updateScreenBackdrop(); // заголовок экрана следует за выбранным типом
    });
    selectedForm->addRow(tr("Screen type:"), roleBox_);
    // Плитки экрана: колонки × ряды применяются сразу, число плиток = колонки × ряды.
    arrangeColsBox_ = new QSpinBox(this);
    arrangeRowsBox_ = new QSpinBox(this);
    arrangeColsBox_->setObjectName("zoneColumns");
    arrangeRowsBox_->setObjectName("zoneRows");
    for (auto* box : {arrangeColsBox_, arrangeRowsBox_}) {
        box->setRange(1, 12);
        box->setToolTip(tr("Tiles of this screen are laid out as columns × rows right away; undo restores the old layout."));
        connect(box, qOverload<int>(&QSpinBox::valueChanged), this, &ZoneEditorDialog::applyGrid);
    }
    auto* arrangeRow = new QHBoxLayout;
    arrangeRow->setSpacing(6);
    arrangeRow->addWidget(arrangeColsBox_);
    arrangeRow->addWidget(new QLabel(QStringLiteral("×"), this));
    arrangeRow->addWidget(arrangeRowsBox_);
    arrangeRow->addStretch(1);
    selectedForm->addRow(tr("Columns × rows:"), arrangeRow);
    gapBox_ = new QDoubleSpinBox(this);
    gapBox_->setObjectName("zoneGap");
    gapBox_->setDecimals(1);
    gapBox_->setRange(0, 40);
    gapBox_->setSingleStep(1);
    gapBox_->setToolTip(tr("Space between tiles and around them, in screen units. Tiles are laid out again right away."));
    connect(gapBox_, &QDoubleSpinBox::valueChanged, this, &ZoneEditorDialog::applyGrid); // сразу, правки сливаются в одну
    selectedForm->addRow(tr("Tile gap:"), gapBox_);
    ownAreaCheck_ = new QCheckBox(tr("Own tile area for this screen"), this);
    ownAreaCheck_->setObjectName("zoneOwnArea");
    ownAreaCheck_->setToolTip(tr("Off: the tile area is common to all screens of the remote, and moving it moves the tiles "
                                 "on every screen without an own area. On: the area of this screen only, for example "
                                 "narrower because of side panels."));
    connect(ownAreaCheck_, &QCheckBox::toggled, this, &ZoneEditorDialog::setOwnArea);
    selectedForm->addRow(ownAreaCheck_);

    // Поле цвета: строка ввода и кнопка выбора; правка применяется по завершении ввода.
    // Ввод цвета сразу виден на экране (live), в файл он пишется по завершении ввода (apply).
    const auto colourRow = [this](QLineEdit*& input, const QString& name, const char* fallback,
                                  const std::function<void()>& apply, const std::function<void(const QString&)>& live) {
        input = new QLineEdit(this);
        input->setObjectName(name);
        if (fallback != nullptr) input->setPlaceholderText(QString::fromLatin1(fallback));
        QLineEdit* field = input;
        connect(field, &QLineEdit::editingFinished, this, apply);
        connect(field, &QLineEdit::textChanged, this, live);
        auto* pick = new QPushButton(tr("Pick…"), this);
        connect(pick, &QPushButton::clicked, this, [this, field, fallback, apply] {
            const QString before = field->text();
            const QString current = before.trimmed();
            const QColor start(!current.isEmpty() ? current : fallback != nullptr ? QString::fromLatin1(fallback) : QStringLiteral("#ffffff"));
            QColorDialog dialog(start, this);
            // Цвет под курсором диалога сразу виден на экране; «Отмена» возвращает прежний.
            connect(&dialog, &QColorDialog::currentColorChanged, field, [field](const QColor& c) { field->setText(c.name()); });
            if (dialog.exec() == QDialog::Accepted && dialog.selectedColor().isValid()) {
                field->setText(dialog.selectedColor().name());
                apply();
            } else {
                field->setText(before);
            }
        });
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        row->addWidget(field, 1);
        row->addWidget(pick);
        return std::pair{row, pick};
    };
    // Поле картинки: путь от папки ресурсов и выбор файла.
    const auto imageRow = [this](QLineEdit*& input, const QString& name, const std::function<void()>& apply) {
        input = new QLineEdit(this);
        input->setObjectName(name);
        input->setPlaceholderText(tr("none"));
        QLineEdit* field = input;
        connect(field, &QLineEdit::editingFinished, this, apply);
        auto* browse = new QPushButton(tr("Browse…"), this);
        connect(browse, &QPushButton::clicked, this, [this, field, apply] {
            if (browseImage(field)) apply();
        });
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        row->addWidget(field, 1);
        row->addWidget(browse);
        return std::pair{row, browse};
    };
    const auto screenColour = [this](QLineEdit*& input, const QStringList& keys) {
        return [this, &input, keys] {
            if (const auto value = colourValue(input)) setScreenField(keys, *value);
        };
    };
    const auto screenLive = [this](const QStringList& keys) {
        return [this, keys](const QString& text) { previewScreenField(keys, text); };
    };
    const auto screenImage = [this](QLineEdit*& input, const QStringList& keys) {
        return [this, &input, keys] {
            if (const auto value = imageValue(input)) setScreenField(keys, *value);
        };
    };
    const auto [bgColourRow, bgColourPick] = colourRow(backgroundColorEdit_, "zoneBackgroundColor", kDefaultBackground,
        screenColour(backgroundColorEdit_, {QStringLiteral("background"), QStringLiteral("color")}),
        screenLive({QStringLiteral("background"), QStringLiteral("color")}));
    selectedForm->addRow(tr("Background colour:"), bgColourRow);
    const auto [bgImageRow, bgImageBrowse] = imageRow(backgroundImageEdit_, "zoneBackgroundImage",
        screenImage(backgroundImageEdit_, {QStringLiteral("background"), QStringLiteral("image")}));
    selectedForm->addRow(tr("Background picture:"), bgImageRow);
    // Зоны фона: шапка и подвал (высота + цвет); между ними — тело для плиток (цвет — background.color).
    // Пустой цвет зоны — зона прозрачна.
    const auto heightRow = [this](QDoubleSpinBox*& box, const QString& name, const QString& zone) {
        box = new QDoubleSpinBox(this);
        box->setObjectName(name);
        box->setDecimals(1);
        box->setRange(0, 400);
        box->setSpecialValueText(tr("none"));
        box->setToolTip(tr("Height in screen units. The zone stays in place when tiles scroll."));
        QDoubleSpinBox* field = box;
        connect(field, &QDoubleSpinBox::editingFinished, this, [this, field, zone] {
            setBackgroundZoneHeight(zone, field->value());
        });
        return field;
    };
    const auto zoneColour = [&](QLineEdit*& input, const QString& name, const QString& zone) {
        const QStringList keys{QStringLiteral("background"), zone, QStringLiteral("color")};
        return colourRow(input, name, nullptr, screenColour(input, keys), screenLive(keys));
    };
    const auto zoneRow = [&](QDoubleSpinBox* height, const std::pair<QHBoxLayout*, QPushButton*>& colour) {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        if (height != nullptr) row->addWidget(height);
        row->addLayout(colour.first, 1);
        return row;
    };
    heightRow(headerHeightBox_, "zoneHeaderHeight", QStringLiteral("header"));
    const auto headerColour = zoneColour(headerColorEdit_, "zoneHeaderColor", QStringLiteral("header"));
    headerColorEdit_->setPlaceholderText(tr("transparent"));
    selectedForm->addRow(tr("Header:"), zoneRow(headerHeightBox_, headerColour));
    heightRow(footerHeightBox_, "zoneFooterHeight", QStringLiteral("footer"));
    const auto footerColour = zoneColour(footerColorEdit_, "zoneFooterColor", QStringLiteral("footer"));
    footerColorEdit_->setPlaceholderText(tr("transparent"));
    selectedForm->addRow(tr("Footer:"), zoneRow(footerHeightBox_, footerColour));
    const auto [cellImageRow, cellImageBrowse] = imageRow(cellImageEdit_, "zoneCellImage",
        screenImage(cellImageEdit_, {QStringLiteral("cell"), QStringLiteral("image")}));
    cellImageEdit_->setToolTip(tr("A tile picture replaces the tile colour and border of the remote."));
    selectedForm->addRow(tr("Tile picture:"), cellImageRow);
    const auto [panelColourRow, panelColourPick] = colourRow(panelColorEdit_, "zonePanelColor", nullptr,
        screenColour(panelColorEdit_, {QStringLiteral("panel"), QStringLiteral("color")}),
        screenLive({QStringLiteral("panel"), QStringLiteral("color")}));
    panelColorEdit_->setPlaceholderText(tr("darker than the background"));
    panelColorEdit_->setToolTip(tr("Colour of the screen panels (bands, side panels, rings). Screens built from pictures have them drawn on the pictures."));
    selectedForm->addRow(tr("Panel colour:"), panelColourRow);
    const auto [panelTextRow, panelTextPick] = colourRow(panelTextEdit_, "zonePanelText", "#ffffff",
        screenColour(panelTextEdit_, {QStringLiteral("panel"), QStringLiteral("text")}),
        screenLive({QStringLiteral("panel"), QStringLiteral("text")}));
    panelTextEdit_->setToolTip(tr("Colour of the text drawn on the screen: title, page, clock, battery and the names "
                                  "of the buttons next to the screen. Pick a dark colour for a light header on the background picture."));
    selectedForm->addRow(tr("Text colour:"), panelTextRow);
    centerLastRowCheck_ = new QCheckBox(tr("Centre an incomplete last row of tiles"), this);
    centerLastRowCheck_->setObjectName("zoneCenterLastRow");
    connect(centerLastRowCheck_, &QCheckBox::toggled, this, [this](bool on) {
        setScreenField({QStringLiteral("centerLastRow")}, on ? QJsonValue(true) : QJsonValue(QJsonValue::Undefined));
    });
    selectedForm->addRow(centerLastRowCheck_);
    // Служебные элементы: флажок добавляет ячейку на экран (её место потом правится на холсте), снятый — убирает.
    auto* serviceRow = new QGridLayout;
    serviceRow->setContentsMargins(0, 0, 0, 0);
    serviceRow->setHorizontalSpacing(12);
    for (int i = 0; i < serviceItems().size(); ++i) {
        auto* check = new QCheckBox(tr(serviceItems()[i].title), this);
        check->setObjectName(QStringLiteral("zoneService") + serviceItems()[i].ids.first());
        connect(check, &QCheckBox::toggled, this, [this, i](bool on) { setService(i, on); });
        serviceRow->addWidget(check, i / 2, i % 2);
        serviceChecks_.append(check);
    }
    selectedForm->addRow(tr("Show on the screen:"), serviceRow);
    screenHint_ = new QLabel(this);
    screenHint_->setObjectName("pageMeta");
    screenHint_->setWordWrap(true);
    selectedForm->addRow(screenHint_);
    screenWidgets_ = {arrangeColsBox_, arrangeRowsBox_, gapBox_, backgroundColorEdit_, bgColourPick,
                      headerHeightBox_, headerColorEdit_, headerColour.second,
                      footerHeightBox_, footerColorEdit_, footerColour.second,
                      backgroundImageEdit_, bgImageBrowse, cellImageEdit_, cellImageBrowse, panelColorEdit_,
                      panelColourPick, panelTextEdit_, panelTextPick, centerLastRowCheck_};
    for (QCheckBox* check : serviceChecks_) screenWidgets_.append(check);
    // Черновик нового экрана: записать или отбросить.
    draftPanel_ = new QWidget(this);
    auto* draftLayout = new QHBoxLayout(draftPanel_);
    draftLayout->setContentsMargins(0, 4, 0, 0);
    draftLayout->setSpacing(6);
    auto* createButton = new QPushButton(tr("Create screen"), this);
    createButton->setObjectName("zoneCreateScreen");
    connect(createButton, &QPushButton::clicked, this, &ZoneEditorDialog::createNewScreen);
    auto* cancelButton = new QPushButton(tr("Cancel"), this);
    connect(cancelButton, &QPushButton::clicked, this, &ZoneEditorDialog::cancelNewScreen);
    draftLayout->addWidget(createButton, 1);
    draftLayout->addWidget(cancelButton);
    draftPanel_->hide();
    selectedForm->addRow(draftPanel_);
    screensLayout->addWidget(selectedGroup);
    screensLayout->addStretch(1); // свободное место внизу вкладки, а не в списке экранов

    // Вкладка «Пульт»: поля remote.json, общие для всего пульта. Каждое поле сразу пишется в файл.
    auto* remotePage = new QWidget(this);
    auto* remoteLayout = new QVBoxLayout(remotePage);
    remoteLayout->setContentsMargins(0, 12, 0, 0);
    remoteLayout->setSpacing(10);
    tabs->addTab(remotePage, tr("Remote"));
    auto* remoteForm = new QFormLayout;
    remoteForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    remoteForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    remoteForm->setHorizontalSpacing(8);
    remoteForm->setVerticalSpacing(6);
    remoteLayout->addLayout(remoteForm);
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setObjectName("zoneRemoteName");
    connect(nameEdit_, &QLineEdit::editingFinished, this, [this] {
        const QString name = nameEdit_->text().trimmed();
        if (name.isEmpty()) {
            note(tr("The name of the remote cannot be empty."));
            refreshRemote();
            return;
        }
        setRemoteField({QStringLiteral("name")}, name);
    });
    remoteForm->addRow(tr("Name:"), nameEdit_);
    driverBox_ = new QComboBox(this);
    driverBox_->setObjectName("zoneDriver");
    for (const auto id : harmony::kLinkNames) driverBox_->addItem(driverTitle(fromView(id)), fromView(id));
    driverBox_->setToolTip(tr("How the program talks to the remote."));
    connect(driverBox_, qOverload<int>(&QComboBox::activated), this, [this] {
        setRemoteField({QStringLiteral("connection"), QStringLiteral("driver")}, driverBox_->currentData().toString());
    });
    remoteForm->addRow(tr("Connection:"), driverBox_);
    touchCheck_ = new QCheckBox(tr("Touch screen"), this);
    touchCheck_->setObjectName("zoneTouch");
    touchCheck_->setToolTip(tr("Tiles on the screen react to clicks. Without a touch screen they are chosen with the buttons next to the screen."));
    connect(touchCheck_, &QCheckBox::toggled, this, [this](bool on) {
        setRemoteField({QStringLiteral("capabilities"), QStringLiteral("touch")}, on);
    });
    remoteForm->addRow(touchCheck_);
    const auto remoteColour = [this](QLineEdit*& input, const QStringList& keys) {
        return [this, &input, keys] {
            if (const auto value = colourValue(input)) setRemoteField(keys, *value);
        };
    };
    const auto [tileColourRow, tileColourPick] = colourRow(tileColorEdit_, "zoneTileColor", kDefaultBackground,
        remoteColour(tileColorEdit_, {QStringLiteral("tiles"), QStringLiteral("color")}),
        [this](const QString& text) { previewTiles(QStringLiteral("color"), text); });
    remoteForm->addRow(tr("Tile colour:"), tileColourRow);
    const auto [tileBorderRow, tileBorderPick] = colourRow(tileBorderEdit_, "zoneTileBorder", kDefaultBorder,
        remoteColour(tileBorderEdit_, {QStringLiteral("tiles"), QStringLiteral("border")}),
        [this](const QString& text) { previewTiles(QStringLiteral("border"), text); });
    remoteForm->addRow(tr("Tile border:"), tileBorderRow);
    tilesHint_ = new QLabel(this);
    tilesHint_->setObjectName("pageMeta");
    tilesHint_->setWordWrap(true);
    remoteForm->addRow(tilesHint_);
    Q_UNUSED(tileColourPick)
    Q_UNUSED(tileBorderPick)
    remoteLayout->addStretch(1);

    // Вкладка «Проверка»
    auto* checkPage = new QWidget(this);
    auto* checkLayout = new QVBoxLayout(checkPage);
    checkLayout->setContentsMargins(0, 12, 0, 0);
    checkLayout->setSpacing(10);
    tabs->addTab(checkPage, tr("Check"));

    checkLabel_ = new QLabel(this);
    checkLabel_->setWordWrap(true);
    checkLayout->addWidget(checkLabel_);

    checkList_ = new QListWidget(this);
    checkLayout->addWidget(checkList_, 1);
    connect(checkList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const int zone = item->data(Qt::UserRole).toInt();
        if (zone >= 0) { canvas_->setSelection({zone}); canvas_->reveal(zone); }
    });

    // ── Низ: одна строка состояния и кнопки ──
    // Обе подписи в одной строке и без переноса: высота низа не прыгает при длинном тексте.
    statusLabel_ = new ElidedLabel(this);

    noteLabel_ = new ElidedLabel(this);
    noteLabel_->setObjectName("pageMeta");
    noteLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(12);
    statusRow->addWidget(statusLabel_, 2);
    statusRow->addWidget(noteLabel_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    saveButton_ = buttons->addButton(tr("Save changes"), QDialogButtonBox::AcceptRole);
    saveButton_->setAutoDefault(false);
    saveButton_->setIcon(icons::make(icons::Icon::Save));
    auto* saveShortcut = new QAction(this);
    saveShortcut->setShortcut(QKeySequence::Save);
    addAction(saveShortcut);
    connect(saveShortcut, &QAction::triggered, this, [this] { if (auto* focus = focusWidget()) focus->clearFocus(); save(); });
    connect(saveButton_, &QPushButton::clicked, this, &ZoneEditorDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &ZoneEditorDialog::reject);

    // ── Сборка ──
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(6);
    splitter->addWidget(canvas_);
    // Без прокрутки: панель занимает всю высоту окна, лишнее место остаётся пустым внутри вкладки.
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 8, 12, 12);
    mainLayout->setSpacing(8);
    mainLayout->addWidget(toolbar);
    mainLayout->addWidget(splitter, 1);
    mainLayout->addLayout(statusRow);
    mainLayout->addWidget(buttons);

    // Списки не растягивают панель под длинный текст (ширину задаёт разделитель),
    // но и не схлопываются: Expanding + минимум в 10 символов.
    for (auto* box : {modelBox_, layerBox_, kindBox_, actionBox_, commandBox_, targetBox_, buttonBox_, roleBox_,
                      panelRoleBox_, driverBox_}) {
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(10);
        box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    const auto refreshUndo = [this, undoAction, redoAction] {
        undoAction->setText(tr("Undo"));
        redoAction->setText(tr("Redo"));
        undoAction->setToolTip(undo_.undoText());
        redoAction->setToolTip(undo_.redoText());
    };
    connect(&undo_, &QUndoStack::undoTextChanged, this, refreshUndo);
    connect(&undo_, &QUndoStack::redoTextChanged, this, refreshUndo);
    // Узкие окна: подписи форм переносятся над полями, а не распирают панель.
    for (auto* form : rightPanel->findChildren<QFormLayout*>()) form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    // Правая панель не уже своего содержимого: ничего не обрезается по ширине.
    rightPanel->setMinimumWidth(std::max(380, rightPanel->minimumSizeHint().width()));
    splitter->setSizes({780, std::max(450, rightPanel->minimumWidth())});
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
        updateScreenBackdrop(); // экран на холсте отражает правки зон
        refreshAll();
    }
}

ZoneEditorDialog::~ZoneEditorDialog() {
    stopAutoZones();
}

void ZoneEditorDialog::reject() {
    if (!confirmDiscard()) return;
    stopAutoZones(); // окно закрывается: результат распознавания уже некуда добавить
    QDialog::reject();
}

// ── Загрузка модели ───────────────────────────────────────────────────────────

QByteArray ZoneEditorDialog::readResource(const QString& relative) {
    QFile file(remoteResources::folder() + QLatin1Char('/') + relative);
    if (file.open(QIODevice::ReadOnly)) return file.readAll();
    return remoteResources::data(QStringLiteral(":/res/") + relative);
}

QString ZoneEditorDialog::remotePath() const {
    return QStringLiteral("remotes/") + model_ + QStringLiteral("/remote.json");
}

bool ZoneEditorDialog::loadModel(const QString& model) {
    // Папка с одним photo.png: создаём заготовку remote.json, иначе запись увидит «внешнее изменение».
    if (const QString stub = remoteResources::createStub(model); !stub.isEmpty()) note(stub);
    // Один разбор файла: описание и текст зон берутся из того же текста, что сверяется при записи.
    const remoteResources::RemoteFile file = remoteResources::readRemote(model);
    const QJsonObject& remote = file.remote;
    const QByteArray& remoteText = file.current;
    const auto hardware = remote.value(QStringLiteral("hardware")).toObject();
    const auto zonesValue = hardware.value(QStringLiteral("zones"));
    QPixmap photo;
    photo.loadFromData(readResource(QStringLiteral("remotes/") + model + QStringLiteral("/photo.png")));
    if (remote.isEmpty() || !zonesValue.isArray() || photo.isNull()) {
        QString text = tr("Could not read the resources of remote %1.").arg(model);
        if (!file.error.isEmpty()) text += QStringLiteral("\n\n") + file.error;
        else if (const QString problem = remoteResources::modelError(model); !problem.isEmpty()) text += QStringLiteral("\n\n") + problem;
        QMessageBox::warning(this, windowTitle(), text);
        return false;
    }

    QList<Layer> layers;
    Layer body;
    body.key = QStringLiteral("body");
    body.caption = tr("Body");
    body.ref = QSizeF(photo.size());
    body.original.zones = zonesValue.toArray();
    const QRectF screen = zoneEditing::rectOf(
        QJsonObject{{QStringLiteral("rect"), remote.value(QStringLiteral("display")).toObject().value(QStringLiteral("rect"))}});
    if (screen.isValid()) body.original.screen = screen;
    body.originalText = zoneEditing::zoneTexts(remoteText).value_or(QList<QByteArray>());
    if (body.originalText.size() != body.original.zones.size()) body.originalText.clear();
    body.current = body.original;
    layers.append(body);

    // Экраны модели: у каждого свои ячейки `cells` в единицах `ref`.
    QSet<QString> modeNames;
    for (const auto& value : remote.value(QStringLiteral("screens")).toArray()) {
        const auto screenObject = value.toObject();
        const QString id = screenObject.value(QStringLiteral("id")).toString();
        const auto ref = screenObject.value(QStringLiteral("ref")).toArray();
        if (id.isEmpty() || ref.size() != 2) continue;
        modeNames.insert(id);
        Layer l;
        l.key = id;
        l.layout = id;
        l.modes = {id};
        l.caption = tr("Screen: %1").arg(RemoteView::screenModeName(id));
        l.role = screenObject.value(QStringLiteral("role")).toString();
        l.screen = screenObject;
        l.screen.remove(QStringLiteral("cells"));
        l.ref = QSizeF(ref[0].toDouble(), ref[1].toDouble());
        l.original.zones = screenObject.value(QStringLiteral("cells")).toArray();
        const QJsonObject gridObject = screenObject.value(QStringLiteral("grid")).toObject();
        l.original.gap = gridObject.value(QStringLiteral("gap")).toDouble(zoneEditing::kDefaultGap);
        // Зона плиток: из grid.area, а без неё — по самим плиткам (в файл тогда пишется, только если её изменить).
        const QRectF area = zoneEditing::rectOf(QJsonObject{{QStringLiteral("rect"), gridObject.value(QStringLiteral("area"))}});
        l.original.ownArea = area.isValid();
        l.original.screen = area.isValid() ? std::optional<QRectF>(area) : tilesBounds(l.original.zones, l.original.gap);
        l.originalText = zoneEditing::zoneTexts(remoteText, id).value_or(QList<QByteArray>());
        if (l.originalText.size() != l.original.zones.size()) l.originalText.clear();
        l.current = l.original;
        layers.append(l);
    }

    // Общая зона плиток: tiles.area, а без неё — самая частая зона экранов без своей (у 1000/1100 — экранов
    // активностей); экраны с другой зоной (команды с боковыми панелями) считаются экранами со своей зоной.
    const QRectF tilesArea = zoneEditing::rectOf(
        QJsonObject{{QStringLiteral("rect"), remote.value(QStringLiteral("tiles")).toObject().value(QStringLiteral("area"))}});
    std::optional<QRectF> common = tilesArea.isValid() ? std::optional<QRectF>(tilesArea) : std::nullopt;
    if (!common) {
        QList<QRectF> seen;
        QList<int> counts;
        for (const Layer& l : layers) {
            if (l.body() || l.original.ownArea || !l.original.screen) continue;
            const qsizetype at = seen.indexOf(*l.original.screen);
            if (at < 0) {
                seen.append(*l.original.screen);
                counts.append(1);
            } else {
                ++counts[at];
            }
        }
        for (qsizetype i = 0; i < seen.size(); ++i)
            if (!common || counts[i] > counts[seen.indexOf(*common)]) common = seen[i];
    }
    for (Layer& l : layers) {
        if (l.body() || l.original.ownArea || !l.original.screen || !common) continue;
        if (tilesArea.isValid()) l.original.screen = common; // зона из файла
        else if (*l.original.screen != *common) l.original.ownArea = true;
        l.current = l.original;
    }
    // Зона плиток не больше остатка между шапкой и подвалом: заходящая под них обрезается.
    for (Layer& l : layers) {
        if (l.body() || !l.original.screen) continue;
        const auto zones = remoteResources::screenZones(l.screen);
        if (!zones.any) continue;
        const QRectF inside = l.original.screen->intersected(zones.body);
        if (inside.isValid() && !inside.isEmpty()) l.original.screen = inside;
        else l.original.screen = zones.body;
        l.current = l.original;
    }

    auto* completer = new QCompleter(RemoteView::hardKeyIds(model), idEdit_);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    if (auto* old = idEdit_->completer()) old->deleteLater();
    idEdit_->setCompleter(completer);
    if (model != model_) stopAutoZones(); // распознавалось фото прежней модели
    model_ = model;
    remote_ = remote;
    liveScreen_.reset();
    liveTiles_.reset();
    modelName_ = remote.value(QStringLiteral("name")).toString().trimmed();
    if (modelName_.isEmpty()) modelName_ = model;
    photo_ = photo;
    photoSize_ = QSizeF(photo.size());
    modes_ = modeNames;
    layers_ = layers;
    current_ = -1;
    files_.clear();
    files_.insert(file.path, file.text); // сверяется с диском при записи: исходный текст, а не переведённый
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
        if (!l.draft && !(l.current == l.original)) return true;
    return false;
}

// ── Показ слоя и обновление виджетов ──────────────────────────────────────────

void ZoneEditorDialog::showLayer(int index, bool keepView) {
    if (index < 0 || index >= layers_.size()) return;
    liveScreen_.reset(); // недописанный цвет относится к прежнему экрану
    current_ = index;
    layerBox_->blockSignals(true);
    layerBox_->setCurrentIndex(index);
    layerBox_->blockSignals(false);
    const Layer& l = layer();
    if (l.body()) {
        // Корпус: фото пульта, зоны в его пикселях.
        canvas_->setBackdrop(photo_, photoSize_);
        canvas_->setSelection({});
        canvas_->setLayer(QRectF(QPointF(0, 0), photoSize_), l.ref, false, keepView);
    } else {
        // Экран: на месте фото — реальный вид экрана, зоны рисуются поверх и правятся прямо на нём.
        const QSizeF size = l.ref.isValid() ? l.ref : QSizeF(320, 240);
        updateScreenBackdrop();
        canvas_->setSelection({});
        canvas_->setLayer(QRectF(QPointF(0, 0), size), l.ref, false, keepView);
    }
    refreshAll();
    canvas_->setFocus();
}

void ZoneEditorDialog::updateScreenBackdrop() {
    if (!hasLayer() || layer().body()) return;
    const Layer& l = layer();
    const QSizeF size = l.ref.isValid() ? l.ref : QSizeF(320, 240);
    // Настоящая отрисовка экрана тем же рендерером, что и в программе: фон, панели, оформление ячеек.
    // Картинка вдвое больше единиц слоя, чтобы при увеличении не была мутной.
    RemoteView preview;
    preview.setRemoteProfile(model_);
    preview.resize(photoSize_.toSize());
    QList<RemoteView::ActivityItem> activities;
    QStringList commands;
    QStringList devices;
    for (int i = 1; i <= 12; ++i) {
        activities.append({tr("Activity %1").arg(i), "VirtualTelevisionN", false});
        commands.append(tr("Command %1").arg(i));
        devices.append(tr("Device %1").arg(i));
    }
    preview.setActivities(activities);
    preview.setDeviceNames(devices);
    preview.setSoftButtons(tr("Commands"), commands);
    // Несохранённые правки зон сразу видны на экране; черновик рисуется целиком из своего объекта экрана.
    if (l.draft || liveScreen_ || !(l.current.zones == l.original.zones)) {
        QJsonObject screen = liveScreen_.value_or(l.screen);
        screen.insert(QStringLiteral("id"), l.layout);
        screen.insert(QStringLiteral("ref"), QJsonArray{l.ref.width(), l.ref.height()});
        screen.insert(QStringLiteral("cells"), l.current.zones);
        // Заголовок в шапке нужен только черновику экрана в редакторе, в remote.json его нет.
        if (l.draft) screen.insert(QStringLiteral("title"), roleTitle(l.role));
        preview.overrideScreen(screen);
    }
    if (liveTiles_) preview.overrideTiles(*liveTiles_);
    if (!l.modes.isEmpty()) preview.selectScreenMode(l.modes.first());
    QPixmap screen = preview.screenPreview((size * 2).toSize().expandedTo(QSize(2, 2)));
    if (screen.isNull()) {
        // У пульта нет области экрана: рисовать нечем, остаётся однотонный фон.
        QColor background(l.screen.value(QStringLiteral("background")).toObject().value(QStringLiteral("color")).toString());
        if (!background.isValid()) background = QColor(QString::fromLatin1(kDefaultBackground));
        screen = QPixmap(size.toSize().expandedTo(QSize(1, 1)));
        screen.fill(background);
    }
    canvas_->setBackdrop(screen, size);
}

void ZoneEditorDialog::refreshAll() {
    if (!hasLayer()) return;
    const Layer& l = layer();
    // Рамка на корпусе — область экрана на фото, на экране — зона плиток.
    canvas_->setFrameCaption(l.body() ? QString() : l.current.ownArea ? tr("Tile area of this screen") : tr("Tile area of all screens"));
    canvas_->setZones(l.current.zones, l.current.screen);
    autoAction_->setEnabled(l.body() || autoProcess_); // идущее распознавание можно отменить с любого слоя
    screenCheck_->blockSignals(true);
    screenCheck_->setVisible(l.body());
    screenCheck_->setChecked(l.body() && l.current.screen.has_value());
    screenCheck_->blockSignals(false);
    refreshList();
    refreshScreens();
    refreshProperties();
    refreshContext();
    refreshCheck();
    refreshRemote();
    updateTitle();
    saveButton_->setEnabled(isDirty());
    const QString ctrl = labels::modifier(Qt::ControlModifier);
    const QString shift = labels::modifier(Qt::ShiftModifier);
    noteLabel_->setFullText(tr("Arrows move, %1+arrows resize, %2 ×10, %3 disables snapping, left button on empty space "
                               "pans (click clears selection), %2/%1+drag selects several zones, %1+wheel zooms.")
                                .arg(ctrl, shift, labels::modifier(Qt::AltModifier)));
}

void ZoneEditorDialog::updateTitle() {
    QString title = tr("Zone Editor — %1").arg(modelName_);
    if (isDirty()) title += QStringLiteral(" *");
    setWindowTitle(title);
}

void ZoneEditorDialog::note(const QString& text) {
    statusLabel_->setFullText(text);
}

// ── История правок и обработчики ──────────────────────────────────────────────

ZoneEditorDialog::State ZoneEditorDialog::followArea(const Layer& l, State next) const {
    // Зона плиток сдвинута или растянута (мышью, полями, выравниванием): плитки сразу ложатся в неё той же сеткой.
    const bool hasTiles = std::any_of(next.zones.begin(), next.zones.end(),
                                      [](const QJsonValue& value) { return isSlot(value.toObject()); });
    if (!l.body() && hasTiles && next.screen && next.screen->isValid() && next.screen != l.current.screen) {
        const QSize grid = slotGrid(next.zones);
        next = laidOut(next, grid.width(), grid.height(), next.gap, *next.screen);
    }
    return next;
}

void ZoneEditorDialog::edit(const QString& text, const State& after, const QString& mergeKey) {
    if (!hasLayer() || after == layer().current) return;
    const Layer& l = layer();
    const State next = followArea(l, after);
    // Общая зона плиток пульта сдвинута: та же зона и раскладка у всех экранов без своей зоны, одним шагом отмены.
    const bool common = !l.body() && !l.current.ownArea && !next.ownArea && next.screen && next.screen != l.current.screen;
    if (!common) {
        undo_.push(new StateCommand(this, l.key, l.current, next, text, mergeKey));
        return;
    }
    undo_.beginMacro(text);
    for (int i = 0; i < layers_.size(); ++i) {
        const Layer& other = layers_[i];
        if (i == current_ || other.body() || other.current.ownArea || !other.current.screen) continue;
        State shifted = other.current;
        shifted.screen = next.screen;
        undo_.push(new StateCommand(this, other.key, other.current, followArea(other, shifted), text));
    }
    undo_.push(new StateCommand(this, l.key, l.current, next, text)); // последним: открытый экран обновляет окно
    undo_.endMacro();
}

void ZoneEditorDialog::onCanvasGeometry(const QString& text, const QString& mergeKey) {
    if (!hasLayer()) return;
    State after = layer().current;
    after.zones = canvas_->zones();
    after.screen = canvas_->screen();
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

} // namespace harmony::app
