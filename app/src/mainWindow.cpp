/**
 * @file mainWindow.cpp
 * @brief Реализация главного окна.
 */

#include "mainWindow.h"
#include "widgets/configTransferDialog.h"
#include "harmony/core/remoteProfile.h"

#include "harmony/core/undo/documentEdit.h"

#include "appSettings.h"
#include "remoteResources.h"
#include "configLibrary.h"
#include "irDatabaseService.h"
#include "documentSource.h"
#include "remoteSession.h"
#include "harmony/core/version.h"
#include "harmony/transport/remoteLink.h"
#include "help.h"
#include "i18n.h"
#include "labels.h"
#include "nameCheck.h"
#include "modern/remoteView.h"
#include "widgets/aboutDialog.h"
#include "widgets/activityDialog.h"
#include "widgets/addDeviceDialog.h"
#include "widgets/commandDialog.h"
#include "widgets/newDeviceDialog.h"
#include "widgets/appHeader.h"
#include "widgets/buttonDialog.h"
#include "widgets/configView.h"
#include "widgets/newConfigDialog.h"
#include "widgets/settingsDialog.h"
#include "widgets/startDialog.h"
#include "widgets/welcomePage.h"
#include "widgets/checkDialog.h"
#include "widgets/deviceStatesDialog.h"
#include "widgets/compareConfigsDialog.h"
#include "widgets/compareDevicesDialog.h"
#include "widgets/copyButtonsDialog.h"
#include "widgets/journalDialog.h"
#include "widgets/learnWizardDialog.h"
#include "widgets/propertiesDialog.h"
#include "widgets/protocolCommandDialog.h"
#include "widgets/replaceDeviceDialog.h"
#include "widgets/quickSearch.h"
#include "widgets/versionsDialog.h"
#include "journal.h"
#include "linuxUsbSetup.h"
#include "versionStore.h"
#include "widgets/writePreviewDialog.h"
#include "widgets/zoneEditorDialog.h"
#include "harmony/core/configDiff.h"

#include <algorithm>

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QProcess>
#include <QMessageBox>
#include <QGuiApplication>
#include <QClipboard>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QFontMetrics>
#include <QStatusBar>
#include <QDesktopServices>
#include <QFileSystemWatcher>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QLocale>
#include <QTimer>
#include <QStyle>
#include <QThread>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <utility>

namespace harmony::app {

namespace {

/// Ключ настроек: размер и положение окна.
constexpr const char* kGeometryKey = "window/geometry";

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
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
    connect(remote_, &RemoteSession::readFinished, this, &MainWindow::openFromRemote);
    connect(remote_, &RemoteSession::hidReadFinished, this, &MainWindow::saveHidBackup);
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
          if (usesHarmony900(selectedRemote_.toStdString()) && settings::autoProbe() && isActiveWindow() && !remote_->busy()) {
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
        if (usesHarmony900(selectedRemote_.toStdString()) && settings::autoProbe()) {
            remote_->probe();
        }
    });

    // Ядро ничего не знает о Qt: связываем стек отмены с окном через обычный колбэк.
    undoStack_.setOnChanged([this] { updateUndoState(); });
    updateUndoState();
    updateTitle();

    restoreGeometry(QSettings().value(kGeometryKey).toByteArray());
}

void MainWindow::createActions() {
    // --- Действия с конфигурацией (показываются в строке меню, см. createMenuBar) ---
    auto makeAction = [this](const QString& text) { return new QAction(text, this); };

    QAction* chooseAction = makeAction(tr("Recent configurations…"));
    connect(chooseAction, &QAction::triggered, this, &MainWindow::showStartDialog);

    QAction* newAction = makeAction(tr("New…"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::createConfig);

    QAction* openAction = makeAction(tr("Open…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::browseConfig);

    QAction* demoAction = makeAction(tr("Open example configuration"));
    connect(demoAction, &QAction::triggered, this, &MainWindow::openDemo);

    saveAction_ = makeAction(tr("Save"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, [this] { save(); });

    saveAsAction_ = makeAction(tr("Save as…"));
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, [this] { saveAs(); });

    versionsAction_ = makeAction(tr("Versions…"));
    versionsAction_->setToolTip(tr("Earlier states of the configuration: autosave, before saving — and "
                                   "restoring them"));
    connect(versionsAction_, &QAction::triggered, this, &MainWindow::showVersions);

    compareConfigsAction_ = makeAction(tr("Compare with…"));
    compareConfigsAction_->setToolTip(tr("How the open configuration differs from another one: a recent one or a "
                                         "file"));
    connect(compareConfigsAction_, &QAction::triggered, this, &MainWindow::compareWithOther);

    quickSearchAction_ = makeAction(tr("Quick search…"));
    quickSearchAction_->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_K), QKeySequence::Find});
    connect(quickSearchAction_, &QAction::triggered, this, &MainWindow::showQuickSearch);

    closeAction_ = makeAction(tr("Close"));
    closeAction_->setShortcut(QKeySequence::Close);
    connect(closeAction_, &QAction::triggered, this, [this] {
        if (confirmDiscard()) {
            closeConfig();
        }
    });

    QAction* aboutAction = makeAction(tr("About"));
    aboutAction->setMenuRole(QAction::AboutRole); // на macOS роль по переведённому тексту не угадывается
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    QAction* quitAction = makeAction(tr("Exit"));
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setMenuRole(QAction::QuitRole);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    // --- Меню пульта (по кнопке состояния справа) ---
    auto* connectionMenu = new QMenu(this);
    transferAction_ = connectionMenu->addAction(tr("Configuration files — all models…"));
    connect(transferAction_, &QAction::triggered, this, [this] {
        if (remote_->busy()) {
            QMessageBox::information(this, tr("Remote"), tr("Wait for the current remote operation to finish."));
            return;
        }
        ConfigTransferDialog dialog(this);
        dialog.exec();
        // Прочитанный с хаба файл сразу открывается в редакторе.
        const QString hubFile = dialog.hubFile();
        if (!hubFile.isEmpty() && confirmDiscard()) {
            openFolder(hubFile);
        }
    });
    chooseRemoteAction_ = connectionMenu->addAction(tr("Change target remote…"));
    connect(chooseRemoteAction_, &QAction::triggered, this, &MainWindow::chooseRemote);
    readRemoteAction_ = connectionMenu->addAction(tr("Read configuration from remote"));
    readRemoteAction_->setToolTip(tr("Reads the configuration from the remote, saves it as a backup and "
                                     "opens it"));
    connect(readRemoteAction_, &QAction::triggered, this, &MainWindow::readFromRemote);
    writeRemoteAction_ = connectionMenu->addAction(tr("Write to remote…"));
    connect(writeRemoteAction_, &QAction::triggered, this, &MainWindow::writeToRemote);
    checkConfigAction_ = connectionMenu->addAction(tr("Check configuration…"));
    checkConfigAction_->setShortcut(Qt::Key_F7);
    checkConfigAction_->setToolTip(tr("What will not work on the remote — and fix it"));
    connect(checkConfigAction_, &QAction::triggered, this, &MainWindow::showCheck);
    recoverIrAction_ = connectionMenu->addAction(tr("Recover IR data from backup…"));
    connect(recoverIrAction_, &QAction::triggered, this, [this] {
        if (!document_) return;
        const auto file = QFileDialog::getOpenFileName(this, tr("Recover IR data from backup"),
                                                       library::backupFolder(), tr("Remote backup (*.EZHex);;All files (*)"));
        if (file.isEmpty()) return;
        auto source = ConfigDocument::open(toPath(file));
        if (!source) {
            QMessageBox::warning(this, tr("Recover IR data"), labels::errorText(source.error()));
            return;
        }
        applyEdit(tr("Recover IR data"), [&source](ConfigDocument& document) -> Result<void> {
            const auto recovered = document.recoverIrTables(*source);
            return recovered ? Result<void>() : std::unexpected(recovered.error());
        });
    });
    connectionMenu->addSeparator();
    QAction* checkAction = connectionMenu->addAction(tr("Test connection"));
    connect(checkAction, &QAction::triggered, this, [this] {
        if (usesHarmony900(selectedRemote_.toStdString())) remote_->probe();
        else showConnectionHelp();
    });
    QAction* backupsAction = connectionMenu->addAction(tr("Backups folder"));
    connect(backupsAction, &QAction::triggered, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(library::backupFolder())); });
    QAction* helpAction = connectionMenu->addAction(tr("How to connect the remote…"));
    connect(helpAction, &QAction::triggered, this, &MainWindow::showConnectionHelp);
    if (linuxUsb::available()) {
        usbSetupAction_ = connectionMenu->addAction(tr("Set up USB connection…"));
        connect(usbSetupAction_, &QAction::triggered, this, [this] { linuxUsb::install(this); });
    }

    // --- Отмена и повтор: только горячие клавиши, кнопки — в шапке ---
    auto* undoAction = new QAction(tr("Undo"), this);
    undoAction->setShortcuts({QKeySequence::Undo});
    connect(undoAction, &QAction::triggered, this, [this] { undo(); });

    auto* redoAction = new QAction(tr("Redo"), this);
    redoAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y),
                              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    connect(redoAction, &QAction::triggered, this, [this] { redo(); });

    // Без строки меню горячие клавиши работают, только если действия добавлены в само окно.
    addActions({newAction, openAction, saveAction_, saveAsAction_, closeAction_, quitAction,
                undoAction, redoAction, checkConfigAction_, quickSearchAction_});

    createMenuBar({chooseAction, newAction, openAction, demoAction}, quitAction, undoAction, redoAction,
                  checkAction, backupsAction, helpAction, aboutAction);

    header_ = new AppHeader(connectionMenu, this);
    connect(header_, &AppHeader::readRequested, this, &MainWindow::readFromRemote);
    connect(header_, &AppHeader::undoRequested, this, [this] { undo(); });
    connect(header_, &AppHeader::redoRequested, this, [this] { redo(); });
    connect(header_, &AppHeader::writeRequested, this, &MainWindow::writeToRemote);
}

