/**
 * @file linuxUsbSetup.cpp
 * @brief Linux: установка правила udev для Harmony 900 через pkexec.
 */

#include "linuxUsbSetup.h"

#include "journal.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QWidget>

#include <array>

namespace harmony::app::linuxUsb {

namespace {

constexpr const char* kRule = "/etc/udev/rules.d/99-harmony-usbnet.rules";
constexpr const char* kHidRule = "/etc/udev/rules.d/70-harmony-hid.rules";
constexpr const char* kInstaller = "install-usbnet.sh";
constexpr std::array<const char*, 4> kFiles = {kInstaller, "harmony-usbnet.sh", "99-harmony-usbnet.rules",
                                               "70-harmony-hid.rules"};

/**
 * Что выполняет pkexec от root. Файлы в папке пользователя может подменить любая его программа,
 * пока он вводит пароль, поэтому root копирует их в свою временную папку (mktemp — 0700),
 * сверяет SHA-256 с ресурсами программы и только потом запускает. Аргументы:
 * $1 — папка с файлами, $2 — список сумм в формате sha256sum, дальше — аргументы установщика.
 */
constexpr const char* kRootLauncher = R"sh(set -eu
src=$1; sums=$2; shift 2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for f in install-usbnet.sh harmony-usbnet.sh 99-harmony-usbnet.rules 70-harmony-hid.rules; do
    cp -- "$src/$f" "$work/$f"
done
cd "$work"
if ! printf '%s' "$sums" | sha256sum -c --status -; then
    echo "setup files were modified after unpacking; nothing was installed" >&2
    exit 4
fi
sh "$work/install-usbnet.sh" "$@"
)sh";

/// @return Папка, куда копируются файлы установки: своя у пользователя, переживает перезапуск.
QString filesDir() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("usbnet");
}

/// @return Суммы файлов установки из ресурсов в формате `sha256sum -c` или пусто при ошибке.
QString resourceChecksums() {
    QString sums;
    for (const char* name : kFiles) {
        QFile file(QStringLiteral(":/usbnet/") + QString::fromLatin1(name));
        if (!file.open(QIODevice::ReadOnly)) return {};
        const QByteArray hash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
        sums += QString::fromLatin1(hash.toHex()) + QStringLiteral("  ") + QString::fromLatin1(name) + QLatin1Char('\n');
    }
    return sums;
}

/// Копирует файлы из ресурсов в filesDir(). @return Путь к установщику или пустая строка.
/// Из AppImage root файлы прочитать не может, поэтому ставим из обычной папки.
QString unpack() {
    const QString dir = filesDir();
    if (!QDir().mkpath(dir)) return {};
    for (const char* name : kFiles) {
        const QString target = QDir(dir).filePath(QString::fromLatin1(name));
        QFile::remove(target); // copy() не перезаписывает, а версия могла смениться
        if (!QFile::copy(QStringLiteral(":/usbnet/") + QString::fromLatin1(name), target)) return {};
        // Из ресурсов файл приходит только для чтения; права на месте ставит install в скрипте.
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    }
    return QDir(dir).filePath(QString::fromLatin1(kInstaller));
}

/**
 * @brief Заключает строку в одинарные кавычки для sh.
 * @param text Любая строка (путь с пробелами, кавычками).
 * @return Строка, которую sh прочитает как одно слово без подстановок.
 */
QString shellQuoted(const QString& text) {
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

} // namespace

bool available() {
#ifdef Q_OS_LINUX
    return QFile::exists(QStringLiteral(":/usbnet/") + QString::fromLatin1(kInstaller));
#else
    return false;
#endif
}

bool installed() {
    return available() &&
           (QFile::exists(QString::fromLatin1(kRule)) || QFile::exists(QString::fromLatin1(kHidRule)));
}

QString manualCommand() {
    return QStringLiteral("sudo sh ") + shellQuoted(QDir(filesDir()).filePath(QString::fromLatin1(kInstaller)));
}

void install(QWidget* parent, bool uninstall) {
    const QString title = QObject::tr("USB connection setup");
    const QString installer = unpack();
    if (installer.isEmpty()) {
        QMessageBox::warning(parent, title, QObject::tr("Could not copy the setup files to %1.")
                                                .arg(QDir::toNativeSeparators(filesDir())));
        return;
    }
    QString command = manualCommand();
    if (uninstall) command += QStringLiteral(" --remove");
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty()) {
        QMessageBox::information(parent, title,
            QObject::tr("pkexec is not installed. Run in a terminal:<br><br><code>%1</code>").arg(command.toHtmlEscaped()));
        return;
    }

    const QString checksums = resourceChecksums();
    if (checksums.isEmpty()) {
        QMessageBox::warning(parent, title, QObject::tr("Could not read the setup files of the program."));
        return;
    }

    auto* process = new QProcess(parent);
    process->setProcessChannelMode(QProcess::MergedChannels);
    // Текст сценария и суммы — в аргументах pkexec: после запуска их уже не подменить.
    QStringList arguments = {QStringLiteral("/bin/sh"), QStringLiteral("-c"), QString::fromLatin1(kRootLauncher),
                             QStringLiteral("harmony-usbnet-setup"), filesDir(), checksums};
    if (uninstall) arguments << QStringLiteral("--remove");

    const auto report = [=](bool ok, int code, const QString& output) {
        process->deleteLater();
        if (ok) {
            journal::info(uninstall ? QObject::tr("USB connection setup removed") : QObject::tr("USB connection set up"));
            QMessageBox::information(parent, title,
                uninstall ? QObject::tr("The USB connection setup is removed.")
                          : QObject::tr("Done. Harmony 600/650/665/700 can be read right away. Reconnect a "
                                        "Harmony 900: from now on the computer gives it an address by itself "
                                        "every time it is plugged in."));
            return;
        }
        if (code == 126) return; // в окне pkexec нажали «Отмена»
        journal::error(QObject::tr("USB connection setup failed: %1").arg(output));
        QMessageBox::warning(parent, title,
            QObject::tr("Setup failed:<br><code>%1</code><br><br>You can run it in a terminal:<br><code>%2</code>")
                .arg(output.toHtmlEscaped(), command.toHtmlEscaped()));
    };
    QObject::connect(process, &QProcess::finished, parent, [=](int code, QProcess::ExitStatus status) {
        report(status == QProcess::NormalExit && code == 0, code, QString::fromLocal8Bit(process->readAll()).trimmed());
    });
    QObject::connect(process, &QProcess::errorOccurred, parent, [=](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) report(false, -1, process->errorString());
    });
    process->start(pkexec, arguments);
}

} // namespace harmony::app::linuxUsb
