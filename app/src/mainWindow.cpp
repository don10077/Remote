/**
 * @file mainWindow.cpp
 * @brief Реализация главного окна: создание, открытие и сохранение конфигурации, история правок.
 */

#include "mainWindow.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/core/writePolicy.h"
#include "harmony/core/undo/documentEdit.h"
#include "appSettings.h"
#include "appUpdater.h"
#include "configLibrary.h"
#include "irDatabaseService.h"
#include "documentSource.h"
#include "remoteSession.h"
#include "labels.h"
#include "widgets/appHeader.h"
#include "widgets/configView.h"
#include "widgets/newConfigDialog.h"
#include "widgets/startDialog.h"
#include "widgets/welcomePage.h"
#include "journal.h"
#include "versionStore.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QFontMetrics>
#include <QStatusBar>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

#include <exception>
#include <memory>
#include <optional>
#include <utility>

namespace harmony::app {

namespace {

/// Ключ настроек: размер и положение окна.
constexpr const char* kGeometryKey = "window/geometry";

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    updater_ = new AppUpdater(this);
    connect(updater_, &AppUpdater::readyToInstall, this, [this] {
        if (property("usbRecoveryRunning").toBool() || (remote_->busy() && !remote_->probing())) {
            QMessageBox::information(this, tr("Program update"),
                tr("Finish the operation with the remote before installing the update."));
            return;
        }
        if (!confirmDiscard() || !updater_->launchInstaller()) return;
        exitingForUpdate_ = true;
        close();
    });
    setWindowTitle(tr("Harmony"));
    setMinimumSize(1000, 600);
    resize(1320, 820);

    createActions();

    welcome_ = new WelcomePage;
    configView_ = new ConfigView;
    connect(configView_, &ConfigView::chooseRemoteRequested, this, &MainWindow::chooseRemote);
    screens_ = new QStackedWidget;
    screens_->addWidget(welcome_);
    screens_->addWidget(configView_);

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(screens_, 1);
    setCentralWidget(central);

    summary_ = new QLabel;
    // Справа от сводки — такой же видимый просвет, как над текстом:
    // поле сверху/снизу плюс пустое место шрифта над заглавными буквами
    // (минус 2 px собственного поля строки состояния справа).
    {
        constexpr int kVertical = 4;
        const QFontMetrics metrics(summary_->font());
        const int right = kVertical + metrics.ascent() - metrics.capHeight() - 2;
        summary_->setContentsMargins(0, kVertical, right, kVertical);
    }
    // Проблемы конфигурации — щелчок открывает проверку.
    problems_ = new QToolButton;
    problems_->setObjectName("problemsButton");
    problems_->setAutoRaise(true);
    problems_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    problems_->setToolTip(tr("Check configuration: what will not work on the remote"));
    problems_->hide();
    connect(problems_, &QToolButton::clicked, this, &MainWindow::showCheck);
    // Проверка разбирает все ИК-сигналы: после серии правок (обучение, вставка) — один раз.
    problemsTimer_ = new QTimer(this);
    problemsTimer_->setSingleShot(true);
    problemsTimer_->setInterval(200);
    connect(problemsTimer_, &QTimer::timeout, this, &MainWindow::updateProblems);
    // Редакторы сохраняют файл несколькими событиями (запись, переименование): перечитываем раз.
    resourceWatcher_ = new QFileSystemWatcher(this);
    resourceTimer_ = new QTimer(this);
    resourceTimer_->setSingleShot(true);
    resourceTimer_->setInterval(500);
    connect(resourceWatcher_, &QFileSystemWatcher::fileChanged, resourceTimer_, qOverload<>(&QTimer::start));
    connect(resourceWatcher_, &QFileSystemWatcher::directoryChanged, resourceTimer_, qOverload<>(&QTimer::start));
    connect(resourceTimer_, &QTimer::timeout, this, [this] { applyRemoteResources(false); });
    statusBar()->addPermanentWidget(problems_);
    statusBar()->addPermanentWidget(summary_);
    statusBar()->setSizeGripEnabled(false);

