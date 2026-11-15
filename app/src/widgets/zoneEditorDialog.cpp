/**
 * @file zoneEditorDialog.cpp
 * @brief Редактор зон пультов: zones.json корпуса и раскладки экранов в layouts.json.
 */

#include "zoneEditorDialog.h"

#include "../icons.h"
#include "../labels.h"
#include "../modern/remoteView.h"
#include "../remoteResources.h"
#include "../zoneEditing.h"

#include "harmony/core/remoteProfile.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QCompleter>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QColorDialog>
#include <QSpinBox>
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
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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
    // Новая модель могла быть отклонена загрузчиком: имя читаем из её remote.json.
    QFile file(remoteResources::folder() + QStringLiteral("/remotes/") + id + QStringLiteral("/remote.json"));
    if (!file.open(QIODevice::ReadOnly))
        file.setFileName(QStringLiteral(":/res/remotes/") + id + QStringLiteral("/remote.json"));
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

/// Роль экрана, которую можно добавить: роль в remote.json и id экрана по умолчанию.
struct ScreenRole {
    const char* role;
    const char* id;
};

constexpr ScreenRole kScreenRoles[] = {{"activity", "activities"}, {"favorites", "favorites"},
                                       {"devices", "devices"},     {"command", "command"},
                                       {"media", "transport"},     {"numpad", "keypad"}};

/// Заголовок экрана выбранной роли (как в списке экранов).
QString roleTitle(const QString& role) {
    for (const ScreenRole& r : kScreenRoles)
        if (role == QLatin1String(r.role)) return RemoteView::screenModeName(QString::fromLatin1(r.id));
    return role;
}

/// Цвет фона экрана, если в remote.json он не задан.
constexpr const char* kDefaultBackground = "#0b3442";
/// Зазор между зонами нового экрана и от краёв, в единицах `ref`.
constexpr qreal kScreenGap = 4;

/// Прямоугольник зоны номер `index` в раскладке cols×rows (по рядам, слева направо) внутри области `area`.
QRectF gridCell(int index, int cols, int rows, const QRectF& area) {
    const qreal width = (area.width() - kScreenGap * (cols + 1)) / cols;
    const qreal height = (area.height() - kScreenGap * (rows + 1)) / rows;
    return {area.left() + kScreenGap + (index % cols) * (width + kScreenGap),
            area.top() + kScreenGap + (index / cols) * (height + kScreenGap), width, height};
}

/// Зона — плитка экрана (а не заголовок, боковая кнопка или панель).
bool isSlot(const QJsonObject& zone) {
    return zone.value(QStringLiteral("kind")).toString(QStringLiteral("ScreenSlot")) == QLatin1String("ScreenSlot");
}

/**
 * @brief Сетка плиток экрана: колонки и ряды по числу различных x и y левых верхних углов плиток.
 * @return Размер cols × rows, каждое значение от 1 до 12.
 */
