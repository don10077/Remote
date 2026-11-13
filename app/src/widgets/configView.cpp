/**
 * @file configView.cpp
 * @brief Реализация экрана конфигурации.
 */

#include "configView.h"
#include "harmony/core/remoteProfile.h"

#include "../icons.h"
#include "../labels.h"
#include "../modern/activityIcon.h"
#include "../modern/modernShell.h"
#include "../modern/remotePanel.h"
#include "../modern/remoteView.h"
#include "toggleSwitch.h"

#include <QDrag>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <map>
#include <utility>

namespace harmony::app {

namespace {

// Идентификаторы страниц и разделов боковой панели.
constexpr const char* kOverview = "overview";
constexpr const char* kActivities = "activities";
constexpr const char* kDevices = "devices";

/// Роли данных строки таблицы кнопок: что подсветить на пульте.
constexpr int kHardKeyRole = Qt::UserRole + 1; ///< Имя аппаратной кнопки (QString)
constexpr int kSoftRole = Qt::UserRole + 2;    ///< Позиция экранной кнопки (int)

/// Таблица только для чтения с заданными заголовками столбцов (@p table — своя, иначе новая).
QTableWidget* makeTable(const QStringList& headers, QTableWidget* table = nullptr) {
    if (table == nullptr) {
        table = new QTableWidget;
    }
    table->setColumnCount(static_cast<int>(headers.size()));
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setWordWrap(false);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

/// Таблица команд устройства: строку можно перетащить на кнопку картинки пульта.
class CommandTable final : public QTableWidget {
public:
    explicit CommandTable(QString deviceId) : deviceId_(std::move(deviceId)) {
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
    }

protected:
    void startDrag(Qt::DropActions /*supported*/) override {
        // Одна команда — та, за которую потянули (при нескольких выделенных тоже).
        const QTableWidgetItem* name = item(currentRow(), 0);
        if (name == nullptr) {
            return;
        }
        auto* mime = new QMimeData;
        mime->setData(RemoteView::MIME_COMMAND, RemoteView::commandPayload(deviceId_, name->text()));
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        const QPixmap pixmap = RemoteView::dragPixmap(name->text(), font());
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(pixmap.width() / 2, pixmap.height() / 2));
        drag->exec(Qt::CopyAction);
    }

private:
    QString deviceId_;
};

/// Добавляет строку в таблицу и возвращает её номер.
int addRow(QTableWidget* table, const QStringList& cells) {
    const int row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < cells.size(); ++column) {
        table->setItem(row, column, new QTableWidgetItem(cells[column]));
    }
    return row;
}

/// Подгоняет ширину столбцов (кроме последнего, он растягивается) под содержимое.
void finishTable(QTableWidget* table) {
    for (int column = 0; column + 1 < table->columnCount(); ++column) {
        table->resizeColumnToContents(column);
        table->setColumnWidth(column, std::min(table->columnWidth(column) + 24, 360));
    }
}

/// Имя устройства по id (или сам id, если устройства нет в конфигурации).
QString deviceName(const model::RemoteConfig& config, const std::string& deviceId) {
    const model::Device* device = config.findDevice(deviceId);
    return device != nullptr ? QString::fromStdString(device->displayName())
                             : QString::fromStdString(deviceId);
}

/// Индекс устройства в конфигурации, -1 — нет такого.
int deviceIndex(const model::RemoteConfig& config, const std::string& deviceId) {
    for (int i = 0; i < static_cast<int>(config.devices.size()); ++i) {
        if (config.devices[static_cast<std::size_t>(i)].id == deviceId) {
            return i;
        }
    }
    return -1;
}

/// Общее число кнопок во всех группах.
qsizetype buttonCount(const std::vector<model::ControlGroup>& groups) {
    qsizetype count = 0;
    for (const auto& group : groups) {
        count += static_cast<qsizetype>(group.buttons.size());
    }
    return count;
}

/// Вкладки страницы: документный вид, подчёркивание выбранной.
QTabWidget* makeTabs() {
    auto* tabs = new QTabWidget;
    tabs->setObjectName("pageTabs");
    tabs->setDocumentMode(true);
    return tabs;
}

/// Страница с отступами вокруг вкладок.
QWidget* wrapPage(QWidget* content) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 4, 24, 16);
    layout->addWidget(content);
    return page;
}

/// Возвращает экран пульта после пересборки панели: setConfig сбрасывает его на активности.
class ScreenModeRestorer {
public:
    explicit ScreenModeRestorer(RemotePanel& panel) : panel_(panel), mode_(panel.screenMode()) {}
    ~ScreenModeRestorer() { panel_.setScreenMode(mode_); }
    ScreenModeRestorer(const ScreenModeRestorer&) = delete;
    ScreenModeRestorer& operator=(const ScreenModeRestorer&) = delete;

private:
    RemotePanel& panel_;
    QString mode_;
};

} // namespace

