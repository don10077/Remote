#include "appUpdater.h"
#include "journal.h"
#include "harmony/core/version.h"

#include <QCoreApplication>
#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QVersionNumber>
#include <QWidget>
#include <memory>
#include <string>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace harmony::app {
namespace {
constexpr qint64 kMaxInstaller = 256 * 1024 * 1024;
constexpr qint64 kMaxMetadata = 2 * 1024 * 1024;
constexpr auto kRepository = "/don10077/Remote/";

/// Принимает только HTTPS-ссылки выпусков нашего репозитория.
bool projectUrl(const QUrl& url, const QString& prefix) {
    return url.scheme() == "https" && url.host() == "github.com" && url.userInfo().isEmpty() &&
           url.port(-1) == -1 && !url.hasQuery() && !url.hasFragment() && url.path().startsWith(prefix);
}

/// Строгое сравнение версий: суффиксы и произвольные имена тегов не становятся обновлениями.
QVersionNumber version(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+$"));
    return pattern.match(text).hasMatch() ? QVersionNumber::fromString(text) : QVersionNumber();
}

/// HTTP-запрос с таймаутом и HTTPS-переадресациями (GitHub отдаёт файл через CDN).
QNetworkRequest request(const QUrl& url) {
    QNetworkRequest result(url);
    result.setRawHeader("User-Agent", "Harmony-Updater");
    result.setTransferTimeout(30000);
    result.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return result;
}

/// Обновление поверх установки применимо к копии с деинсталлятором NSIS.
bool installedCopy() {
#ifdef Q_OS_WIN
    return QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath("uninstall.exe"));
#else
    return false;
#endif
}
} // namespace

std::optional<UpdateRelease> findUpdate(const QByteArray& json, const QString& current) {
    const auto currentVersion = version(current);
    if (currentVersion.isNull()) return std::nullopt;
    auto newest = currentVersion;
    std::optional<UpdateRelease> selected;
    for (const auto& value : QJsonDocument::fromJson(json).array()) {
        const auto object = value.toObject();
        if (object.value("draft").toBool()) continue;
        QString tag = object.value("tag_name").toString();
        const QString originalTag = tag;
        if (tag.startsWith('v')) tag.remove(0, 1);
        const auto candidate = version(tag);
        if (candidate.isNull() || candidate <= newest) continue;
        UpdateRelease release;
        release.version = tag;
        release.page = QUrl(object.value("html_url").toString());
        if (!projectUrl(release.page, QString::fromLatin1(kRepository) + "releases/tag/") ||
            release.page.path() != QString::fromLatin1(kRepository) + "releases/tag/" + originalTag) continue;
        const QString name = "Harmony-" + tag + "-setup.exe";
        const QString assetPath = QString::fromLatin1(kRepository) + "releases/download/" + originalTag + '/' + name;
        for (const auto& assetValue : object.value("assets").toArray()) {
            const auto asset = assetValue.toObject();
            if (asset.value("name").toString() != name) continue;
            const QUrl url(asset.value("browser_download_url").toString());
            const QString digest = asset.value("digest").toString();
            static const QRegularExpression sha(QStringLiteral("^sha256:([a-fA-F0-9]{64})$"));
            const auto match = sha.match(digest);
            const qint64 size = asset.value("size").toInteger();
            if (!projectUrl(url, assetPath) || url.path() != assetPath || !match.hasMatch() ||
                size <= 0 || size > kMaxInstaller) continue;
            release.installer = url;
            release.sha256 = QByteArray::fromHex(match.captured(1).toLatin1());
            release.size = size;
        }
        newest = candidate;
        selected = release;
    }
    return selected;
}

bool verifyUpdate(const QString& file, const UpdateRelease& release) {
    QFile input(file);
    if (release.sha256.size() != 32 || release.size <= 0 || release.size > kMaxInstaller ||
        !input.open(QIODevice::ReadOnly) || input.size() != release.size) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&input) && hash.result() == release.sha256;
}

AppUpdater::AppUpdater(QWidget* parent) : QObject(parent), window_(parent) {}

void AppUpdater::fail(const QString& message, bool interactive) {
    journal::warning(tr("Program update: %1").arg(message));
    if (interactive) QMessageBox::warning(window_, tr("Program update"), message);
}