QSize slotGrid(const QJsonArray& zones) {
    QSet<int> xs;
    QSet<int> ys;
    for (const auto& value : zones) {
        const QJsonObject zone = value.toObject();
        if (!isSlot(zone)) continue;
        const QRectF r = zoneEditing::rectOf(zone);
        xs.insert(static_cast<int>(std::lround(r.x())));
        ys.insert(static_cast<int>(std::lround(r.y())));
    }
    return {std::clamp(static_cast<int>(xs.size()), 1, 12), std::clamp(static_cast<int>(ys.size()), 1, 12)};
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
                               "(Ctrl+A, then Delete) if you want to replace them. "
                               "Rename the added zones and fix or delete what is not needed."));
    autoAction_->setEnabled(false);
    connect(autoAction_, &QAction::triggered, this, &ZoneEditorDialog::runAutoZones);

    toolbar->addSeparator();

    // Выравнивание выделенных зон: меню, чтобы восемь команд не занимали всю панель.
    auto* alignMenu = new QMenu(this);
    auto* alignButton = new QToolButton(this);
    alignButton->setText(tr("Align selected"));
    alignButton->setToolTip(tr("Select several zones with Ctrl. The first selected zone is the reference."));
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

    // Ручная правка JSON — отдельным блоком под свойствами, а не группой внутри формы.
    auto* advanced = new QGroupBox(tr("Advanced properties (JSON)"), this);
    advanced->setCheckable(true);
    advanced->setChecked(false);
    auto* advancedLayout = new QVBoxLayout(advanced);
    advancedLayout->setContentsMargins(10, 6, 10, 10);
    fieldsEdit_ = new QLineEdit(this);
    fieldsEdit_->setToolTip(tr("Additional JSON properties. Most button actions can be configured using the fields above."));
    fieldsEdit_->hide();
    advancedLayout->addWidget(fieldsEdit_);
    connect(advanced, &QGroupBox::toggled, fieldsEdit_, &QWidget::setVisible);
    connect(fieldsEdit_, &QLineEdit::editingFinished, this, &ZoneEditorDialog::applyFields);
    zonesLayout->addWidget(advanced);

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
    backgroundEdit_ = new QLineEdit(this);
    backgroundEdit_->setPlaceholderText(QString::fromLatin1(kDefaultBackground));
    connect(backgroundEdit_, &QLineEdit::editingFinished, this, &ZoneEditorDialog::applyBackground);
    pickButton_ = new QPushButton(tr("Pick…"), this);
    connect(pickButton_, &QPushButton::clicked, this, [this] {
        const QString current = backgroundEdit_->text().trimmed();
        const QColor color = QColorDialog::getColor(QColor(current.isEmpty() ? QString::fromLatin1(kDefaultBackground) : current), this);
        if (!color.isValid()) return;
        backgroundEdit_->setText(color.name());
        applyBackground();
    });
    auto* colourRow = new QHBoxLayout;
    colourRow->setSpacing(6);
    colourRow->addWidget(backgroundEdit_, 1);
    colourRow->addWidget(pickButton_);
    selectedForm->addRow(tr("Tile colour:"), colourRow);
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
    for (auto* box : {modelBox_, layerBox_, kindBox_, actionBox_, commandBox_, targetBox_, buttonBox_, roleBox_}) {
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
    const QString remotePath = dir + QStringLiteral("remote.json");
    const QByteArray remoteText = readResource(remotePath);
    const auto remote = QJsonDocument::fromJson(remoteText).object();
    const auto hardware = remote.value(QStringLiteral("hardware")).toObject();
    const auto zonesValue = hardware.value(QStringLiteral("zones"));
    QPixmap photo;
    photo.loadFromData(readResource(dir + QStringLiteral("photo.png")));
    if (remote.isEmpty() || !zonesValue.isArray() || photo.isNull()) {
        QString text = tr("Could not read the resources of remote %1.").arg(model);
        if (const QString problem = remoteResources::modelError(model); !problem.isEmpty()) text += QStringLiteral("\n\n") + problem;
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
        // Поле цвета в редакторе задаёт цвет плиток (cell.color), фон экрана остаётся как у модели.
        // Цвет один на пульт: блок "tiles"; у старых файлов — цвет самого экрана.
        l.background = remote.value(QStringLiteral("tiles")).toObject().value(QStringLiteral("color")).toString();
        if (l.background.isEmpty())
            l.background = screenObject.value(QStringLiteral("cell")).toObject().value(QStringLiteral("color")).toString();
        l.ref = QSizeF(ref[0].toDouble(), ref[1].toDouble());
        l.original.zones = screenObject.value(QStringLiteral("cells")).toArray();
        l.originalText = zoneEditing::zoneTexts(remoteText, id).value_or(QList<QByteArray>());
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
    layers_ = layers;
    current_ = -1;
    files_.clear();
    files_.insert(remotePath, remoteText);
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
    // Несохранённые правки зон и фона сразу видны на экране; без правок рисуется готовый вид пульта.
    if (l.draft || !(l.current.zones == l.original.zones))
        preview.overrideLayout(l.layout, l.ref, l.current.zones, l.background, l.draft ? roleTitle(l.role) : QString());
    if (!l.modes.isEmpty()) preview.selectScreenMode(l.modes.first());
    QPixmap screen = preview.screenPreview((size * 2).toSize().expandedTo(QSize(2, 2)));
    if (screen.isNull()) {
        // У пульта нет области экрана: рисовать нечем, остаётся однотонный фон.
        QColor background(l.background);
        if (!background.isValid()) background = QColor(QString::fromLatin1(kDefaultBackground));
        screen = QPixmap(size.toSize().expandedTo(QSize(1, 1)));
        screen.fill(background);
    }
    canvas_->setBackdrop(screen, size);
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
    refreshScreens();
    refreshProperties();
    refreshContext();
    refreshCheck();
    updateTitle();
    saveButton_->setEnabled(isDirty());
    noteLabel_->setFullText(tr("Arrows move, Ctrl+arrows resize, Shift ×10, Alt disables snapping, left button on empty space pans (click clears selection), Shift/Ctrl+drag selects several zones, Ctrl+wheel zooms."));
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

void ZoneEditorDialog::updateTitle() {
    QString title = tr("Zone Editor — %1").arg(modelName_);
    if (isDirty()) title += QStringLiteral(" *");
    setWindowTitle(title);
}

void ZoneEditorDialog::note(const QString& text) {
    statusLabel_->setFullText(text);
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
    const QStringList arguments{script, photoPath, QStringLiteral("--json")};
    const QString python = qEnvironmentVariable("HARMONY_AUTO_ZONES_PYTHON");
    const bool ok = python.isEmpty()
        ? run(QStringLiteral("python"), arguments, out, err)
            || run(QStringLiteral("py"), QStringList{QStringLiteral("-3")} + arguments, out, err)
        : run(python, arguments, out, err);
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
        if (l.draft || l.current == l.original) continue;
        for (const auto& issue : issuesOf(l)) {
            if (issue.level != zoneEditing::Issue::Level::Error) continue;
            QMessageBox::warning(this, windowTitle(),
                                 tr("Layer %1 has errors, the program would reject these resources: %2").arg(l.caption, issue.text));
            return false;
        }
    }

    const QString dir = QStringLiteral("remotes/") + model_ + QLatin1Char('/');
    const QString remotePath = dir + QStringLiteral("remote.json");

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
        if (box.clickedButton() == reload) loadModel(model_);
        if (box.clickedButton() != merge) return false;
    }
    QByteArray remote = disk.value(remotePath);
    const auto fail = [this](const QString& file) {
        QMessageBox::warning(this, windowTitle(), tr("Could not update %1: the file structure is not recognised.").arg(file));
        return false;
    };

    // У каждого пульта свои экраны: правка одного файла не затрагивает другие модели.
    for (const Layer& l : layers_) {
        if (l.draft || l.current == l.original) continue;
        if (l.current.zones != l.original.zones) {
            const auto patched = zoneEditing::patchZones(remote, l.body() ? QString() : l.layout, linesOf(l));
            if (!patched) return fail(remotePath);
            remote = *patched;
            if (!l.body()) {
                // Сетка экрана (cols × rows) хранится в remote.json рядом с зонами и всегда им соответствует.
                const QSize grid = slotGrid(l.current.zones);
                const auto withGrid = zoneEditing::patchGrid(remote, l.layout, grid.width(), grid.height());
                if (!withGrid) return fail(remotePath);
                remote = *withGrid;
            }
        }
        if (l.body() && l.current.screen != l.original.screen) {
            const auto patched = zoneEditing::patchDisplay(remote, l.current.screen);
            if (!patched) return fail(remotePath);
            remote = *patched;
        }
    }

    // Сначала проверить, что получился корректный JSON, и только потом писать файл.
    QJsonParseError parseError;
    QJsonDocument::fromJson(remote, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, windowTitle(), tr("%1: the result is not valid JSON (%2). Nothing was saved.").arg(remotePath, parseError.errorString()));
        return false;
    }
    QHash<QString, QByteArray> updates;
    updates.insert(remotePath, remote);
    if (const auto error = zoneEditing::saveFiles(remoteResources::folder(), disk, updates); !error.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), error);
        return false;
    }

    const int index = current_;
    remoteResources::reload(); // превью слоёв строится из снимка
    if (loadModel(model_)) showLayer(qMin(index, static_cast<int>(layers_.size()) - 1));
    note(tr("Saved"));
    emit resourcesSaved();
    return true;
}

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
        arrangeColsBox_->setValue(grid.width());
        arrangeRowsBox_->setValue(grid.height());
    }
    backgroundEdit_->setEnabled(onScreen);
    pickButton_->setEnabled(onScreen);
    for (QWidget* w : {static_cast<QWidget*>(arrangeColsBox_), static_cast<QWidget*>(arrangeRowsBox_)})
        w->setEnabled(onScreen);
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
    QSignalBlocker block(backgroundEdit_);
    backgroundEdit_->setText(onScreen ? layer().background : QString());
}

