/**
 * @file irDatabasePanel.cpp
 * @brief Вкладка настроек «База ИК-кодов».
 */

#include "irDatabasePanel.h"

#include "../appSettings.h"
#include "../irDatabaseService.h"
#include "../labels.h"

#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

QLabel* hint(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("pageMeta");
    label->setWordWrap(true);
    // Высоту строки форма считает по более узкой ширине — прижимаем текст к верху, чтобы не
    // «висел».
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return label;
}

/// Поле пути с кнопкой «Обзор…».
QHBoxLayout* pathRow(QLineEdit* edit, QPushButton* browse) {
    auto* row = new QHBoxLayout;
    row->addWidget(edit, 1);
    row->addWidget(browse);
    return row;
}

} // namespace

IrDatabasePanel::IrDatabasePanel(IrDatabaseService* service, QWidget* parent)
    : QWidget(parent), service_(service) {
    auto* form = new QFormLayout;
    form->setContentsMargins(16, 16, 16, 8);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    form->addRow(tr("Database:"), status_);

    // Полные адреса двух нужных файлов: в новом релизе может поменяться и папка, и имя.
    devicesUrl_ = new QLineEdit(this);
    devicesUrl_->setPlaceholderText(settings::defaultIrdbUrl(settings::IrdbArchive::Devices));
    devicesUrl_->setToolTip(tr("Devices and commands, about 330 MB"));
    form->addRow(tr("Devices file:"), devicesUrl_);
    protocolsUrl_ = new QLineEdit(this);
    protocolsUrl_->setPlaceholderText(settings::defaultIrdbUrl(settings::IrdbArchive::Protocols));
    protocolsUrl_->setToolTip(tr("IR protocols, about 70 KB"));
    form->addRow(tr("Protocols file:"), protocolsUrl_);
    files_ = new QLabel(this);
    files_->setObjectName("pageMeta");
    files_->setWordWrap(true);
    files_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->addRow({}, files_);

    folder_ = new QLineEdit(this);
    auto* browseFolder = new QPushButton(tr("Browse…"), this);
    form->addRow(tr("Archive files:"), pathRow(folder_, browseFolder));
    form->addRow({}, hint(tr("Files are downloaded here and indexed from here. A folder with "
                             "already downloaded files works too (compressed .gz or unpacked)."),
                          this));

    file_ = new QLineEdit(this);
    auto* browseFile = new QPushButton(tr("Browse…"), this);
    form->addRow(tr("Database file:"), pathRow(file_, browseFile));
    form->addRow(
        {},
        hint(tr("The index takes about 50 MB; the archive files are not needed after indexing."),
             this));

    download_ = new QPushButton(tr("Download"), this);
    build_ = new QPushButton(tr("Index"), this);
    cancel_ = new QPushButton(tr("Cancel"), this);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(download_);
    buttons->addWidget(build_);
    buttons->addStretch(1);
    buttons->addWidget(cancel_);

    progress_ = new QProgressBar(this);
    progress_->setTextVisible(true);
    message_ = new QLabel(this);
    message_->setObjectName("pageMeta");
    message_->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(form);
    auto* actions = new QVBoxLayout;
    actions->setContentsMargins(16, 0, 16, 16);
    actions->setSpacing(8);
    actions->addLayout(buttons);
    actions->addWidget(progress_);
    actions->addWidget(message_);
    actions->addStretch(1);
    layout->addLayout(actions, 1);

    // Адреса видны целиком, даже если они по умолчанию; курсор в начало — видно начало адреса.
    for (auto [edit, archive] : {std::pair{devicesUrl_, settings::IrdbArchive::Devices},
                                 std::pair{protocolsUrl_, settings::IrdbArchive::Protocols}}) {
        edit->setText(settings::irdbUrl(archive));
        edit->setCursorPosition(0);
        connect(edit, &QLineEdit::textChanged, this, &IrDatabasePanel::updateStatus);
    }
    folder_->setText(QDir::toNativeSeparators(settings::irdbSourceFolder()));
    file_->setText(QDir::toNativeSeparators(settings::irdbFile()));

    connect(browseFolder, &QPushButton::clicked, this, [this] {
        const QString chosen =
            QFileDialog::getExistingDirectory(this, tr("Archive files"), folder_->text());
        if (!chosen.isEmpty()) {
            folder_->setText(QDir::toNativeSeparators(chosen));
            updateStatus();
        }
    });
    connect(browseFile, &QPushButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getSaveFileName(
            this, tr("Database file"), file_->text(), tr("IR code database (*.irdb)"), nullptr,
            QFileDialog::DontConfirmOverwrite);
        if (!chosen.isEmpty()) {
            file_->setText(QDir::toNativeSeparators(chosen));
            store();
            service_->reload();
            updateStatus();
        }
    });
    connect(folder_, &QLineEdit::editingFinished, this, &IrDatabasePanel::updateStatus);
    connect(build_, &QPushButton::clicked, this, [this] {
        store();
        message_->clear();
        service_->build();
        updateStatus();
    });
    connect(download_, &QPushButton::clicked, this, [this] {
        store();
        message_->clear();
        // Всё уже скачано — значит, хотят заново (например, файл на сервере обновился).
        QString devices;
        QString protocols;
        bool all = false;
        if (IrDatabaseService::findSources(settings::irdbSourceFolder(), devices, protocols)) {
            if (QMessageBox::question(
                    this, tr("Download"),
                    tr("Both files are already in the folder. Download them again?")) !=
                QMessageBox::Yes) {
                return;
            }
            all = true;
        }
        service_->download(all);
        updateStatus();
    });
    connect(cancel_, &QPushButton::clicked, service_, &IrDatabaseService::cancel);
    connect(service_, &IrDatabaseService::progress, this,
            [this](const QString& stage, qint64 done, qint64 total) {
                message_->setText(stage);
                progress_->setRange(0, total > 0 ? 1000 : 0);
                progress_->setValue(total > 0 ? static_cast<int>(done * 1000 / total) : 0);
                progress_->setFormat(
                    total > 0 ? QStringLiteral("%1 / %2").arg(QLocale().formattedDataSize(done),
                                                              QLocale().formattedDataSize(total))
                              : QString());
            });
    connect(service_, &IrDatabaseService::finished, this, [this](bool, const QString& message) {
        message_->setText(message);
        updateStatus();
    });
    updateStatus();
}