void MainWindow::createMenuBar(const QList<QAction*>& openActions, QAction* quitAction, QAction* undoAction,
                               QAction* redoAction, QAction* checkAction, QAction* backupsAction,
                               QAction* helpAction, QAction* aboutAction) {
    // Обычная строка меню: все команды программы в одном месте (шапка — для самых частых).
    QMenuBar* bar = menuBar();

    // --- Файл ---
    QMenu* file = bar->addMenu(tr("&File"));
    file->addActions(openActions);
    QMenu* recent = file->addMenu(tr("Recent"));
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        const QStringList list = library::recent();
        for (const QString& path : list) {
            QAction* action = recent->addAction(QDir::toNativeSeparators(path));
            connect(action, &QAction::triggered, this, [this, path] {
                if (confirmDiscard()) {
                    openFolder(path);
                }
            });
        }
        if (list.isEmpty()) {
            recent->addAction(tr("(empty)"))->setEnabled(false);
        }
    });
    file->addSeparator();
    file->addActions({saveAction_, saveAsAction_});
    file->addSeparator();
    file->addAction(versionsAction_);
    file->addAction(compareConfigsAction_);
    file->addSeparator();
    file->addAction(closeAction_);
    file->addSeparator();
    file->addAction(quitAction);

    // --- Правка: отмена и действия над выбранным слева объектом ---
    QMenu* edit = bar->addMenu(tr("&Edit"));
    edit->addActions({undoAction, redoAction});
    edit->addSeparator();
    // Буфер обмена: кнопки (на вкладке «Кнопки») и команды устройства (на вкладке «Команды»).
    QAction* cut = edit->addAction(tr("Cut"));
    cut->setShortcut(QKeySequence::Cut);
    QAction* copy = edit->addAction(tr("Copy"));
    copy->setShortcut(QKeySequence::Copy);
    QAction* paste = edit->addAction(tr("Paste"));
    paste->setShortcut(QKeySequence::Paste);
    connect(cut, &QAction::triggered, this, [this] { copySelection(true); });
    connect(copy, &QAction::triggered, this, [this] { copySelection(false); });
    connect(paste, &QAction::triggered, this, &MainWindow::pasteClipboard);
    edit->addSeparator();
    edit->addAction(quickSearchAction_);
    edit->addSeparator();
    QAction* rename = edit->addAction(tr("Rename…"));
    rename->setShortcut(Qt::Key_F2);
    QAction* remove = edit->addAction(tr("Delete…"));
    remove->setShortcut(QKeySequence::Delete);
    QAction* compose = edit->addAction(tr("Activity setup…"));
    edit->addSeparator();
    QAction* addDevice = edit->addAction(tr("Add device…"));
    QAction* newDevice = edit->addAction(tr("New device…"));
    QAction* addActivity = edit->addAction(tr("Add activity…"));
    edit->addSeparator();
    QAction* learn = edit->addAction(tr("Learn commands…"));
    connect(edit, &QMenu::aboutToShow, this, [=, this] {
        const auto selected = configView_->selection();
        const bool open = document_ != nullptr;
        const bool activity = selected && selected->kind == ConfigView::Item::Activity;
        const model::Activity* current =
            activity && open ? document_->model().findActivity(selected->id) : nullptr;
        const bool powerOff = current != nullptr && current->isPowerOff();
        undoAction->setEnabled(undoStack_.canUndo());
        redoAction->setEnabled(undoStack_.canRedo());
        undoAction->setText(undoStack_.canUndo()
                                ? tr("Undo: %1").arg(QString::fromStdString(undoStack_.undoText()))
                                : tr("Undo"));
        redoAction->setText(undoStack_.canRedo()
                                ? tr("Redo: %1").arg(QString::fromStdString(undoStack_.redoText()))
                                : tr("Redo"));
        cut->setEnabled(canCopy(true));
        copy->setEnabled(canCopy(false));
        paste->setEnabled(canPaste());
        rename->setEnabled(selected.has_value());
        remove->setEnabled(selected.has_value() && !powerOff);
        compose->setEnabled(activity && !powerOff);
        learn->setEnabled(selected && selected->kind == ConfigView::Item::Device);
        for (QAction* action : {addDevice, newDevice, addActivity}) {
            action->setEnabled(open);
        }
    });
    // После закрытия меню пункты снова включены: горячие клавиши проверяют всё сами.
    connect(edit, &QMenu::aboutToHide, this, [=] {
        for (QAction* action : {undoAction, redoAction, cut, copy, paste, rename, remove, compose, addDevice,
                                newDevice, addActivity, learn}) {
            action->setEnabled(true);
        }
    });
    connect(rename, &QAction::triggered, this, [this] {
        if (const auto selected = configView_->selection()) {
            renameItem(selected->kind, QString::fromStdString(selected->id));
        }
    });
    connect(remove, &QAction::triggered, this, [this] {
        if (const auto selected = configView_->selection()) {
            removeItem(selected->kind, QString::fromStdString(selected->id));
        }
    });
    connect(compose, &QAction::triggered, this, [this] {
        if (const auto selected = configView_->selection(); selected && selected->kind == ConfigView::Item::Activity) {
            editActivity(QString::fromStdString(selected->id));
        }
    });
    connect(addDevice, &QAction::triggered, this, &MainWindow::addDevices);
    connect(newDevice, &QAction::triggered, this, &MainWindow::createDevice);
    connect(addActivity, &QAction::triggered, this, &MainWindow::addActivity);
    connect(learn, &QAction::triggered, this, [this] {
        if (const auto selected = configView_->selection(); selected && selected->kind == ConfigView::Item::Device) {
            learnCommands(QString::fromStdString(selected->id));
        }
    });

    // --- Вид ---
    QMenu* view = bar->addMenu(tr("&View"));
    QAction* overview = view->addAction(tr("Remote overview"));
    overview->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(overview, &QAction::triggered, this, [this] {
        if (document_) {
            configView_->showOverview();
        }
    });

    // --- Пульт ---
    QMenu* remote = bar->addMenu(tr("&Remote"));
    remote->addAction(chooseRemoteAction_);
    auto* reloadResources = remote->addAction(tr("Reload remote resources"));
    connect(reloadResources, &QAction::triggered, this, &MainWindow::reloadRemoteResources);
    auto* exportResources = remote->addAction(tr("Export remote resources…"));
    exportResources->setToolTip(tr("Copy the photo, zones and profile to a folder to edit them without rebuilding"));
    connect(exportResources, &QAction::triggered, this, &MainWindow::exportRemoteResources);
    auto* openResources = remote->addAction(tr("Open remote resources folder"));
    connect(openResources, &QAction::triggered, this, &MainWindow::openRemoteResourcesFolder);
    auto* zoneEditor = remote->addAction(tr("Edit remote zones…"));
    zoneEditor->setToolTip(tr("Edit button zones and screen layouts of a remote model on its photo"));
    connect(zoneEditor, &QAction::triggered, this, &MainWindow::editRemoteZones);
    remote->addSeparator();
    remote->addActions({readRemoteAction_, writeRemoteAction_, checkConfigAction_, recoverIrAction_});
    remote->addAction(transferAction_);
    remote->addSeparator();
    QAction* remoteSettingsAction = remote->addAction(tr("Remote settings…"));
    remoteSettingsAction->setToolTip(tr("Key beep, backlight, font, theme, clock, child lock"));
    connect(remoteSettingsAction, &QAction::triggered, this, &MainWindow::showRemoteSettings);
    QAction* remoteLogAction = remote->addAction(tr("Remote system log…"));
    remoteLogAction->setToolTip(tr("The remote's own log: what it sent and why it rejected a configuration"));
    connect(remoteLogAction, &QAction::triggered, this, &MainWindow::showRemoteLog);
    remote->addSeparator();
    remote->addActions({checkAction, backupsAction});
    remote->addSeparator();
    remote->addAction(helpAction);
    if (usbSetupAction_ != nullptr) {
        remote->addAction(usbSetupAction_);
    }

    // --- Настройки ---
    QMenu* options = bar->addMenu(tr("&Settings"));
    QAction* settingsAction = options->addAction(tr("Settings…"));
    settingsAction->setShortcut(QKeySequence::Preferences);
    settingsAction->setMenuRole(QAction::PreferencesRole);
    connect(settingsAction, &QAction::triggered, this, [this] { showSettings(); });
    QAction* resetAction = options->addAction(tr("Reset settings…"));
    connect(resetAction, &QAction::triggered, this, [this] {
        if (QMessageBox::question(this, tr("Reset settings"),
                                  tr("Reset the settings to their defaults? The list of configurations "
                                     "stays.")) == QMessageBox::Yes) {
            settings::reset();
            reloadRemoteResources();
            statusBar()->showMessage(tr("Settings reset."), 5000);
        }
    });

    // --- Справка ---
    QMenu* help = bar->addMenu(tr("&Help"));
    QAction* contentsAction = help->addAction(tr("Help contents"));
    contentsAction->setShortcut(QKeySequence::HelpContents);
    contentsAction->setStatusTip(tr("Help on the open page; the same key in a dialog opens help on that dialog"));
    connect(contentsAction, &QAction::triggered, this, &MainWindow::showHelp);
    help->addAction(helpAction);
    QAction* journalAction = help->addAction(tr("Program journal…"));
    journalAction->setToolTip(tr("What the program did and what went wrong"));
    connect(journalAction, &QAction::triggered, this, [this] {
        // Одно немодальное окно: журнал можно держать открытым рядом.
        if (journalDialog_ == nullptr) {
            journalDialog_ = new JournalDialog(this);
        }
        journalDialog_->show();
        journalDialog_->raise();
        journalDialog_->activateWindow();
    });
    help->addSeparator();
    help->addAction(aboutAction);
    QAction* aboutQt = help->addAction(tr("About Qt"));
    aboutQt->setMenuRole(QAction::AboutQtRole);
    connect(aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);

    // Горячие клавиши пунктов меню работают и без открытия меню.
    addActions({rename, remove, overview, settingsAction, contentsAction});
}

