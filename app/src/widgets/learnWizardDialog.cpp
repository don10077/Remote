/**
 * @file learnWizardDialog.cpp
 * @brief Мастер обучения команд: одна или много подряд.
 */

#include "learnWizardDialog.h"

#include "../appSettings.h"
#include "../labels.h"
#include "../remoteSession.h"


#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

constexpr int kNameRole = Qt::UserRole + 1;

/// Заголовок страницы.
QLabel* titleLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("pageTitle");
    label->setWordWrap(true);
    return label;
}

/// Пояснение.
QLabel* hintLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("pageMeta");
    label->setWordWrap(true);
    return label;
}

} // namespace

const QStringList& standardCommandNames() {
    static const QStringList names = {
        "PowerToggle", "PowerOn", "PowerOff", "VolumeUp", "VolumeDown", "Mute", "ChannelUp", "ChannelDown",
        "DirectionUp", "DirectionDown", "DirectionLeft", "DirectionRight", "Select", "Menu", "Exit", "Back",
        "Info", "Guide", "Home", "Play", "Pause", "Stop", "Rewind", "FastForward", "SkipBack", "SkipForward",
        "Record", "InputNext", "Red", "Green", "Yellow", "Blue", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    return names;
}

LearnWizardDialog::LearnWizardDialog(RemoteSession* session, const model::Device& device, QWidget* parent)
    : QDialog(parent), session_(session) {
    setWindowTitle(tr("Learn commands — “%1”").arg(QString::fromStdString(device.displayName())));
    setMinimumSize(560, 560);
    for (const auto& command : device.commands) {
        existing_ << QString::fromStdString(command.name);
    }
    pages_ = new QStackedWidget(this);
    buildChoosePage();
    buildLearnPage();
    buildDonePage();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->addWidget(pages_);

    // Ход и итог обучения приходят от сеанса с пультом.
    connect(session_, &RemoteSession::progress, this, [this](transport::Stage stage, qint64 done, qint64 total) {
        if (listening_ && after_ == After::Nothing && stage == transport::Stage::Learn) {
            status_->setText((first_ ? tr("Press the same button once more to check (%1 s left)")
                                     : tr("Press the button on the original remote (%1 s left)"))
                                 .arg(total - done));
        }
    });
    connect(session_, &RemoteSession::learnFinished, this, [this](const ir::IrSignal& signal) {
        if (!listening_) {
            return;
        }
        listening_ = false;
        if (after_ != After::Nothing) {
            perform(std::exchange(after_, After::Nothing)); // сигнал пришёл, когда уже просили прервать
            return;
        }
        received(signal);
    });
    connect(session_, &RemoteSession::failed, this, [this](const QString& message, bool) {
        if (!listening_) {
            return;
        }
        listening_ = false;
        if (after_ != After::Nothing) {
            perform(std::exchange(after_, After::Nothing));
            return;
        }
        // Не дождались нажатия или пульт не ответил: решает пользователь.
        status_->setText(tr("%1 — press “Repeat” or “Skip”.").arg(message));
        updateLearnButtons();
    });
    resize(600, 620);
}

LearnWizardDialog::~LearnWizardDialog() {
    alive_ = false;
    if (listening_) {
        session_->cancel();
    }
}

void LearnWizardDialog::reject() {
    alive_ = false;
    if (listening_) {
        session_->cancel();
    }
    QDialog::reject();
}

// --- Выбор команд ----------------------------------------------------------------

void LearnWizardDialog::buildChoosePage() {
    auto* page = new QWidget(pages_);
    list_ = new QListWidget(page);
    // Привычные (по ним пульт сам назначит кнопки и питание), потом свои команды устройства.
    QStringList names = standardCommandNames();
    for (const QString& name : existing_) {
        if (!names.contains(name)) {
            names << name;
        }
    }
    for (const QString& name : names) {
        auto* item = new QListWidgetItem(existing_.contains(name) ? tr("%1  (exists — replace)").arg(name) : name,
                                         list_);
        item->setData(kNameRole, name);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    }

    auto* missing = new QPushButton(tr("Missing"), page);
    missing->setToolTip(tr("Mark the usual commands the device does not have yet"));
    auto* all = new QPushButton(tr("All"), page);
    auto* none = new QPushButton(tr("None"), page);
    auto* presets = new QHBoxLayout;
    presets->addWidget(new QLabel(tr("Mark:"), page));
    presets->addWidget(missing);
    presets->addWidget(all);
    presets->addWidget(none);
    presets->addStretch(1);

    own_ = new QLineEdit(page);
    own_->setPlaceholderText(tr("Own command, e.g. Netflix"));
    auto* add = new QPushButton(tr("Add"), page);
    auto* ownRow = new QHBoxLayout;
    ownRow->addWidget(own_, 1);
    ownRow->addWidget(add);

    verify_ = new QCheckBox(tr("Check: press each button twice"), page);
    verify_->setToolTip(tr("A command is saved only if both presses give the same signal"));
    verify_->setChecked(true);
    chosen_ = hintLabel({}, page);
    start_ = new QPushButton(tr("Start"), page);
    start_->setObjectName("primaryButton");
    start_->setDefault(true);
    auto* close = new QPushButton(tr("Close"), page);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(chosen_, 1);
    buttons->addWidget(close);
    buttons->addWidget(start_);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(titleLabel(tr("Which commands to learn"), page));
    layout->addWidget(hintLabel(tr("The wizard listens for each command in turn: press the button on the original "
                                   "remote, and it moves on to the next one by itself. Point the original remote "
                                   "at the top edge of the Harmony from 5–10 cm; the Harmony must be connected via "
                                   "USB."),
                                page));
    layout->addLayout(presets);
    layout->addWidget(list_, 1);
    layout->addLayout(ownRow);
    layout->addWidget(verify_);
    layout->addLayout(buttons);
    pages_->addWidget(page);

    connect(missing, &QPushButton::clicked, this, [this] { mark(true, false); });
    connect(all, &QPushButton::clicked, this, [this] { mark(false, true); });
    connect(none, &QPushButton::clicked, this, [this] { mark(false, false); });
    auto addOwn = [this] {
        const QString name = own_->text().trimmed();
        if (name.isEmpty()) {
            return;
        }
        for (int row = 0; row < list_->count(); ++row) {
            if (list_->item(row)->data(kNameRole).toString() == name) {
                list_->item(row)->setCheckState(Qt::Checked);
                list_->scrollToItem(list_->item(row));
                own_->clear();
                return;
            }
        }
        auto* item = new QListWidgetItem(name, list_);
        item->setData(kNameRole, name);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        list_->scrollToItem(item);
        own_->clear();
    };
    connect(add, &QPushButton::clicked, this, addOwn);
    connect(own_, &QLineEdit::returnPressed, this, addOwn);
    connect(list_, &QListWidget::itemChanged, this, &LearnWizardDialog::updateChooseState);
    connect(close, &QPushButton::clicked, this, &LearnWizardDialog::reject);
    connect(start_, &QPushButton::clicked, this, &LearnWizardDialog::start);
    mark(true, false);
}

void LearnWizardDialog::mark(bool missingOnly, bool all) {
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem* item = list_->item(row);
        const bool exists = existing_.contains(item->data(kNameRole).toString());
        item->setCheckState(all || (missingOnly && !exists) ? Qt::Checked : Qt::Unchecked);
    }
    updateChooseState();
}

