/**
 * @file labels.cpp
 * @brief Таблицы перевода служебных значений конфигурации.
 */

#include "labels.h"

#include "i18n.h"
#include "harmony/core/macro.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>

namespace harmony::app::labels {

QString signalDetails(const ir::IrSignal& signal) {
    const auto decoded = ir::decode(signal);
    if (!decoded) return QCoreApplication::translate("CommandDialog", "Protocol:") + " " +
        QCoreApplication::translate("CommandDialog", "not recognised") + "\n" + signalSummary(signal);
    QStringList lines;
    const auto row = [&](const char* title, const QString& value) {
        lines << QCoreApplication::translate("CommandDialog", title) + " " + value;
    };
    row("Protocol:", protocol(decoded->protocol));
    row("Address:", "0x" + QString::number(decoded->address, 16).toUpper());
    row("Command:", "0x" + QString::number(decoded->command, 16).toUpper());
    row("Data (IRremote):", QString::fromStdString(decoded->rawData()));
    row("Bits in order:", QString::fromStdString(ir::bitsToHex(decoded->bits)) +
        QStringLiteral(" (%1)").arg(decoded->bits.size()));
    return lines.join('\n');
}


namespace {

/// Ищет название в таблице (и его перевод на язык интерфейса); нет в таблице — исходное значение.
QString lookup(const QHash<QString, QString>& table, const std::string& key) {
    const QString source = QString::fromStdString(key);
    const auto found = table.constFind(source);
    if (found == table.constEnd()) {
        return source;
    }
    return QCoreApplication::translate("labels", found.value().toUtf8().constData());
}

} // namespace

QString deviceType(const std::string& type) {
    static const QHash<QString, QString> table = {
        {"Television", "Television"},
        {"Projector", "Projector"},
        {"Receiver", "AV receiver"},
        {"Amplifier", "Amplifier"},
        {"DvdCd", "Player"},
        {"DvdCdGame", "Game console"},
        {"SetTopBox", "Set-top box"},
        {"Pvr", "Recording receiver"},
        {"Vcr", "VCR"},
        {"Cd", "CD player"},
        {"Computer", "Computer"},
        {"MediaCenter", "Media center"},
        {"Lighting", "Lighting"},
        // Типы устройств из базы Logitech.
        {"GameConsole", "Game console"},
        {"Satellite", "Satellite receiver"},
        {"CableBox", "Cable box"},
        {"Monitor", "Monitor"},
        {"Laptop", "Laptop"},
        {"TvVcr", "TV/VCR"},
        {"TvDvd", "TV/DVD"},
        {"TvDvdVcr", "TV/DVD/VCR"},
        {"TvHdd", "TV with recorder"},
        {"DvdVcr", "DVD/VCR"},
        {"DvdrVcr", "DVD recorder/VCR"},
        {"DvdRecorder", "DVD recorder"},
        {"DvdCdRadio", "DVD/CD/radio"},
        {"DvdVcrReceiver", "DVD/VCR receiver"},
        {"Cassette", "Cassette deck"},
        {"CdJukebox", "CD changer"},
        {"CdRadioCassette", "CD/radio/cassette"},
        {"Dat", "DAT recorder"},
        {"Laserdisc", "Laserdisc"},
        {"Minidisc", "MiniDisc"},
        {"AudioVideoSwitch", "AV switch"},
        {"MusicServer", "Music server"},
        {"RadioTuner", "Radio tuner"},
        {"HomeAppliance", "Home appliance"},
        {"ClimateControl", "Climate control"},
    };
    return lookup(table, type);
}

const std::vector<std::string>& deviceTypes() {
    static const std::vector<std::string> types = {"Television", "Projector", "Receiver", "Amplifier",
                                                   "SetTopBox", "Pvr", "DvdCd", "DvdCdGame", "Cd",
                                                   "Computer", "MediaCenter", "Vcr", "Lighting"};
    return types;
}

QString activityType(const std::string& type) {
    static const QHash<QString, QString> table = {
        {"PowerOff", "Turn everything off"},
        {"VirtualTelevisionN", "Watch TV"},
        {"VirtualDvd", "Movie"},
        {"VirtualCdMulti", "Music"},
        {"VirtualRadioSimple", "Radio"},
        {"VirtualGameConsole", "Games"},
        {"VirtualPvr", "Recorder (PVR)"},
        {"VirtualVcr", "Video tape"},
        {"VirtualGeneric", "Other"},
        {"VirtualMusicServer", "Music server"},
        {"VirtualSatelliteMusic", "Satellite radio"},
        {"VirtualTeletext", "Teletext"},
        {"VirtualPresentation", "Presentation"},
        {"VirtualMediaCenterTv", "TV (media center)"},
        {"VirtualMediaCenterVideos", "Video (media center)"},
        {"VirtualMediaCenterMusic", "Music (media center)"},
        {"VirtualMediaCenterPictures", "Pictures"},
        {"VirtualTiVo", "TiVo"},
        {"VirtualReplayTV", "ReplayTV"},
        {"VirtualVcrRecorder", "Record to video"},
        {"VirtualLaserdisc", "Laserdisc"},
        {"VirtualCdJukebox", "CD jukebox"},
        {"VirtualMinidisc", "MiniDisc"},
        {"VirtualTape", "Cassette"},
    };
    return lookup(table, type);
}

QString role(const std::string& name) {
    static const QHash<QString, QString> table = {
        {"DEFAULT", "Main"},
        {"VOLUME", "Volume"},
        {"DISPLAY", "Picture"},
        {"CHANNELCHANGING", "Channels"},
    };
    return lookup(table, name);
}

QString action(const model::RemoteConfig& config, const std::string& actionId) {
    // Формат: <id устройства>_<команда>_<модификатор>; в имени команды тоже бывает «_»,
    // поэтому id — до первого «_», модификатор — после последнего.
    if (isMacroAction(actionId)) {
        return QCoreApplication::translate("labels", "Macro (several steps)");
    }
    const auto first = actionId.find('_');
    const auto last = actionId.rfind('_');
    if (first == std::string::npos || last == first) {
        return QString::fromStdString(actionId);
    }

    const std::string deviceId = actionId.substr(0, first);
    const std::string command = actionId.substr(first + 1, last - first - 1);
    const std::string modifier = actionId.substr(last + 1);

    static const QHash<QString, QString> modifiers = {
        {"Press", "press"},
        {"Hold", "hold"},
    };

    const model::Device* device = config.findDevice(deviceId);
    const QString deviceName = device != nullptr ? QString::fromStdString(device->displayName())
                                                 : QString::fromStdString(deviceId);
    return deviceName + " · " + QString::fromStdString(command) + " · " + lookup(modifiers, modifier);
}

QString controlGroup(const std::string& name) {
    static const QHash<QString, QString> table = {
        {"Misc", "Screen"},
        {"HardButtons", "Keys"},
    };
    return lookup(table, name);
}

QString button(const model::Button& button) {
    if (!button.label.empty()) {
        return QString::fromStdString(button.label);
    }
    if (!button.icon.empty()) {
        return "[" + QString::fromStdString(button.icon) + "]";
    }
    // Аппаратная клавиша без подписи: показываем команду — середину «<id>_<команда>_<модификатор>».
    if (isMacroAction(button.actionId)) {
        return QCoreApplication::translate("labels", "Macro");
    }
    const auto first = button.actionId.find('_');
    const auto last = button.actionId.rfind('_');
    if (first != std::string::npos && last > first) {
        return QString::fromStdString(button.actionId.substr(first + 1, last - first - 1));
    }
    return QString("—");
}

QString plural(qsizetype count, const char* one, const char* many) {
    // Перевод формы — «устройство|устройства|устройств» (три формы, как в русском)
    // или «Gerät|Geräte» (две); ключ перевода — «device|devices».
    const QString key = QString::fromUtf8(one) + QLatin1Char('|') + QString::fromUtf8(many);
    const QStringList forms = QCoreApplication::translate("labels", key.toUtf8().constData()).split(QLatin1Char('|'));
    QString form;
    if (forms.size() >= 3) {
        const qsizetype tens = count % 100;
        const qsizetype units = count % 10;
        form = forms[2];
        if (tens < 11 || tens > 14) {
            if (units == 1) {
                form = forms[0];
            } else if (units >= 2 && units <= 4) {
                form = forms[1];
            }
        }
    } else if (forms.size() == 2) {
        form = count == 1 ? forms[0] : forms[1];
    } else {
        form = QString::fromUtf8(count == 1 ? one : many);
    }
    return QLocale().toString(count) + QLatin1Char(' ') + form; // 276 233 — с разделителем разрядов
}

QString protocol(const std::string& name) {
    // Названия протоколов — по-английски; переводится только пояснение.
    static const QHash<QString, QString> table = {
        {"NEC (extended)", QT_TRANSLATE_NOOP("labels", "NEC (extended)")},
    };
    return lookup(table, name);
}

QString signalSummary(const ir::IrSignal& signal) {
    if (const auto decoded = ir::decode(signal)) {
        auto hex = [](std::uint32_t value) {
            return QStringLiteral("0x") +
                   QString::number(value, 16).toUpper().rightJustified(value > 0xFF ? 4 : 2, '0');
        };
        return QCoreApplication::translate("labels", "%1 · address %2, command %3")
            .arg(protocol(decoded->protocol), hex(decoded->address), hex(decoded->command));
    }
    qsizetype marks = 0;
    for (const int duration : signal.durations) {
        marks += duration > 0 ? 1 : 0;
    }
    return QCoreApplication::translate("labels", "protocol not recognised · pulses: %1, carrier %2 kHz")
        .arg(marks)
        .arg(signal.frequency / 1000.0, 0, 'f', 1);
}

QString errorText(const Error& error) {
    // Ошибки ядра переводятся по шаблону; подстановка — как в ядре (%1…%9 по порядку).
    std::vector<std::string> args = error.args;
    const QString pattern = QCoreApplication::translate("core", error.format.c_str());
    QString text;
    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern[i];
        if (c == '%' && i + 1 < pattern.size() && pattern[i + 1] >= '1' && pattern[i + 1] <= '9') {
            const auto index = static_cast<std::size_t>(pattern[i + 1].unicode() - '1');
            if (index < args.size()) {
                text += QString::fromStdString(args[index]);
                ++i;
                continue;
            }
        }
        text += c;
    }
    if (error.cause) {
        text += QStringLiteral(": ") + errorText(*error.cause);
    }
    return text;
}

QString shortcut(QKeySequence::StandardKey key) {
    return QKeySequence(key).toString(QKeySequence::NativeText);
}

} // namespace harmony::app::labels
