#include "windowsUsbConnection.h"
#include "journal.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QTimer>
#include <QVariant>
#include <QWidget>
#include <memory>
#include <array>
#include <string>
#include <utility>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace harmony::app::windowsUsb {
namespace {
/// Кодирование команды PowerShell без интерпретации имён адаптеров оболочкой.
QString encodedScript(const QString& name) {
    QFile file(":/connection/" + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QString script = QString::fromUtf8(file.readAll());
    QByteArray utf16;
    utf16.reserve(script.size() * 2);
    for (const QChar character : script) {
        utf16.append(static_cast<char>(character.unicode() & 0xff));
        utf16.append(static_cast<char>(character.unicode() >> 8));
    }
    return QString::fromLatin1(utf16.toBase64());
}

/// Путь системной PowerShell; PATH пользователя не участвует в выборе программы.
QString powershell() {
#ifdef Q_OS_WIN
    std::array<wchar_t, MAX_PATH> path{};
    const UINT length = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    return QDir(QString::fromWCharArray(path.data(), static_cast<qsizetype>(length)))
        .filePath("WindowsPowerShell/v1.0/powershell.exe");
#else
    return {};
#endif
}
} // namespace

bool available() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

Status parseStatus(const QByteArray& json) {
    Status status;
    const auto object = QJsonDocument::fromJson(json).object();
    const QString state = object.value("state").toString();
    if (state == "absent") status.state = State::Absent;
    else if (state == "driver-missing") status.state = State::DriverMissing;
    else if (state == "disabled") status.state = State::Disabled;
    else if (state == "no-address") status.state = State::NoAddress;
    else if (state == "ready") status.state = State::Ready;
    else if (state == "device-error") status.state = State::DeviceError;
    else if (state == "multiple") status.state = State::Multiple;
    status.deviceId = object.value("deviceId").toString();
    status.adapter = object.value("adapter").toString();
    status.address = object.value("address").toString();
    status.detail = object.value("detail").toString();
    return status;
}

Status inspect() {
    if (!available()) return {};
    const QString command = encodedScript("usb-status.ps1");
    if (command.isEmpty()) return {};
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    process.start(powershell(), {"-NoProfile", "-NonInteractive", "-EncodedCommand", command});
    if (!process.waitForFinished(8000)) {
        process.kill();
        process.waitForFinished(1000);
        Status status;
        status.detail = process.errorString();
        return status;
    }
    return parseStatus(process.readAllStandardOutput());
}

QString describe(const Status& status) {
    QString text;
    switch (status.state) {
    case State::Absent: return QObject::tr("No Harmony USB network device is connected.");
    case State::DriverMissing: text = QObject::tr("Harmony is connected by USB, but its USB network driver is missing."); break;
    case State::Disabled: text = QObject::tr("Harmony is connected by USB, but its network adapter is disabled."); break;
    case State::NoAddress: text = QObject::tr("Harmony is connected by USB; waiting for its network adapter to obtain an IP address."); break;
    case State::Ready: text = QObject::tr("Harmony's USB network adapter is ready, but the remote is not answering."); break;
    case State::DeviceError: text = QObject::tr("Windows reports a problem with the Harmony USB device (code %1).").arg(status.detail); break;
    case State::Multiple: return QObject::tr("Several Harmony USB network devices are connected. Connect only one.");
    case State::Unknown: return QObject::tr("Could not inspect the Harmony USB connection: %1").arg(status.detail);
    }
    if (!status.adapter.isEmpty()) text += "\n" + QObject::tr("Adapter: %1").arg(status.adapter);
    if (!status.address.isEmpty()) text += "\n" + QObject::tr("IP address: %1").arg(status.address);
    return text;
}

void recover(QWidget* parent, std::function<void()> done) {
#ifdef Q_OS_WIN
    if (parent->property("usbRecoveryRunning").toBool()) return;
    const QString command = encodedScript("usb-recover.ps1");
    if (command.isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("USB connection"), QObject::tr("The USB recovery script is missing."));
        return;
    }
    const std::wstring executable = QDir::toNativeSeparators(powershell()).toStdWString();
    const std::wstring arguments = ("-NoProfile -NonInteractive -EncodedCommand " + command).toStdWString();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.hwnd = reinterpret_cast<HWND>(parent->winId());
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.lpParameters = arguments.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        const DWORD error = GetLastError();
        if (error != ERROR_CANCELLED) QMessageBox::warning(parent, QObject::tr("USB connection"),
            QObject::tr("Could not start USB connection recovery (Windows error %1).").arg(error));
        return;
    }
    auto handle = std::shared_ptr<void>(info.hProcess, [](void* value) { CloseHandle(value); });
    parent->setProperty("usbRecoveryRunning", true);
    auto* progress = new QProgressDialog(QObject::tr("Recovering the Harmony USB connection…"), QString(), 0, 0, parent);
    progress->setWindowTitle(QObject::tr("USB connection"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    journal::info(QObject::tr("Recovering the Harmony USB connection…"));
    auto* timer = new QTimer(parent);
    timer->setInterval(300);
    QObject::connect(timer, &QTimer::timeout, parent, [parent, timer, progress, handle, done = std::move(done)] {
        if (WaitForSingleObject(handle.get(), 0) != WAIT_OBJECT_0) return;
        timer->stop();
        parent->setProperty("usbRecoveryRunning", false);
        timer->deleteLater();
        progress->deleteLater();
        DWORD code = 0;
        GetExitCodeProcess(handle.get(), &code);
        if (code != 0) {
            QString message;
            if (code == 2) message = QObject::tr("Connect the remote by USB before recovering its connection.");
            else if (code == 3) message = QObject::tr("Connect only one Harmony USB network device before recovering its connection.");
            else if (code == 4) message = QObject::tr("The Harmony USB network driver is missing or its adapter could not be found.");
            else message = QObject::tr("USB connection recovery failed (code %1).").arg(code);
            journal::error(message);
            QMessageBox::warning(parent, QObject::tr("USB connection"), message);
        } else {
            journal::info(QObject::tr("The Harmony USB adapter was enabled or restarted. Waiting for the remote…"));
        }
        // Драйвер и DHCP поднимаются после завершения системной команды.
        QTimer::singleShot(1500, parent, done);
    });
    timer->start();
#else
    Q_UNUSED(parent)
    Q_UNUSED(done)
#endif
}
} // namespace harmony::app::windowsUsb