    connect(configView_, &ConfigView::message, this,
            [this](const QString& text) { statusBar()->showMessage(text, 6000); });
    connect(configView_, &ConfigView::renameRequested, this, &MainWindow::renameItem);
    connect(configView_, &ConfigView::removeRequested, this, &MainWindow::removeItem);
    connect(configView_, &ConfigView::activityMoveRequested, this, &MainWindow::moveActivity);
    connect(configView_, &ConfigView::deviceMoveRequested, this, [this](const QString& id, int index) {
        if (!document_ || index < 0) return;
        applyEdit(tr("Move device"), [&](ConfigDocument& document) {
            return document.moveDevice(id.toStdString(), static_cast<std::size_t>(index));
        });
    });
    connect(configView_, &ConfigView::buttonEditRequested, this, &MainWindow::editButton);
    connect(configView_, &ConfigView::buttonRemoveRequested, this, &MainWindow::removeButton);
    connect(configView_, &ConfigView::buttonMoveRequested, this, &MainWindow::moveButton);
    connect(configView_, &ConfigView::commandAssignRequested, this, &MainWindow::assignCommand);
    connect(configView_, &ConfigView::activityEditRequested, this, &MainWindow::editActivity);
    connect(configView_, &ConfigView::devicePowerRequested, this,
            [this](const QString& deviceId) { editDeviceStates(deviceId, false); });
    connect(configView_, &ConfigView::deviceInputsRequested, this,
            [this](const QString& deviceId) { editDeviceStates(deviceId, true); });
    connect(configView_, &ConfigView::replaceDeviceRequested, this, &MainWindow::replaceDevice);
    connect(configView_, &ConfigView::compareDevicesRequested, this, &MainWindow::compareDevices);
    connect(configView_, &ConfigView::propertiesRequested, this, &MainWindow::editProperties);
    connect(configView_, &ConfigView::buttonCopyRequested, this, &MainWindow::copyButtons);
    connect(configView_, &ConfigView::notSentToggled, this, &MainWindow::setNotSent);
    configView_->setCommandLookup(
        [this](const std::string& deviceId, const std::string& name) -> std::optional<ConfigDocument::CommandDetails> {
            if (!document_) {
                return std::nullopt;
            }
            auto details = document_->commandDetails(deviceId, name);
            return details ? std::optional(std::move(*details)) : std::nullopt;
        });
    configView_->setCommandErrorLookup([this](const std::string& device, const std::string& command) {
        if (!document_) return QString();
        const auto details = document_->commandDetails(device, command);
        return details ? QString() : labels::errorText(details.error());
    });
    connect(configView_, &ConfigView::commandDetailsRequested, this, &MainWindow::showCommandDetails);
    connect(configView_, &ConfigView::learnRequested, this, &MainWindow::learnCommands);
    connect(configView_, &ConfigView::protocolCommandRequested, this, &MainWindow::protocolCommand);
    connect(configView_, &ConfigView::commandRemoveRequested, this, &MainWindow::removeCommand);
    connect(configView_, &ConfigView::addRequested, this, [this](ConfigView::Item item) {
        if (item == ConfigView::Item::Activity) {
            addActivity();
        } else {
            addDevices();
        }
    });
    connect(welcome_, &WelcomePage::createRequested, this, &MainWindow::createConfig);
    connect(welcome_, &WelcomePage::chooseRequested, this, &MainWindow::showStartDialog);
    connect(welcome_, &WelcomePage::demoRequested, this, &MainWindow::openDemo);
    connect(welcome_, &WelcomePage::connectRequested, this, &MainWindow::showConnectionHelp);