void MainWindow::showSettings(SettingsPage page) {
    SettingsDialog dialog(irDatabase_, this);
    if (page == SettingsPage::IrDatabase) {
        dialog.showIrDatabase();
    }
    if (dialog.exec() != QDialog::Accepted) return;
    reloadRemoteResources();
    if (!dialog.languageChanged()) return;
    // Тексты окон созданы на старом языке: проще и надёжнее перезапуститься.
    const auto answer = QMessageBox::question(this, tr("Language"),
                                              tr("The new language takes effect after a restart. Restart now?"));
    if (answer == QMessageBox::Yes && confirmDiscard()) {
        undoStack_.setClean(); // уже спросили о несохранённых изменениях
        QProcess::startDetached(QApplication::applicationFilePath(), QApplication::arguments().mid(1));
        close();
    }
}

void MainWindow::reloadRemoteResources() {
    applyRemoteResources(true);
}

void MainWindow::applyRemoteResources(bool interactive) {
    if (!interactive && remote_->busy()) {
        resourceTimer_->start(); // идёт обмен с пультом — попробуем позже
        return;
    }
    if (interactive && !remoteReady([this] { reloadRemoteResources(); })) return;
    const QString error = remoteResources::reload();
    // Сохранение с переименованием снимает наблюдение с файла: подписываемся заново.
    if (const auto watched = resourceWatcher_->files() + resourceWatcher_->directories(); !watched.isEmpty())
        resourceWatcher_->removePaths(watched);
    if (const auto paths = remoteResources::watchPaths(); !paths.isEmpty()) resourceWatcher_->addPaths(paths);
    if (!error.isEmpty()) {
        if (interactive) {
            QMessageBox::warning(this, tr("Remote resources"),
                tr("Resources were not updated. The previous resources are still in use.\n\n%1").arg(error));
        } else {
            journal::warning(tr("Remote resources were not updated: %1").arg(error));
            statusBar()->showMessage(tr("Remote resources were not updated: %1").arg(error), 15000);
        }
        return;
    }
    configView_->refresh();
    updateTitle();
    statusBar()->showMessage(tr("Remote resources updated"), 4000);
}