ConfigView::ConfigView(QWidget* parent) : QWidget(parent) {
    shell_ = new ModernShell(this);
    remote_ = new RemotePanel;
    shell_->setSidePanel(remote_);

    activityTabs_ = makeTabs();
    deviceTabs_ = makeTabs();
    overview_ = new QLabel;
    overview_->setObjectName("overviewText");
    overview_->setWordWrap(true);
    overview_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    overview_->setTextFormat(Qt::RichText);

    shell_->addPage(kOverview, tr("Remote"), wrapPage(overview_));
    auto* changeRemote = new QPushButton(tr("Change remote"));
    changeRemote->setObjectName("pageAction");
    changeRemote->setCursor(Qt::PointingHandCursor);
    connect(changeRemote, &QPushButton::clicked, this, &ConfigView::chooseRemoteRequested);
    shell_->setHeaderWidget(changeRemote);
    changeRemote->setVisible(shell_->currentPageId() == kOverview);
    connect(shell_, &ModernShell::currentPageChanged, changeRemote,
            [changeRemote](const QString& page) { changeRemote->setVisible(page == kOverview); });
    // Причина «не отправляется» видна всегда, а не только в подсказке к переключателю.
    auto* notice = new QFrame;
    notice->setObjectName("pageNotice");
    auto* noticeLayout = new QHBoxLayout(notice);
    noticeLayout->setContentsMargins(12, 8, 8, 8);
    activityNoticeText_ = new QLabel;
    activityNoticeText_->setObjectName("pageNoticeText");
    activityNoticeText_->setWordWrap(true);
    activityNoticeButton_ = new QPushButton;
    activityNoticeButton_->setObjectName("pageAction");
    activityNoticeButton_->setCursor(Qt::PointingHandCursor);
    noticeLayout->addWidget(activityNoticeText_, 1);
    noticeLayout->addWidget(activityNoticeButton_);
    notice->setVisible(false);
    activityNotice_ = notice;
    connect(activityNoticeButton_, &QPushButton::clicked, this, [this] {
        if (!noticeDeviceId_.empty()) {
            emit notSentToggled(Item::Device, QString::fromStdString(noticeDeviceId_), false);
        }
    });
    auto* activityPage = new QWidget;
    auto* activityLayout = new QVBoxLayout(activityPage);
    activityLayout->setContentsMargins(0, 0, 0, 0);
    activityLayout->setSpacing(8);
    activityLayout->addWidget(notice);
    activityLayout->addWidget(activityTabs_, 1);
    shell_->addPage(kActivities, tr("Activities"), wrapPage(activityPage));
    shell_->addPage(kDevices, tr("Devices"), wrapPage(deviceTabs_));
    shell_->setCardPage(kOverview);
    shell_->addItemSection(kActivities, tr("Activities"), kActivities, tr("Add"));
    shell_->addItemSection(kDevices, tr("Devices"), kDevices, tr("Add"));
    connect(shell_, &ModernShell::addRequested, this, [this](const QString& section) {
        if (config_ != nullptr) {
            emit addRequested(section == kActivities ? Item::Activity : Item::Device);
        }
    });
    // Порядок активностей в файле — порядок на экране пульта: меняем перетаскиванием.
    shell_->setItemsReorderable(kActivities, true);
    shell_->setItemsReorderable(kDevices, true);

    // Действия над выбранным объектом — справа в заголовке страницы.
    actions_ = new QWidget;
    auto* actionsLayout = new QHBoxLayout(actions_);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);
    renameButton_ = new QPushButton(tr("Rename…")); // как пункт меню: открывает окно ввода
    renameButton_->setObjectName("pageAction");
    editActivityButton_ = new QPushButton(tr("Setup"));
    editActivityButton_->setObjectName("pageAction");
    editActivityButton_->setToolTip(tr("Devices, roles and inputs of the activity"));
    compareDevicesButton_ = new QPushButton(tr("Compare"));
    compareDevicesButton_->setObjectName("pageAction");
    compareDevicesButton_->setToolTip(tr("Compare the commands with another device and copy the ones you mark"));
    replaceDeviceButton_ = new QPushButton(tr("Replace"));
    replaceDeviceButton_->setObjectName("pageAction");
    replaceDeviceButton_->setToolTip(tr("Put another device in its place in activities — e.g. a new TV"));
    propertiesButton_ = new QPushButton(tr("Properties"));
    propertiesButton_->setObjectName("pageAction");
    propertiesButton_->setToolTip(tr("Settings the remote reads from the configuration: pauses between "
                                     "presses, power behaviour, start page and others"));
    // «Активно» — переключатель-ползунок: это состояние объекта, а не пункт списка.
    activeSwitch_ = new ToggleSwitch(tr("Active"), tr("Inactive"));
    activeSwitch_->setObjectName("pageSwitch");
    removeButton_ = new QPushButton(tr("Delete"));
    removeButton_->setObjectName("pageActionDanger");
    for (QPushButton* button :
         {renameButton_, editActivityButton_, compareDevicesButton_, replaceDeviceButton_, propertiesButton_}) {
        button->setCursor(Qt::PointingHandCursor);
        actionsLayout->addWidget(button);
    }
    removeButton_->setCursor(Qt::PointingHandCursor);
    actionsLayout->addWidget(removeButton_);
    connect(editActivityButton_, &QPushButton::clicked, this, [this] {
        if (hasSelection_ && selectedKind_ == Item::Activity) {
            emit activityEditRequested(QString::fromStdString(selectedId_));
        }
    });
    connect(activeSwitch_, &ToggleSwitch::clicked, this, [this](bool active) {
        if (hasSelection_) {
            emit notSentToggled(selectedKind_, QString::fromStdString(selectedId_), !active);
        }
    });
    connect(replaceDeviceButton_, &QPushButton::clicked, this, [this] {
        if (hasSelection_ && selectedKind_ == Item::Device) {
            emit replaceDeviceRequested(QString::fromStdString(selectedId_));
        }
    });
    connect(compareDevicesButton_, &QPushButton::clicked, this, [this] {
        if (hasSelection_ && selectedKind_ == Item::Device) {
            emit compareDevicesRequested(QString::fromStdString(selectedId_));
        }
    });
    connect(propertiesButton_, &QPushButton::clicked, this, [this] {
        if (hasSelection_) {
            emit propertiesRequested(selectedKind_, QString::fromStdString(selectedId_));
        }
    });
    shell_->setHeaderWidget(actions_);
    // «Активно» — рядом с названием объекта, а не в ряду кнопок действий.
    shell_->setTitleWidget(activeSwitch_);
    connect(renameButton_, &QPushButton::clicked, this, [this] {
        if (hasSelection_) {
            emit renameRequested(selectedKind_, QString::fromStdString(selectedId_));
        }
    });
    connect(removeButton_, &QPushButton::clicked, this, [this] {
        if (hasSelection_) {
            emit removeRequested(selectedKind_, QString::fromStdString(selectedId_));
        }
    });
    connect(shell_, &ModernShell::itemMoved, this,
            [this](const QString& section, uint32_t id, int newIndex) {
                const auto index = static_cast<std::size_t>(id - 1);
                if (section == kDevices && config_ && index < config_->devices.size()) {
                    emit deviceMoveRequested(QString::fromStdString(config_->devices[index].id), newIndex);
                    return;
                }
                if (section != kActivities || config_ == nullptr || index >= config_->activities.size()) {
                    return;
                }
                // newIndex — место среди видимых активностей (без «кнопки Off»), а ядру нужно
                // место среди всех остальных: ставим перед той, что окажется следующей.
                const auto& all = config_->activities;
                std::vector<std::size_t> visible;
                for (std::size_t i = 0; i < all.size(); ++i) {
                    if (i != index && !all[i].isPowerOff()) {
                        visible.push_back(i);
                    }
                }
                std::size_t target = all.size() - 1; // в конец
                if (newIndex >= 0 && static_cast<std::size_t>(newIndex) < visible.size()) {
                    const std::size_t next = visible[static_cast<std::size_t>(newIndex)];
                    target = next > index ? next - 1 : next; // индекс среди остальных (без переносимой)
                }
                emit activityMoveRequested(QString::fromStdString(all[index].id), static_cast<int>(target));
            }, Qt::QueuedConnection);

    // id строки боковой панели = индекс в конфигурации + 1 (0 у оболочки — «нет выбора»).
    connect(shell_, &ModernShell::itemActivated, this, [this](const QString& section, uint32_t id) {
        const int index = static_cast<int>(id) - 1;
        if (section == kActivities) {
            showActivity(index);
        } else {
            showDevice(index);
        }
    });
    connect(shell_, &ModernShell::currentPageChanged, this, [this](const QString& page) {
        if (page == kOverview) {
            hasSelection_ = false;
            remote_->showOverview();
            updateActions();
        }
    });
    connect(remote_, &RemotePanel::activityClicked, this, [this](int index) {
        shell_->setCurrentItem(kActivities, static_cast<uint32_t>(index + 1));
        showActivity(index);
    });
    connect(remote_, &RemotePanel::deviceClicked, this, [this](int index) {
        shell_->setCurrentItem(kDevices, static_cast<uint32_t>(index + 1));
        showDevice(index);
        remote_->setScreenMode(QStringLiteral("command"));
    });
    connect(remote_, &RemotePanel::message, this, &ConfigView::message);
    connect(remote_, &RemotePanel::hardKeyClicked, this,
            [this](const QString& key) { remoteButtonClicked(ButtonRef::hard(key)); });
    connect(remote_, &RemotePanel::softButtonClicked, this,
            [this](int position) { remoteButtonClicked(ButtonRef::screen(position)); });
    // Перетаскивание кнопок на картинке пульта — перенос назначения в открытой активности.
    connect(remote_, &RemotePanel::hardKeyDropped, this,
            [this](const QString& from, const QString& to, bool copy) {
                if (hasSelection_) {
                    emit buttonMoveRequested(currentOwner(), ButtonRef::hard(from), ButtonRef::hard(to), copy);
                } else {
                    emit message(tr("Open an activity or a device on the left to reassign buttons."));
                }
            });
    connect(remote_, &RemotePanel::softButtonDropped, this, [this](int from, int to) {
        if (hasSelection_) {
            emit buttonMoveRequested(currentOwner(), ButtonRef::screen(from), ButtonRef::screen(to), false);
        }
    });
    connect(remote_, &RemotePanel::commandDropped, this,
            [this](const QString& key, const QString& deviceId, const QString& command) {
                commandDropped(ButtonRef::hard(key), deviceId, command);
            });
    connect(remote_, &RemotePanel::commandDroppedOnSoft, this,
            [this](int position, const QString& deviceId, const QString& command) {
                commandDropped(ButtonRef::screen(position), deviceId, command);
            });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(shell_);
}