    // Пульт: проверяем подключение при запуске и время от времени, пока ничего не делаем.
    remote_ = new RemoteSession(this);
    irDatabase_ = new IrDatabaseService(this);
    connect(remote_, &RemoteSession::stateChanged, this, [this](bool connected, const QString& text) {
        remoteConnected_ = connected;
        header_->setConnectionState(connected, text);
        updateTitle();
    });
    connect(remote_, &RemoteSession::progress, this, &MainWindow::showRemoteProgress);
    connect(remote_, &RemoteSession::captureReadFinished, this, &MainWindow::saveCapture);
    connect(remote_, &RemoteSession::backupSaved, this, &MainWindow::backupSaved);
    connect(remote_, &RemoteSession::writeFinished, this, &MainWindow::remoteWritten);
    connect(remote_, &RemoteSession::failed, this, &MainWindow::remoteFailed);
    connect(remote_, &RemoteSession::idle, this, [this] {
        if (pendingRemoteAction_) {
            std::exchange(pendingRemoteAction_, {})();
        }
    });
    auto* probeTimer = new QTimer(this);
    probeTimer->setInterval(10'000);
    connect(probeTimer, &QTimer::timeout, this, [this] {
        // Проверку можно выключить в настройках (например, пульт подключают редко).
          if (settings::autoProbe() && isActiveWindow() && !remote_->busy()) {
            remote_->probe();
        }
    });
    probeTimer->start();
    // Несохранённые правки — версией раз в три минуты (если с прошлого раза что-то менялось).
    auto* autosaveTimer = new QTimer(this);
    autosaveTimer->setInterval(3 * 60 * 1000);
    connect(autosaveTimer, &QTimer::timeout, this, &MainWindow::autosave);
    autosaveTimer->start();
    QTimer::singleShot(500, this, [this] {
        if (settings::autoProbe()) {
            remote_->probe();
        }
    });

    // Ядро ничего не знает о Qt: связываем стек отмены с окном через обычный колбэк.
    undoStack_.setOnChanged([this] { updateUndoState(); });
    updateUndoState();
    updateTitle();

    restoreGeometry(QSettings().value(kGeometryKey).toByteArray());
}

void MainWindow::startup() {
    QTimer::singleShot(3000, this, [this] {
        if (settings::checkUpdates()) updater_->check();
    });
    // reload() создаёт resources и восстанавливает недостающие файлы перед чтением.
    applyRemoteResources(false);
    openStartupConfig();
}

void MainWindow::openStartupConfig() {
    // Конфигурация «открывать при запуске» — сразу; иначе по настройке:
    // последняя открытая или окно выбора.
    const QString folder = library::startupFolder();
    if (!folder.isEmpty() && openFolder(folder)) {
        return;
    }
    if (settings::startup() == settings::Startup::Last && !library::recent().isEmpty() &&
        openFolder(library::recent().constFirst())) {
        return;
    }
    showStartDialog();
}

void MainWindow::showStartDialog() {
    StartDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    switch (dialog.choice()) {
    case StartDialog::Choice::Open:
        openFolder(dialog.folder());
        break;
    case StartDialog::Choice::Create:
        createConfig();
        break;
    case StartDialog::Choice::Browse:
        browseConfig();
        break;
    case StartDialog::Choice::Demo:
        openDemo();
        break;
    case StartDialog::Choice::None:
        break;
    }
}

void MainWindow::createConfig() {
    NewConfigDialog dialog({}, this);
    // О несохранённых правках — после выбора: отказ от создания не должен спрашивать зря.
    if (dialog.exec() != QDialog::Accepted || !confirmDiscard()) {
        return;
    }

    // Основа: пустая, встроенный пример или копия существующей конфигурации.
    auto document = loadDocument(dialog.source(), dialog.name());
    if (!document) {
        QMessageBox::warning(this, tr("Could not create"),
                             labels::errorText(document.error()));
        return;
    }

    // U2: модель нового проекта выбирает пользователь. Выбор «Без модели» тоже решение
    // пользователя: для копии он убирает явное свойство основы, иначе остаётся модель
    // исходного файла. Если свойства нет, `targetRemote()` отвечает skin файла (M2).
    const auto target = document->setTargetRemote(dialog.modelId().toStdString());
    if (!target) {
        QMessageBox::warning(this, tr("Could not create"), labels::errorText(target.error()));
        return;
    }

    // Новая конфигурация сразу ложится на диск — дальше её можно просто сохранять.
    // Для копии «Сохранить как» переносит и остальные файлы пульта из основы.
    if (QFileInfo::exists(dialog.filePath())) {
        QMessageBox::warning(this, tr("Could not create"), tr("A configuration with this name already exists here."));
        return;
    }
    const auto saved = document->saveAsEzHex(toPath(dialog.filePath()));
    if (!saved) {
        QMessageBox::warning(this, tr("Could not create"),
                             labels::errorText(saved.error()));
        return;
    }
    library::touch(dialog.filePath());
    library::setLastDialogDir(QFileInfo(dialog.filePath()).absolutePath());
    showDocument(std::move(*document));
    statusBar()->showMessage(
        tr("Configuration created: %1").arg(QDir::toNativeSeparators(dialog.filePath())), 8000);
}

void MainWindow::browseConfig() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open configuration"), library::lastDialogDir(),
        tr("Remote configuration (*.EZHex *.ezhex *.remote.json UserConfiguration.xml);;Remote "
           "backup (*.EZHex *.ezhex *.remote.json);;XML files (*.xml);;All files (*)"));
    if (path.isEmpty()) {
        return; // пользователь передумал
    }
    library::setLastDialogDir(QFileInfo(path).absolutePath());
    openFolder(path);
}