void LearnWizardDialog::markOnly(const QStringList& names) {
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem* item = list_->item(row);
        item->setCheckState(names.contains(item->data(kNameRole).toString()) ? Qt::Checked : Qt::Unchecked);
    }
    updateChooseState();
}

void LearnWizardDialog::updateChooseState() {
    int count = 0;
    for (int row = 0; row < list_->count(); ++row) {
        count += list_->item(row)->checkState() == Qt::Checked ? 1 : 0;
    }
    chosen_->setText(tr("Marked: %1").arg(labels::plural(count, "command", "commands")));
    start_->setEnabled(count > 0);
}

// --- Обучение ---------------------------------------------------------------------

void LearnWizardDialog::buildLearnPage() {
    auto* page = new QWidget(pages_);
    step_ = hintLabel({}, page);
    command_ = new QLabel(page);
    command_->setObjectName("pageTitle");
    QFont big = command_->font();
    big.setPointSizeF(big.pointSizeF() * 1.6);
    command_->setFont(big);
    status_ = hintLabel({}, page);
    view_ = new QLabel(page);
    view_->setWordWrap(true);
    view_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    back_ = new QPushButton(tr("Back"), page);
    back_->setToolTip(tr("Learn the previous command again"));
    repeat_ = new QPushButton(tr("Repeat"), page);
    repeat_->setToolTip(tr("Listen for this command again"));
    skip_ = new QPushButton(tr("Skip"), page);
    stop_ = new QPushButton(tr("Finish"), page);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(back_);
    buttons->addWidget(repeat_);
    buttons->addWidget(skip_);
    buttons->addStretch(1);
    buttons->addWidget(stop_);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(step_);
    layout->addWidget(command_);
    layout->addWidget(status_);
    layout->addSpacing(8);
    layout->addWidget(view_);
    layout->addStretch(1);
    layout->addLayout(buttons);
    pages_->addWidget(page);

    connect(back_, &QPushButton::clicked, this, [this] { interrupt(After::Back); });
    connect(repeat_, &QPushButton::clicked, this, [this] { interrupt(After::Repeat); });
    connect(skip_, &QPushButton::clicked, this, [this] { interrupt(After::Next); });
    connect(stop_, &QPushButton::clicked, this, [this] { interrupt(After::Finish); });
}

void LearnWizardDialog::start() {
    queue_.clear();
    for (int row = 0; row < list_->count(); ++row) {
        if (list_->item(row)->checkState() == Qt::Checked) {
            queue_ << list_->item(row)->data(kNameRole).toString();
        }
    }
    if (queue_.isEmpty()) {
        return;
    }
    index_ = 0;
    learned_ = 0;
    skipped_ = 0;
    pages_->setCurrentIndex(1);
    showCurrent();
}