bool ZoneEditorDialog::readRemoteJson(QJsonObject& remote, QByteArray& text) {
    const QString remotePath = QStringLiteral("remotes/") + model_ + QStringLiteral("/remote.json");
    QFile in(remoteResources::folder() + QLatin1Char('/') + remotePath);
    if (in.open(QIODevice::ReadOnly)) {
        text = in.readAll();
        remote = QJsonDocument::fromJson(text).object();
    }
    if (remote.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Could not read %1.").arg(remotePath));
        return false;
    }
    return true;
}

bool ZoneEditorDialog::writeRemoteJson(const QByteArray& original, const QJsonObject& remote, const QString& screenId) {
    const QString remotePath = QStringLiteral("remotes/") + model_ + QStringLiteral("/remote.json");
    const QByteArray text = QJsonDocument(remote).toJson(QJsonDocument::Indented);
    if (const auto error = zoneEditing::saveFiles(remoteResources::folder(), {{remotePath, original}},
                                                  {{remotePath, text}});
        !error.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), error);
        return false;
    }
    // Превью экранов строится из снимка: сначала перечитать его, потом слои.
    remoteResources::reload();
    if (!loadModel(model_)) return false;
    for (int i = 1; i < layers_.size(); ++i)
        if (layers_[i].key == screenId) {
            showLayer(i);
            break;
        }
    note(tr("Screens saved"));
    emit resourcesSaved();
    return true;
}