void IrDatabasePanel::store() {
    settings::setIrdbUrl(settings::IrdbArchive::Devices, devicesUrl_->text());
    settings::setIrdbUrl(settings::IrdbArchive::Protocols, protocolsUrl_->text());
    settings::setIrdbSourceFolder(QDir::fromNativeSeparators(folder_->text().trimmed()));
    const QString before = settings::irdbFile();
    settings::setIrdbFile(QDir::fromNativeSeparators(file_->text().trimmed()));
    if (settings::irdbFile() != before) {
        service_->reload();
    }
}

void IrDatabasePanel::loadDefaults() {
    devicesUrl_->setText(settings::defaultIrdbUrl(settings::IrdbArchive::Devices));
    protocolsUrl_->setText(settings::defaultIrdbUrl(settings::IrdbArchive::Protocols));
    devicesUrl_->setCursorPosition(0);
    protocolsUrl_->setCursorPosition(0);
    folder_->setText(QDir::toNativeSeparators(settings::defaultIrdbFolder()));
    file_->setText(
        QDir::toNativeSeparators(QDir(settings::defaultIrdbFolder()).filePath("harmony.irdb")));
    updateStatus();
}

void IrDatabasePanel::updateStatus() {
    const bool busy = service_->busy();
    cancel_->setVisible(busy);
    progress_->setVisible(busy);
    for (QWidget* widget : std::initializer_list<QWidget*>{download_, build_, devicesUrl_,
                                                           protocolsUrl_, folder_, file_}) {
        widget->setEnabled(!busy);
    }
    if (busy) {
        return;
    }

    // Собранная база.
    if (const irdb::IrDatabase* database = service_->database()) {
        const QFileInfo info(settings::irdbFile());
        status_->setText(tr("%1 · indexed %2")
                             .arg(labels::plural(static_cast<qsizetype>(database->entries().size()),
                                                 "device", "devices"),
                                  QLocale().toString(info.lastModified(), QLocale::ShortFormat)));
        const auto metadata = QJsonDocument::fromJson(QByteArray::fromStdString(database->buildInfo())).object();
        QStringList passport;
        passport << tr("Format version: %1").arg(database->formatVersion());
        passport << tr("Source: %1").arg(QString::fromStdString(database->source()));
        passport << tr("Size: %1").arg(QLocale().formattedDataSize(info.size()));
        if (!metadata.isEmpty()) {
            passport << tr("Built: %1").arg(QLocale().toString(
                QDateTime::fromSecsSinceEpoch(metadata.value("builtAt").toInteger()), QLocale::ShortFormat));
            passport << tr("Commands: %1; skipped records: %2")
                .arg(metadata.value("commands").toInteger()).arg(metadata.value("skippedLines").toInteger());
        }
        status_->setText(status_->text() + QStringLiteral("\n") + passport.join(QStringLiteral("\n")));
        status_->setWordWrap(true);
    } else {
        status_->setText(tr("not indexed yet"));
    }

    // Файлы архива в папке: имя — последняя часть адреса из поля (ещё не сохранённого).
    const QString folder = QDir::fromNativeSeparators(folder_->text().trimmed());
    const IrDatabaseService::Archive archives[] = {
        IrDatabaseService::archive(settings::IrdbArchive::Devices, devicesUrl_->text()),
        IrDatabaseService::archive(settings::IrdbArchive::Protocols, protocolsUrl_->text())};
    QStringList rows;
    int present = 0;
    for (const auto& archive : archives) {
        const QString found = IrDatabaseService::findArchive(folder, archive);
        present += found.isEmpty() ? 0 : 1;
        rows << (found.isEmpty() ? tr("%1 — not in the folder yet").arg(archive.name)
                                 : tr("%1 — in the folder").arg(QFileInfo(found).fileName()));
    }
    files_->setText(rows.join(QLatin1Char('\n')) + QLatin1Char('\n') +
                    tr("The file name is taken from the address. The third file of the release, "
                       "catalog_raw.jsonl.gz, is not needed."));

    // «Собрать» — только когда есть оба файла; главная кнопка — следующий шаг.
    const bool haveFiles = present == 2;
    build_->setEnabled(haveFiles);
    build_->setToolTip(haveFiles ? QString() : tr("Download the archive files first"));
    build_->setObjectName(haveFiles ? "primaryButton" : "");
    download_->setObjectName(haveFiles ? "" : "primaryButton");
    for (QPushButton* button : {build_, download_}) {
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
}

} // namespace harmony::app
