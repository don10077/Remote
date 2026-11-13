/**
 * @file documentSource.cpp
 * @brief Загрузка конфигурации по источнику.
 */

#include "documentSource.h"

#include <QCoreApplication>
#include <QFile>

namespace harmony::app {

namespace {

/// Содержимое ресурса; nullopt — ресурса нет (файл примера необязателен, кроме XML).
std::optional<QByteArray> readResource(const char* path) {
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return file.readAll();
}

} // namespace

std::filesystem::path toPath(const QString& path) {
    return std::filesystem::path(path.toStdU16String());
}

QString fromPath(const std::filesystem::path& path) {
    return QString::fromStdU16String(path.u16string());
}

Result<ConfigDocument> loadDemo(const QString& name) {
    const auto xml = readResource(":/demo/UserConfiguration.xml");
    if (!xml) {
        return makeError(ErrorCode::NotFound, "the built-in example is missing from the program");
    }
    ConfigDocument::Files files;
    files.userConfiguration = xml->toStdString();
    files.actionLists = readResource(":/demo/ActionLists.xml").value_or(QByteArray()).toStdString();
    auto bytes = [](const char* path) {
        const QByteArray data = readResource(path).value_or(QByteArray());
        return std::vector<std::uint8_t>(data.begin(), data.end());
    };
    files.irProto = bytes(":/demo/IrProto.bin");
    files.ssIr = bytes(":/demo/SsIr.bin");
    return ConfigDocument::fromFiles(files, name.toStdString());
}

Result<ConfigDocument> loadDocument(const QString& source, const QString& name) {
    if (source.isEmpty()) {
        // Подпись служебной активности видна на экране пульта — на языке интерфейса.
        return ConfigDocument::createNew(name.toStdString(),
                                         QCoreApplication::translate("labels", "Power off").toStdString());
    }
    if (source == QLatin1String(kDemoSource)) {
        return loadDemo(name);
    }
    return ConfigDocument::open(toPath(source));
}

} // namespace harmony::app