bool MainWindow::openFolder(const QString& folder) {
    auto document = ConfigDocument::open(toPath(folder));
    journal::write(document ? journal::Level::Info : journal::Level::Error,
                   document ? tr("Opened: %1").arg(QDir::toNativeSeparators(folder))
                            : tr("Could not open %1: %2")
                                  .arg(QDir::toNativeSeparators(folder), labels::errorText(document.error())));
    if (!document) {
        QMessageBox::warning(this, tr("Could not open"),
                             labels::errorText(document.error()));
        return false;
    }
    // О несохранённых правках — когда файл уже прочитан: при ошибке открытия вопрос был бы зря.
    const bool hadChanges = document_ && !undoStack_.isClean();
    if (!confirmDiscard()) {
        return false;
    }
    if (hadChanges && undoStack_.isClean()) {
        // Правки сохранены, возможно, в этот же файл: прочитанное до сохранения устарело.
        document = ConfigDocument::open(toPath(folder));
        if (!document) {
            QMessageBox::warning(this, tr("Could not open"), labels::errorText(document.error()));
            return false;
        }
    }
    library::touch(document->location().empty() ? folder : fromPath(document->location()));
    const bool isHub = document->backend()->format() == ConfigFormat::Hub;
    showDocument(std::move(*document));
    statusBar()->showMessage(tr("Opened: %1").arg(QDir::toNativeSeparators(folder)), 8000);
    if (isHub) {
        resolveHubCodes();
    }
    return true;
}

void MainWindow::openDemo() {
    auto document = loadDemo(tr("Example configuration"));
    if (!document) {
        QMessageBox::warning(this, tr("Example"), labels::errorText(document.error()));
        return;
    }
    if (!confirmDiscard()) {
        return;
    }
    showDocument(std::move(*document));
    statusBar()->showMessage(
        tr("The built-in example is open. To keep it as your own configuration "
           "use “Save as”."),
        10000);
}

bool MainWindow::save() {
    if (!document_) {
        return false;
    }
    if (document_->ezhexFile().empty()) {
        return saveAs();
    }
    // Что лежало в файле — версией: к нему можно вернуться («Файл → Версии…»).
    if (auto kept = versions::keepOnDisk(*document_); !kept) {
        statusBar()->showMessage(tr("Could not keep the previous version: %1").arg(labels::errorText(kept.error())),
                                 8000);
    }
    const auto saved = document_->save();
    if (!saved) {
        journal::error(tr("Could not save: %1").arg(labels::errorText(saved.error())));
        QMessageBox::warning(this, tr("Could not save"),
                             labels::errorText(saved.error()));
        return false;
    }
    undoStack_.setClean();
    journal::info(tr("Saved: %1").arg(QDir::toNativeSeparators(fromPath(document_->location()))));
    statusBar()->showMessage(tr("Saved"), 4000);
    return true;
}

bool MainWindow::confirmDiscard() {
    if (!document_ || undoStack_.isClean()) {
        return true;
    }
    const auto answer = QMessageBox::question(
        this, tr("Unsaved changes"),
        tr("The configuration “%1” has unsaved changes. Save them?")
            .arg(QString::fromStdString(document_->name())),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) {
        return save();
    }
    if (answer == QMessageBox::Discard) {
        // На случай, если передумаете: правки остаются версией («Файл → Версии…»).
        (void)versions::save(*document_, versions::keyOf(*document_), versions::Kind::Discarded);
        return true;
    }
    return false;
}

