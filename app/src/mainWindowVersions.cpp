/**
 * @file mainWindowVersions.cpp
 * @brief Автосохранение и восстановление версий конфигурации.
 */
#include "mainWindow.h"
#include "documentSource.h"
#include "labels.h"
#include "versionStore.h"
#include "widgets/versionsDialog.h"
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QStatusBar>

namespace harmony::app {

void MainWindow::autosave() {
    if (!document_ || undoStack_.isClean() || editGeneration_ == autosavedGeneration_) {
        return;
    }
    if (versions::save(*document_, versions::keyOf(*document_), versions::Kind::Auto)) {
        autosavedGeneration_ = editGeneration_;
    }
}

void MainWindow::offerAutosave() {
    if (!document_ || !document_->hasLocation()) {
        return;
    }
    const auto list = versions::list(versions::keyOf(*document_));
    if (list.isEmpty() || list.front().kind != versions::Kind::Auto) {
        return;
    }
    // Время файла: у папки — UserConfiguration.xml, у .EZHex — сам файл.
    const QString location = fromPath(document_->location());
    const QFileInfo info(QFileInfo(location).isDir() ? QDir(location).filePath("UserConfiguration.xml") : location);
    if (!info.exists() || list.front().time <= info.lastModified()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Unsaved changes found"),
        tr("There is an autosave of “%1” from %2 that is newer than the file — the changes were not saved. "
           "Restore them?<br><br>You can also do it later: File → Versions…")
            .arg(QString::fromStdString(document_->name()).toHtmlEscaped(),
                 QLocale().toString(list.front().time, QLocale::ShortFormat)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes) {
        restoreVersion(list.front());
    }
}

void MainWindow::restoreVersion(const versions::Version& version) {
    if (!document_) {
        return;
    }
    auto old = versions::open(version, document_->name());
    if (!old) {
        QMessageBox::warning(this, tr("Versions"), labels::errorText(old.error()));
        return;
    }
    // Текущее состояние — тоже версией, а сама замена — обычная правка с отменой.
    (void)versions::save(*document_, versions::keyOf(*document_), versions::Kind::Restore);
    if (applyEdit(tr("Restore version from %1").arg(QLocale().toString(version.time, QLocale::ShortFormat)),
                  [&old](ConfigDocument& target) { return target.replaceContent(*old); })) {
        statusBar()->showMessage(tr("Version restored. “Save” writes it to the file; “Undo” brings back the "
                                    "previous state."),
                                 10000);
    }
}

void MainWindow::showVersions() {
    if (!document_) {
        return;
    }
    autosave(); // свежие правки — тоже в списке
    VersionsDialog dialog(*document_, this);
    if (dialog.exec() == QDialog::Accepted && dialog.chosen()) {
        restoreVersion(*dialog.chosen());
    }
}

} // namespace harmony::app
