/**
 * @file protocolCommandDialog.cpp
 * @brief Окно «Команда по протоколу».
 */

#include "protocolCommandDialog.h"

#include "../labels.h"
#include "harmony/core/ir/irEncode.h"
#include "learnWizardDialog.h"
#include "../remoteSession.h"
#include "../appSettings.h"
#include <QSignalBlocker>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

/// Число из поля: «0x1F», «1Fh» или десятичное; nullopt — не число.
std::optional<std::uint32_t> parseNumber(const QString& text) {
    QString value = text.trimmed();
    bool ok = false;
    std::uint32_t number = 0;
    if (value.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        number = value.mid(2).toUInt(&ok, 16);
    } else if (value.endsWith(QLatin1Char('h'), Qt::CaseInsensitive)) {
        number = value.chopped(1).toUInt(&ok, 16);
    } else {
        number = value.toUInt(&ok, 10);
    }
    return ok ? std::optional(number) : std::nullopt;
}

/// «0x04»: цифр — по разрядности протокола.
QString hex(std::uint64_t value, int bits) {
    return QStringLiteral("0x") +
           QString::number(value, 16).rightJustified((bits + 3) / 4, QLatin1Char('0')).toUpper();
}

} // namespace

ProtocolCommandDialog::ProtocolCommandDialog(const QString& deviceName, const QStringList& existing,
                                             const QString& name,
                                             const std::optional<ir::IrDecoded>& initial,
                                             QWidget* parent, RemoteSession* session)
    : QDialog(parent), session_(session), existing_(existing) {
    setWindowTitle(tr("Manual command"));
    setMinimumWidth(580);

    auto* title = new QLabel(tr("Manual command — “%1”").arg(deviceName), this);
    title->setObjectName("pageTitle");
    title->setWordWrap(true);
    auto* hint = new QLabel(
        tr("Choose a protocol and input format: address and command, IRremote hexadecimal data, "
           "or bits in transmission order. You can also read a command from the original remote."),
        this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    name_ = new QComboBox(this);
    name_->setObjectName("commandName");
    name_->setEditable(true);
    name_->setInsertPolicy(QComboBox::NoInsert);
    QStringList names = standardCommandNames();
    for (const QString& own : existing_) {
        if (!names.contains(own)) {
            names << own;
        }
    }
    for (const QString& item : names) {
        name_->addItem(existing_.contains(item) ? tr("%1  (exists — replace)").arg(item) : item,
                       item);
    }
    if (!name.isEmpty()) {
        name_->setCurrentIndex(name_->findData(name));
        if (name_->currentIndex() < 0) name_->setEditText(name);
    } else {
        name_->setCurrentIndex(-1);
        name_->setEditText({});
    }

    protocol_ = new QComboBox(this);
    for (const auto& protocol : ir::encodableProtocols()) {
        protocol_->addItem(labels::protocol(protocol.name), QString::fromStdString(protocol.name));
    }
    address_ = new QLineEdit(this);
    address_->setObjectName("commandAddress");
    address_->setPlaceholderText(QStringLiteral("0x04"));
    command_ = new QLineEdit(this);
    command_->setObjectName("commandCode");
    command_->setPlaceholderText(QStringLiteral("0x08"));
    ranges_ = new QLabel(this);
    ranges_->setObjectName("pageMeta");
    ranges_->setWordWrap(true);

    auto* form = new QFormLayout;
    form->setVerticalSpacing(8);
    form->addRow(tr("Name:"), name_);
    form->addRow(tr("Protocol:"), protocol_);
    form->addRow(tr("Address:"), address_);
    form->addRow(tr("Command:"), command_);
    form->addRow({}, ranges_);

    inputMode_ = new QComboBox(this);
    inputMode_->setObjectName("commandInputMode");
    inputMode_->addItems({tr("Address and command"), tr("Data (IRremote)"), tr("Bits in transmission order")});
    raw_ = new QLineEdit(this);
    raw_->setObjectName("commandRaw");
    raw_->setPlaceholderText("0xF708FB04");
    bits_ = new QLineEdit(this);
    bits_->setObjectName("commandBits");
    bits_->setPlaceholderText(tr("010… (first transmitted bit on the left)"));
    form->insertRow(2, tr("Input format:"), inputMode_);
    form->addRow(tr("Data (IRremote):"), raw_);
    form->addRow(tr("Bits in order:"), bits_);
    read_ = new QPushButton(tr("Read from remote…"), this);
    read_->setEnabled(session != nullptr);
    form->addRow({}, read_);
    connect(read_, &QPushButton::clicked, this, [this] {
        if (listening_) { session_->cancel(); return; }
        if (session_->busy()) {
            result_->setText(tr("The remote is busy. Try again when the current operation finishes."));
            return;
        }
        listening_ = true;
        captured_.reset();
        rebuild();
        read_->setText(tr("Cancel reading"));
        result_->setText(tr("Press the button on the original remote."));
        session_->learn(settings::learnTimeout());
    });
    if (session) {
        connect(session, &RemoteSession::learnFinished, this, [this](const ir::IrSignal& signal) {
            if (!listening_) return;
            listening_ = false;
            read_->setText(tr("Read from remote…"));
            const auto decoded = ir::decode(signal);
            // Не оставляем поля предыдущей команды под новым нераспознанным сигналом.
            protocol_->setCurrentIndex(-1);
            address_->clear();
            command_->clear();
            raw_->clear();
            bits_->clear();
            if (decoded) {
                const int index = protocol_->findData(QString::fromStdString(decoded->protocol == "RC5X" ? "RC5" : decoded->protocol));
                if (index >= 0) {
                    protocol_->setCurrentIndex(index);
                    inputMode_->setCurrentIndex(0);
                    address_->setText(hex(decoded->address, 16));
                    command_->setText(hex(decoded->command, 16));
                }
            }
            captured_ = signal;
            rebuild();
        });
        connect(session, &RemoteSession::failed, this, [this](const QString& message, bool) {
            if (!listening_) return;
            listening_ = false;
            read_->setText(tr("Read from remote…"));
            rebuild();
            result_->setText(message);
        });
    }
    result_ = new QLabel(this);
    result_->setWordWrap(true);
    result_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    saveNext_ = new QPushButton(tr("Save and next"), this);
    saveNext_->setObjectName("commandSaveNext");
    saveNext_->setToolTip(
        tr("Save and enter the next command (the command code is increased by one)"));
    auto* close = new QPushButton(tr("Close"), this);
    save_ = new QPushButton(tr("Save"), this);
    save_->setObjectName("primaryButton");
    save_->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(saveNext_);
    buttons->addStretch(1);
    buttons->addWidget(close);
    buttons->addWidget(save_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addLayout(form);
    layout->addWidget(result_);
    layout->addLayout(buttons);

    // Начальные значения: распознанное в сигнале (у правки) или протокол других команд.
    if (initial) {
        const int index = protocol_->findData(QString::fromStdString(
            initial->protocol == "RC5X" ? std::string("RC5") : initial->protocol));
        if (index >= 0) {
            protocol_->setCurrentIndex(index);
            const auto& info = ir::encodableProtocols()[static_cast<std::size_t>(index)];
            address_->setText(hex(initial->address, info.addressBits));
            if (!name.isEmpty()) {
                command_->setText(hex(initial->command, info.commandBits));
            }
        }
    }

    connect(inputMode_, &QComboBox::currentIndexChanged, this, [this] { captured_.reset(); rebuild(); });
    connect(raw_, &QLineEdit::textChanged, this, [this] { captured_.reset(); rebuild(); });
    connect(bits_, &QLineEdit::textChanged, this, [this] { captured_.reset(); rebuild(); });
    connect(protocol_, &QComboBox::currentIndexChanged, this, [this] { captured_.reset(); rebuild(); });
    connect(address_, &QLineEdit::textChanged, this, [this] { captured_.reset(); rebuild(); });
    connect(command_, &QLineEdit::textChanged, this, [this] { captured_.reset(); rebuild(); });
    connect(name_, &QComboBox::currentTextChanged, this, &ProtocolCommandDialog::rebuild);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(save_, &QPushButton::clicked, this, [this] { save(false); });
    connect(saveNext_, &QPushButton::clicked, this, [this] { save(true); });
    rebuild();
}

ProtocolCommandDialog::~ProtocolCommandDialog() {
    if (listening_) session_->cancel();
}

QString ProtocolCommandDialog::commandName() const {
    // Пункт списка «Имя (есть — заменить)» — имя в данных; введённое руками — как есть.
    const int index = name_->currentIndex();
    if (index >= 0 && name_->itemText(index) == name_->currentText()) {
        return name_->itemData(index).toString();
    }
    return name_->currentText().trimmed();
}

void ProtocolCommandDialog::rebuild() {
    if (captured_) {
        result_->setText(labels::signalDetails(*captured_));
        save_->setEnabled(!listening_ && !commandName().isEmpty());
        saveNext_->setEnabled(false);
        return;
    }
    signal_.reset();
    if (protocol_->currentIndex() < 0) {
        result_->setText(tr("Choose a protocol."));
        save_->setEnabled(false);
        saveNext_->setEnabled(false);
        return;
    }
    const auto& info =
        ir::encodableProtocols()[static_cast<std::size_t>(std::max(0, protocol_->currentIndex()))];
    ranges_->setText(tr("Address: 0…%1, command: 0…%2 (decimal or 0x… hexadecimal)")
                         .arg(hex((1ULL << info.addressBits) - 1, info.addressBits),
                              hex((1ULL << info.commandBits) - 1, info.commandBits)));
    const auto address = parseNumber(address_->text());
    const auto command = parseNumber(command_->text());
    QString problem;
    const int mode = inputMode_->currentIndex();
    address_->setEnabled(mode == 0);
    command_->setEnabled(mode == 0);
    raw_->setEnabled(mode == 1);
    bits_->setEnabled(mode == 2);
    if (mode == 0) {
        if (!address || !command) problem = tr("Enter the address and the command.");
        else if (auto signal = ir::encode(info.name, *address, *command); signal) signal_ = *signal;
        else problem = labels::errorText(signal.error());
    } else {
        std::vector<bool> inputBits;
        if (mode == 1) {
            QString text = raw_->text().trimmed();
            bool ok = false;
            const auto number = text.toULongLong(&ok, text.startsWith("0x", Qt::CaseInsensitive) ? 0 : 16);
            const auto sample = ir::encode(info.name, 0, 0);
            const auto decoded = sample ? ir::decode(*sample) : std::nullopt;
            if (ok && decoded && (decoded->bits.size() == 64 || number >> decoded->bits.size() == 0)) {
                for (std::size_t i = 0; i < decoded->bits.size(); ++i)
                    inputBits.push_back(((number >> (decoded->lsbFirst ? i : decoded->bits.size() - 1 - i)) & 1) != 0);
            } else problem = tr("Enter hexadecimal IRremote data for the selected protocol.");
        } else {
            for (const QChar ch : bits_->text()) {
                if (ch.isSpace()) continue;
                if (ch != '0' && ch != '1') { problem = tr("Use only 0 and 1, in transmission order."); break; }
                inputBits.push_back(ch == '1');
            }
        }
        if (problem.isEmpty()) {
            if (auto signal = ir::encodeBits(info.name, inputBits); signal) signal_ = *signal;
            else problem = labels::errorText(signal.error());
        }
    }
    if (signal_) {
        const auto decoded = ir::decode(*signal_);
        protocolName_ = QString::fromStdString(mode == 0 ? info.name :
            decoded->protocol == "RC5X" ? "RC5" : decoded->protocol);
        addressValue_ = mode == 0 ? *address : decoded->address;
        commandValue_ = mode == 0 ? *command : decoded->command;
        result_->setText(labels::signalDetails(*signal_));
        const QSignalBlocker rawBlock(raw_), bitsBlock(bits_);
        if (mode != 1) raw_->setText(QString::fromStdString(decoded->rawData()));
        if (mode != 2) {
            QString text;
            for (bool bit : decoded->bits) text += bit ? '1' : '0';
            bits_->setText(text);
        }
    } else result_->setText(problem);
    const bool ready = !listening_ && signal_.has_value() && !commandName().isEmpty();
    save_->setEnabled(ready);
    saveNext_->setEnabled(ready);
}

void ProtocolCommandDialog::save(bool next) {
    const QString name = commandName();
    if (captured_ && !name.isEmpty() && !listening_) {
        emit signalReady(name, *captured_);
        accept();
        return;
    }
    if (!signal_ || name.isEmpty() || listening_) {
        return;
    }
    emit commandReady(name, protocolName_, addressValue_, commandValue_);
    if (!existing_.contains(name)) {
        existing_ << name;
        const int index = name_->findData(name);
        if (index >= 0) {
            name_->setItemText(index, tr("%1  (exists — replace)").arg(name));
        }
    }
    if (!next) {
        accept();
        return;
    }
    // Следующая: имя — следующее по списку, код — на единицу больше.
    const auto nextAddress = addressValue_;
    const auto nextCommand = commandValue_;
    inputMode_->setCurrentIndex(0);
    address_->setText(hex(nextAddress, 16));
    command_->setText(hex(nextCommand, 16));
    if (const auto command = parseNumber(command_->text())) {
        const auto& info =
            ir::encodableProtocols()[static_cast<std::size_t>(protocol_->currentIndex())];
        command_->setText(hex(*command + 1, info.commandBits));
    }
    const int index = name_->findData(name);
    if (index >= 0 && index + 1 < name_->count()) {
        name_->setCurrentIndex(index + 1);
    } else {
        name_->setEditText({});
    }
    result_->setText(tr("“%1” saved. Enter the next command.").arg(name) + QLatin1Char('\n') +
                     result_->text());
    command_->setFocus();
    command_->selectAll();
}

} // namespace harmony::app