void MainWindow::showDocument(ConfigDocument document) {
    // Сначала отвязываем экран от старой модели, потом заменяем её.
    configView_->setConfig(nullptr);
    document_ = std::make_unique<ConfigDocument>(std::move(document));
    ++documentSerial_;
    undoStack_.clear();
    configView_->setConfig(document_.get(), QString::fromStdString(document_->name()));
    screens_->setCurrentWidget(configView_);
    updateTitle();
    autosavedGeneration_ = editGeneration_;

    const auto& report = document_->importReport();
    for (const auto& warning : report.warnings) {
        journal::warning(QString::fromStdString(warning));
    }
    if (!report.warnings.empty()) {
        statusBar()->showMessage(QString::fromStdString(report.warnings.front()), 10000);
    }
    // Программа закрылась, не сохранив правки (сбой, выключение)? Предложить автосохранение.
    QTimer::singleShot(0, this, &MainWindow::offerAutosave);
}

void MainWindow::closeConfig() {
    configView_->setConfig(nullptr);
    document_.reset();
    undoStack_.clear();
    screens_->setCurrentWidget(welcome_);
    updateTitle();
}

void MainWindow::updateTitle() {
    const bool open = document_ != nullptr;
    // Право записи решает ядро (canWriteTo): каталог и формат документа. Профиль оформления
    // на него не влияет (W3).
    const auto decision = open ? harmony::canWriteTo(*document_) : harmony::WriteDecision{};
    const bool canWrite = open && decision.verdict == harmony::WriteVerdict::Allowed;
    const QString writing = open ? labels::writeVerdict(decision.verdict) : QString();
    saveAction_->setEnabled(open);
    saveAsAction_->setEnabled(open);
    closeAction_->setEnabled(open);
    recoverIrAction_->setEnabled(open);
    if (open) selectedRemote_ = QString::fromStdString(document_->targetRemote());
    // Связь выбирается по модели документа (U4b): решение принимает фабрика связи.
    const auto remoteModel = harmony::modelInfo(selectedRemote_.toStdString());
    remote_->setModel(remoteModel);
    writeRemoteAction_->setEnabled(canWrite);
    writeRemoteAction_->setToolTip(writing);
    // Чтение — сбор сведений с любого подключённого пульта: профиль и целевая модель на него не влияют.
    readRemoteAction_->setEnabled(true);
    header_->setWriteEnabled(canWrite, writing);
    header_->setReadSavesFile(true);
    const auto link = remoteModel.link;
    if (link == LinkKind::None && remote_->connectionStatus().isEmpty())
        header_->setConnectionState(false, tr("No connection driver"));
    else if (link == LinkKind::UsbHid && remote_->connectionStatus().isEmpty())
        header_->setConnectionState(false, tr("USB HID · connect and read"));
    checkConfigAction_->setEnabled(open);
    versionsAction_->setEnabled(open);
    compareConfigsAction_->setEnabled(open);
    quickSearchAction_->setEnabled(open);

    const auto displayName = modelDisplayName(selectedRemote_.toStdString());
    const QString modelName = displayName.empty() ? tr("No model selected")
                                                 : QString::fromStdString(displayName);
    if (!open) {
        setWindowTitle(tr("Harmony — %1").arg(modelName));
        summary_->clear();
        return;
    }

    const QString name = QString::fromStdString(document_->name());
    setWindowTitle(tr("%1 [*] — %2 — Harmony").arg(name, modelName));
    configView_->setDocumentName(name); // после «Сохранить как» имя меняется

    const auto& model = document_->model();
    QString summary =
        labels::plural(static_cast<qsizetype>(model.devices.size()), "device", "devices") +
        " · " +
        (model.property("ActivitiesUndetermined") == "true" ? tr("activity count undetermined") :
         labels::plural(static_cast<qsizetype>(model.userActivityCount()), "activity", "activities"));
    if (!document_->hasLocation()) {
        summary = tr("%1 · not saved").arg(summary);
    }
    summary_->setText(summary);
    // Полный путь — в подсказке: в строке состояния он налезал бы на сообщения.
    summary_->setToolTip(document_->hasLocation()
                             ? QDir::toNativeSeparators(fromPath(document_->location()))
                             : tr("The configuration is not saved to disk yet"));
}