void AppUpdater::check(bool interactive) {
    if (busy_) return;
    busy_ = true;
    auto query = request(QUrl("https://api.github.com/repos/don10077/Remote/releases?per_page=20"));
    query.setRawHeader("Accept", "application/vnd.github+json");
    auto* reply = network_.get(query);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > kMaxMetadata) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, interactive] {
        busy_ = false;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(tr("Could not check for updates: %1").arg(reply->errorString()), interactive);
            return;
        }
        const QByteArray json = reply->readAll();
        if (json.size() > kMaxMetadata || !QJsonDocument::fromJson(json).isArray()) {
            fail(tr("The update server returned an invalid release list."), interactive);
            return;
        }
        const auto release = findUpdate(json, QString::fromUtf8(kVersion));
        if (release) offer(*release);
        else if (interactive) QMessageBox::information(window_, tr("Program update"),
            tr("Harmony %1 is up to date.").arg(QString::fromUtf8(kVersion)));
    });
}

void AppUpdater::offer(const UpdateRelease& release) {
    busy_ = true;
    if (QApplication::activeModalWidget() != nullptr) {
        QTimer::singleShot(1000, this, [this, release] { offer(release); });
        return;
    }
    const bool install = installedCopy() && !release.installer.isEmpty();
    QMessageBox box(QMessageBox::Information, tr("Program update"),
        tr("Harmony %1 is available. Current version: %2.").arg(release.version, QString::fromUtf8(kVersion)),
        QMessageBox::NoButton, window_);
    box.setInformativeText(install
        ? tr("Download and install the update? Your settings and configurations will be kept. Harmony will close before installation.")
        : tr("Open the release page to update this copy of Harmony?"));
    auto* update = box.addButton(install ? tr("Update") : tr("Open release page"), QMessageBox::AcceptRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.exec();
    busy_ = false;
    if (box.clickedButton() != update) return;
    if (install) download(release);
    else QDesktopServices::openUrl(release.page);
}

void AppUpdater::download(const UpdateRelease& release) {
    if (busy_) return;
    const QString folder = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("updates");
    if (!QDir().mkpath(folder)) {
        fail(tr("Could not create the update folder."), true);
        return;
    }
    const QString filename = QDir(folder).filePath("Harmony-" + release.version + "-setup.exe");
    if (verifyUpdate(filename, release)) {
        readyFile_ = filename;
        readyRelease_ = release;
        emit readyToInstall();
        return;
    }
    auto output = std::make_shared<QSaveFile>(filename);
    if (!output->open(QIODevice::WriteOnly)) { fail(output->errorString(), true); return; }
    busy_ = true;
    auto* progress = new QProgressDialog(tr("Downloading Harmony %1…").arg(release.version), tr("Cancel"), 0, 100, window_);
    progress->setWindowTitle(tr("Program update"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    auto* reply = network_.get(request(release.installer));
    auto received = std::make_shared<qint64>(0);
    auto cancelled = std::make_shared<bool>(false);
    connect(progress, &QProgressDialog::canceled, reply, [reply, cancelled] { *cancelled = true; reply->abort(); });
    connect(reply, &QNetworkReply::readyRead, this, [reply, output, received, release, progress] {
        const QByteArray bytes = reply->readAll();
        *received += bytes.size();
        if (*received > release.size || output->write(bytes) != bytes.size()) { reply->abort(); return; }
        progress->setValue(static_cast<int>(*received * 99 / release.size));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, output, received, cancelled, release, filename, progress] {
        busy_ = false;
        progress->deleteLater();
        reply->deleteLater();
        if (*cancelled) return;
        if (reply->error() != QNetworkReply::NoError || *received != release.size || !output->commit()) {
            fail(tr("Could not download the update: %1").arg(reply->errorString()), true);
            return;
        }
        if (!verifyUpdate(filename, release)) {
            QFile::remove(filename);
            fail(tr("The update checksum does not match. Installation was cancelled."), true);
            return;
        }
        readyFile_ = filename;
        readyRelease_ = release;
        emit readyToInstall();
    });
}

bool AppUpdater::launchInstaller() {
    if (!verifyUpdate(readyFile_, readyRelease_)) {
        fail(tr("The update checksum does not match. Installation was cancelled."), true);
        return false;
    }
#ifdef Q_OS_WIN
    const std::wstring executable = QDir::toNativeSeparators(readyFile_).toStdWString();
    // /D — последний аргумент NSIS, без кавычек: пробелы до конца строки входят в путь.
    const std::wstring arguments = QString("/UPDATEPID=%1 /D=%2")
        .arg(QCoreApplication::applicationPid()).arg(QDir::toNativeSeparators(QCoreApplication::applicationDirPath())).toStdWString();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.hwnd = reinterpret_cast<HWND>(window_->winId());
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.lpParameters = arguments.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        const DWORD error = GetLastError();
        if (error != ERROR_CANCELLED) fail(tr("Could not start the installer (Windows error %1).").arg(error), true);
        return false;
    }
    if (info.hProcess != nullptr) CloseHandle(info.hProcess);
    return true;
#else
    return false;
#endif
}
} // namespace harmony::app