void ConfigView::setConfig(const model::RemoteConfig* config, const QString& name) {
    config_ = config;
    hasSelection_ = false;
    remote_->setConfig(config);
    shell_->setDocumentName(name.isEmpty() ? tr("no configuration") : name);
    fillSidebar();
    fillOverview();
    selectFirst();
}

void ConfigView::select(Item item, const std::string& id) {
    if (config_ == nullptr) {
        return;
    }
    if (item == Item::Activity) {
        for (int i = 0; i < static_cast<int>(config_->activities.size()); ++i) {
            if (config_->activities[static_cast<std::size_t>(i)].id == id &&
                !config_->activities[static_cast<std::size_t>(i)].isPowerOff()) {
                shell_->setCurrentItem(kActivities, static_cast<uint32_t>(i + 1));
                showActivity(i);
                return;
            }
        }
    } else if (const int i = deviceIndex(*config_, id); i >= 0) {
        shell_->setCurrentItem(kDevices, static_cast<uint32_t>(i + 1));
        showDevice(i);
    }
}

void ConfigView::refresh() {
    // Пересборка панели переключает пульт на активности; возвращаем прежний экран,
    // чтобы сохранение правки не меняло вид (например, не уводило на состав кнопок).
    const ScreenModeRestorer screenMode(*remote_);
    remote_->setConfig(config_); // модель перепарсена: старые указатели в панели недействительны
    fillSidebar();
    fillOverview();
    if (config_ == nullptr || !hasSelection_) {
        if (config_ != nullptr && shell_->currentPageId() == kOverview) {
            remote_->showOverview();
            updateActions();
            return;
        }
        selectFirst();
        return;
    }
    // Ищем прежний объект по id: после перестановки или отмены его индекс другой.
    // «Выключить всё» в интерфейсе не показывается — как и в select().
    if (selectedKind_ == Item::Activity) {
        for (int i = 0; i < static_cast<int>(config_->activities.size()); ++i) {
            if (config_->activities[static_cast<std::size_t>(i)].id == selectedId_ &&
                !config_->activities[static_cast<std::size_t>(i)].isPowerOff()) {
                shell_->setCurrentItem(kActivities, static_cast<uint32_t>(i + 1));
                showActivity(i);
                return;
            }
        }
    } else if (const int i = deviceIndex(*config_, selectedId_); i >= 0) {
        shell_->setCurrentItem(kDevices, static_cast<uint32_t>(i + 1));
        showDevice(i);
        return;
    }
    selectFirst(); // объект удалён
}

void ConfigView::showOverview() {
    shell_->setCurrentPage(kOverview); // остальное делает обработчик смены страницы
}

ConfigView::Page ConfigView::currentPage() const {
    const auto selected = selection();
    if (!selected) {
        return Page::Overview;
    }
    // Порядок вкладок задают вызовы replaceTabs() в showActivity() и showDevice().
    if (selected->kind == Item::Activity) {
        return activityTabs_->currentIndex() == 1 ? Page::ActivityButtons : Page::ActivityDevices;
    }
    switch (deviceTabs_->currentIndex()) {
    case 1: return Page::DeviceButtons;
    case 2: return Page::DeviceActivities;
    default: return Page::DeviceCommands;
    }
}

void ConfigView::selectFirst() {
    hasSelection_ = false;
    if (config_ != nullptr) {
        // Сразу показываем первую «настоящую» активность — так экран не пустой.
        for (int i = 0; i < static_cast<int>(config_->activities.size()); ++i) {
            if (!config_->activities[static_cast<std::size_t>(i)].isPowerOff()) {
                shell_->setCurrentItem(kActivities, static_cast<uint32_t>(i + 1));
                showActivity(i);
                return;
            }
        }
    }
    shell_->setCurrentItem(kActivities, 0);
    shell_->setCurrentItem(kDevices, 0);
    shell_->setCurrentPage(kOverview);
    remote_->showOverview();
    updateActions();
}

void ConfigView::updateActions() {
    const bool selected = hasSelection_ && config_ != nullptr;
    actions_->setVisible(selected);
    activityNotice_->setVisible(false);
    noticeDeviceId_.clear();
    if (!selected) {
        // Переключатель стоит у названия, вне actions_; его тоже прячем.
        activeSwitch_->setVisible(false);
        return;
    }
    // «Выключить всё» в интерфейсе не открывается; проверки ниже — на случай, если её всё же выбрали.
    const model::Activity* activity =
        selectedKind_ == Item::Activity ? config_->findActivity(selectedId_) : nullptr;
    const bool powerOff = activity != nullptr && activity->isPowerOff();
    removeButton_->setEnabled(!powerOff);
    editActivityButton_->setVisible(selectedKind_ == Item::Activity && !powerOff);
    const bool device = selectedKind_ == Item::Device;
    compareDevicesButton_->setVisible(device);
    replaceDeviceButton_->setVisible(device);
    propertiesButton_->setVisible(!powerOff);
    // «Активно» — обратное пометке «не отправлять»: включено = объект пишется на пульт.
    const model::Device* selectedDevice = device ? config_->findDevice(selectedId_) : nullptr;
    const bool activityNotSent = activity != nullptr && config_->isActivityNotSent(*activity);
    const bool blockedByDevice = !device && activityNotSent && activity != nullptr && !activity->notSent;
    const bool notSent = device ? selectedDevice != nullptr && selectedDevice->notSent : activityNotSent;
    activeSwitch_->setVisible(!powerOff);
    activeSwitch_->setEnabled(!blockedByDevice);
    activeSwitch_->setChecked(!notSent);
    if (blockedByDevice) {
        activeSwitch_->setToolTip(tr("Not sent: the activity needs a device that is not sent to the remote. "
                                    "Make that device active first."));
        const auto blocking = config_->notSentDevicesOf(*activity);
        if (!blocking.empty()) {
            QStringList names;
            for (const auto* item : blocking) {
                names << "«" + QString::fromStdString(item->displayName()) + "»";
            }
            activityNoticeText_->setText(
                blocking.size() == 1
                    ? tr("Not sent to the remote: the device %1 is inactive.").arg(names.front())
                    : tr("Not sent to the remote: the devices %1 are inactive.").arg(names.join(", ")));
            // Кнопка включает первое; если их несколько, полоса после этого покажет оставшиеся.
            noticeDeviceId_ = blocking.front()->id;
            activityNoticeButton_->setText(tr("Make %1 active").arg(names.front()));
            activityNotice_->setVisible(true);
        }
    } else if (device) {
        activeSwitch_->setToolTip(
            notSent ? tr("The device is not sent to the remote. Switch it on to write it to the remote again; "
                         "activities that need it will be sent too.")
                    : tr("The device is sent to the remote. Switch it off to keep the device in the configuration "
                         "but leave it out when writing to the remote; activities that need it are left out too."));
    } else {
        activeSwitch_->setToolTip(
            notSent ? tr("The activity is not sent to the remote. Switch it on to write it to the remote again.")
                    : tr("The activity is sent to the remote. Switch it off to keep the activity in the configuration "
                         "but leave it out when writing to the remote."));
    }
    removeButton_->setToolTip(powerOff ? tr("The remote needs this activity: it turns everything off")
                                       : QString());
    renameButton_->setToolTip(selectedKind_ == Item::Activity
                                  ? tr("New name of the activity on the remote's screen")
                                  : tr("New name of the device"));
}

