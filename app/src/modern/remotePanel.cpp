#include <QComboBox>
#include <QSignalBlocker>
/**
 * @file remotePanel.cpp
 * @brief Реализация панели пульта поверх модели конфигурации.
 */

#include "remotePanel.h"
#include <QPixmap>

#include "../labels.h"
#include "../theme.h"
#include "harmony/core/macro.h"
#include "remoteView.h"

#include <QHBoxLayout>
#include <QGridLayout>
#include <QHash>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>
#include <algorithm>
#include <iterator>

namespace harmony::app {

namespace {

constexpr const char* kHardGroup = "HardButtons"; ///< Группа аппаратных кнопок в файле
constexpr const char* kSoftGroup = "Misc";        ///< Группа экранных кнопок в файле

/// Команда из действия «<id>_<команда>_<модификатор>»; пусто, если формат другой.
std::string commandOf(const std::string& actionId) {
    const auto first = actionId.find('_');
    const auto last = actionId.rfind('_');
    if (first == std::string::npos || last <= first) {
        return {};
    }
    return actionId.substr(first + 1, last - first - 1);
}

/// Группа кнопок по имени или nullptr.
const model::ControlGroup* findGroup(const std::vector<model::ControlGroup>& groups,
                                     const char* name) {
    const auto it = std::ranges::find(groups, std::string(name), &model::ControlGroup::name);
    return it != groups.end() ? &*it : nullptr;
}

/// Подписи экранных кнопок по позициям (индекс = позиция, пусто = свободное место).
QStringList softLabels(const std::vector<model::ControlGroup>& groups) {
    QStringList labels;
    const auto* group = findGroup(groups, kSoftGroup);
    if (group == nullptr) {
        return labels;
    }
    for (const auto& button : group->buttons) {
        const int position = std::max(0, button.position);
        while (labels.size() <= position) {
            labels.append(QString());
        }
        // На экране пульта значок без подписи показываем его именем (без скобок таблицы).
        labels[position] = !button.label.empty() ? QString::fromStdString(button.label)
                                                 : QString::fromStdString(button.icon);
    }
    return labels;
}

/// Образец цвета для легенды — нарисован так же, как подсветка кнопки на пульте.
QPixmap legendSwatch(const QColor& fill, const QColor& line, bool dashed) {
    const qreal dpr = 2.0;
    QPixmap pixmap(QSize(18, 10) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(fill);
    QPen pen(line, dashed ? 1.0 : 2.0);
    if (dashed) {
        pen.setStyle(Qt::DashLine);
    }
    painter.setPen(line.isValid() ? pen : QPen(Qt::NoPen));
    painter.drawRoundedRect(QRectF(1, 1, 16, 8), 3, 3);
    return pixmap;
}

} // namespace

RemotePanel::RemotePanel(QWidget* parent) : QWidget(parent) {
    setObjectName("remotePanel");
    setAttribute(Qt::WA_StyledBackground);
    createView();
    refresh();
}

QColor RemotePanel::deviceColour(int index) {
    // Спокойные, хорошо различимые оттенки разной светлоты.
    static const QColor colours[] = {QColor("#6f9be0"), QColor("#e0a33a"), QColor("#3fb6a8"),
                                     QColor("#a184e0"), QColor("#d9738a"), QColor("#7cb45a"),
                                     QColor("#c98f5b"), QColor("#5bb0d9")};
    if (index < 0) {
        return QColor("#9aa3ad");
    }
    return colours[static_cast<std::size_t>(index) % std::size(colours)];
}

void RemotePanel::createView() {
    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setRowStretch(0, 1);
    layout->setColumnStretch(1, 1);
    layout->setSpacing(0);

    // Узкая колонка: что показано, переключатель экрана, легенда.
    auto* column = new QWidget(this);
    column->setObjectName("remoteColumn");
    column->setFixedWidth(132);
    auto* columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(12, 14, 8, 14);
    columnLayout->setSpacing(4);
    auto* controls = new QVBoxLayout();
    columnLayout->addLayout(controls);

    auto* title = new QLabel(tr("Remote"), column);
    title->setObjectName("remoteTitle");
    controls->addWidget(title);
    context_ = new QLabel(column);
    context_->setObjectName("remoteMeta");
    context_->setWordWrap(true);
    controls->addWidget(context_);
    controls->addSpacing(12);

    auto* screenTitle = new QLabel(tr("SCREEN"), column);
    screenTitle->setObjectName("remoteSection");
    controls->addWidget(screenTitle);

    screenModes_ = new QComboBox(column);
    controls->addWidget(screenModes_);
    controls->addSpacing(12);

    legend_ = new QWidget(column);
    legendLayout_ = new QVBoxLayout(legend_);
    legendLayout_->setContentsMargins(0, 0, 0, 0);
    legendLayout_->setSpacing(5);
    columnLayout->addWidget(legend_);
    columnLayout->addStretch(1);

    auto* hint = new QLabel(tr("Click a remote button to add or edit its assignment."), column);
    hint->setObjectName("remoteHint");
    hint->setWordWrap(true);
    hint->setContentsMargins(12, 0, 8, 14);
    hint->setFixedWidth(132);
    hint->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(hint, 0, 0, Qt::AlignLeft | Qt::AlignBottom);
    remoteHint_ = hint;

    layout->setColumnMinimumWidth(0, 142);
    layout->addWidget(column, 0, 0, Qt::AlignLeft | Qt::AlignTop);
    remoteControls_ = column;

    // Пульт занимает всю высоту панели, но не меньше своего минимума
    // (читаемый экран, попадание по кнопкам); ниже — прокрутка.
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("remoteScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    remote_ = new RemoteView(scroll);
    remote_->setAutoFillBackground(false);
    // На картинку перетаскиваются её кнопки (перенос, с Ctrl — копия) и команды из таблицы устройства.
    scroll->setWidget(remote_);
    // Сохраняем предпочтительную ширину панели при смене фотографии.
    layout->addItem(new QSpacerItem(scroll->sizeHint().width() + 8, 0,
                                    QSizePolicy::Preferred, QSizePolicy::Minimum), 0, 1);
    scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    layout->addWidget(scroll, 0, 0, 1, 2);
    scroll->lower();
    interactiveRemote_ = scroll;

    connect(screenModes_, &QComboBox::currentIndexChanged, this, [this](int index) {
        remote_->selectScreenMode(screenModes_->itemData(index).toString());
    });
    connect(remote_, &RemoteView::screenChanged, this, [this] { updateScreenButtons(); });
    connect(remote_, &RemoteView::keyClicked, this, [this](const QString& key) {
        remote_->setSelectedKey(key);
        emit hardKeyClicked(key);
    });
    connect(remote_, &RemoteView::systemKeyClicked, this, [this](const QString& key) {
        emit message(tr("“%1” is a system button of the remote, it has no command of its own.")
                         .arg(RemoteView::keyName(key)));
    });
    connect(remote_, &RemoteView::softButtonClicked, this, [this](int position) {
        remote_->setSelectedSoft(position);
        emit softButtonClicked(position);
    });
    connect(remote_, &RemoteView::activityClicked, this, [this](int index) {
        // На экране пульта нет «кнопки Off»: переводим номер на экране в индекс конфигурации.
        if (index >= 0 && index < static_cast<int>(activityOrder_.size())) {
            emit activityClicked(activityOrder_[static_cast<std::size_t>(index)]);
        }
    });
    connect(remote_, &RemoteView::deviceClicked, this, [this](int index) {
        if (index >= 0 && index < static_cast<int>(deviceOrder_.size()))
            emit deviceClicked(deviceOrder_[static_cast<std::size_t>(index)]);
    });
    connect(remote_, &RemoteView::keyDropped, this, &RemotePanel::hardKeyDropped);
    connect(remote_, &RemoteView::softDropped, this, &RemotePanel::softButtonDropped);
    connect(remote_, &RemoteView::commandDropped, this, &RemotePanel::commandDropped);
    connect(remote_, &RemoteView::commandDroppedOnSoft, this, &RemotePanel::commandDroppedOnSoft);
}

void RemotePanel::setConfig(const model::RemoteConfig* config) {
    config_ = config;
    showOverview();

}

void RemotePanel::showDevice(int index) {
    const bool changed = focus_ != Focus::Device || index_ != index;
    focus_ = Focus::Device;
    index_ = index;
    if (changed) {
        // Устройство настраивается через его кнопки: сразу открываем экранные кнопки.
        remote_->setSelectedKey({});
        remote_->setSelectedSoft(-1);
        remote_->showSoftPage(0);
        remote_->setScreen(RemoteView::Screen::SoftButtons);
    }
    refresh();
}

void RemotePanel::showActivity(int index) {
    const bool changed = focus_ != Focus::Activity || index_ != index;
    focus_ = Focus::Activity;
    index_ = index;
    if (changed) {
        // Как на настоящем пульте: запущенная активность показывает свои кнопки,
        // список активностей — за кнопкой «Активности».
        remote_->setSelectedKey({});
        remote_->setSelectedSoft(-1);
        remote_->showSoftPage(0);
        remote_->setScreen(RemoteView::Screen::SoftButtons);
    }
    refresh();
    // Страница списка — по месту активности на экране (без «кнопки Off»).
    const auto found = std::ranges::find(activityOrder_, index);
    remote_->showActivityPage(
        found != activityOrder_.end() ? static_cast<int>(found - activityOrder_.begin()) : -1);
}

void RemotePanel::showOverview() {
    focus_ = Focus::Overview;
    index_ = -1;
    remote_->setSelectedKey({});
    remote_->setScreen(RemoteView::Screen::Activities);
    refresh();
}

void RemotePanel::selectCommand(const std::string& deviceId, const std::string& command) {
    QSet<QString> keys;
    int soft = -1;
    const std::vector<model::ControlGroup>* groups = nullptr;
    if (config_ && index_ >= 0) {
        if (focus_ == Focus::Device && static_cast<std::size_t>(index_) < config_->devices.size())
            groups = &config_->devices[static_cast<std::size_t>(index_)].controlGroups;
        else if (focus_ == Focus::Activity && static_cast<std::size_t>(index_) < config_->activities.size())
            groups = &config_->activities[static_cast<std::size_t>(index_)].controlGroups;
    }
    if (groups) for (const auto& group : *groups) for (const auto& button : group.buttons) {
        if (button.actionId.substr(0, button.actionId.find('_')) != deviceId || commandOf(button.actionId) != command) continue;
        if (group.name == kHardGroup) keys.insert(QString::fromStdString(button.name));
        else if (group.name == kSoftGroup && soft < 0) soft = button.position;
    }
    remote_->setSelectedKeys(keys);
    selectSoftButton(soft);
}

void RemotePanel::selectHardKey(const QString& key) {
    remote_->setSelectedKey(key);
}

void RemotePanel::selectSoftButton(int position) {
    remote_->setSelectedSoft(position);
    if (position >= 0) {
        remote_->setScreen(RemoteView::Screen::SoftButtons);
        remote_->showSoftPage(position);
        updateScreenButtons();
    }
}

QString RemotePanel::screenMode() const {
    return remote_->screenMode();
}

void RemotePanel::setScreenMode(const QString& mode) {
    if (mode.isEmpty() || mode == remote_->screenMode()) {
        return;
    }
    remote_->selectScreenMode(mode);
    updateScreenButtons();
}

void RemotePanel::updateScreenButtons() {
    const QSignalBlocker blocker(screenModes_);
    screenModes_->clear();
    for (const auto& mode : remote_->screenModes()) {
        screenModes_->addItem(RemoteView::screenModeName(mode), mode);
    }
    screenModes_->setCurrentIndex(screenModes_->findData(remote_->screenMode()));
}

void RemotePanel::refresh() {
    const auto id = config_ ? config_->property("HarmonyTargetRemote") : std::string();
    const bool editable = !RemoteView::hardKeyIds(QString::fromStdString(id)).isEmpty();
    remote_->setRemoteProfile(QString::fromStdString(id));
    remoteHint_->setVisible(editable);
    interactiveRemote_->setVisible(true);
    remoteControls_->setVisible(editable);
    QStringList names;
    deviceOrder_.clear();
    if (config_) {
        for (int i = 0; i < static_cast<int>(config_->devices.size()); ++i) {
            const auto& device = config_->devices[static_cast<std::size_t>(i)];
            if (device.notSent) continue;
            names.append(QString::fromStdString(device.displayName()));
            deviceOrder_.push_back(i);
        }
    }
    remote_->setDeviceNames(names);
    refreshActivityList();
    if (config_ == nullptr) {
        focus_ = Focus::Overview;
    }
    switch (focus_) {
    case Focus::Device:
        refreshDevice();
        break;
    case Focus::Activity:
        refreshActivity();
        break;
    case Focus::Overview:
        remote_->setMode(RemoteView::Mode::Neutral);
        remote_->setSoftButtons(tr("Devices"), {});
        context_->setText(tr("overview"));
        break;
    }
    rebuildLegend();
    updateScreenButtons();
}

void RemotePanel::refreshActivityList() {
    QList<RemoteView::ActivityItem> items;
    activityOrder_.clear();
    if (config_ != nullptr) {
        for (int i = 0; i < static_cast<int>(config_->activities.size()); ++i) {
            const auto& activity = config_->activities[static_cast<std::size_t>(i)];
            if (activity.isPowerOff() || config_->isActivityNotSent(activity)) {
                continue; // служебная активность: её выполняет кнопка Off, на экране её нет
            }
            activityOrder_.push_back(i);
            RemoteView::ActivityItem item;
            item.label = QString::fromStdString(activity.label);
            item.type = QString::fromStdString(activity.type);
            item.selected = focus_ == Focus::Activity && i == index_;
            items.append(item);
        }
    }
    remote_->setActivities(items);
}

int RemotePanel::deviceIndexOf(const std::string& actionId) const {
    const std::string deviceId = actionId.substr(0, actionId.find('_'));
    for (int i = 0; i < static_cast<int>(config_->devices.size()); ++i) {
        if (config_->devices[static_cast<std::size_t>(i)].id == deviceId) {
            return i;
        }
    }
    return -1;
}

void RemotePanel::refreshDevice() {
    if (index_ < 0 || index_ >= static_cast<int>(config_->devices.size())) {
        showOverview();
        return;
    }
    const auto& device = config_->devices[static_cast<std::size_t>(index_)];
    context_->setText(QString::fromStdString(device.displayName()));

    // Состояние кнопок: назначена, свободна, или команда устройства не найдена.
    QHash<QString, RemoteView::KeyState> states;
    if (const auto* group = findGroup(device.controlGroups, kHardGroup)) {
        for (const auto& button : group->buttons) {
            if (button.name.empty()) {
                continue;
            }
            auto state = RemoteView::KeyState::Assigned;
            if (button.actionId.empty()) {
                state = RemoteView::KeyState::Free;
            } else if (!isMacroAction(button.actionId) && device.findCommand(commandOf(button.actionId)) == nullptr) {
                state = RemoteView::KeyState::Broken;
            }
            states.insert(QString::fromStdString(button.name), state);
        }
    }

    remote_->setMode(RemoteView::Mode::Device);
    remote_->setDeviceKeys(states);
    remote_->setSoftButtons(QString::fromStdString(device.displayName()),
                            softLabels(device.controlGroups));
}

void RemotePanel::refreshActivity() {
    if (index_ < 0 || index_ >= static_cast<int>(config_->activities.size())) {
        showOverview();
        return;
    }
    const auto& activity = config_->activities[static_cast<std::size_t>(index_)];
    context_->setText(QString::fromStdString(activity.label));

    // Каждая кнопка окрашена цветом устройства, которому уходит её команда.
    QHash<QString, QColor> colours;
    QList<int> usedDevices;
    if (const auto* group = findGroup(activity.controlGroups, kHardGroup)) {
        for (const auto& button : group->buttons) {
            if (button.name.empty() || button.actionId.empty()) {
                continue;
            }
            const int device = deviceIndexOf(button.actionId);
            if (device < 0) {
                continue;
            }
            colours.insert(QString::fromStdString(button.name), deviceColour(device));
            if (!usedDevices.contains(device)) {
                usedDevices.append(device);
            }
        }
    }
    std::ranges::sort(usedDevices);
    activityDevices_.clear();
    for (const int device : usedDevices) {
        activityDevices_.append(
            {QString::fromStdString(
                 config_->devices[static_cast<std::size_t>(device)].displayName()),
             deviceColour(device)});
    }

    remote_->setMode(RemoteView::Mode::Activity);
    remote_->setActivityKeys(colours);
    remote_->setSoftButtons(QString::fromStdString(activity.label),
                            softLabels(activity.controlGroups));
}

void RemotePanel::rebuildLegend() {
    while (legendLayout_->count() > 0) {
        QLayoutItem* item = legendLayout_->takeAt(0);
        if (item->widget() != nullptr) {
            // Удаление отложено до цикла событий — прячем сразу, чтобы старые
            // строки не наложились на новые.
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }

    auto addTitle = [this](const QString& text) {
        auto* label = new QLabel(text, legend_);
        label->setObjectName("remoteSection");
        legendLayout_->addWidget(label);
    };
    auto addRow = [this](const QPixmap& swatch, const QString& text) {
        auto* row = new QWidget(legend_);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);
        auto* icon = new QLabel(row);
        icon->setPixmap(swatch);
        rowLayout->addWidget(icon, 0, Qt::AlignTop);
        auto* label = new QLabel(text, row);
        label->setObjectName("remoteLegend");
        label->setWordWrap(true);
        rowLayout->addWidget(label, 1);
        legendLayout_->addWidget(row);
    };

    using theme::Color;
    const QColor key = theme::color(Color::RemoteKey);
    switch (focus_) {
    case Focus::Device:
        addTitle(tr("BUTTONS"));
        addRow(legendSwatch(key, QColor(), false), tr("assigned"));
        addRow(legendSwatch(theme::color(Color::RemoteKeyFree), theme::color(Color::RemoteKeyFreeLine), true),
               tr("free"));
        addRow(legendSwatch(key, theme::color(Color::RemoteBroken), false), tr("no command"));
        addRow(legendSwatch(key, theme::color(Color::RemoteSelected), false), tr("selected"));
        break;
    case Focus::Activity:
        addTitle(tr("DEVICES"));
        if (activityDevices_.isEmpty()) {
            auto* none = new QLabel(tr("buttons not assigned yet"), legend_);
            none->setObjectName("remoteLegend");
            none->setWordWrap(true);
            legendLayout_->addWidget(none);
        }
        for (const auto& [name, colour] : activityDevices_) {
            QColor fill = colour;
            fill.setAlpha(70);
            addRow(legendSwatch(fill, colour, false), name);
        }
        break;
    case Focus::Overview:
        break;
    }
}

} // namespace harmony::app