void ZoneEditorDialog::applyGrid() {
    if (!hasLayer() || layer().body()) return;
    const int cols = arrangeColsBox_->value();
    const int rows = arrangeRowsBox_->value();
    const int count = cols * rows;
    State after = layer().current;

    // Область плиток — прямоугольник, занятый текущими плитками (с зазором); служебные зоны не трогаем.
    QRectF area;
    for (const auto& value : after.zones) {
        const QJsonObject zone = value.toObject();
        if (isSlot(zone)) area = area.isNull() ? zoneEditing::rectOf(zone) : area.united(zoneEditing::rectOf(zone));
    }
    area = area.isValid() ? area.adjusted(-kScreenGap, -kScreenGap, kScreenGap, kScreenGap)
                          : QRectF(QPointF(0, 0), layer().ref);

    // Плитки в порядке следования; лишние убираются с конца, недостающие добавляются.
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
        zoneEditing::setRect(zone, gridCell(k, cols, rows, area));
        if (k < tiles.size()) after.zones[tiles[k]] = zone; else after.zones.append(zone);
    }
    edit(tr("Change tile grid"), after, QStringLiteral("grid"));
    refreshScreens();
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
    l.background = QString::fromLatin1(kDefaultBackground);
    // Фон и оформление берутся у экрана этого пульта той же роли, иначе у первого: у всех экранов один вид.
    {
        const auto screens = QJsonDocument::fromJson(readResource(QStringLiteral("remotes/") + model_ + QStringLiteral("/remote.json"))).object()
                                 .value(QStringLiteral("screens")).toArray();
        QJsonObject donor;
        for (const auto& value : screens) {
            const QJsonObject screen = value.toObject();
            if (donor.isEmpty() || screen.value(QStringLiteral("role")).toString() == l.role) donor = screen;
            if (screen.value(QStringLiteral("role")).toString() == l.role) break;
        }
        l.backgroundStyle = donor.value(QStringLiteral("background")).toObject();
        l.cellStyle = donor.value(QStringLiteral("cell")).toObject();
        // Служебные зоны (шапка, часы, нижние кнопки) и размер — как у образца: экран выглядит как на пульте.
        const auto donorRef = donor.value(QStringLiteral("ref")).toArray();
        if (donorRef.size() == 2 && donorRef[0].toDouble() > 0 && donorRef[1].toDouble() > 0)
            size = QSizeF(donorRef[0].toDouble(), donorRef[1].toDouble());
        for (const auto& value : donor.value(QStringLiteral("cells")).toArray())
            if (!isSlot(value.toObject())) l.current.zones.append(value);
        const QString tilesColor = QJsonDocument::fromJson(readResource(QStringLiteral("remotes/") + model_ + QStringLiteral("/remote.json")))
                                       .object().value(QStringLiteral("tiles")).toObject().value(QStringLiteral("color")).toString();
        if (const QString color = !tilesColor.isEmpty() ? tilesColor : l.cellStyle.value(QStringLiteral("color")).toString();
            !color.isEmpty())
            l.background = color;
    }
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
    for (int i = 0; i < 4; ++i) {
        QJsonObject cell;
        cell.insert(QStringLiteral("id"), QStringLiteral("Slot%1").arg(i + 1));
        cell.insert(QStringLiteral("kind"), QStringLiteral("ScreenSlot"));
        zoneEditing::setRect(cell, gridCell(i, 2, 2, area));
        cell.insert(QStringLiteral("radius"), 6);
        l.current.zones.append(cell);
    }
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
    const Layer draft = layer(); // confirmDiscard() может перечитать модель и потерять слои
    if (!confirmDiscard()) return;
    QJsonObject remote;
    QByteArray original;
    if (!readRemoteJson(remote, original)) return;
    QJsonArray screens = remote.value(QStringLiteral("screens")).toArray();

    QString base = draft.role;
    for (const ScreenRole& r : kScreenRoles)
        if (draft.role == QLatin1String(r.role)) base = QString::fromLatin1(r.id);
    QSet<QString> taken;
    for (const auto& value : screens) taken.insert(value.toObject().value(QStringLiteral("id")).toString());
    const QString id = uniqueId(base, taken);

    QJsonObject screen;
    screen.insert(QStringLiteral("id"), id);
    screen.insert(QStringLiteral("role"), draft.role);
    screen.insert(QStringLiteral("ref"), QJsonArray{draft.ref.width(), draft.ref.height()});
    // Сколько плиток рисовать: колонки и ряды; координаты зон лежат в cells.
    const QSize grid = slotGrid(draft.current.zones);
    screen.insert(QStringLiteral("grid"), QJsonObject{{QStringLiteral("cols"), grid.width()},
                                                      {QStringLiteral("rows"), grid.height()}});
    screen.insert(QStringLiteral("touch"), remote.value(QStringLiteral("capabilities")).toObject().value(QStringLiteral("touch")).toBool());
    screen.insert(QStringLiteral("background"), draft.backgroundStyle); // фон как у образца
    QJsonObject cell = draft.cellStyle.isEmpty()
                           ? QJsonObject{{QStringLiteral("color"), QString::fromLatin1(kDefaultBackground)},
                                         {QStringLiteral("border"), QStringLiteral("#ffffff")}}
                           : draft.cellStyle;
    const QString donorBorder = cell.value(QStringLiteral("border")).toString();
    cell.remove(QStringLiteral("color")); // цвет и рамка плиток общие, лежат в "tiles"
    cell.remove(QStringLiteral("border"));
    if (cell.isEmpty()) screen.remove(QStringLiteral("cell"));
    else screen.insert(QStringLiteral("cell"), cell);
    screen.insert(QStringLiteral("cells"), draft.current.zones); // зоны ровно такие, как на холсте
    screens.append(screen);
    remote.insert(QStringLiteral("screens"), screens);
    {
        // Выбранный в черновике цвет становится цветом плиток всего пульта.
        auto tiles = remote.value(QStringLiteral("tiles")).toObject();
        tiles.insert(QStringLiteral("color"), draft.background);
        if (!tiles.contains(QStringLiteral("border")))
            tiles.insert(QStringLiteral("border"), donorBorder.isEmpty() ? QStringLiteral("#ffffff") : donorBorder);
        remote.insert(QStringLiteral("tiles"), tiles);
    }
    writeRemoteJson(original, remote, id);
}