void ConfigView::setDocumentName(const QString& name) {
    shell_->setDocumentName(name);
}

void ConfigView::fillSidebar() {
    QList<NavItem> activities;
    QList<NavItem> devices;
    if (config_ != nullptr) {
        for (int i = 0; i < static_cast<int>(config_->activities.size()); ++i) {
            const auto& activity = config_->activities[static_cast<std::size_t>(i)];
            if (activity.isPowerOff()) {
                continue; // служебная «кнопка Off» — на странице «Пульт», не в списке активностей
            }
            NavItem item;
            item.id = static_cast<uint32_t>(i + 1);
            item.label = QString::fromStdString(activity.label);
            QStringList names;
            for (const auto& deviceId : activity.powerOn) {
                names << deviceName(*config_, deviceId);
            }
            item.meta = names.join(", ");
            if (activity.notSent) {
                item.meta = tr("not sent · %1").arg(item.meta);
            } else if (const auto blocking = config_->notSentDevicesOf(activity); !blocking.empty()) {
                // Конкретная причина: какое устройство держит активность.
                QStringList because;
                for (const auto* device : blocking) {
                    because << QString::fromStdString(device->displayName());
                }
                item.meta = tr("not sent because of %1 · %2").arg(because.join(", "), item.meta);
            }
            item.icon = activityIcon(QString::fromStdString(activity.type));
            item.iconSize = 30;
            item.tooltip = QString("<b>%1</b><br>%2").arg(item.label.toHtmlEscaped(),
                                                          item.meta.toHtmlEscaped());
            activities.append(item);
        }
        for (int i = 0; i < static_cast<int>(config_->devices.size()); ++i) {
            const auto& device = config_->devices[static_cast<std::size_t>(i)];
            NavItem item;
            item.id = static_cast<uint32_t>(i + 1);
            item.label = QString::fromStdString(device.displayName());
            item.meta = QString::fromStdString(device.manufacturer + " " + device.model);
            if (!config_->isDeviceUsed(device.id)) {
                item.meta = tr("not used · %1").arg(item.meta);
            }
            if (device.notSent) {
                item.meta = tr("not sent · %1").arg(item.meta);
            }
            item.icon = icons::deviceType(device.type);
            item.colour = RemotePanel::deviceColour(i); // как кнопки этого устройства на пульте
            item.tileTooltip = labels::deviceType(device.type);
            item.tooltip = QString("<b>%1</b><br>%2 · %3")
                               .arg(item.label.toHtmlEscaped(), labels::deviceType(device.type),
                                    item.meta.toHtmlEscaped());
            devices.append(item);
        }
    }
    shell_->setItems(kActivities, activities);
    shell_->setItems(kDevices, devices);
}

void ConfigView::fillOverview() {
    const auto id = config_ ? config_->property("HarmonyTargetRemote") : std::string();
    const auto* profile = harmony::remoteProfile(id.empty() ? "900" : id);
    const QString modelName = profile ? QString::fromUtf8(profile->name.data()) : tr("Remote");
    shell_->setRemoteName(modelName);
    if (config_ == nullptr) {
        overview_->clear();
        shell_->setPageHeader(kOverview, tr("Remote"), {});
        return;
    }
    shell_->setPageHeader(
        kOverview, modelName,
        labels::plural(static_cast<qsizetype>(config_->devices.size()), "device", "devices") +
            " · " +
            labels::plural(static_cast<qsizetype>(config_->userActivityCount()), "activity", "activities"));

    // Дата обновления в файле: «20240802 154354» → «02.08.2024 15:43».
    QString updated = QString::fromStdString(config_->property("LastUpdated"));
    if (updated.size() >= 13) {
        updated = QString("%1.%2.%3 %4:%5")
                      .arg(updated.mid(6, 2), updated.mid(4, 2), updated.left(4), updated.mid(9, 2),
                           updated.mid(11, 2));
    }
    if (config_->devices.empty()) {
        overview_->setText(
            tr("<p>The configuration is empty: no devices yet.</p><p>Start with the "
               "<b>“+ Add”</b> button under devices on the left: devices are copied "
               "with their IR commands from another configuration — a backup of the "
               "remote or the built-in example.</p><p>Then build an activity with "
               "<b>“+ Add”</b> under activities: choose the devices, which one is "
               "controlled and which one handles the sound — the remote's buttons are "
               "assigned automatically.</p>"));
        return;
    }
    // Кнопка Off — служебная активность пульта (PowerOff): какие устройства она выключает.
    QString powerOff;
    for (const auto& activity : config_->activities) {
        if (!activity.isPowerOff()) {
            continue;
        }
        QStringList names;
        for (const auto& deviceId : activity.powerOff) {
            names << deviceName(*config_, deviceId).toHtmlEscaped();
        }
        powerOff = tr("<h3>Off button</h3><p>Turns off: %1.</p><p>This is the remote's "
                      "service activity “%2”: what to do on the Off button and from which "
                      "state the remote starts activities. It cannot be deleted. A device "
                      "with power commands is added to this list automatically.</p>")
                       .arg(names.isEmpty() ? tr("nothing") : names.join(", "),
                            QString::fromStdString(activity.label).toHtmlEscaped());
    }
    overview_->setText(
        tr("<p>Configuration updated: <b>%1</b>.</p><p>%2.</p><p>Select an "
           "activity or a device on the left. On the right you see how it looks "
           "on the remote: screen buttons and the colour of the device each "
           "button sends to.</p>")
            .arg(updated.isEmpty() ? tr("unknown") : updated,
                 labels::plural(static_cast<qsizetype>(config_->commandCount()), "IR command", "IR commands")) +
        powerOff);
}

void ConfigView::replaceTabs(QTabWidget* tabs, const QList<QPair<QWidget*, QString>>& pages) {
    const QString previous =
        tabs->count() > 0 ? tabs->tabText(tabs->currentIndex()).section(" · ", 0, 0) : QString();
    while (tabs->count() > 0) {
        QWidget* tab = tabs->widget(0);
        tabs->removeTab(0);
        // Позже, а не сразу: замену вкладок может вызвать сигнал виджета на этой же вкладке
        // (двойной щелчок по таблице → правка → обновление экрана).
        tab->hide();
        tab->deleteLater();
    }
    for (const auto& [page, title] : pages) {
        tabs->addTab(page, title);
        if (title.section(" · ", 0, 0) == previous) {
            tabs->setCurrentIndex(tabs->count() - 1);
        }
    }
}