bool MainWindow::saveAs() {
    if (!document_) {
        return false;
    }
    const QString oldLocation = fromPath(document_->location());
    const QString suggested = document_->ezhexFile().empty()
        ? QDir(library::lastDialogDir()).filePath(QString::fromStdString(document_->name()) + ".EZHex")
        : fromPath(document_->ezhexFile());
    QFileDialog dialog(this, tr("Save as"), suggested, tr("Harmony configuration (*.EZHex *.ezhex)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix(QStringLiteral("EZHex"));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return false;
    QString file = dialog.selectedFiles().first();
    if (!file.endsWith(".EZHex", Qt::CaseInsensitive)) {
        file += ".EZHex";
        if (QFileInfo::exists(file) && QMessageBox::question(this, tr("Save as"),
            tr("Replace the existing file %1?").arg(QDir::toNativeSeparators(file)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;
    }
    if (!document_->ezhexFile().empty() && library::normalized(file) == library::normalized(oldLocation))
        return save();
    library::setLastDialogDir(QFileInfo(file).absolutePath());
    const auto saved = document_->saveAsEzHex(toPath(file));
    journal::write(saved ? journal::Level::Info : journal::Level::Error,
                   saved ? tr("Saved: %1").arg(QDir::toNativeSeparators(file))
                         : tr("Could not save: %1").arg(labels::errorText(saved.error())));
    if (!saved) {
        QMessageBox::warning(this, tr("Could not save"), labels::errorText(saved.error()));
        return false;
    }
    if (!oldLocation.isEmpty() && library::startupFolder() == library::normalized(oldLocation))
        library::setStartupFolder(file);
    library::touch(file);
    undoStack_.setClean();
    updateTitle();
    statusBar()->showMessage(tr("Saved to %1").arg(QDir::toNativeSeparators(file)), 8000);
    return true;
}

bool MainWindow::applyEdit(const QString& text,
                           const std::function<Result<void>(ConfigDocument&)>& change) {
    if (!document_) {
        return false;
    }
    auto command = undo::editDocument(*document_, text.toStdString(), change);
    if (!command) {
        journal::error(tr("%1: %2").arg(text, labels::errorText(command.error())));
        QMessageBox::warning(this, tr("Harmony"), labels::errorText(command.error()));
        return false;
    }
    journal::info(text);
    undoStack_.push(std::move(*command)); // правка уже применена, push её не повторяет
    configView_->refresh();
    return true;
}

void MainWindow::undo() {
    if (document_ && undoStack_.canUndo()) {
        journal::info(tr("Undo: %1").arg(QString::fromStdString(undoStack_.undoText())));
        try {
            undoStack_.undo();
        } catch (const std::exception& e) {
            // Снимок не восстановился: документ и история не изменились
            QMessageBox::warning(this, tr("Undo"), QString::fromStdString(e.what()));
            return;
        }
        configView_->refresh();
    }
}

void MainWindow::redo() {
    if (document_ && undoStack_.canRedo()) {
        journal::info(tr("Redo: %1").arg(QString::fromStdString(undoStack_.redoText())));
        try {
            undoStack_.redo();
        } catch (const std::exception& e) {
            // Снимок не восстановился: документ и история не изменились
            QMessageBox::warning(this, tr("Redo"), QString::fromStdString(e.what()));
            return;
        }
        configView_->refresh();
    }
}

void MainWindow::updateUndoState() {
    ++editGeneration_;
    header_->setUndoState(undoStack_.canUndo(), QString::fromStdString(undoStack_.undoText()),
                          undoStack_.canRedo(), QString::fromStdString(undoStack_.redoText()));
    setWindowModified(!undoStack_.isClean());
    updateTitle(); // целевой пульт тоже может измениться при отмене или восстановлении
    if (problemsTimer_ != nullptr) {
        problemsTimer_->start(); // любая правка, отмена или другой документ
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!exitingForUpdate_ && !confirmDiscard()) {
        event->ignore();
        return;
    }
    QSettings().setValue(kGeometryKey, saveGeometry());
    // Фоновые потоки обращаются к окну и к базе ИК-кодов: дождаться их до разрушения окна.
    for (QThread* thread : {dbImportThread_.data(), remoteLogThread_.data()}) {
        if (thread != nullptr) {
            thread->wait();
        }
    }
    event->accept();
}

} // namespace harmony::app