void MainWindow::exportRemoteResources() {
    const auto* profile = remoteProfile(selectedRemote_.toStdString());
    QMessageBox ask(QMessageBox::Question, tr("Export remote resources"),
                    tr("Which resources to export?"), QMessageBox::Cancel, this);
    ask.setInformativeText(
        tr("Export the current resources to the working resources folder. Existing files are replaced "
           "only after confirmation."));
    QPushButton* modelButton = nullptr;
    if (profile) {
        modelButton = ask.addButton(tr("Model %1").arg(QString::fromStdString(profile->name)),
                                    QMessageBox::AcceptRole);
    }
    QPushButton* allButton = ask.addButton(tr("All resources"), QMessageBox::AcceptRole);
    ask.setDefaultButton(modelButton ? modelButton : allButton);
    ask.exec();
    const bool onlyModel = modelButton != nullptr && ask.clickedButton() == modelButton;
    if (!onlyModel && ask.clickedButton() != allButton) return;
    const QStringList files = remoteResources::exportFiles(onlyModel ? selectedRemote_ : QString());
    if (files.isEmpty()) return;

    const QString target = remoteResources::folder();
    QStringList existing;
    for (const QString& file : files) {
        if (QFileInfo::exists(QDir(target).filePath(file))) existing.append(QDir::toNativeSeparators(file));
    }
    if (!existing.isEmpty()) {
        QString list = existing.mid(0, 10).join('\n');
        if (existing.size() > 10) list += QStringLiteral("\n…");
        if (QMessageBox::question(this, tr("Export remote resources"),
                                  tr("These files already exist and will be replaced:\n\n%1").arg(list)) !=
            QMessageBox::Yes) {
            return;
        }
    }
    QString error = remoteResources::exportTo(target, files);
    if (error.isEmpty()) {
        // Описание структуры: на языке интерфейса, если есть, иначе английское.
        QFile readme(QStringLiteral(":/res/export/README.%1.md").arg(i18n::current()));
        if (!readme.exists()) readme.setFileName(QStringLiteral(":/res/export/README.en.md"));
        QSaveFile out(QDir(target).filePath(QStringLiteral("HARMONY-RESOURCES.md")));
        if (!readme.open(QIODevice::ReadOnly)) {
            error = readme.fileName() + ": " + readme.errorString();
        } else if (!out.open(QIODevice::WriteOnly) || out.write(readme.readAll()) < 0 || !out.commit()) {
            error = out.fileName() + ": " + out.errorString();
        }
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Export remote resources"), tr("Could not export: %1").arg(error));
        return;
    }
    journal::info(tr("Remote resources exported: %1").arg(QDir::toNativeSeparators(target)));
    reloadRemoteResources();
    statusBar()->showMessage(tr("Remote resources exported"), 4000);
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

void MainWindow::editRemoteZones() {
    // Одно немодальное окно: повторный вызов выводит его на передний план.
    if (zoneEditorDialog_ != nullptr) {
        zoneEditorDialog_->show();
        zoneEditorDialog_->raise();
        zoneEditorDialog_->activateWindow();
        return;
    }
    if (!remoteReady([this] { editRemoteZones(); })) return;
    auto* dialog = new ZoneEditorDialog(selectedRemote_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    zoneEditorDialog_ = dialog;
    // Файлы записаны редактором: перечитать снимок, чтобы пульт на экране перерисовался.
    connect(dialog, &ZoneEditorDialog::resourcesSaved, this, [this] { applyRemoteResources(false); });
    dialog->show();
}

void MainWindow::openRemoteResourcesFolder() {
    const QString folder = remoteResources::folder();
    if (folder.isEmpty() || !QDir(folder).exists()) {
        QMessageBox::information(this, tr("Remote resources"),
            tr("Could not open the resources folder: %1").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MainWindow::startup() {
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
        if (confirmDiscard()) {
            openFolder(dialog.folder());
        }
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
        tr("Remote configuration (*.EZHex *.ezhex UserConfiguration.xml);;Remote "
           "backup (*.EZHex *.ezhex);;XML files (*.xml);;All files (*)"));
    if (path.isEmpty()) {
        return; // пользователь передумал
    }
    library::setLastDialogDir(QFileInfo(path).absolutePath());
    if (confirmDiscard()) {
        openFolder(path);
    }
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
    library::touch(fromPath(document->location()));
    const bool isHub = document->backend()->format() == ConfigFormat::Hub;
    showDocument(std::move(*document));
    statusBar()->showMessage(tr("Opened: %1").arg(QDir::toNativeSeparators(folder)), 8000);
    if (isHub) {
        resolveHubCodes();
    }
    return true;
}

void MainWindow::resolveHubCodes() {
    // У команд хаба есть только KeyCode: IR-коды берём из базы (одна правка, отменяется).
    const irdb::IrDatabase* database = irDatabase_->database();
    if (!document_ || database == nullptr) {
        statusBar()->showMessage(
            tr("Hub configuration opened without IR codes: the IR database is not available."), 10000);
        return;
    }
    HubCodesReport report;
    const bool applied = applyEdit(tr("Resolve hub IR codes"), [&](ConfigDocument& document) -> Result<void> {
        auto resolved = document.resolveHubCodes(database->protocols());
        if (!resolved) {
            return std::unexpected(std::move(resolved.error()));
        }
        report = std::move(*resolved);
        return {};
    });
    if (!applied) {
        return;
    }
    for (const std::string& skipped : report.skipped) {
        journal::warning(tr("IR code not resolved: %1").arg(QString::fromStdString(skipped)));
    }
    statusBar()->showMessage(tr("Hub IR codes: %1 as protocol, %2 as signal, %3 skipped")
                                 .arg(report.asProtocol)
                                 .arg(report.asSignal)
                                 .arg(report.skipped.size()),
                             10000);
    matchStockHubDevices(*database);
}

void MainWindow::matchStockHubDevices(const irdb::IrDatabase& database) {
    // Стоковый хаб отдаёт только имена команд: коды берём у такой же модели из базы.
    struct Pending {
        std::string id;
        QString name;
        std::string query;
        std::string modelOnly;
        std::vector<std::string> commandNames; ///< Имена команд для ранжирования кандидатов
    };
    std::vector<Pending> pending;
    for (const model::Device& device : document_->model().devices) {
        const bool needsCodes = std::ranges::any_of(device.commands, [](const model::Command& command) {
            return command.code.empty() && command.native.empty() &&
                   (command.transport == model::Transport::Ir || command.transport == model::Transport::Unknown);
        });
        if (needsCodes) {
            std::vector<std::string> names;
            for (const model::Command& command : device.commands) {
                names.push_back(command.name);
            }
            pending.push_back({device.id, QString::fromStdString(device.displayName()),
                               device.manufacturer + " " + device.model, device.model, std::move(names)});
        }
    }
    int matched = 0;
    int unmatched = 0;
    for (const Pending& item : pending) {
        std::vector<std::size_t> found = database.search(item.query, 30);
        if (found.empty() && !item.modelOnly.empty()) {
            found = database.search(item.modelOnly, 30);
        }
        if (found.empty()) {
            ++unmatched;
            journal::warning(tr("No device in the IR database for “%1”").arg(item.name));
            continue;
        }
        // Лучше подходит та модель, у которой совпало больше имён команд: она первая в списке.
        const std::vector<irdb::CandidateScore> ranked =
            irdb::rankByCommandNames(database, found, item.commandNames);
        if (!ranked.empty()) {
            found.clear();
            for (const irdb::CandidateScore& score : ranked) {
                found.push_back(score.entry);
            }
        }
        std::size_t entry = found.front();
        if (found.size() > 1) {
            // Несколько подходящих моделей: выбирает пользователь, а не программа.
            QStringList choices;
            for (std::size_t i = 0; i < found.size(); ++i) {
                const irdb::CatalogueEntry& catalogue = database.entries()[found[i]];
                QString text = QString::fromStdString(catalogue.manufacturer + " " + catalogue.model);
                if (i < ranked.size()) {
                    text += tr(" — %1 of %2 commands match").arg(ranked[i].matched).arg(ranked[i].total);
                }
                choices << text;
            }
            const QString skip = tr("— skip this device —");
            choices << skip;
            bool accepted = false;
            const QString chosen =
                QInputDialog::getItem(this, tr("IR codes for “%1”").arg(item.name),
                                      tr("Several models in the IR database fit “%1”. Choose the one that matches "
                                         "your device:")
                                          .arg(item.name),
                                      choices, 0, false, &accepted);
            if (!accepted || chosen == skip) {
                ++unmatched;
                continue;
            }
            entry = found[static_cast<std::size_t>(choices.indexOf(chosen))];
        }
        auto record = database.device(entry);
        if (!record) {
            ++unmatched;
            journal::warning(labels::errorText(record.error()));
            continue;
        }
        HubCodesReport report;
        const bool applied = applyEdit(tr("IR codes from the database for “%1”").arg(item.name),
                                       [&](ConfigDocument& document) -> Result<void> {
                                           auto result = document.matchHubDevice(item.id, *record, database.protocols());
                                           if (!result) {
                                               return std::unexpected(std::move(result.error()));
                                           }
                                           report = std::move(*result);
                                           return {};
                                       });
        if (!applied) {
            continue;
        }
        matched += report.asProtocol + report.asSignal;
        for (const std::string& skipped : report.skipped) {
            journal::warning(tr("IR code not resolved: %1").arg(QString::fromStdString(skipped)));
        }
    }
    if (!pending.empty()) {
        statusBar()->showMessage(tr("IR codes from the database: %1 commands filled, %2 devices without a match. "
                                    "These are not the hub's own codes and may differ.")
                                     .arg(matched)
                                     .arg(unmatched),
                                 15000);
        if (matched > 0) {
            journal::warning(tr("%1 commands got IR codes picked from the database by device model and command "
                                "name (highlighted in the command list). The stock hub does not report its codes, "
                                "so the real ones may differ.")
                                 .arg(matched));
        }
    }
}

void MainWindow::openDemo() {
    if (!confirmDiscard()) {
        return;
    }
    auto document = loadDemo(tr("Example configuration"));
    if (!document) {
        QMessageBox::warning(this, tr("Example"), labels::errorText(document.error()));
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
    configView_->setConfig(&document_->model(), QString::fromStdString(document_->name()));
    screens_->setCurrentWidget(configView_);
    updateTitle();
    autosavedGeneration_ = editGeneration_;
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
    // Правится всё, но конфигурация другого формата (хаб) на пульт не пишется.
    const bool canWrite = open && document_->backend()->canWriteToRemote();
    saveAction_->setEnabled(open);
    saveAsAction_->setEnabled(open);
    closeAction_->setEnabled(open);
    recoverIrAction_->setEnabled(open);
    if (open) selectedRemote_ = QString::fromStdString(document_->targetRemote());
    const auto* target = remoteProfile(selectedRemote_.toStdString());
    writeRemoteAction_->setEnabled(canWrite && target && target->writable);
    readRemoteAction_->setEnabled(target && target->driver != "none");
    header_->setWriteEnabled(canWrite && target && target->writable);
    if (!target || target->driver == "none")
        header_->setConnectionState(false, tr("No connection driver"));
    else if (target->driver == "hid")
        header_->setConnectionState(false, tr("USB HID · connect and read"));
    checkConfigAction_->setEnabled(open);
    versionsAction_->setEnabled(open);
    compareConfigsAction_->setEnabled(open);
    quickSearchAction_->setEnabled(open);

    if (!open) {
        setWindowTitle(tr("Harmony"));
        summary_->clear();
        return;
    }

    const QString name = QString::fromStdString(document_->name());
    setWindowTitle(tr("%1 [*] — Harmony").arg(name));
    configView_->setDocumentName(name); // после «Сохранить как» имя меняется

    const auto& model = document_->model();
    QString summary =
        labels::plural(static_cast<qsizetype>(model.devices.size()), "device", "devices") +
        " · " +
        labels::plural(static_cast<qsizetype>(model.userActivityCount()), "activity", "activities");
    if (!document_->hasLocation()) {
        summary = tr("%1 · not saved").arg(summary);
    }
    summary_->setText(summary);
    // Полный путь — в подсказке: в строке состояния он налезал бы на сообщения.
    summary_->setToolTip(document_->hasLocation()
                             ? QDir::toNativeSeparators(fromPath(document_->location()))
                             : tr("The configuration is not saved to disk yet"));
}

void MainWindow::showConnectionHelp() {
    const auto* profile = remoteProfile(selectedRemote_.toStdString());
    if (!profile || profile->driver == "none") {
        QMessageBox::information(this, tr("Remote"), tr("No connection driver"));
        return;
    }
    QMessageBox box(this);
    box.setWindowTitle(tr("Connecting the remote"));
    box.setIcon(QMessageBox::Information);
    box.setProperty(help::kTopicProperty, static_cast<int>(help::Topic::Connect));
    if (!usesHarmony900(selectedRemote_.toStdString())) {
        box.setText(tr("Connect one Harmony One/600/650/665/700 by USB, close other Logitech applications and click Read from remote. No external utility is needed. Windows uses its HID driver; on Linux, access to the hidraw device is required. The program saves a backup for analysis without changing the remote."));
        if (linuxUsb::available()) {
            box.setInformativeText(tr("On Linux press “Set up USB” once (administrator password needed)."));
        }
    } else {
        box.setText(tr("<b>The Harmony 900/1100 connects via USB and shows up as a network "
                       "adapter.</b>"));
        box.setInformativeText(
            tr("The program finds the remote at %1 by itself — the state is shown at "
               "the top right.<br><br><b>Windows:</b> you need the driver from "
               "Logitech Harmony Remote Software 7.8 (Logitech's servers are closed, "
               "the installer is available in archives, e.g. on archive.org). You "
               "don't need to run the Logitech program itself.<br><br><b>Linux:</b> "
               "the remote waits for an address via DHCP. Press “Set up USB” once "
               "(administrator password needed), then reconnect the remote.<br><br>After connecting wait a few seconds: "
               "the remote is not ready immediately. Before any write the program "
               "saves a backup of the remote's current configuration to the folder "
               "“%2”.")
                .arg(QString::fromLatin1(transport::kDefaultRemoteHost),
                     QDir::toNativeSeparators(library::backupFolder())));
    }
    QPushButton* setUp = nullptr;
    QPushButton* removeSetup = nullptr;
    if (linuxUsb::available()) {
        setUp = box.addButton(linuxUsb::installed() ? tr("Set up USB again") : tr("Set up USB"),
                              QMessageBox::ActionRole);
        if (linuxUsb::installed()) {
            removeSetup = box.addButton(tr("Remove USB setup"), QMessageBox::ActionRole);
        }
    }
    box.addButton(QMessageBox::Close);
    box.exec();
    if (setUp != nullptr && box.clickedButton() == setUp) {
        linuxUsb::install(this);
    } else if (removeSetup != nullptr && box.clickedButton() == removeSetup) {
        linuxUsb::install(this, true);
    }
}

void MainWindow::showAbout() {
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::showHelp() {
    help::Topic topic = help::Topic::Start;
    if (document_ && screens_->currentWidget() == configView_) {
        switch (configView_->currentPage()) {
        case ConfigView::Page::Overview: topic = help::Topic::Start; break;
        case ConfigView::Page::ActivityDevices: topic = help::Topic::Activities; break;
        case ConfigView::Page::ActivityButtons:
        case ConfigView::Page::DeviceButtons: topic = help::Topic::Buttons; break;
        case ConfigView::Page::DeviceCommands: topic = help::Topic::Commands; break;
        case ConfigView::Page::DeviceActivities: topic = help::Topic::Devices; break;
        }
    }
    help::show(this, topic);
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

void MainWindow::renameItem(ConfigView::Item item, const QString& id) {
    if (!document_) {
        return;
    }
    const std::string key = id.toStdString();
    const bool isActivity = item == ConfigView::Item::Activity;
    QString current;
    if (isActivity) {
        if (const auto* activity = document_->model().findActivity(key)) {
            current = QString::fromStdString(activity->label);
        }
    } else if (const auto* device = document_->model().findDevice(key)) {
        // Без своего имени в списке видно «Производитель Модель» — его и предлагаем.
        current = QString::fromStdString(device->displayName());
    }

    const QString title = isActivity ? tr("Rename activity") : tr("Rename device");
    QString label = current;
    for (;;) {
        bool ok = false;
        label = QInputDialog::getText(this, title, tr("New name:"), QLineEdit::Normal, label, &ok).trimmed();
        if (!ok || label.isEmpty() || label == current) {
            return;
        }
        const bool taken = isActivity ? activityNameTaken(document_->model(), label, key)
                                      : deviceNameTaken(document_->model(), label, key);
        if (!taken) {
            break;
        }
        QMessageBox::warning(this, title,
                             isActivity ? tr("Another activity is already called “%1”. The remote shows only "
                                             "names, so choose a different one.")
                                              .arg(label)
                                        : tr("Another device is already called “%1”. The remote shows only "
                                             "names, so choose a different one.")
                                              .arg(label));
    }
    applyEdit(tr("Rename “%1”").arg(current), [&](ConfigDocument& document) {
        return isActivity ? document.setActivityLabel(key, label.toStdString())
                          : document.setDeviceLabel(key, label.toStdString());
    });
}

void MainWindow::removeItem(ConfigView::Item item, const QString& id) {
    if (!document_) {
        return;
    }
    const std::string key = id.toStdString();
    const bool isActivity = item == ConfigView::Item::Activity;
    QString name;
    if (isActivity) {
        if (const auto* activity = document_->model().findActivity(key)) {
            // «Выключить всё» удалить нельзя (пункт меню отключён); Delete не должен спрашивать зря.
            if (activity->isPowerOff()) {
                return;
            }
            name = QString::fromStdString(activity->label);
        }
    } else if (const auto* device = document_->model().findDevice(key)) {
        name = QString::fromStdString(device->displayName());
    }

    // Используемое устройство: сразу предлагаем убрать его из активностей или заменить другим.
    if (!isActivity && !document_->activitiesUsingDevice(key).empty()) {
        removeUsedDevice(key, name);
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Delete"),
        isActivity ? tr("Delete the activity “%1”?").arg(name)
                   : tr("Delete the device “%1” with its commands?").arg(name),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (applyEdit(tr("Delete “%1”").arg(name), [&](ConfigDocument& document) {
            return isActivity ? document.removeActivity(key) : document.removeDevice(key);
        })) {
        statusBar()->showMessage(tr("Deleted “%1”. Undo — %2").arg(name, labels::shortcut(QKeySequence::Undo)), 6000);
    }
}

void MainWindow::removeUsedDevice(const std::string& deviceId, const QString& name) {
    QStringList usedIn;
    QStringList emptied; // в них нет других устройств — удалятся целиком
    for (const auto& activity : document_->model().activities) {
        if (activity.isPowerOff() || !model::RemoteConfig::activityNeedsDevice(activity, deviceId)) {
            continue;
        }
        const QString label = "«" + QString::fromStdString(activity.label) + "»";
        usedIn << label;
        if (activity.powerOn.size() == 1 && activity.powerOn.front() == deviceId) {
            emptied << label;
        }
    }
    QString text = tr("The device “%1” is used in activities: %2.\n\nIt can be removed from them: its buttons, "
                      "input switching and start steps in these activities will be deleted.")
                       .arg(name, usedIn.join(", "));
    if (!emptied.isEmpty()) {
        text += "\n\n" + tr("These activities have no other devices and will be deleted: %1.").arg(emptied.join(", "));
    }
    QMessageBox box(QMessageBox::Question, tr("Delete device"), text, QMessageBox::Cancel, this);
    QPushButton* removeAll = box.addButton(tr("Remove from activities and delete"), QMessageBox::DestructiveRole);
    QPushButton* replace = box.addButton(tr("Replace…"), QMessageBox::ActionRole);
    replace->setToolTip(tr("Put another device in its place in activities — e.g. a new TV"));
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == replace) {
        replaceDevice(QString::fromStdString(deviceId));
        return;
    }
    if (box.clickedButton() != removeAll) {
        return;
    }
    if (applyEdit(tr("Delete “%1”").arg(name), [&](ConfigDocument& document) -> Result<void> {
            if (auto detached = document.removeDeviceFromActivities(deviceId); !detached) {
                return std::unexpected(std::move(detached.error()));
            }
            return document.removeDevice(deviceId);
        })) {
        statusBar()->showMessage(
            tr("Deleted “%1”, it is removed from %2. Undo — %3")
                .arg(name, labels::plural(usedIn.size(), "activity", "activities"),
                     labels::shortcut(QKeySequence::Undo)),
            8000);
    }
}

void MainWindow::addDevices() {
    if (!document_) {
        return;
    }
    AddDeviceDialog dialog(document_->model(), fromPath(document_->folder()), irDatabase_, remote_, this);
    const int result = dialog.exec();
    if (result == AddDeviceDialog::kNewDevice) {
        createDevice();
        return;
    }
    if (result == AddDeviceDialog::kOpenSettings) {
        showSettings(SettingsPage::IrDatabase);
        return;
    }
    if (result == QDialog::Accepted && dialog.fromDatabase()) {
        addFromDatabase(dialog.databaseEntry());
        return;
    }
    if (result != QDialog::Accepted || dialog.source() == nullptr) {
        return;
    }
    const auto ids = dialog.selectedDevices();
    if (ids.empty()) {
        return;
    }
    const ConfigDocument& source = *dialog.source();
    const model::Device* first = source.model().findDevice(ids.front());
    const QString text = ids.size() == 1
                             ? tr("Add “%1”").arg(QString::fromStdString(first->displayName()))
                             : tr("Add devices (%1)").arg(ids.size());

    // Все устройства — одним шагом истории: одна отмена убирает их вместе.
    std::string lastId;
    const bool added = applyEdit(text, [&](ConfigDocument& document) -> Result<void> {
        for (const auto& id : ids) {
            auto newId = document.importDevice(source, id);
            if (!newId) {
                return std::unexpected(std::move(newId.error()));
            }
            lastId = *newId;
        }
        return {};
    });
    if (added) {
        configView_->select(ConfigView::Item::Device, lastId);
        statusBar()->showMessage(
            ids.size() == 1
                ? tr("Added “%1”. In existing activities it is turned off.")
                      .arg(QString::fromStdString(first->displayName()))
                : tr("Added: %1. In existing activities they are turned off.")
                      .arg(labels::plural(static_cast<qsizetype>(ids.size()), "device", "devices")),
            8000);
    }
}

void MainWindow::createDevice() {
    if (!document_) {
        return;
    }
    NewDeviceDialog dialog(document_->model(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const DeviceDraft draft = dialog.draft();
    std::string id;
    const bool created = applyEdit(tr("Create “%1”").arg(QString::fromStdString(draft.label)),
                                   [&](ConfigDocument& document) -> Result<void> {
                                       auto newId = document.createDevice(draft);
                                       if (!newId) {
                                           return std::unexpected(std::move(newId.error()));
                                       }
                                       id = *newId;
                                       return {};
                                   });
    if (!created) {
        return;
    }
    configView_->select(ConfigView::Item::Device, id);
    const auto answer = QMessageBox::question(
        this, tr("New device"),
        tr("The device is created but has no commands yet.\n\nLearn commands from "
           "the original remote now?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes) {
        learnCommands(QString::fromStdString(id));
    }
}

void MainWindow::learnCommands(const QString& deviceId, const QStringList& commands) {
    if (!document_) {
        return;
    }
    const model::Device* device = document_->model().findDevice(deviceId.toStdString());
    if (device == nullptr) {
        return;
    }
    // Один мастер и для одной команды, и для многих: выделенные в таблице — отмечены сразу.
    LearnWizardDialog dialog(remote_, *device, this);
    if (!commands.isEmpty()) {
        dialog.markOnly(commands);
    }
    // Каждая выученная команда — отдельный шаг истории (отменяется по одной).
    connect(&dialog, &LearnWizardDialog::commandLearned, this,
            [this, deviceId](const QString& name, const ir::IrSignal& signal) {
                bool native = false;
                if (applyEdit(tr("Learn “%1”").arg(name), [&](ConfigDocument& document) -> Result<void> {
                        auto stored = document.storeLearned(deviceId.toStdString(), name.toStdString(), signal);
                        if (!stored) {
                            return std::unexpected(std::move(stored.error()));
                        }
                        native = *stored;
                        return {};
                    })) {
                    if (native) {
                        journal::info(tr("Command “%1” recognised as %2 and stored as protocol "
                                         "(the remote sends repeats itself)")
                                          .arg(name, labels::signalSummary(signal)));
                    }
                    configView_->select(ConfigView::Item::Device, deviceId.toStdString());
                }
            });
    dialog.exec();
}

void MainWindow::protocolCommand(const QString& deviceId, const QString& command) {
    if (!document_) {
        return;
    }
    const std::string id = deviceId.toStdString();
    const model::Device* device = document_->model().findDevice(id);
    if (device == nullptr) {
        return;
    }
    QStringList existing;
    std::optional<ir::IrDecoded> selected; // распознанный сигнал выбранной команды
    std::optional<ir::IrDecoded> initial;
    // Чаще всего у устройства один протокол и адрес — их и предлагаем для новой команды.
    std::map<std::pair<std::string, std::uint32_t>, int> seen;
    for (const auto& item : device->commands) {
        existing << QString::fromStdString(item.name);
        if (auto details = document_->commandDetails(id, item.name); details && details->decoded) {
            if (item.name == command.toStdString()) {
                selected = details->decoded;
            }
            ++seen[{details->decoded->protocol, details->decoded->address}];
        }
    }
    // Правка — у выбранной распознанной команды; иначе новая (протокол и адрес — самые частые).
    QString name;
    if (selected) {
        name = command;
        initial = selected;
    } else if (!seen.empty()) {
        const auto common = std::ranges::max_element(seen, {}, &decltype(seen)::value_type::second);
        initial = ir::IrDecoded{common->first.first, common->first.second, 0, {}};
    }
    ProtocolCommandDialog dialog(QString::fromStdString(device->displayName()), existing, name, initial, this, remote_);
    connect(&dialog, &ProtocolCommandDialog::signalReady, this,
            [this, id](const QString& learnedName, const ir::IrSignal& signal) {
        if (applyEdit(tr("Learn “%1”").arg(learnedName), [&](ConfigDocument& document) -> Result<void> {
            auto stored = document.storeLearned(id, learnedName.toStdString(), signal);
            if (!stored) return std::unexpected(stored.error());
            return {};
        })) configView_->selectCommand(id, learnedName.toStdString());
    });
    connect(&dialog, &ProtocolCommandDialog::commandReady, this,
            [this, id](const QString& commandName, const QString& protocol, quint32 address, quint32 code) {
                bool native = false;
                if (applyEdit(tr("Command “%1” entered manually").arg(commandName), [&](ConfigDocument& document) -> Result<void> {
                        auto stored = document.setProtocolCommand(id, commandName.toStdString(),
                                                                  protocol.toStdString(), address, code);
                        if (!stored) {
                            return std::unexpected(std::move(stored.error()));
                        }
                        native = *stored;
                        return {};
                    })) {
                    journal::info(native ? tr("Command “%1” stored as protocol %2 (the remote sends repeats itself)")
                                               .arg(commandName, protocol)
                                         : tr("Command “%1” stored as a signal").arg(commandName));
                    configView_->selectCommand(id, commandName.toStdString());
                }
            });
    dialog.exec();
}

void MainWindow::editProperties(ConfigView::Item item, const QString& id) {
    if (!document_) {
        return;
    }
    const std::string key = id.toStdString();
    const bool activity = item == ConfigView::Item::Activity;
    QString title;
    if (activity) {
        const model::Activity* found = document_->model().findActivity(key);
        if (found == nullptr) return;
        title = QString::fromStdString(found->label);
    } else {
        const model::Device* found = document_->model().findDevice(key);
        if (found == nullptr) return;
        title = QString::fromStdString(found->displayName());
    }
    PropertiesDialog dialog(*document_, activity, key, title, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto changes = dialog.changes();
    if (changes.empty()) {
        return;
    }
    applyEdit(tr("Properties of “%1”").arg(title), [&](ConfigDocument& document) -> Result<void> {
        for (const auto& change : changes) {
            if (auto set = document.setProperty(change.scope, key, change.name, change.value); !set) {
                return set;
            }
        }
        return {};
    });
}

void MainWindow::showCommandDetails(const QString& deviceId, const QString& command) {
    if (!document_) {
        return;
    }
    const model::Device* device = document_->model().findDevice(deviceId.toStdString());
    auto details = document_->commandDetails(deviceId.toStdString(), command.toStdString());
    if (device == nullptr || !details) {
        QMessageBox::warning(this, tr("Command"),
                             details ? tr("Device not found.") : labels::errorText(details.error()));
        return;
    }
    CommandDialog dialog(QString::fromStdString(device->displayName()), command, *details, this);
    dialog.exec();
}

void MainWindow::removeCommand(const QString& deviceId, const QString& command) {
    if (!document_) {
        return;
    }
    const std::string device = deviceId.toStdString();
    const std::string name = command.toStdString();
    const auto usage = document_->commandUsage(device, name);
    bool clearable = true; // кнопки и запуск активностей можно почистить вместе с удалением
    if (!usage.empty()) {
        QStringList places;
        for (const auto& use : usage) {
            const QString where = QString::fromStdString(use.name);
            QString text;
            switch (use.place) {
            case ConfigDocument::CommandUse::Place::DeviceButtons: text = tr("device buttons"); break;
            case ConfigDocument::CommandUse::Place::ActivityButtons: text = tr("buttons of activity “%1”").arg(where); break;
            case ConfigDocument::CommandUse::Place::DeviceStates:
                text = tr("power or inputs of the device");
                clearable = false;
                break;
            case ConfigDocument::CommandUse::Place::ActivityStart: text = tr("start of activity “%1”").arg(where); break;
            case ConfigDocument::CommandUse::Place::MacroButton:
                text = tr("a macro button");
                clearable = false;
                break;
            }
            places << QStringLiteral("• ") + text;
        }
        if (!clearable) {
            // Шаг из питания или макроса без замены сломал бы их — правятся в своих окнах.
            QMessageBox::information(this, tr("Delete command"),
                                     tr("The command “%1” is used in:\n%2\n\nPower, inputs and macro buttons "
                                        "are changed in their own windows: remove the command there first.")
                                         .arg(command, places.join('\n')));
            return;
        }
        QMessageBox box(QMessageBox::Question, tr("Delete command"),
                        tr("The command “%1” is used in:\n%2\n\nThese buttons and start steps will be deleted "
                           "together with the command.")
                            .arg(command, places.join('\n')),
                        QMessageBox::Cancel, this);
        QPushButton* removeAll = box.addButton(tr("Remove from there and delete"), QMessageBox::DestructiveRole);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != removeAll) {
            return;
        }
    }
    // Неиспользуемую команду удаляем без вопроса: шаг отменяется.
    if (applyEdit(tr("Delete command “%1”").arg(command), [&](ConfigDocument& document) -> Result<void> {
            if (!usage.empty()) {
                if (auto cleared = document.clearCommandUses(device, name); !cleared) {
                    return cleared;
                }
            }
            return document.removeCommand(device, name);
        })) {
        configView_->select(ConfigView::Item::Device, device);
        statusBar()->showMessage(
            tr("Deleted command “%1”. Undo — %2").arg(command, labels::shortcut(QKeySequence::Undo)), 6000);
    }
}

void MainWindow::addFromDatabase(int entry) {
    const irdb::IrDatabase* database = irDatabase_->database();
    if (!document_ || database == nullptr || entry < 0 || dbImportThread_ != nullptr) {
        return;
    }
    auto record = database->device(static_cast<std::size_t>(entry));
    if (!record) {
        QMessageBox::warning(this, tr("Add device"), labels::errorText(record.error()));
        return;
    }
    const QString name = QString::fromStdString(record->manufacturer + " " + record->model);
    const QString text = tr("Add “%1”").arg(name);

    // Сигналы всех команд собираются из протоколов — на медленных машинах это секунды.
    // Поэтому в фоне устройство собирается во временной конфигурации, а в открытую
    // переносится готовым (importDevice) — документ меняется только в GUI-потоке.
    struct Prepared {
        std::optional<ConfigDocument> scratch;
        Result<DbImportReport> report;
    };
    auto prepared = std::make_shared<Prepared>();
    auto source = std::make_shared<const irdb::DbDevice>(std::move(*record));
    const irdb::ProtocolLibrary* protocols = &database->protocols();
    QThread* worker = QThread::create([prepared, source, protocols] {
        ConfigDocument scratch = ConfigDocument::createNew("import");
        prepared->report = scratch.importDbDevice(*source, *protocols);
        prepared->scratch.emplace(std::move(scratch));
    });
    worker->setParent(this); // closeEvent дожидается потока
    dbImportThread_ = worker;

    // Окно модальное и не закрывается: пока идёт сборка, базу нельзя закрыть или пересобрать.
    auto* busy = new GuardedProgressDialog(tr("Converting the device's commands…"), QString(), 0, 0, this);
    busy->setCancellable(false);
    busy->setWindowTitle(tr("Add device"));
    busy->setWindowModality(Qt::WindowModal);
    busy->setMinimumDuration(0);
    busy->setMinimumWidth(420);
    busy->show();

    const int serial = documentSerial_;
    connect(worker, &QThread::finished, this, [this, worker, busy, prepared, serial, text, name] {
        busy->deleteLater();
        worker->deleteLater();
        dbImportThread_ = nullptr;
        if (!document_ || documentSerial_ != serial) {
            return; // конфигурацию успели закрыть или сменить
        }
        if (!prepared->report) {
            QMessageBox::warning(this, tr("Add device"), labels::errorText(prepared->report.error()));
            return;
        }
        DbImportReport report = std::move(*prepared->report);
        const ConfigDocument& scratch = *prepared->scratch;
        const bool added = applyEdit(text, [&](ConfigDocument& document) -> Result<void> {
            auto newId = document.importDevice(scratch, report.deviceId);
            if (!newId) {
                return std::unexpected(std::move(newId.error()));
            }
            report.deviceId = *newId;
            return {};
        });
        if (!added) {
            return;
        }
        configView_->select(ConfigView::Item::Device, report.deviceId);
        QStringList parts;
        parts << labels::plural(report.commands, "command", "commands")
              << labels::plural(report.hardButtons + report.softButtons, "button", "buttons");
        parts << (report.power ? tr("the remote turns it on and off") : tr("power: manual"));
        if (report.inputs) {
            parts << tr("inputs described");
        }
        if (!report.skipped.empty()) {
            parts << tr("not converted: %1").arg(report.skipped.size());
        }
        statusBar()->showMessage(tr("Added “%1”: %2. In existing activities it is turned off.")
                                     .arg(name, parts.join(", ")),
                                 10000);
    });
    worker->start();
}

void MainWindow::addActivity() {
    if (!document_) {
        return;
    }
    if (document_->model().devices.empty()) {
        const auto answer = QMessageBox::question(
            this, tr("Add activity"),
            tr("An activity turns devices on, but the configuration has none "
               "yet.\n\nAdd a device?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            addDevices();
        }
        return;
    }
    ActivityDialog dialog(document_->model(), nullptr, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const ActivityDraft draft = dialog.draft();
    std::string id;
    const bool added = applyEdit(tr("Add “%1”").arg(QString::fromStdString(draft.label)),
                                 [&](ConfigDocument& document) -> Result<void> {
                                     auto newId = document.addActivity(draft);
                                     if (!newId) {
                                         return std::unexpected(std::move(newId.error()));
                                     }
                                     id = *newId;
                                     return {};
                                 });
    if (added) {
        configView_->select(ConfigView::Item::Activity, id);
    }
}

namespace {

/// Название кнопки для истории и сообщений: «Меню» или «экранная кнопка 3».
QString buttonTitle(const ButtonRef& button) {
    return button.soft ? QObject::tr("screen button %1").arg(button.position + 1)
                       : RemoteView::keyName(button.key);
}

/// Назначенная кнопка владельца: аппаратная по имени, экранная по месту; nullptr — свободна.
const model::Button* findButton(const model::RemoteConfig& config, const ButtonOwner& owner, const ButtonRef& ref) {
    const model::Device* device = owner.isDevice() ? config.findDevice(owner.id) : nullptr;
    const model::Activity* activity = owner.isDevice() ? nullptr : config.findActivity(owner.id);
    if (device == nullptr && activity == nullptr) {
        return nullptr;
    }
    const auto& groups = device != nullptr ? device->controlGroups : activity->controlGroups;
    for (const auto& group : groups) {
        for (const auto& button : group.buttons) {
            const bool match = ref.soft ? button.name.empty() && button.position == ref.position
                                        : !ref.key.isEmpty() && button.name == ref.key.toStdString();
            if (match) {
                return &button;
            }
        }
    }
    return nullptr;
}

} // namespace

void MainWindow::editButton(const ButtonOwner& owner, const ButtonRef& button) {
    if (!document_ || RemoteView::hardKeyIds(QString::fromStdString(document_->targetRemote())).isEmpty()) {
        return;
    }
    if (!button.soft && !button.key.isEmpty() &&
        !RemoteView::hardKeyIds(QString::fromStdString(document_->targetRemote())).contains(button.key)) return;
    // Макрос кнопки (если она его выполняет) — для правки шагов.
    Macro initialMacro;
    {
        const model::Device* ownerDevice = owner.isDevice() ? document_->model().findDevice(owner.id) : nullptr;
        const model::Activity* ownerActivity = owner.isDevice() ? nullptr : document_->model().findActivity(owner.id);
        static const std::vector<model::ControlGroup> kNone;
        const auto& groups = ownerDevice != nullptr     ? ownerDevice->controlGroups
                             : ownerActivity != nullptr ? ownerActivity->controlGroups
                                                        : kNone;
        for (const auto& group : groups) {
            for (const auto& item : group.buttons) {
                const bool match = button.soft ? item.name.empty() && item.position == button.position
                                               : !button.key.isEmpty() && item.name == button.key.toStdString();
                if (match && isMacroAction(item.actionId)) {
                    if (auto steps = document_->macro(item.actionId)) {
                        initialMacro = std::move(*steps);
                    }
                }
            }
        }
    }
    ButtonDialog dialog(document_->model(), owner, button, this, initialMacro);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (dialog.removeChosen()) {
        removeButton(owner, button);
        return;
    }

    ButtonRef target = dialog.button();
    if (target.soft && target.position < 0) {
        target.position = document_->freeSoftPosition(owner);
    }
    const std::string deviceId = dialog.deviceId();
    const std::string command = dialog.command();
    const std::string label = dialog.label();
    const std::string icon = dialog.icon();
    const bool macro = dialog.macroChosen();
    const Macro steps = dialog.macro();
    const bool applied = applyEdit(
        tr("Assign “%1”").arg(buttonTitle(target)), [&](ConfigDocument& document) -> Result<void> {
            if (target.soft) {
                // У кнопки со значком подпись не видна; макросу нужна хоть какая-то.
                const std::string text = macro && label.empty() ? std::string("Macro") : label;
                auto set = macro ? document.setSoftMacro(owner, target.position, text, steps)
                                 : document.setSoftButton(owner, target.position, text, deviceId, command);
                if (!set) {
                    return set;
                }
                return document.setSoftButtonIcon(owner, target.position, icon);
            }
            // Аппаратную кнопку в окне сменили на другую — прежняя освобождается.
            if (!button.key.isEmpty() && button.key != target.key) {
                if (auto cleared = document.setHardButton(owner, button.key.toStdString(), {}, {}); !cleared) {
                    return cleared;
                }
            }
            return macro ? document.setHardMacro(owner, target.key.toStdString(), steps)
                         : document.setHardButton(owner, target.key.toStdString(), deviceId, command);
        });
    if (applied) {
        configView_->selectButton(target);
    }
}

void MainWindow::removeButton(const ButtonOwner& owner, const ButtonRef& button) {
    if (!document_ || RemoteView::hardKeyIds(QString::fromStdString(document_->targetRemote())).isEmpty()) {
        return;
    }
    const QString title = buttonTitle(button);
    const bool applied = applyEdit(
        button.soft ? tr("Delete “%1”").arg(title) : tr("Clear assignment of “%1”").arg(title),
        [&](ConfigDocument& document) {
            return button.soft ? document.removeSoftButton(owner, button.position)
                               : document.setHardButton(owner, button.key.toStdString(), {}, {});
        });
    if (applied) {
        const QString undoKey = labels::shortcut(QKeySequence::Undo);
        statusBar()->showMessage(button.soft ? tr("Deleted “%1”. Undo — %2").arg(title, undoKey)
                                             : tr("Assignment of “%1” cleared. Undo — %2").arg(title, undoKey),
                                 6000);
    }
}

void MainWindow::moveButton(const ButtonOwner& owner, const ButtonRef& from, const ButtonRef& to, bool copy) {
    if (!document_ || from.soft != to.soft ||
        RemoteView::hardKeyIds(QString::fromStdString(document_->targetRemote())).isEmpty()) {
        return;
    }
    if (!to.soft && !RemoteView::hardKeyIds(QString::fromStdString(document_->targetRemote())).contains(to.key)) return;
    // Перетащили свободную кнопку — переносить нечего, ошибка в окне тут ни к чему.
    if (findButton(document_->model(), owner, from) == nullptr) {
        statusBar()->showMessage(tr("“%1” has no assignment: nothing to move.").arg(buttonTitle(from)), 6000);
        return;
    }
    // На занятое место кнопки меняются местами (копия заменяет прежнее назначение).
    const bool swap = !copy && findButton(document_->model(), owner, to) != nullptr;
    const QString text = (copy   ? tr("Copy “%1” → “%2”")
                          : swap ? tr("Swap “%1” and “%2”")
                                 : tr("Move “%1” → “%2”"))
                             .arg(buttonTitle(from), buttonTitle(to));
    const bool applied = applyEdit(text, [&](ConfigDocument& document) {
        return from.soft ? document.moveSoftButton(owner, from.position, to.position)
                         : document.moveHardButton(owner, from.key.toStdString(), to.key.toStdString(), copy);
    });
    if (applied) {
        configView_->selectButton(to);
        statusBar()->showMessage(tr("%1. Undo — %2").arg(text, labels::shortcut(QKeySequence::Undo)), 6000);
    }
}

void MainWindow::assignCommand(const ButtonOwner& owner, const ButtonRef& button, const QString& deviceId,
                               const QString& command) {
    const QStringList keys = document_ ? RemoteView::hardKeyIds(QString::fromStdString(document_->targetRemote()))
                                       : QStringList();
    if (keys.isEmpty() || (!button.soft && !keys.contains(button.key)) || (button.soft && button.position < 0)) {
        return;
    }
    // Подпись экранной кнопки: своя (или значок) остаётся, а сделанная из имени
    // прежней команды сменилась бы на неверную — её заменяем именем новой.
    std::string label;
    if (const model::Button* existing = findButton(document_->model(), owner, button);
        button.soft && existing != nullptr && existing->icon.empty() &&
        existing->label == splitActionId(existing->actionId).second) {
        label = command.toStdString();
    }
    const QString text = tr("Assign “%1” to “%2”").arg(command, buttonTitle(button));
    const bool applied = applyEdit(text, [&](ConfigDocument& document) {
        return button.soft
                   ? document.setSoftButton(owner, button.position, label, deviceId.toStdString(), command.toStdString())
                   : document.setHardButton(owner, button.key.toStdString(), deviceId.toStdString(),
                                            command.toStdString());
    });
    if (applied) {
        statusBar()->showMessage(tr("%1. Undo — %2").arg(text, labels::shortcut(QKeySequence::Undo)), 6000);
    }
}

void MainWindow::editDeviceStates(const QString& deviceId, bool inputs) {
    if (!document_) {
        return;
    }
    const std::string id = deviceId.toStdString();
    const model::Device* device = document_->model().findDevice(id);
    auto states = document_->deviceStates(id);
    if (device == nullptr || !states) {
        QMessageBox::warning(this, inputs ? tr("Inputs") : tr("Power"),
                             states ? tr("The device is not found.") : labels::errorText(states.error()));
        return;
    }
    DeviceStatesDialog dialog(*device, *states,
                              inputs ? DeviceStatesDialog::Section::Inputs : DeviceStatesDialog::Section::Power,
                              this);
    if (dialog.exec() != QDialog::Accepted || dialog.states() == *states) {
        return;
    }
    const DeviceStates changed = dialog.states();
    const QString name = QString::fromStdString(device->displayName());
    if (applyEdit(inputs ? tr("Inputs of “%1”").arg(name) : tr("Power of “%1”").arg(name),
                  [&](ConfigDocument& document) { return document.setDeviceStates(id, changed); })) {
        configView_->select(ConfigView::Item::Device, id);
    }
}

void MainWindow::replaceDevice(const QString& deviceId) {
    if (!document_) {
        return;
    }
    const std::string id = deviceId.toStdString();
    const model::Device* device = document_->model().findDevice(id);
    if (device == nullptr) {
        return;
    }
    const QString oldName = QString::fromStdString(device->displayName());
    ReplaceDeviceDialog dialog(*document_, id, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const DeviceReplacement replacement = dialog.replacement();
    const model::Device* target = document_->model().findDevice(replacement.newId);
    const QString newName = target != nullptr ? QString::fromStdString(target->displayName()) : QString();
    DeviceReplacementReport report;
    const bool applied = applyEdit(tr("Replace “%1” with “%2”").arg(oldName, newName),
                                   [&](ConfigDocument& document) -> Result<void> {
                                       auto done = document.replaceDevice(replacement);
                                       if (!done) {
                                           return std::unexpected(std::move(done.error()));
                                       }
                                       report = *done;
                                       return {};
                                   });
    if (!applied) {
        return;
    }
    configView_->select(ConfigView::Item::Device, replacement.newId);
    // Итог: что перешло, что убрано.
    QStringList lines;
    lines << tr("Activities changed: %1.").arg(report.activities);
    lines << tr("Buttons moved: %1, start steps: %2.").arg(report.buttons).arg(report.steps);
    if (report.macroSteps > 0) {
        lines << tr("Macro steps moved: %1.").arg(report.macroSteps);
    }
    if (report.removed > 0) {
        lines << tr("Removed without a matching command: %1.").arg(report.removed);
    }
    if (report.inputsLost > 0) {
        lines << tr("Input switches removed (“%1” has no such input): %2.").arg(newName).arg(report.inputsLost);
    }
    if (report.oldRemoved) {
        lines << tr("“%1” is deleted.").arg(oldName);
    }
    lines << tr("Undo — %1.").arg(labels::shortcut(QKeySequence::Undo));
    QMessageBox::information(this, tr("Replace device"), lines.join('\n'));
}

void MainWindow::compareDevices(const QString& deviceId) {
    if (!document_ || document_->model().findDevice(deviceId.toStdString()) == nullptr) {
        return;
    }
    CompareDevicesDialog dialog(*document_, deviceId.toStdString(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const std::string from = dialog.fromId();
    const std::string to = dialog.toId();
    const std::vector<std::string> names = dialog.marked();
    const model::Device* target = document_->model().findDevice(to);
    const QString targetName = target != nullptr ? QString::fromStdString(target->displayName()) : QString();
    const QString commands = labels::plural(static_cast<qsizetype>(names.size()), "command", "commands");
    if (applyEdit(tr("Copy %1 to “%2”").arg(commands, targetName),
                  [&](ConfigDocument& document) -> Result<void> {
                      auto copied = document.copyCommands(from, to, names);
                      if (!copied) {
                          return std::unexpected(std::move(copied.error()));
                      }
                      return {};
                  })) {
        configView_->select(ConfigView::Item::Device, to);
        statusBar()->showMessage(
            tr("Copied %1 to “%2”. Undo — %3").arg(commands, targetName, labels::shortcut(QKeySequence::Undo)), 6000);
    }
}

void MainWindow::setNotSent(ConfigView::Item item, const QString& id, bool notSent) {
    if (!document_) {
        return;
    }
    const std::string key = id.toStdString();
    const bool device = item == ConfigView::Item::Device;
    const model::Device* found = device ? document_->model().findDevice(key) : nullptr;
    const model::Activity* activity = device ? nullptr : document_->model().findActivity(key);
    if (found == nullptr && activity == nullptr) {
        return;
    }
    const QString name = QString::fromStdString(device ? found->displayName() : activity->label);
    if (device && notSent) {
        // Активности, которым устройство нужно, тоже перестанут уходить на пульт — показываем это до правки.
        const auto& config = document_->model();
        QStringList affected;
        for (const auto& other : config.activities) {
            if (!other.isPowerOff() && !config.isActivityNotSent(other) &&
                model::RemoteConfig::activityNeedsDevice(other, key)) {
                affected << "«" + QString::fromStdString(other.label) + "»";
            }
        }
        if (!affected.isEmpty()) {
            QMessageBox box(QMessageBox::Question, tr("Make the device inactive"),
                            tr("The device “%1” is used in activities: %2.\n\nThey will not be sent to the "
                               "remote either. When the device is made active again, they are sent again.")
                                .arg(name, affected.join(", ")),
                            QMessageBox::Cancel, this);
            QPushButton* confirm =
                box.addButton(tr("Make the device and these activities inactive"), QMessageBox::AcceptRole);
            box.setDefaultButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() != confirm) {
                configView_->refresh(); // переключатель — обратно
                return;
            }
        }
    }
    const QString text = notSent ? tr("Do not send “%1” to the remote").arg(name)
                                 : tr("Send “%1” to the remote").arg(name);
    if (!applyEdit(text, [&](ConfigDocument& document) {
            return device ? document.setDeviceNotSent(key, notSent) : document.setActivityNotSent(key, notSent);
        })) {
        configView_->refresh(); // флажок в меню — обратно
        return;
    }
    // Открытая страница остаётся: устройство включают и с полосы на странице активности.
    // Сколько активностей теперь не уходит на пульт.
    const auto held = document_->activitiesNotSent();
    const QString undoKey = labels::shortcut(QKeySequence::Undo);
    statusBar()->showMessage(
        held.empty() ? tr("%1. Undo — %2").arg(text, undoKey)
                     : tr("%1. Not sent to the remote: %2. Undo — %3")
                           .arg(text,
                                labels::plural(static_cast<qsizetype>(held.size()), "activity", "activities"),
                                undoKey),
        8000);
}

void MainWindow::copyButtons(const QString& activityId, bool hasButton, const ButtonRef& button) {
    if (!document_ || RemoteView::hardKeyIds(QString::fromStdString(document_->targetRemote())).isEmpty() ||
        document_->model().findActivity(activityId.toStdString()) == nullptr) {
        return;
    }
    CopyButtonsDialog dialog(document_->model(), activityId.toStdString(),
                             hasButton ? std::optional(button) : std::nullopt, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const ButtonCopy copy = dialog.copy();
    ButtonCopyReport report;
    const bool applied = applyEdit(tr("Copy buttons to %1").arg(labels::plural(
                                       static_cast<qsizetype>(copy.toActivities.size()), "activity", "activities")),
                                   [&](ConfigDocument& document) -> Result<void> {
                                       auto done = document.copyButtons(copy);
                                       if (!done) {
                                           return std::unexpected(std::move(done.error()));
                                       }
                                       report = *done;
                                       return {};
                                   });
    if (!applied) {
        return;
    }
    QStringList parts;
    parts << tr("Remote buttons: %1, screen buttons: %2.").arg(report.hard).arg(report.soft);
    if (report.kept > 0) {
        parts << tr("Already taken, kept: %1.").arg(report.kept);
    }
    if (report.skipped > 0) {
        parts << tr("Skipped (the device is not turned on there): %1.").arg(report.skipped);
    }
    // Пропуски стоит показать окном — иначе достаточно строки состояния.
    const QString undoKey = labels::shortcut(QKeySequence::Undo);
    if (report.skipped > 0 || report.hard + report.soft == 0) {
        QMessageBox::information(this, tr("Copy buttons"),
                                 (parts + QStringList{tr("Undo — %1.").arg(undoKey)}).join('\n'));
    } else {
        statusBar()->showMessage(tr("%1 Undo — %2").arg(parts.join(' '), undoKey), 8000);
    }
}

void MainWindow::editActivity(const QString& activityId) {
    if (!document_) {
        return;
    }
    const std::string id = activityId.toStdString();
    const model::Activity* activity = document_->model().findActivity(id);
    if (activity == nullptr) {
        return;
    }
    ActivityDialog dialog(document_->model(), activity, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const ActivityDraft draft = dialog.draft();
    const bool reassign = dialog.reassignButtons();
    applyEdit(tr("Edit “%1”").arg(QString::fromStdString(draft.label)),
              [&](ConfigDocument& document) { return document.updateActivity(id, draft, reassign); });
}

void MainWindow::moveActivity(const QString& id, int index) {
    if (!document_ || index < 0) {
        return;
    }
    const std::string key = id.toStdString();
    const auto* activity = document_->model().findActivity(key);
    const QString name = activity != nullptr ? QString::fromStdString(activity->label) : id;
    applyEdit(tr("Move “%1”").arg(name), [&](ConfigDocument& document) {
        return document.moveActivity(key, static_cast<std::size_t>(index));
    });
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

void MainWindow::compareWithOther() {
    if (!document_) {
        return;
    }
    CompareConfigsDialog dialog(*document_, this);
    dialog.exec();
}

void MainWindow::showQuickSearch() {
    if (!document_) {
        return;
    }
    QuickSearch search(document_->model(), this);
    if (search.exec() != QDialog::Accepted || !search.chosen()) {
        return;
    }
    using Kind = QuickSearch::Hit::Kind;
    const QuickSearch::Hit& hit = *search.chosen();
    switch (hit.kind) {
    case Kind::Device:
    case Kind::Activity:
        configView_->select(hit.kind == Kind::Device ? ConfigView::Item::Device : ConfigView::Item::Activity, hit.id);
        break;
    case Kind::DeviceKey:
    case Kind::ActivityKey:
        configView_->select(hit.kind == Kind::DeviceKey ? ConfigView::Item::Device : ConfigView::Item::Activity, hit.id);
        configView_->selectButton(ButtonRef::hard(QString::fromStdString(hit.key)));
        break;
    case Kind::DeviceSoft:
    case Kind::ActivitySoft:
        configView_->select(hit.kind == Kind::DeviceSoft ? ConfigView::Item::Device : ConfigView::Item::Activity,
                            hit.id);
        configView_->selectButton(ButtonRef::screen(hit.position));
        break;
    case Kind::Command:
        configView_->selectCommand(hit.id, hit.command);
        break;
    }
}

void MainWindow::updateProblems() {
    if (problems_ == nullptr) {
        return; // ещё строится окно
    }
    if (!document_) {
        problems_->hide();
        return;
    }
    const auto problems = document_->check();
    const auto errors = static_cast<qsizetype>(countProblems(problems, Problem::Severity::Error));
    const auto warnings = static_cast<qsizetype>(countProblems(problems, Problem::Severity::Warning));
    if (problems.empty()) {
        problems_->hide();
        return;
    }
    QStringList parts;
    if (errors > 0) {
        parts << labels::plural(errors, "error", "errors");
    }
    if (warnings > 0) {
        parts << labels::plural(warnings, "warning", "warnings");
    }
    problems_->setIcon(style()->standardIcon(errors > 0 ? QStyle::SP_MessageBoxCritical
                                                        : QStyle::SP_MessageBoxWarning));
    problems_->setText(parts.join(QStringLiteral(" · ")));
    problems_->show();
}

void MainWindow::showCheck() {
    if (!document_) {
        return;
    }
    CheckDialog dialog(*document_, [this](const QString& text, const auto& change) { return applyEdit(text, change); },
                       this);
    if (dialog.exec() == QDialog::Accepted && dialog.target()) {
        showProblem(*dialog.target());
    }
}

void MainWindow::showProblem(const Problem& problem) {
    if (problem.where == Problem::Where::Config) {
        return;
    }
    configView_->select(problem.where == Problem::Where::Device ? ConfigView::Item::Device
                                                               : ConfigView::Item::Activity,
                        problem.id);
    if (!problem.hardKey.empty()) {
        configView_->selectButton(ButtonRef::hard(QString::fromStdString(problem.hardKey)));
    } else if (problem.softPosition >= 0) {
        configView_->selectButton(ButtonRef::screen(problem.softPosition));
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmDiscard()) {
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