void LearnWizardDialog::showCurrent() {
    if (index_ >= queue_.size()) {
        finish();
        return;
    }
    first_.reset();
    view_->clear();
    step_->setText(tr("Command %1 of %2").arg(index_ + 1).arg(queue_.size()));
    command_->setText(queue_[index_]);
    status_->setText(tr("Connecting to the remote…"));
    updateLearnButtons();
    listenSoon();
}

void LearnWizardDialog::listenSoon() {
    if (!alive_ || pages_->currentIndex() != 1) {
        return;
    }
    // Прежняя операция ещё завершается (поток не освобождён) — чуть позже.
    if (session_->busy()) {
        QTimer::singleShot(150, this, &LearnWizardDialog::listenSoon);
        return;
    }
    listening_ = true;
    session_->learn(settings::learnTimeout());
    updateLearnButtons();
}

void LearnWizardDialog::interrupt(After after) {
    if (listening_) {
        after_ = after;
        session_->cancel();
        status_->setText(tr("Stopping…"));
        updateLearnButtons();
        return;
    }
    perform(after);
}

void LearnWizardDialog::perform(After after) {
    switch (after) {
    case After::Nothing: break;
    case After::Repeat: showCurrent(); break;
    case After::Next:
        ++skipped_;
        ++index_;
        showCurrent();
        break;
    case After::Back:
        index_ = std::max(0, index_ - 1);
        showCurrent();
        break;
    case After::Finish: finish(); break;
    }
}

void LearnWizardDialog::received(const ir::IrSignal& signal) {
    view_->setText(labels::signalDetails(signal));
    if (!verify_->isChecked()) {
        save(signal);
        return;
    }
    if (!first_) {
        first_ = signal;
        status_->setText(tr("Received: %1. Press the same button once more to check.")
                             .arg(labels::signalSummary(signal)));
        listenSoon();
        return;
    }
    if (ir::sameSignal(*first_, signal)) {
        save(signal);
        return;
    }
    // Нажатия разошлись: последнее становится первым, ждём подтверждения.
    first_ = signal;
    status_->setText(tr("The two presses gave different signals — press the button once more."));
    listenSoon();
}

void LearnWizardDialog::save(const ir::IrSignal& signal) {
    const QString name = queue_[index_];
    emit commandLearned(name, signal);
    if (!existing_.contains(name)) {
        existing_ << name;
    }
    ++learned_;
    ++index_;
    if (index_ < queue_.size()) {
        first_.reset();
        step_->setText(tr("Command %1 of %2").arg(index_ + 1).arg(queue_.size()));
        command_->setText(queue_[index_]);
        status_->setText(tr("“%1” saved (%2). Next: press “%3” on the original remote.")
                             .arg(name, labels::signalSummary(signal), queue_[index_]));
        updateLearnButtons();
        listenSoon();
    } else {
        finish();
    }
}

void LearnWizardDialog::updateLearnButtons() {
    const bool stopping = after_ != After::Nothing;
    back_->setEnabled(!stopping && index_ > 0);
    repeat_->setEnabled(!stopping);
    skip_->setEnabled(!stopping);
    stop_->setEnabled(!stopping);
}

// --- Итог -------------------------------------------------------------------------

void LearnWizardDialog::buildDonePage() {
    auto* page = new QWidget(pages_);
    summary_ = hintLabel({}, page);
    auto* more = new QPushButton(tr("Learn more…"), page);
    auto* close = new QPushButton(tr("Close"), page);
    close->setObjectName("primaryButton");
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(more);
    buttons->addWidget(close);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(titleLabel(tr("Done"), page));
    layout->addWidget(summary_);
    layout->addStretch(1);
    layout->addLayout(buttons);
    pages_->addWidget(page);
    connect(more, &QPushButton::clicked, this, [this] {
        // Обратно к списку: выученные теперь «есть», отмечены недостающие.
        for (int row = 0; row < list_->count(); ++row) {
            QListWidgetItem* item = list_->item(row);
            const QString name = item->data(kNameRole).toString();
            if (existing_.contains(name)) {
                item->setText(tr("%1  (exists — replace)").arg(name));
            }
        }
        mark(true, false);
        pages_->setCurrentIndex(0);
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
}

void LearnWizardDialog::finish() {
    const int left = static_cast<int>(queue_.size()) - index_;
    QStringList lines;
    lines << tr("Learned: %1.").arg(labels::plural(learned_, "command", "commands"));
    if (skipped_ > 0) {
        lines << tr("Skipped: %1.").arg(skipped_);
    }
    if (left > 0) {
        lines << tr("Not reached: %1.").arg(left);
    }
    lines << tr("Each learned command is a separate undo step (%1).").arg(labels::shortcut(QKeySequence::Undo));
    summary_->setText(lines.join('\n') + QStringLiteral("\n\n") + view_->text());
    pages_->setCurrentIndex(2);
}

} // namespace harmony::app
