/**
 * @file mainWindowMenus.cpp
 * @brief Главное окно: действия, меню, настройки, ресурсы пультов, справка.
 */

#include "mainWindow.h"
#include "widgets/configTransferDialog.h"
#include "harmony/core/remoteProfile.h"
#include "appSettings.h"
#include "remoteResources.h"
#include "configLibrary.h"
#include "irDatabaseService.h"
#include "documentSource.h"
#include "remoteSession.h"
#include "harmony/transport/remoteLink.h"
#include "help.h"
#include "i18n.h"
#include "labels.h"
#include "widgets/aboutDialog.h"
#include "widgets/appHeader.h"
#include "widgets/configView.h"
#include "widgets/settingsDialog.h"
#include "widgets/journalDialog.h"
#include "journal.h"
#include "linuxUsbSetup.h"
#include "widgets/zoneEditorDialog.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QProcess>
#include <QMessageBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QDesktopServices>
#include <QFileSystemWatcher>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

namespace harmony::app {

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
        if (!hubFile.isEmpty()) {
            openFolder(hubFile); // о несохранённых правках спросит после чтения файла
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
        if (usesUsbNet(selectedRemote_.toStdString())) remote_->probe();
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
            connect(action, &QAction::triggered, this, [this, path] { openFolder(path); });
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
    for (const QString& file : remoteResources::migratedByReload()) {
        journal::info(tr("Remote description %1 is in an old format and was converted on load. "
                         "It is updated on disk the next time it is saved in the zone editor.")
                          .arg(QDir::toNativeSeparators(file)));
    }
    // Ошибки чужих пультов не шумят при запуске: они показываются при выборе пульта и в редакторе зон.
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

void MainWindow::showConnectionHelp() {
    const auto* profile = remoteProfile(selectedRemote_.toStdString());
    if (!profile || profile->driver == driver::kNone) {
        QMessageBox::information(this, tr("Remote"), tr("No connection driver"));
        return;
    }
    QMessageBox box(this);
    box.setWindowTitle(tr("Connecting the remote"));
    box.setIcon(QMessageBox::Information);
    box.setProperty(help::kTopicProperty, static_cast<int>(help::Topic::Connect));
    if (!usesUsbNet(selectedRemote_.toStdString())) {
        box.setText(tr("Connect the remote by USB, close other Logitech applications and click Read from remote and save. The model is detected automatically and one file for analysis (*.remote.json) is saved and checked; the remote is not changed. No external utility is needed. Windows uses its HID driver; on Linux, access to the hidraw device is required."));
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

} // namespace harmony::app