void ConfigView::linkToRemote(QTableWidget* table) {
    connect(table, &QTableWidget::currentCellChanged, this, [this, table](int row) {
        const QTableWidgetItem* item = table->item(row, 0);
        if (item == nullptr) {
            return;
        }
        const QString key = item->data(kHardKeyRole).toString();
        if (!key.isEmpty()) {
            remote_->selectSoftButton(-1);
            remote_->selectHardKey(key);
        } else if (item->data(kSoftRole).isValid()) {
            remote_->selectHardKey({});
            remote_->selectSoftButton(item->data(kSoftRole).toInt());
        }
    });
}

void ConfigView::showActivity(int index) {
    if (config_ == nullptr || index < 0 || index >= static_cast<int>(config_->activities.size())) {
        return;
    }
    const auto& activity = config_->activities[static_cast<std::size_t>(index)];
    const auto buttons = buttonCount(activity.controlGroups);
    shell_->setPageHeader(kActivities, QString::fromStdString(activity.label),
                          labels::activityType(activity.type) + " · " +
                              labels::plural(static_cast<qsizetype>(activity.powerOn.size()),
                                             "device", "devices") +
                              " · " + labels::plural(buttons, "button", "buttons"));

    // Устройства: кто включается, кто выключается и какую роль выполняет.
    auto* devices = makeTable({tr("Device"), tr("Power"), tr("Role")});
    auto rolesOf = [&activity](const std::string& deviceId) {
        QStringList roles;
        for (const auto& role : activity.roles) {
            if (role.deviceId == deviceId) {
                roles << labels::role(role.name);
            }
        }
        return roles.join(", ");
    };
    auto addDevice = [&](const std::string& deviceId, const QString& power) {
        const int row = addRow(devices, {deviceName(*config_, deviceId), power, rolesOf(deviceId)});
        const int colourIndex = deviceIndex(*config_, deviceId);
        // Цветная метка — тот же цвет, что кнопки устройства на пульте.
        devices->item(row, 0)->setIcon(
            icons::make(icons::Icon::Dot, RemotePanel::deviceColour(colourIndex)));
    };
    for (const auto& deviceId : activity.powerOn) {
        addDevice(deviceId, tr("turns on"));
    }
    for (const auto& deviceId : activity.powerOff) {
        addDevice(deviceId, tr("turns off"));
    }
    finishTable(devices);

    resetPending(Item::Activity, activity.id);
    QWidget* buttonsPage = makeButtonsPage(ButtonOwner::activity(activity.id), activity.controlGroups,
                                           !activity.powerOn.empty());

    replaceTabs(activityTabs_,
                {{devices, tr("Devices · %1").arg(activity.powerOn.size())},
                 {buttonsPage, tr("Buttons · %1").arg(buttons)}});
    shell_->setCurrentPage(kActivities);
    remote_->showActivity(index);
    hasSelection_ = true;
    selectedKind_ = Item::Activity;
    selectedId_ = activity.id;
    updateActions();
}

void ConfigView::resetPending(Item kind, const std::string& id) {
    // Подсказки «щёлкнули свободную кнопку на пульте» относятся к одному объекту.
    if (!hasSelection_ || selectedKind_ != kind || selectedId_ != id) {
        pendingKey_ = ButtonRef::hard({});
        pendingSoft_ = -1;
    }
}

ButtonOwner ConfigView::currentOwner() const {
    return selectedKind_ == Item::Activity ? ButtonOwner::activity(selectedId_)
                                           : ButtonOwner::device(selectedId_);
}

bool ConfigView::remoteHasKeys() const {
    return config_ != nullptr &&
           !RemoteView::hardKeyIds(QString::fromStdString(config_->property("HarmonyTargetRemote"))).isEmpty();
}

void ConfigView::commandDropped(const ButtonRef& button, const QString& deviceId, const QString& command) {
    if (!hasSelection_) {
        emit message(tr("Open an activity or a device on the left to assign buttons."));
        return;
    }
    // Позже: правка пересобирает таблицу команд, а сигнал пришёл из её QDrag::exec().
    const ButtonOwner owner = currentOwner();
    QMetaObject::invokeMethod(
        this, [this, owner, button, deviceId, command] { emit commandAssignRequested(owner, button, deviceId, command); },
        Qt::QueuedConnection);
}

QWidget* ConfigView::makeButtonsPage(const ButtonOwner& owner, const std::vector<model::ControlGroup>& groups,
                                     bool canAssign) {
    auto* page = new QWidget;
    const bool editable = remoteHasKeys();
    page->setEnabled(editable);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(8);

    auto* assign = new QPushButton(tr("Assign button…"));
    assign->setToolTip(tr("Assign a command to a button of the remote"));
    auto* addSoft = new QPushButton(tr("Screen button…"));
    addSoft->setToolTip(tr("Add a button to the remote's screen"));
    editButtonAction_ = new QPushButton(tr("Edit…"));
    removeButtonAction_ = new QPushButton(tr("Clear assignment"));
    for (QPushButton* button : {assign, addSoft, editButtonAction_}) {
        button->setObjectName("pageAction");
    }
    removeButtonAction_->setObjectName("pageActionDanger");
    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);
    for (QPushButton* button : {assign, addSoft, editButtonAction_, removeButtonAction_}) {
        button->setCursor(Qt::PointingHandCursor);
    }
    toolbar->addWidget(assign);
    toolbar->addWidget(addSoft);
    toolbar->addStretch(1);
    // Раскладку или выбранную кнопку — в другие активности (у устройства кнопки только свои).
    if (!owner.isDevice()) {
        auto* copyButtons = new QPushButton(tr("Copy to activities…"));
        copyButtons->setObjectName("pageAction");
        copyButtons->setCursor(Qt::PointingHandCursor);
        copyButtons->setToolTip(tr("Copy the layout or the selected button to other activities"));
        toolbar->addWidget(copyButtons);
        connect(copyButtons, &QPushButton::clicked, this, [this, owner] {
            const auto button = selectedButton();
            emit buttonCopyRequested(QString::fromStdString(owner.id), button.has_value(),
                                     button.value_or(ButtonRef::hard({})));
        });
    }
    toolbar->addWidget(editButtonAction_);
    toolbar->addWidget(removeButtonAction_);
    layout->addLayout(toolbar);

    // Командам некуда уходить: «выключить всё», активность без устройств, устройство без команд.
    if (!canAssign) {
        for (QPushButton* button : {assign, addSoft}) {
            button->setEnabled(false);
            button->setToolTip(owner.isDevice() ? tr("The device has no commands")
                                                : tr("The activity includes no devices"));
        }
    }

    buttonsTable_ = makeTable({tr("Where"), tr("Button"), tr("Action")});
    buttonsTable_->installEventFilter(this);
    fillButtons(buttonsTable_, groups);
    linkToRemote(buttonsTable_);
    layout->addWidget(buttonsTable_, 1);

    auto* hint = new QLabel(editable
        ? tr("Double-click to edit. Buttons on the remote picture can be dragged: "
             "move the assignment, with Ctrl — copy. Commands are dragged from the Commands tab.")
        : tr("Button editing is unavailable: this remote has no command zones yet."));
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(assign, &QPushButton::clicked, this, [this, owner] {
        emit buttonEditRequested(owner, pendingKey_.key.isEmpty() ? ButtonRef::hard({}) : pendingKey_);
    });
    connect(addSoft, &QPushButton::clicked, this,
            [this, owner] { emit buttonEditRequested(owner, ButtonRef::screen(pendingSoft_)); });
    connect(editButtonAction_, &QPushButton::clicked, this, [this, owner] {
        if (const auto button = selectedButton()) {
            emit buttonEditRequested(owner, *button);
        }
    });
    connect(removeButtonAction_, &QPushButton::clicked, this, [this, owner] {
        if (const auto button = selectedButton()) {
            emit buttonRemoveRequested(owner, *button);
        }
    });
    connect(buttonsTable_, &QTableWidget::cellDoubleClicked, this, [this, owner] {
        if (const auto button = selectedButton()) {
            emit buttonEditRequested(owner, *button);
        }
    });
    connect(buttonsTable_, &QTableWidget::itemSelectionChanged, this, &ConfigView::updateButtonActions);
    updateButtonActions();
    return page;
}

