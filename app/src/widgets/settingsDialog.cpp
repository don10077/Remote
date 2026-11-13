/**
 * @file settingsDialog.cpp
 * @brief Окно настроек программы.
 */

#include "settingsDialog.h"

#include "../journal.h"
#include "../theme.h"

#include "../appSettings.h"
#include "../i18n.h"
#include "harmony/transport/remoteLink.h"
#include "irDatabasePanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

/// Страница вкладки с формой.
QFormLayout* addPage(QTabWidget* tabs, const QString& title) {
    auto* page = new QWidget(tabs);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(16, 16, 16, 16);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    tabs->addTab(page, title);
    return form;
}

/// Подсказка под полем.
QLabel* hint(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("pageMeta");
    label->setWordWrap(true);
    // Высоту строки форма считает по более узкой ширине — прижимаем текст к верху, чтобы не
    // «висел».
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return label;
}

} // namespace

SettingsDialog::SettingsDialog(IrDatabaseService* irDatabase, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setMinimumWidth(680);

    auto* tabs = new QTabWidget(this);
    tabs_ = tabs;

    // --- Общие ---
    QFormLayout* general = addPage(tabs, tr("General"));
    language_ = new QComboBox(this);
    for (const auto& language : i18n::languages()) {
        language_->addItem(language.name, language.code);
    }
    general->addRow(tr("Language:"), language_);
    general->addRow({},
                    hint(tr("The new language takes effect after restarting the program."), this));

    // Свой перевод: файл harmony_<код>.ts в папке переводов — без пересборки программы.
    auto* folderButton = new QPushButton(tr("Translations folder"), this);
    auto* templateButton = new QPushButton(tr("Translation template…"), this);
    auto* translationButtons = new QHBoxLayout;
    translationButtons->addWidget(folderButton);
    translationButtons->addWidget(templateButton);
    translationButtons->addStretch(1);
    general->addRow(tr("Own translation:"), translationButtons);
    general->setRowWrapPolicy(QFormLayout::DontWrapRows);
    general->addRow(
        {}, hint(tr("A harmony_<code>.ts file in the translations folder adds a language "
                    "(harmony_de.ts) or corrects a built-in one (harmony_ru.ts). The template "
                    "contains all program strings; untranslated ones stay in English."),
                 this));
    connect(folderButton, &QPushButton::clicked, this, [] {
        QDir().mkpath(i18n::userTranslationFolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(i18n::userTranslationFolder()));
    });
    connect(templateButton, &QPushButton::clicked, this, [this] {
        QDir().mkpath(i18n::userTranslationFolder());
        const QString file = QFileDialog::getSaveFileName(
            this, tr("Translation template"),
            QDir(i18n::userTranslationFolder()).filePath("harmony_de.ts"),
            tr("Qt Linguist translation (harmony_*.ts)"));
        if (file.isEmpty()) {
            return;
        }
        // Код языка — из имени файла: harmony_<код>.ts.
        static const QRegularExpression name(
            QStringLiteral("harmony_([A-Za-z]{2,3}(?:_[A-Za-z]{2,4})?)\\.ts$"));
        const auto match = name.match(QFileInfo(file).fileName());
        if (!match.hasMatch()) {
            QMessageBox::warning(
                this, tr("Translation template"),
                tr("The file name must be harmony_<code>.ts, for example harmony_de.ts."));
            return;
        }
        if (!i18n::writeTemplate(file, match.captured(1))) {
            QMessageBox::warning(this, tr("Translation template"),
                                 tr("Could not write %1").arg(file));
            return;
        }
        QMessageBox::information(
            this, tr("Translation template"),
            tr("Template saved: %1\n\nTranslate the strings and restart the program — "
               "the language will appear in the list.")
                .arg(QDir::toNativeSeparators(file)));
    });
    startup_ = new QComboBox(this);
    startup_->addItem(tr("Configuration chooser"), static_cast<int>(settings::Startup::Choose));
    startup_->addItem(tr("Last opened configuration"), static_cast<int>(settings::Startup::Last));
    general->addRow(tr("At startup open:"), startup_);
    general->addRow({},
                    hint(tr("A configuration marked “open at startup” in the chooser is opened in "
                            "any case."),
                         this));

    theme_ = new QComboBox(this);
    theme_->addItem(tr("As in the system"), static_cast<int>(settings::ThemeMode::System));
    theme_->addItem(tr("Light"), static_cast<int>(settings::ThemeMode::Light));
    theme_->addItem(tr("Dark"), static_cast<int>(settings::ThemeMode::Dark));
    general->addRow(tr("Appearance:"), theme_);

    journalLevel_ = new QComboBox(this);
    for (const auto level : {journal::Level::Error, journal::Level::Warning, journal::Level::Info,
                             journal::Level::Debug}) {
        journalLevel_->addItem(journal::levelName(level), static_cast<int>(level));
    }
    general->addRow(tr("Journal records:"), journalLevel_);
    general->addRow({}, hint(tr("Help → Program journal… shows the records; they are also kept in a file."), this));

    // --- Пульт ---
    QFormLayout* remote = addPage(tabs, tr("Remote"));
    remotePage_ = remote->parentWidget();
    host_ = new QLineEdit(this);
    host_->setPlaceholderText(QString::fromUtf8(transport::kDefaultRemoteHost));
    port_ = new QSpinBox(this);
    port_->setRange(1, 65535);
    auto* address = new QHBoxLayout;
    address->addWidget(host_, 1);
    address->addWidget(new QLabel(tr("port"), this));
    address->addWidget(port_);
    remote->addRow(tr("Remote address:"), address);
    remote->addRow({},
                   hint(tr("Over USB the Harmony 900 appears as a network adapter at 169.254.1.2, "
                           "port 3074. Change this only if the remote is reachable differently."),
                        this));
    autoProbe_ = new QCheckBox(tr("Check the remote connection every 10 seconds"), this);
    remote->addRow({}, autoProbe_);
    learnTimeout_ = new QSpinBox(this);
    learnTimeout_->setRange(5, 120);
    learnTimeout_->setSuffix(tr(" s"));
    remote->addRow(tr("Wait for a signal when learning:"), learnTimeout_);

    backupFolder_ = new QLineEdit(this);
    auto* browse = new QPushButton(tr("Browse…"), this);
    auto* folder = new QHBoxLayout;
    folder->addWidget(backupFolder_, 1);
    folder->addWidget(browse);
    remote->addRow(tr("Backups:"), folder);
    remote->addRow({},
                   hint(tr("The remote's configuration is saved here before every write and when "
                           "reading."),
                        this));
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString chosen =
            QFileDialog::getExistingDirectory(this, tr("Backups folder"), backupFolder_->text());
        if (!chosen.isEmpty()) {
            backupFolder_->setText(QDir::toNativeSeparators(chosen));
        }
    });

    // --- База ИК-кодов ---
    irDatabase_ = new IrDatabasePanel(irDatabase, tabs);
    tabs->addTab(irDatabase_, tr("IR database"));

    // --- Кнопки ---
    auto* defaults = new QPushButton(tr("Defaults"), this);
    defaults->setToolTip(tr("Default values on the open tab"));
    auto* cancel = new QPushButton(tr("Cancel"), this);
    auto* save = new QPushButton(tr("Save"), this);
    save->setObjectName("primaryButton");
    save->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(defaults);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(save);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);
    layout->addWidget(tabs, 1);
    layout->addLayout(buttons);

    connect(defaults, &QPushButton::clicked, this, &SettingsDialog::loadDefaults);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this] {
        // Адрес — IPv4/IPv6 или имя узла без пробелов.
        const QString host = host_->text().trimmed();
        if (!host.isEmpty() && host.contains(' ')) {
            QMessageBox::warning(this, tr("Settings"), tr("The remote's address is invalid."));
            return;
        }
        store();
        accept();
    });
    load();
}

