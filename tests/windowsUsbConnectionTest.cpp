#include "../app/src/windowsUsbConnection.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <gtest/gtest.h>

namespace {
using namespace harmony::app::windowsUsb;

TEST(WindowsUsbConnection, DistinguishesHardwareDriverAdapterAndAddressFailures) {
    EXPECT_EQ(parseStatus(R"({"state":"absent"})").state, State::Absent);
    EXPECT_EQ(parseStatus(R"({"state":"driver-missing"})").state, State::DriverMissing);
    EXPECT_EQ(parseStatus(R"({"state":"disabled"})").state, State::Disabled);
    EXPECT_EQ(parseStatus(R"({"state":"no-address"})").state, State::NoAddress);
    EXPECT_EQ(parseStatus(R"({"state":"multiple"})").state, State::Multiple);
    EXPECT_EQ(parseStatus("invalid").state, State::Unknown);
    const auto status = parseStatus(R"({"state":"ready","adapter":"Ethernet 2","address":"169.254.1.1","deviceId":"USB-ID"})");
    EXPECT_EQ(status.state, State::Ready);
    EXPECT_TRUE(describe(status).contains("Ethernet 2"));
    EXPECT_TRUE(describe(status).contains("169.254.1.1"));
}

#ifdef Q_OS_WIN
/// Выполняет настоящий сценарий со штатными функциями WMI, подменёнными фикстурой.
QByteArray runScript(const QString& name, const QString& fixture, int& code) {
    QFile file(":/connection/" + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QString script = fixture + '\n' + QString::fromUtf8(file.readAll());
    QByteArray utf16;
    for (QChar c : script) { utf16.append(static_cast<char>(c.unicode() & 255)); utf16.append(static_cast<char>(c.unicode() >> 8)); }
    QProcess process;
    process.start(QDir(qEnvironmentVariable("SystemRoot")).filePath("System32/WindowsPowerShell/v1.0/powershell.exe"),
                  {"-NoProfile", "-NonInteractive", "-EncodedCommand", QString::fromLatin1(utf16.toBase64())});
    // PowerShell must see EOF on redirected stdin; CI hosts can also start it slowly.
    process.closeWriteChannel();
    if (!process.waitForFinished(60000)) {
        ADD_FAILURE() << "PowerShell did not finish: " << process.errorString().toStdString()
                      << "; stderr: " << process.readAllStandardError().toStdString();
        process.kill(); process.waitForFinished(1000); code = -1; return {};
    }
    code = process.exitCode();
    return process.readAllStandardOutput();
}

TEST(WindowsUsbConnection, ActualDiagnosticScriptFindsDisabledAndReadyAdapters) {
    for (const bool enabled : {false, true}) {
        const QString fixture = QString::fromLatin1(R"(
function Get-CimInstance {
    param([string]$ClassName, [string]$Filter)
    switch ($ClassName) {
        'Win32_PnPEntity' { [pscustomobject]@{PNPDeviceID='USB\VID_046D&PID_C11F\TEST';ConfigManagerErrorCode=0} }
        'Win32_NetworkAdapter' { [pscustomobject]@{PNPDeviceID='USB\VID_046D&PID_C11F\TEST';NetEnabled=%1;NetConnectionID='USB test';Index=3} }
        'Win32_NetworkAdapterConfiguration' { [pscustomobject]@{IPAddress=@('169.254.1.1')} }
    }
})").arg(enabled ? "$true" : "$false");
        int code = -1;
        const auto status = parseStatus(runScript("usb-status.ps1", fixture, code));
        EXPECT_EQ(code, 0);
        EXPECT_EQ(status.state, enabled ? State::Ready : State::Disabled);
    }
}

TEST(WindowsUsbConnection, RecoveryNeverSelectsAnotherNetworkAdapter) {
    const QString fixture = QString::fromLatin1(R"ps(
function Get-CimInstance {
    param([string]$ClassName)
    if ($ClassName -eq 'Win32_PnPEntity') {
        [pscustomobject]@{PNPDeviceID='USB\VID_046D&PID_C11F\TEST';ConfigManagerErrorCode=0}
    } else {
        [pscustomobject]@{PNPDeviceID='USB\SOME_OTHER_DEVICE';NetEnabled=$true;NetConnectionID='Ethernet'}
        [pscustomobject]@{PNPDeviceID='USB\VID_046D&PID_C11F\TEST';NetEnabled=$true;NetConnectionID='Harmony *';GUID='target-guid'}
    }
}
function Get-NetAdapter {
    param([switch]$IncludeHidden)
    [pscustomobject]@{InterfaceGuid='other-guid';Name='Ethernet'}
    [pscustomobject]@{InterfaceGuid='target-guid';Name='Harmony *'}
}
function Restart-NetAdapter { param([object]$InputObject, [switch]$Confirm); Write-Output "RESTARTED:$($InputObject.Name)" }
)ps");
    int code = -1;
    const QByteArray output = runScript("usb-recover.ps1", fixture, code);
    EXPECT_EQ(code, 0);
    EXPECT_TRUE(output.contains("RESTARTED:Harmony *"));
    EXPECT_FALSE(output.contains("RESTARTED:Ethernet"));
}
#endif
} // namespace
