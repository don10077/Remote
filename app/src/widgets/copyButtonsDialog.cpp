/**
 * @file copyButtonsDialog.cpp
 * @brief Окно копирования кнопок в другие активности.
 */

#include "copyButtonsDialog.h"

#include "../labels.h"
#include "../modern/activityIcon.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;

/// Заголовок раздела окна.
QLabel* section(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("sectionTitle");
    return label;
}

} // namespace

CopyButtonsDialog::CopyButtonsDialog(const model::RemoteConfig& config, const std::string& activityId,
                                     std::optional<ButtonRef> selected, QWidget* parent)
    : QDialog(parent), activityId_(activityId), selected_(std::move(selected)) {
    const model::Activity* source = config.findActivity(activityId);
    const QString sourceName = source != nullptr ? QString::fromStdString(source->label) : QString();
    setWindowTitle(tr("Copy buttons"));
    setMinimumWidth(560);

    // Сколько кнопок каждого вида и подпись выбранной.
    int& hardCount = hardCount_;
    int& softCount = softCount_;
    QString selectedText;
    if (source != nullptr) {
        for (const auto& group : source->controlGroups) {
            for (const auto& button : group.buttons) {
                const bool hard = group.name == "HardButtons";
                (hard ? hardCount : softCount) += 1;
                if (selected_ && selected_->soft != hard &&
                    (hard ? QString::fromStdString(button.name) == selected_->key
                          : button.position == selected_->position)) {
                    selectedText = (hard ? tr("remote button %1").arg(selected_->key)
                                         : tr("screen button “%1”").arg(labels::button(button))) +
                                   QStringLiteral(" → ") + labels::action(config, button.actionId);
                }
            }
        }
    }
    if (selectedText.isEmpty()) {
        selected_.reset();
    }

    auto* title = new QLabel(tr("Copy buttons of “%1”").arg(sourceName), this);
    title->setObjectName("pageTitle");
    title->setWordWrap(true);
    auto* hint = new QLabel(tr("A button whose device the activity does not turn on would not work there — it is "
                               "skipped."),
                            this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    onlySelected_ = new QRadioButton(tr("Only the selected one: %1").arg(selectedText), this);
    layout_ = new QRadioButton(tr("The layout:"), this);
    hard_ = new QCheckBox(tr("remote buttons (%1)").arg(hardCount), this);
    soft_ = new QCheckBox(tr("screen buttons (%1)").arg(softCount), this);
    hard_->setChecked(hardCount > 0);
    soft_->setChecked(softCount > 0);
    auto* what = new QButtonGroup(this);
    what->addButton(onlySelected_);
    what->addButton(layout_);
    onlySelected_->setVisible(selected_.has_value());
    (selected_ ? onlySelected_ : layout_)->setChecked(true);
    auto* layoutRow = new QHBoxLayout;
    layoutRow->addWidget(layout_);
    layoutRow->addWidget(hard_);
    layoutRow->addWidget(soft_);
    layoutRow->addStretch(1);

    targets_ = new QListWidget(this);
    for (const auto& activity : config.activities) {
        if (activity.isPowerOff() || activity.id == activityId) {
            continue;
        }
        auto* item = new QListWidgetItem(activityIcon(QString::fromStdString(activity.type)),
                                         QString::fromStdString(activity.label), targets_);
        item->setData(kIdRole, QString::fromStdString(activity.id));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    auto* all = new QPushButton(tr("All"), this);
    auto* none = new QPushButton(tr("None"), this);
    auto* marks = new QHBoxLayout;
    marks->addWidget(section(tr("To activities"), this));
    marks->addStretch(1);
    marks->addWidget(all);
    marks->addWidget(none);

    replace_ = new QRadioButton(tr("Replace what is already there"), this);
    keep_ = new QRadioButton(tr("Keep what is there (screen buttons go to free places)"), this);
    replace_->setChecked(true);
    auto* mode = new QButtonGroup(this);
    mode->addButton(replace_);
    mode->addButton(keep_);

    summary_ = new QLabel(this);
    summary_->setObjectName("pageMeta");
    auto* cancel = new QPushButton(tr("Cancel"), this);
    copy_ = new QPushButton(tr("Copy"), this);
    copy_->setObjectName("primaryButton");
    copy_->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(summary_, 1);
    buttons->addWidget(cancel);
    buttons->addWidget(copy_);

    auto* main = new QVBoxLayout(this);
    main->setContentsMargins(24, 20, 24, 18);
    main->setSpacing(10);
    main->addWidget(title);
    main->addWidget(hint);
    main->addWidget(section(tr("What"), this));
    main->addWidget(onlySelected_);
    main->addLayout(layoutRow);
    main->addLayout(marks);
    main->addWidget(targets_, 1);
    main->addWidget(section(tr("If the button or place is taken"), this));
    main->addWidget(replace_);
    main->addWidget(keep_);
    main->addLayout(buttons);

    auto markAll = [this](bool on) {
        for (int row = 0; row < targets_->count(); ++row) {
            targets_->item(row)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        }
    };
    connect(all, &QPushButton::clicked, this, [markAll] { markAll(true); });
    connect(none, &QPushButton::clicked, this, [markAll] { markAll(false); });
    connect(targets_, &QListWidget::itemChanged, this, &CopyButtonsDialog::updateState);
    for (QAbstractButton* button : std::initializer_list<QAbstractButton*>{onlySelected_, layout_, hard_, soft_}) {
        connect(button, &QAbstractButton::toggled, this, &CopyButtonsDialog::updateState);
    }
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(copy_, &QPushButton::clicked, this, &QDialog::accept);
    updateState();
    resize(620, 600);
}

void CopyButtonsDialog::updateState() {
    hard_->setEnabled(layout_->isChecked() && hardCount_ > 0);
    soft_->setEnabled(layout_->isChecked() && softCount_ > 0);
    const auto chosen = copy().toActivities.size();
    const bool something = onlySelected_->isChecked() || hard_->isChecked() || soft_->isChecked();
    summary_->setText(chosen > 0 ? tr("To %1").arg(labels::plural(static_cast<qsizetype>(chosen), "activity",
                                                                   "activities"))
                                 : tr("Mark the activities"));
    copy_->setEnabled(chosen > 0 && something);
}

ButtonCopy CopyButtonsDialog::copy() const {
    ButtonCopy result;
    result.fromActivity = activityId_;
    for (int row = 0; row < targets_->count(); ++row) {
        if (targets_->item(row)->checkState() == Qt::Checked) {
            result.toActivities.push_back(targets_->item(row)->data(kIdRole).toString().toStdString());
        }
    }
    if (onlySelected_->isChecked() && selected_) {
        result.hard = !selected_->soft;
        result.soft = selected_->soft;
        if (selected_->soft) {
            result.softPositions = {selected_->position};
        } else {
            result.hardKeys = {selected_->key.toStdString()};
        }
    } else {
        result.hard = hard_->isChecked();
        result.soft = soft_->isChecked();
    }
    result.replace = replace_->isChecked();
    return result;
}

} // namespace harmony::app