void SettingsDialog::showIrDatabase() {
    tabs_->setCurrentWidget(irDatabase_);
}

void SettingsDialog::load() {
    language_->setCurrentIndex(std::max(0, language_->findData(settings::language())));
    startup_->setCurrentIndex(
        std::max(0, startup_->findData(static_cast<int>(settings::startup()))));
    journalLevel_->setCurrentIndex(std::max(0, journalLevel_->findData(static_cast<int>(journal::level()))));
    theme_->setCurrentIndex(std::max(0, theme_->findData(static_cast<int>(settings::themeMode()))));
    backupFolder_->setText(QDir::toNativeSeparators(settings::backupFolder()));
    const QString host = settings::remoteHost();
    host_->setText(host == QString::fromUtf8(transport::kDefaultRemoteHost) ? QString() : host);
    port_->setValue(settings::remotePort());
    autoProbe_->setChecked(settings::autoProbe());
    learnTimeout_->setValue(settings::learnTimeout());
}

void SettingsDialog::loadDefaults() {
    // Только открытая вкладка: остальные настройки пользователь не трогал.
    // Сохраняются, как и любые правки в окне, только по «Сохранить».
    QWidget* page = tabs_->currentWidget();
    if (page == irDatabase_) {
        irDatabase_->loadDefaults();
    } else if (page == remotePage_) {
        host_->clear();
        port_->setValue(transport::Endpoint{}.port);
        autoProbe_->setChecked(true);
        learnTimeout_->setValue(30);
        backupFolder_->setText(QDir::toNativeSeparators(settings::defaultBackupFolder()));
    } else {
        // Язык не сбрасываем: это выбор пользователя, а не «настройка по умолчанию».
        startup_->setCurrentIndex(startup_->findData(static_cast<int>(settings::Startup::Choose)));
        journalLevel_->setCurrentIndex(journalLevel_->findData(static_cast<int>(journal::Level::Info)));
        theme_->setCurrentIndex(theme_->findData(static_cast<int>(settings::ThemeMode::System)));
    }
}

void SettingsDialog::store() {
    irDatabase_->store();
    const QString language = language_->currentData().toString();
    languageChanged_ = language != i18n::current();
    settings::setLanguage(language);
    settings::setStartup(static_cast<settings::Startup>(startup_->currentData().toInt()));
    journal::setLevel(static_cast<journal::Level>(journalLevel_->currentData().toInt()));
    // Оформление меняется сразу, без перезапуска.
    const auto mode = static_cast<settings::ThemeMode>(theme_->currentData().toInt());
    if (mode != settings::themeMode()) {
        settings::setThemeMode(mode);
        theme::apply();
    }
    settings::setBackupFolder(QDir::fromNativeSeparators(backupFolder_->text().trimmed()));
    settings::setRemoteHost(host_->text());
    settings::setRemotePort(port_->value());
    settings::setAutoProbe(autoProbe_->isChecked());
    settings::setLearnTimeout(learnTimeout_->value());
}

} // namespace harmony::app