std::optional<ButtonRef> ConfigView::selectedButton() const {
    if (buttonsTable_ == nullptr || buttonsTable_->currentRow() < 0) {
        return std::nullopt;
    }
    const QTableWidgetItem* item = buttonsTable_->item(buttonsTable_->currentRow(), 0);
    if (item == nullptr || buttonsTable_->selectedItems().isEmpty()) {
        return std::nullopt;
    }
    const QString key = item->data(kHardKeyRole).toString();
    if (!key.isEmpty()) {
        return ButtonRef::hard(key);
    }
    return ButtonRef::screen(item->data(kSoftRole).toInt());
}

bool ConfigView::eventFilter(QObject* watched, QEvent* event) {
    const bool table = watched == buttonsTable_ || watched == commandsTable_.data();
    if (table && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        const auto* key = static_cast<const QKeyEvent*>(event);
        if (key->key() == Qt::Key_Delete && key->modifiers() == Qt::NoModifier) {
            if (event->type() == QEvent::KeyPress) {
                deleteSelectedRow(watched);
            }
            event->accept();
            return true;
        }
        // Enter — как двойной щелчок: правка кнопки или подробности команды.
        const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        if (enter && event->type() == QEvent::KeyPress &&
            (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::KeypadModifier)) {
            openSelectedRow(watched);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ConfigView::deleteSelectedRow(const QObject* table) {
    if (table == buttonsTable_) {
        if (removeButtonAction_ != nullptr && removeButtonAction_->isEnabled()) {
            removeButtonAction_->click();
        }
        return;
    }
    if (table == commandsTable_.data() && hasSelection_ && selectedKind_ == Item::Device) {
        if (const QTableWidgetItem* item = commandsTable_->item(commandsTable_->currentRow(), 0)) {
            emit commandRemoveRequested(QString::fromStdString(selectedId_), item->text());
        }
    }
}

void ConfigView::openSelectedRow(const QObject* table) {
    if (table == buttonsTable_) {
        if (editButtonAction_ != nullptr && editButtonAction_->isEnabled()) {
            editButtonAction_->click();
        }
        return;
    }
    if (table == commandsTable_.data() && hasSelection_ && selectedKind_ == Item::Device) {
        if (const QTableWidgetItem* item = commandsTable_->item(commandsTable_->currentRow(), 0)) {
            emit commandDetailsRequested(QString::fromStdString(selectedId_), item->text());
        }
    }
}

ConfigView::ClipboardTarget ConfigView::clipboardTarget() const {
    ClipboardTarget target;
    if (!hasSelection_ || config_ == nullptr) {
        return target;
    }
    const bool commandsShown = commandsTable_ != nullptr && commandsTable_->isVisible();
    const bool buttonsShown = buttonsTable_ != nullptr && buttonsTable_->isVisible();
    if (selectedKind_ == Item::Device && (commandsShown || !buttonsShown)) {
        target.kind = ClipboardTarget::Kind::Commands;
        target.deviceId = selectedId_;
        if (commandsShown) {
            for (const QModelIndex& index : commandsTable_->selectionModel()->selectedRows()) {
                target.commands.push_back(commandsTable_->item(index.row(), 0)->text().toStdString());
            }
        }
        return target;
    }
    const model::Activity* activity = selectedKind_ == Item::Activity ? config_->findActivity(selectedId_) : nullptr;
    if (activity != nullptr && activity->isPowerOff()) {
        return target; // у «выключить всё» нет кнопок
    }
    target.kind = ClipboardTarget::Kind::Buttons;
    target.owner = currentOwner();
    if (buttonsShown) {
        target.button = selectedButton();
    }
    return target;
}

void ConfigView::updateButtonActions() {
    const auto selected = selectedButton();
    const auto keys = config_ ? RemoteView::hardKeyIds(
        QString::fromStdString(config_->property("HarmonyTargetRemote"))) : QStringList();
    editButtonAction_->setEnabled(selected && !keys.isEmpty() &&
                                  (selected->soft || keys.contains(selected->key)));
    removeButtonAction_->setEnabled(selected.has_value() && !keys.isEmpty());
    // Экранная кнопка удаляется целиком, у аппаратной снимается только назначение.
    removeButtonAction_->setText(selected && selected->soft ? tr("Delete") : tr("Clear assignment"));
}

void ConfigView::selectButton(const ButtonRef& button) {
    if (buttonsTable_ == nullptr) {
        return;
    }
    for (int row = 0; row < buttonsTable_->rowCount(); ++row) {
        const QTableWidgetItem* item = buttonsTable_->item(row, 0);
        const bool match = button.soft ? item->data(kSoftRole).isValid() &&
                                             item->data(kSoftRole).toInt() == button.position
                                       : item->data(kHardKeyRole).toString() == button.key;
        if (match) {
            showButtonsTab();
            buttonsTable_->setCurrentCell(row, 0);
            buttonsTable_->scrollToItem(buttonsTable_->item(row, 0));
            return;
        }
    }
}

void ConfigView::selectCommand(const std::string& deviceId, const std::string& name) {
    select(Item::Device, deviceId);
    if (commandsTable_ == nullptr || !hasSelection_ || selectedId_ != deviceId) {
        return;
    }
    for (int row = 0; row < commandsTable_->rowCount(); ++row) {
        if (const QTableWidgetItem* item = commandsTable_->item(row, 0); item != nullptr && item->text().toStdString() == name) {
            // Таблица лежит в своей странице вкладок (вместе с кнопками действий).
            for (QWidget* page = commandsTable_->parentWidget(); page != nullptr; page = page->parentWidget()) {
                if (deviceTabs_->indexOf(page) >= 0) {
                    deviceTabs_->setCurrentWidget(page);
                    break;
                }
            }
            commandsTable_->setCurrentCell(row, 0);
            commandsTable_->scrollToItem(commandsTable_->item(row, 0));
            return;
        }
    }
}

void ConfigView::showButtonsTab() {
    // Вкладка кнопок — в той странице, где лежит таблица (активность или устройство).
    QWidget* page = buttonsTable_->parentWidget();
    for (QTabWidget* tabs : {activityTabs_, deviceTabs_}) {
        if (tabs->indexOf(page) >= 0) {
            tabs->setCurrentWidget(page);
        }
    }
}

void ConfigView::remoteButtonClicked(const ButtonRef& button) {
    if (!hasSelection_ || buttonsTable_ == nullptr) {
        return;
    }
    buttonsTable_->clearSelection();
    buttonsTable_->setCurrentCell(-1, -1);
    selectButton(button);
    if (button.soft) {
        remote_->selectSoftButton(button.position);
    } else {
        remote_->selectHardKey(button.key);
    }
    emit buttonEditRequested(currentOwner(), button);
}

void ConfigView::showDevice(int index) {
    if (config_ == nullptr || index < 0 || index >= static_cast<int>(config_->devices.size())) {
        return;
    }
    const auto& device = config_->devices[static_cast<std::size_t>(index)];

    // В каких активностях участвует устройство (включает, роль или кнопки) — те же, что держат удаление.
    std::vector<const model::Activity*> usedIn;
    for (const auto& activity : config_->activities) {
        if (!activity.isPowerOff() && model::RemoteConfig::activityNeedsDevice(activity, device.id)) {
            usedIn.push_back(&activity);
        }
    }

    shell_->setPageHeader(
        kDevices, QString::fromStdString(device.displayName()),
        QString::fromStdString(device.manufacturer + " " + device.model) + " · " +
            labels::deviceType(device.type) + " · " +
            labels::plural(static_cast<qsizetype>(device.commands.size()), "command", "commands") +
            " · " +
            (usedIn.empty() ? tr("not used in activities")
                            : tr("in activities: %1").arg(usedIn.size())));

    // Команды: протокол, адрес и код команды, код как в Arduino IRremote (моноширинным:
    // так коды удобно сравнивать). Протокол узнаётся по сигналу, который собирает пульт.
    auto* commands = makeTable({tr("Command"), tr("Protocol"), tr("Address"), tr("Code"), tr("Data (IRremote)"),
                                tr("Stored as"), tr("Logitech code")},
                               new CommandTable(QString::fromStdString(device.id)));
    commands->installEventFilter(this);
    commandsTable_ = commands;
    commands->setSelectionMode(QAbstractItemView::ExtendedSelection); // для копирования нескольких
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
                      QStringLiteral("DejaVu Sans Mono"), QStringLiteral("monospace")});
    auto hex = [](std::uint32_t value) {
        return QStringLiteral("0x%1").arg(value, value > 0xFF ? 4 : 2, 16, QChar('0')).toUpper().replace("0X", "0x");
    };
    // Для сводки над таблицей: сколько команд у каждой пары «протокол, адрес».
    std::map<std::pair<QString, std::uint32_t>, int> groups;
    int unrecognised = 0;
    int asSignal = 0;
    for (const auto& command : device.commands) {
        const auto details = commandLookup_ ? commandLookup_(device.id, command.name) : std::nullopt;
        QString protocol = tr("not recognised");
        QString address;
        QString code;
        if (details && details->decoded) {
            protocol = labels::protocol(details->decoded->protocol);
            address = hex(details->decoded->address);
            code = hex(details->decoded->command);
            ++groups[{protocol, details->decoded->address}];
        } else {
            ++unrecognised;
            if (!details) {
                protocol = tr("no data");
            }
        }
        QString stored;
        if (details) {
            stored = details->learned ? tr("signal") : tr("protocol");
            asSignal += details->learned ? 1 : 0;
        }
        const int row = addRow(commands, {QString::fromStdString(command.name), protocol, address, code,
                                          details ? QString::fromStdString(details->rawData()) : QString(),
                                          stored, QString::fromStdString(command.code)});
        if (details) {
            commands->item(row, 5)->setToolTip(
                details->learned ? tr("A frozen signal (SsIr.bin): while the button is held, the remote repeats it "
                                      "as is")
                                 : tr("Protocol program (IrProto.bin): the remote sends real repeat frames and "
                                      "flips the toggle bit itself"));
        }
        for (const int column : {2, 3, 4, 6}) {
            commands->item(row, column)->setFont(mono);
        }
        commands->item(row, 0)->setToolTip(QString::fromStdString(command.code)); // код Logitech как в файле
        if (!details && commandErrorLookup_) {
            commands->item(row, 1)->setToolTip(commandErrorLookup_(device.id, command.name));
        }
    }
    finishTable(commands);

    // Страница команд: панель над таблицей. Слева — откуда берутся команды
    // («Обучить», «Вручную»), рядом — как пульт ими управляет устройством
    // («Питание», «Входы»), справа — действия над выбранной командой.
    auto* commandsPage = new QWidget;
    auto* commandsLayout = new QVBoxLayout(commandsPage);
    commandsLayout->setContentsMargins(0, 8, 0, 0);
    commandsLayout->setSpacing(8);
    // Одно «Обучить» вместо двух окон: мастер учит и одну команду, и много подряд.
    auto* learn = new QPushButton(tr("Learn…"));
    learn->setObjectName("primaryButton");
    learn->setToolTip(tr("Learn IR signals from the device's original remote, one after another (a connected "
                         "Harmony is required). Commands selected in the table are offered for relearning."));
    // Вручную: новая команда по протоколу, адресу и коду — или правка выбранной.
    auto* manual = new QPushButton(tr("Manual…"));
    manual->setToolTip(tr("Enter a command by hand: protocol, address and code — or change the selected "
                          "command's code"));
    auto* power = new QPushButton(tr("Power…"));
    power->setToolTip(tr("How the remote switches the device on and off when an activity starts or ends"));
    auto* inputs = new QPushButton(tr("Inputs…"));
    inputs->setToolTip(tr("How the remote selects the device's input that an activity needs"));
    auto* detailsButton = new QPushButton(tr("Details…"));
    detailsButton->setToolTip(tr("Command code details"));
    detailsButton->setEnabled(false);
    auto* removeCommand = new QPushButton(tr("Delete"));
    removeCommand->setToolTip(tr("Delete the selected command (if no button or activity uses it)"));
    removeCommand->setEnabled(false);
    for (QPushButton* button : {learn, manual, power, inputs, detailsButton, removeCommand}) {
        button->setCursor(Qt::PointingHandCursor);
    }
    // Тонкий разделитель между группами «команды» и «питание и входы».
    auto* divider = new QFrame;
    divider->setObjectName("toolbarDivider");
    divider->setFrameShape(QFrame::NoFrame); // цвет и отступы — в harmony.qss
    divider->setFixedWidth(1);
    auto* commandsToolbar = new QHBoxLayout;
    commandsToolbar->setSpacing(8);
    commandsToolbar->addWidget(learn);
    commandsToolbar->addWidget(manual);
    commandsToolbar->addSpacing(4);
    commandsToolbar->addWidget(divider);
    commandsToolbar->addSpacing(4);
    commandsToolbar->addWidget(power);
    commandsToolbar->addWidget(inputs);
    commandsToolbar->addStretch(1);
    commandsToolbar->addWidget(detailsButton);
    commandsToolbar->addWidget(removeCommand);
    commandsLayout->addLayout(commandsToolbar);
    if (!device.commands.empty()) {
        // Протокол устройства: самые частые пары «протокол, адрес» — сразу видно, какие команды выбиваются.
        std::vector<std::pair<std::pair<QString, std::uint32_t>, int>> sorted(groups.begin(), groups.end());
        std::ranges::stable_sort(sorted, std::greater{}, [](const auto& group) { return group.second; });
        constexpr std::size_t kShown = 3;
        QStringList parts;
        for (std::size_t i = 0; i < std::min(sorted.size(), kShown); ++i) {
            parts << tr("%1, address %2 (%3)")
                         .arg(sorted[i].first.first, hex(sorted[i].first.second),
                              labels::plural(sorted[i].second, "command", "commands"));
        }
        if (sorted.size() > kShown) {
            parts << tr("%n more", nullptr, static_cast<int>(sorted.size() - kShown));
        }
        QString text = parts.isEmpty() ? tr("Protocol: not recognised") : tr("Protocol: %1").arg(parts.join("; "));
        if (unrecognised > 0 && !parts.isEmpty()) {
            text += QStringLiteral(" · ") + tr("not recognised: %1").arg(unrecognised);
        }
        if (asSignal > 0) {
            text += QStringLiteral(" · ") + tr("stored as signal: %1").arg(asSignal);
        }
        auto* summary = new QLabel(text);
        summary->setObjectName("pageMeta");
        summary->setWordWrap(true);
        summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        commandsLayout->addWidget(summary);
    }
    commandsLayout->addWidget(commands, 1);
    if (!device.commands.empty() && remoteHasKeys()) {
        auto* dragHint = new QLabel(tr("Drag a command onto a button of the remote picture to assign it."));
        dragHint->setObjectName("pageMeta");
        dragHint->setWordWrap(true);
        commandsLayout->addWidget(dragHint);
    }

    const QString deviceId = QString::fromStdString(device.id);
    connect(learn, &QPushButton::clicked, this, [this, commands, deviceId] {
        // Выделенные строки — кандидаты на переобучение (по имени, без повторов).
        QStringList selected;
        for (const QTableWidgetItem* item : commands->selectedItems()) {
            const QTableWidgetItem* name = commands->item(item->row(), 0);
            if (name != nullptr && !selected.contains(name->text())) {
                selected << name->text();
            }
        }
        emit learnRequested(deviceId, selected);
    });
    connect(manual, &QPushButton::clicked, this, [this, commands, deviceId] {
        const QTableWidgetItem* item = commands->item(commands->currentRow(), 0);
        emit protocolCommandRequested(deviceId, item != nullptr ? item->text() : QString());
    });
    connect(power, &QPushButton::clicked, this, [this, deviceId] { emit devicePowerRequested(deviceId); });
    connect(inputs, &QPushButton::clicked, this, [this, deviceId] { emit deviceInputsRequested(deviceId); });
    connect(commands, &QTableWidget::itemSelectionChanged, removeCommand,
            [commands, removeCommand, detailsButton] {
                const QTableWidgetItem* item = commands->item(commands->currentRow(), 0);
                removeCommand->setEnabled(item != nullptr);
                detailsButton->setEnabled(item != nullptr);
            });
    connect(commands, &QTableWidget::currentCellChanged, this, [this, commands, deviceId](int row) {
        const auto* item = commands->item(row, 0);
        remote_->selectCommand(deviceId.toStdString(), item ? item->text().toStdString() : std::string());
    });
    auto showDetails = [this, commands, deviceId] {
        if (const QTableWidgetItem* item = commands->item(commands->currentRow(), 0)) {
            emit commandDetailsRequested(deviceId, item->text());
        }
    };
    connect(detailsButton, &QPushButton::clicked, this, showDetails);
    connect(commands, &QTableWidget::cellDoubleClicked, this, showDetails);
    connect(removeCommand, &QPushButton::clicked, this, [this, commands, deviceId] {
        if (const QTableWidgetItem* item = commands->item(commands->currentRow(), 0)) {
            emit commandRemoveRequested(deviceId, item->text());
        }
    });

    resetPending(Item::Device, device.id);
    QWidget* buttonTable = makeButtonsPage(ButtonOwner::device(device.id), device.controlGroups,
                                           !device.commands.empty());

    auto* activities = makeTable({tr("Activity"), tr("Role"), tr("On the remote")});
    for (const auto* activity : usedIn) {
        QStringList roles;
        for (const auto& role : activity->roles) {
            if (role.deviceId == device.id) {
                roles << labels::role(role.name);
            }
        }
        // Отдельно видно, какие активности не уйдут на пульт именно из-за этого устройства.
        QString state = tr("sent");
        if (activity->notSent) {
            state = tr("not sent");
        } else if (config_->isActivityNotSent(*activity)) {
            state = device.notSent ? tr("not sent because of this device") : tr("not sent because of another device");
        }
        const int row = addRow(activities, {QString::fromStdString(activity->label), roles.join(", "), state});
        activities->item(row, 0)->setIcon(activityIcon(QString::fromStdString(activity->type)));
        activities->item(row, 0)->setData(Qt::UserRole, QString::fromStdString(activity->id));
        activities->item(row, 0)->setToolTip(tr("Double-click to open the activity"));
    }
    activities->setIconSize(QSize(24, 20));
    finishTable(activities);
    connect(activities, &QTableWidget::cellDoubleClicked, this, [this, activities](int row) {
        if (const QTableWidgetItem* item = activities->item(row, 0)) {
            // Позже: переход пересоздаёт вкладки, а сигнал пришёл из таблицы на одной из них.
            const std::string id = item->data(Qt::UserRole).toString().toStdString();
            QMetaObject::invokeMethod(this, [this, id] { select(Item::Activity, id); }, Qt::QueuedConnection);
        }
    });

    replaceTabs(deviceTabs_,
                {{commandsPage, tr("Commands · %1").arg(device.commands.size())},
                 {buttonTable, tr("Buttons · %1").arg(buttonCount(device.controlGroups))},
                 {activities, tr("Activities · %1").arg(usedIn.size())}});
    shell_->setCurrentPage(kDevices);
    remote_->showDevice(index);
    hasSelection_ = true;
    selectedKind_ = Item::Device;
    selectedId_ = device.id;
    updateActions();
}

void ConfigView::fillButtons(QTableWidget* table,
                             const std::vector<model::ControlGroup>& groups) const {
    for (const auto& group : groups) {
        // Экранные кнопки — в порядке места на экране, аппаратные — сверху вниз по пульту.
        std::vector<const model::Button*> ordered;
        for (const auto& button : group.buttons) {
            ordered.push_back(&button);
        }
        const QStringList keyOrder = RemoteView::hardKeyIds(
            QString::fromStdString(config_->property("HarmonyTargetRemote")));
        std::ranges::stable_sort(ordered, {}, [&keyOrder](const model::Button* button) {
            return button->name.empty() ? button->position
                                        : static_cast<int>(keyOrder.indexOf(QString::fromStdString(button->name)));
        });
        for (const auto* button : ordered) {
            const bool hard = !button->name.empty();
            const QString where = hard ? tr("Remote button") : tr("Screen");
            const QString text = hard ? RemoteView::keyName(QString::fromStdString(button->name))
                                      : labels::button(*button);
            const int row = addRow(table, {where, text, labels::action(*config_, button->actionId)});
            if (hard) {
                table->item(row, 0)->setData(kHardKeyRole, QString::fromStdString(button->name));
            } else {
                table->item(row, 0)->setData(kSoftRole, button->position);
            }
        }
    }
    finishTable(table);
}

} // namespace harmony::app
