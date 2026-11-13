/**
 * @file remoteSettingsDialog.cpp
 * @brief Окно «Настройки пульта».
 */

#include "remoteSettingsDialog.h"

#include "../appSettings.h"
#include "../journal.h"
#include "../labels.h"
#include "harmony/core/remotePreferences.h"
#include "harmony/transport/remoteServices.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

/// Курсор ожидания на время обмена с пультом (несколько секунд).
class WaitCursor {
public:
    WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QApplication::restoreOverrideCursor(); }
    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;
};

QString qs(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// Подписи вариантов — для перевода.
QString choiceLabel(std::string_view label) {
    static const QMap<QString, QString> labels = {
        {"Off", QObject::tr("Off")},         {"On", QObject::tr("On")},
        {"24-hour", QObject::tr("24-hour")}, {"12-hour", QObject::tr("12-hour")},
        {"Default", QObject::tr("Default")}, {"Diode", QObject::tr("Diode")},
        {"Polymer", QObject::tr("Polymer")}, {"Tron", QObject::tr("Tron")},
    };
    return labels.value(qs(label), qs(label));
}

QString preferenceLabel(std::string_view label) {
    static const QMap<QString, QString> labels = {
        {"Key beep", QObject::tr("Key beep")},
        {"Backlight brightness, %", QObject::tr("Backlight brightness, %")},
        {"Backlight timeout, s", QObject::tr("Backlight timeout, s")},
        {"Large font", QObject::tr("Large font")},
        {"Screen theme", QObject::tr("Screen theme")},
        {"Clock", QObject::tr("Clock")},
        {"Child lock", QObject::tr("Child lock")},
    };
    return labels.value(qs(label), qs(label));
}

} // namespace

RemoteSettingsDialog::RemoteSettingsDialog(const ConfigDocument* document,
                                           std::function<bool(const QMap<QString, QString>&)> applyToDocument,
                                           QWidget* parent)
    : QDialog(parent), document_(document), applyToDocument_(std::move(applyToDocument)) {
    setWindowTitle(tr("Remote settings"));
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Settings of the remote itself. “Save to configuration” — they are written to the "
                               "remote together with the configuration. “Apply to remote” — the connected remote "
                               "changes them at once. “As on the remote” — the remote keeps its own value."),
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* form = new QFormLayout;
    for (const auto& preference : remotePreferences()) {
        const QString key = qs(preference.key);
        QWidget* field = nullptr;
        if (!preference.choices.empty()) {
            auto* combo = new QComboBox(this);
            combo->addItem(tr("As on the remote"), QString());
            for (const auto& choice : preference.choices) {
                combo->addItem(choiceLabel(choice.label), qs(choice.value));
            }
            field = combo;
        } else {
            auto* spin = new QSpinBox(this);
            // Значение на единицу меньше минимума — «как на пульте».
            spin->setRange(preference.min - 1, preference.max);
            spin->setSpecialValueText(tr("As on the remote"));
            spin->setValue(preference.min - 1);
            field = spin;
        }
        fields_.insert(key, field);
        form->addRow(preferenceLabel(preference.label) + ":", field);
    }
    layout->addLayout(form);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(status_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* read = buttons->addButton(tr("Read from remote"), QDialogButtonBox::ActionRole);
    QPushButton* write = buttons->addButton(tr("Apply to remote"), QDialogButtonBox::ActionRole);
    connect(read, &QPushButton::clicked, this, &RemoteSettingsDialog::readFromRemote);
    connect(write, &QPushButton::clicked, this, &RemoteSettingsDialog::writeToRemote);
    if (document_ != nullptr && applyToDocument_) {
        QPushButton* save = buttons->addButton(tr("Save to configuration"), QDialogButtonBox::AcceptRole);
        connect(save, &QPushButton::clicked, this, &RemoteSettingsDialog::saveToDocument);
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    if (document_ != nullptr) {
        QMap<QString, QString> current;
        for (const auto& preference : remotePreferences()) {
            if (auto value = document_->remotePreference(preference.key)) {
                current.insert(qs(preference.key), QString::fromStdString(*value));
            }
        }
        setValues(current);
        status_->setText(tr("Values from the open configuration."));
    }
}

QMap<QString, QString> RemoteSettingsDialog::values() const {
    QMap<QString, QString> out;
    for (auto it = fields_.cbegin(); it != fields_.cend(); ++it) {
        if (const auto* combo = qobject_cast<const QComboBox*>(it.value())) {
            out.insert(it.key(), combo->currentData().toString());
        } else if (const auto* spin = qobject_cast<const QSpinBox*>(it.value())) {
            out.insert(it.key(), spin->value() < spin->minimum() + 1 ? QString() : QString::number(spin->value()));
        }
    }
    return out;
}

void RemoteSettingsDialog::setValues(const QMap<QString, QString>& values) {
    for (auto it = fields_.cbegin(); it != fields_.cend(); ++it) {
        const QString value = values.value(it.key());
        if (auto* combo = qobject_cast<QComboBox*>(it.value())) {
            const int index = combo->findData(value);
            combo->setCurrentIndex(index < 0 ? 0 : index);
        } else if (auto* spin = qobject_cast<QSpinBox*>(it.value())) {
            bool ok = false;
            const int number = value.toInt(&ok);
            spin->setValue(ok ? number : spin->minimum());
        }
    }
}

void RemoteSettingsDialog::readFromRemote() {
    const std::string host = settings::remoteHost().toStdString();
    QMap<QString, QString> read;
    {
        WaitCursor wait;
        for (const auto& preference : remotePreferences()) {
            auto value = transport::readRemoteSetting(host, preference.endpoint());
            if (!value) {
                status_->setText(labels::errorText(value.error()));
                return;
            }
            read.insert(qs(preference.key), QString::fromStdString(*value));
        }
    }
    setValues(read);
    status_->setText(tr("Values read from the connected remote."));
    journal::info(tr("Remote settings read from the remote"));
}

void RemoteSettingsDialog::writeToRemote() {
    const std::string host = settings::remoteHost().toStdString();
    const auto chosen = values();
    int written = 0;
    {
        WaitCursor wait;
        for (const auto& preference : remotePreferences()) {
            const QString value = chosen.value(qs(preference.key));
            if (value.isEmpty()) {
                continue;
            }
            auto result = transport::writeRemoteSetting(host, preference.endpoint(), value.toStdString());
            if (!result) {
                status_->setText(labels::errorText(result.error()));
                journal::error(tr("Remote settings: %1").arg(labels::errorText(result.error())));
                return;
            }
            ++written;
        }
    }
    status_->setText(tr("Applied to the connected remote: %1.").arg(written));
    journal::info(tr("Remote settings applied to the remote: %1").arg(written));
}

void RemoteSettingsDialog::saveToDocument() {
    if (applyToDocument_ && applyToDocument_(values())) {
        accept();
    }
}

} // namespace harmony::app
