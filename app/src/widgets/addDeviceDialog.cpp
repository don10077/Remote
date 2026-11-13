/**
 * @file addDeviceDialog.cpp
 * @brief Окно переноса устройств из другой конфигурации.
 */

#include "addDeviceDialog.h"

#include "../configLibrary.h"
#include "../documentSource.h"
#include "../icons.h"
#include "../irDatabaseService.h"
#include "../labels.h"
#include "findByRemoteDialog.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QTabWidget>
#include <QTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace harmony::app {

namespace {

constexpr int kDeviceIdRole = Qt::UserRole + 1; ///< Id устройства в источнике (QString)

/// Такое же устройство уже есть: тот же производитель, модель и имя.
bool alreadyPresent(const model::RemoteConfig& target, const model::Device& device) {
    return std::ranges::any_of(target.devices, [&](const model::Device& own) {
        return own.manufacturer == device.manufacturer && own.model == device.model &&
               own.displayName() == device.displayName();
    });
}

} // namespace

AddDeviceDialog::AddDeviceDialog(const model::RemoteConfig& target, const QString& currentFolder,
                                 IrDatabaseService* irDatabase, RemoteSession* session, QWidget* parent)
    : QDialog(parent), target_(target), irDatabase_(irDatabase), session_(session) {
    setWindowTitle(tr("Add device"));
    setMinimumSize(620, 580);

    auto* title = new QLabel(tr("Add device"), this);
    title->setObjectName("pageTitle");
    auto* hint = new QLabel(tr("Find the device in the Logitech database or copy it with its IR commands "
                               "from another configuration — e.g. a remote backup or the built-in example."),
                            this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    // Источники: пример и недавние конфигурации, кроме открытой сейчас.
    sources_ = new QComboBox(this);
    sources_->addItem(tr("Built-in example"), QString::fromLatin1(kDemoSource));
    const QString current = library::normalized(currentFolder);
    for (const QString& folder : library::recent()) {
        if (library::normalized(folder) != current &&
            QFileInfo::exists(QDir(folder).filePath(QStringLiteral("UserConfiguration.xml")))) {
            sources_->addItem(QFileInfo(folder).fileName(), folder);
            sources_->setItemData(sources_->count() - 1, QDir::toNativeSeparators(folder),
                                  Qt::ToolTipRole);
        }
    }
    auto* pick = new QPushButton(tr("Choose folder…"), this);
    auto* sourceRow = new QHBoxLayout;
    sourceRow->addWidget(new QLabel(tr("From:"), this));
    sourceRow->addWidget(sources_, 1);
    sourceRow->addWidget(pick);

    list_ = new QListWidget(this);
    list_->setIconSize(QSize(28, 28));
    list_->setSpacing(2);
    list_->setAlternatingRowColors(true);

    status_ = new QLabel(this);
    status_->setObjectName("pageMeta");
    status_->setWordWrap(true);

    auto* cancel = new QPushButton(tr("Cancel"), this);
    add_ = new QPushButton(this);
    add_->setObjectName("primaryButton");
    add_->setDefault(true);
    // Своё устройство с нуля: команды потом выучиваются с родного пульта.
    auto* createNew = new QPushButton(tr("New device…"), this);
    createNew->setToolTip(tr("Create a device without commands and learn them from the original "
                             "remote"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(createNew);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(add_);

    // Страница «Другая конфигурация».
    auto* configPage = new QWidget(this);
    auto* configLayout = new QVBoxLayout(configPage);
    configLayout->setContentsMargins(0, 10, 0, 0);
    configLayout->addLayout(sourceRow);
    configLayout->addWidget(list_, 1);
    configLayout->addWidget(status_);

    tabs_ = new QTabWidget(this);
    tabs_->addTab(makeDatabasePage(), tr("Logitech database"));
    tabs_->addTab(configPage, tr("Other configuration"));
    connect(tabs_, &QTabWidget::currentChanged, this, &AddDeviceDialog::updateButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addWidget(tabs_, 1);
    layout->addLayout(buttons);

    connect(sources_, &QComboBox::currentIndexChanged, this,
            [this] { loadSource(sources_->currentData().toString()); });
    connect(pick, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Source configuration"), library::lastDialogDir(),
            tr("Remote configuration (UserConfiguration.xml);;XML files (*.xml);;All "
               "files (*)"));
        if (!path.isEmpty()) {
            library::setLastDialogDir(QFileInfo(path).absolutePath());
            addSourceFolder(QFileInfo(path).absolutePath());
        }
    });
    connect(list_, &QListWidget::itemChanged, this, &AddDeviceDialog::updateButton);
    // Двойной щелчок — отметить или снять отметку.
    connect(list_, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* item) {
        if ((item->flags() & Qt::ItemIsUserCheckable) != 0) {
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        }
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(createNew, &QPushButton::clicked, this, [this] { done(kNewDevice); });
    connect(add_, &QPushButton::clicked, this, &QDialog::accept);

    loadSource(sources_->currentData().toString());
}

AddDeviceDialog::~AddDeviceDialog() = default;

void AddDeviceDialog::addSourceFolder(const QString& folder) {
    const int existing = sources_->findData(folder);
    if (existing >= 0) {
        sources_->setCurrentIndex(existing);
        return;
    }
    sources_->addItem(QFileInfo(folder).fileName(), folder);
    sources_->setItemData(sources_->count() - 1, QDir::toNativeSeparators(folder), Qt::ToolTipRole);
    sources_->setCurrentIndex(sources_->count() - 1);
}

void AddDeviceDialog::loadSource(const QString& source) {
    const QSignalBlocker blocker(list_); // заполнение — не выбор пользователя
    list_->clear();
    source_.reset();

    auto document = loadDocument(source, tr("Example"));
    if (!document) {
        status_->setText(tr("Could not open: %1").arg(labels::errorText(document.error())));
        updateButton();
        return;
    }
    source_ = std::make_unique<ConfigDocument>(std::move(*document));

    // Без таблиц ИК-данных команды не перенести — честно говорим об этом сразу.
    const auto ir = source_->checkIrData();
    const auto& devices = source_->model().devices;
    for (const auto& device : devices) {
        const bool present = alreadyPresent(target_, device);
        QString details = QString::fromStdString(device.manufacturer + " " + device.model) + " · " +
                          labels::deviceType(device.type) + " · " +
                          labels::plural(static_cast<qsizetype>(device.commands.size()), "command", "commands");
        if (present) {
            details += tr(" · already in the configuration");
        }
        auto* item = new QListWidgetItem(icons::deviceType(device.type),
                                         QString::fromStdString(device.displayName()) + "\n" + details);
        item->setData(kDeviceIdRole, QString::fromStdString(device.id));
        if (ir) {
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        } else {
            item->setFlags(Qt::NoItemFlags);
        }
        list_->addItem(item);
    }

    if (!ir) {
        status_->setText(tr("Devices cannot be taken from this configuration: %1.")
                             .arg(labels::errorText(ir.error())));
    } else if (devices.empty()) {
        status_->setText(tr("This configuration has no devices."));
    } else {
        status_->setText(tr("Tick the devices. They are added with all their commands; in existing "
                            "activities they will be off — turn them on in your activities."));
    }
    updateButton();
}

QWidget* AddDeviceDialog::makeDatabasePage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 10, 0, 0);
    layout->setSpacing(8);
    const irdb::IrDatabase* database = irDatabase_->database();
    if (database == nullptr) {
        // Базы нет: что делать — скачать и проиндексировать архив в настройках.
        auto* missing = new QLabel(tr("The IR code database is not indexed yet. Download the Logitech archive files (or "
                                      "choose a folder with them) and index them once in Settings → IR database."),
                                   page);
        missing->setWordWrap(true);
        auto* open = new QPushButton(tr("Open settings…"), page);
        open->setObjectName("primaryButton");
        connect(open, &QPushButton::clicked, this, [this] { done(kOpenSettings); });
        auto* row = new QHBoxLayout;
        row->addWidget(open);
        row->addStretch(1);
        layout->addWidget(missing);
        layout->addLayout(row);
        layout->addStretch(1);
        return page;
    }
    search_ = new QLineEdit(page);
    search_->setPlaceholderText(tr("Manufacturer and model, e.g. samsung ue55"));
    search_->setClearButtonEnabled(true);
    found_ = new QLabel(page);
    found_->setObjectName("pageMeta");
    results_ = new QListWidget(page);
    results_->setIconSize(QSize(24, 24));
    results_->setAlternatingRowColors(true);
    results_->setUniformItemSizes(true);
    details_ = new QLabel(page);
    details_->setObjectName("pageMeta");
    details_->setWordWrap(true);
    // Модель неизвестна — по нескольким кнопкам родного пульта.
    auto* byRemote = new QPushButton(tr("Find by remote…"), page);
    byRemote->setToolTip(tr("Press a few buttons of the original remote — the Harmony recognises them and "
                            "finds the model"));
    connect(byRemote, &QPushButton::clicked, this, &AddDeviceDialog::findByRemote);
    auto* searchRow = new QHBoxLayout;
    searchRow->addWidget(search_, 1);
    searchRow->addWidget(byRemote);
    layout->addLayout(searchRow);
    layout->addWidget(found_);
    layout->addWidget(results_, 1);
    layout->addWidget(details_);

    // Поиск — когда пользователь перестал печатать.
    searchTimer_ = new QTimer(this);
    searchTimer_->setSingleShot(true);
    searchTimer_->setInterval(200);
    connect(search_, &QLineEdit::textChanged, searchTimer_, qOverload<>(&QTimer::start));
    connect(searchTimer_, &QTimer::timeout, this, &AddDeviceDialog::searchDatabase);
    connect(results_, &QListWidget::currentRowChanged, this, &AddDeviceDialog::showDatabaseDevice);
    connect(results_, &QListWidget::itemDoubleClicked, this, [this] {
        if (databaseEntry() >= 0) {
            accept();
        }
    });
    found_->setText(tr("%1 in the database. Type a manufacturer and a model.")
                        .arg(labels::plural(static_cast<qsizetype>(database->entries().size()), "device", "devices")));
    QTimer::singleShot(0, search_, qOverload<>(&QWidget::setFocus));
    return page;
}

void AddDeviceDialog::searchDatabase() {
    const irdb::IrDatabase* database = irDatabase_->database();
    if (database == nullptr) {
        return;
    }
    constexpr std::size_t kMaxResults = 500;
    const QSignalBlocker blocker(results_);
    results_->clear();
    details_->clear();
    const QString text = search_->text().trimmed();
    if (text.size() < 2) {
        found_->setText(tr("Type at least two letters."));
        updateButton();
        return;
    }
    std::size_t total = 0;
    for (const std::size_t index : database->search(text.toStdString(), kMaxResults, &total)) {
        const irdb::CatalogueEntry& entry = database->entries()[index];
        const std::string type = irdb::deviceTypeName(entry.deviceType);
        auto* item = new QListWidgetItem(icons::deviceType(type), QString::fromStdString(entry.manufacturer + " " +
                                                                                         entry.model) +
                                                                      QStringLiteral("  ·  ") +
                                                                      labels::deviceType(type));
        item->setData(kDeviceIdRole, static_cast<qulonglong>(index));
        results_->addItem(item);
    }
    found_->setText(total > kMaxResults ? tr("Found %1, showing the first %2 — refine the search.")
                                              .arg(total)
                                              .arg(kMaxResults)
                                        : tr("Found: %1").arg(total));
    updateButton();
}

void AddDeviceDialog::showDatabaseDevice() {
    const int entry = databaseEntry();
    const irdb::IrDatabase* database = irDatabase_->database();
    if (entry < 0 || database == nullptr) {
        details_->clear();
        updateButton();
        return;
    }
    auto device = database->device(static_cast<std::size_t>(entry));
    if (!device) {
        details_->setText(labels::errorText(device.error()));
        updateButton();
        return;
    }
    // Что получится: команды, питание, входы.
    QString power = tr("power: manual");
    if (device->powerType == 1) {
        power = tr("power: separate on and off");
    } else if (device->powerType == 2) {
        power = tr("power: toggle");
    }
    QStringList inputs;
    for (const auto& input : device->inputs) {
        inputs << QString::fromStdString(input.name);
    }
    details_->setText(labels::plural(static_cast<qsizetype>(device->commands.size()), "command", "commands") +
                      QStringLiteral(" · ") + power +
                      (inputs.isEmpty() ? QString() : QStringLiteral(" · ") + tr("inputs: %1").arg(inputs.join(", "))));
    updateButton();
}

void AddDeviceDialog::findByRemote() {
    FindByRemoteDialog dialog(session_, irDatabase_, this);
    if (dialog.exec() == QDialog::Accepted && dialog.entry() >= 0) {
        pickedEntry_ = dialog.entry();
        tabs_->setCurrentIndex(0);
        accept();
    }
}

bool AddDeviceDialog::fromDatabase() const {
    return tabs_ != nullptr && tabs_->currentIndex() == 0;
}

int AddDeviceDialog::databaseEntry() const {
    if (pickedEntry_ >= 0) {
        return pickedEntry_;
    }
    if (results_ == nullptr || results_->currentItem() == nullptr) {
        return -1;
    }
    return static_cast<int>(results_->currentItem()->data(kDeviceIdRole).toULongLong());
}

std::vector<std::string> AddDeviceDialog::selectedDevices() const {
    std::vector<std::string> ids;
    for (int row = 0; row < list_->count(); ++row) {
        const QListWidgetItem* item = list_->item(row);
        if (item->checkState() == Qt::Checked) {
            ids.push_back(item->data(kDeviceIdRole).toString().toStdString());
        }
    }
    return ids;
}

void AddDeviceDialog::updateButton() {
    if (add_ == nullptr) {
        return; // ещё строится окно
    }
    if (fromDatabase()) {
        add_->setEnabled(databaseEntry() >= 0);
        add_->setText(tr("Add"));
        return;
    }
    const auto count = static_cast<qsizetype>(selectedDevices().size());
    add_->setEnabled(count > 0 && source_ != nullptr);
    add_->setText(count > 1 ? tr("Add (%1)").arg(count) : tr("Add"));
}

} // namespace harmony::app