void ZoneEditorDialog::removeScreen() {
    if (!hasLayer() || layer().body()) return;
    const QString id = layer().key;
    if (QMessageBox::question(this, windowTitle(),
                              tr("Remove screen «%1»? Its zones will be deleted.").arg(RemoteView::screenModeName(id))) !=
        QMessageBox::Yes)
        return;
    if (!confirmDiscard()) return;
    QJsonObject remote;
    QByteArray original;
    if (!readRemoteJson(remote, original)) return;
    QJsonArray screens = remote.value(QStringLiteral("screens")).toArray();
    for (int i = 0; i < screens.size(); ++i)
        if (screens[i].toObject().value(QStringLiteral("id")).toString() == id) {
            screens.removeAt(i);
            break;
        }
    remote.insert(QStringLiteral("screens"), screens);
    writeRemoteJson(original, remote, QString());
}

void ZoneEditorDialog::applyBackground() {
    if (!hasLayer() || layer().body()) return;
    const QString id = layer().key;
    const QString text = backgroundEdit_->text().trimmed();
    if (text == layer().background) return;
    if (!text.isEmpty() && !QColor::isValidColorName(text)) {
        note(tr("Not a valid colour: %1").arg(text));
        refreshScreens();
        return;
    }
    if (layer().draft) {
        // Черновик: цвет только меняет вид экрана на холсте, файл не трогаем.
        layer().background = text.isEmpty() ? QString::fromLatin1(kDefaultBackground) : text;
        updateScreenBackdrop();
        refreshScreens();
        return;
    }
    if (!confirmDiscard()) {
        refreshScreens();
        return;
    }
    QJsonObject remote;
    QByteArray original;
    if (!readRemoteJson(remote, original)) return;
    QJsonArray screens = remote.value(QStringLiteral("screens")).toArray();
    // Цвет плиток один на весь пульт: пишем в "tiles", а персональные цвета экранов убираем.
    // Рамка без редактора остаётся в "tiles" как есть; у экранов её тоже убираем.
    auto tilesBorder = remote.value(QStringLiteral("tiles")).toObject().value(QStringLiteral("border")).toString();
    for (int i = 0; i < screens.size(); ++i) {
        auto screen = screens[i].toObject();
        auto cell = screen.value(QStringLiteral("cell")).toObject();
        if (!cell.contains(QStringLiteral("color")) && !cell.contains(QStringLiteral("border"))) continue;
        if (tilesBorder.isEmpty()) tilesBorder = cell.value(QStringLiteral("border")).toString();
        cell.remove(QStringLiteral("color"));
        cell.remove(QStringLiteral("border"));
        screen.insert(QStringLiteral("cell"), cell);
        screens[i] = screen;
    }
    remote.insert(QStringLiteral("screens"), screens);
    auto tiles = remote.value(QStringLiteral("tiles")).toObject();
    tiles.insert(QStringLiteral("color"), text.isEmpty() ? QString::fromLatin1(kDefaultBackground) : text);
    if (!tiles.contains(QStringLiteral("border")) && !tilesBorder.isEmpty())
        tiles.insert(QStringLiteral("border"), tilesBorder);
    remote.insert(QStringLiteral("tiles"), tiles);
    writeRemoteJson(original, remote, id);
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
