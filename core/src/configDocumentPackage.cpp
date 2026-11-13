/**
 * @file configDocumentPackage.cpp
 * @brief Конфигурация в формате .EZHex: открытие, сборка для файла и для пульта.
 *
 * Дерево файлов внутри .EZHex (так его распаковывает пульт):
 * @code
 *   userconfig/UserConfiguration.xml, ActionLists.xml, IrProto.bin, SsIr.bin
 *   .preinstall, .postinstall   — скрипты, которые пульт выполняет при установке
 *   .version, META-INF/MANIFEST.MF
 *   platformconfig/…            — настройки пульта (есть в снятой с пульта копии)
 * @endcode
 */

#include "configDocumentImpl.h"
#include "harmony/core/remotePreferences.h"
#include "harmony/core/remoteProfile.h"

#include <array>
#include <ctime>
#include <iterator>
#include <set>
#include <string_view>

namespace harmony {

namespace {

using formats::Bytes;

constexpr std::string_view kUserFolder = "userconfig/";
constexpr std::string_view kPlatformFolder = "platformconfig/";

/// Файлы конфигурации внутри архива.
struct PackedFile {
    std::string_view path;
    Bytes ConfigDocument::Files::*binary = nullptr;     ///< IrProto.bin, SsIr.bin
    std::string ConfigDocument::Files::*text = nullptr; ///< XML
};

const std::array<PackedFile, 4>& packedFiles() {
    static const std::array<PackedFile, 4> files = {{
        {"userconfig/UserConfiguration.xml", nullptr, &ConfigDocument::Files::userConfiguration},
        {"userconfig/ActionLists.xml", nullptr, &ConfigDocument::Files::actionLists},
        {"userconfig/IrProto.bin", &ConfigDocument::Files::irProto, nullptr},
        {"userconfig/SsIr.bin", &ConfigDocument::Files::ssIr, nullptr},
    }};
    return files;
}

Bytes bytesOf(std::string_view text) {
    return {text.begin(), text.end()};
}

/**
 * @brief Права, с которыми пульт устанавливает файл.
 *
 * Скрипты должны быть исполняемыми; настройки system_*.dat — доступными на
 * запись всем: служба настроек пульта работает не от root, а файлы ставятся
 * от root, и при 0600 (так они выглядят в снятой копии) пульт перестаёт
 * сохранять настройки. Таблица проверена по настоящим конфигурациям.
 */
std::uint32_t remoteMode(const std::string& path) {
    if (path == "META-INF/") {
        return 040777;
    }
    if (path.ends_with('/')) {
        return 040775;
    }
    if (path == ".preinstall" || path == ".postinstall") {
        return 0100775;
    }
    if (path == ".version" || path == "META-INF/MANIFEST.MF") {
        return 0100666;
    }
    if (path.starts_with(kUserFolder)) {
        return 0100775;
    }
    if (path.starts_with("platformconfig/")) {
        const std::string name = path.substr(std::string_view("platformconfig/").size());
        if (name.starts_with("system_")) {
            return 0100666;
        }
        if (name == "tiltcfg.dat" || name == "sleepcfg.dat" || name == "pmiccfg.dat" || name == "batt_lvls.dat") {
            return 0100644;
        }
    }
    return 0100664;
}

/**
 * @brief Каркас архива для конфигурации, у которой нет своего .EZHex (новой или из папки).
 *
 * Те же служебные файлы, что в конфигурациях с пульта. Скрипты — с концами строк LF
 * (с CRLF пульт не найдёт интерпретатор и отвергнет конфигурацию), .version и
 * манифест — с CRLF. Настроек пульта (platformconfig) нет: пульт оставит свои.
 */
formats::ZipArchive skeleton(std::int64_t now) {
    static constexpr std::string_view kPreinstall =
        "#!/bin/sh\n\nif test -z \"$CONFIGHOME\"; then\n    CONFIGHOME=/usr/data\n    export CONFIGHOME\nfi\n\n"
        "rm -rf $CONFIGHOME/userconfig\n\n";
    static constexpr std::string_view kPostinstall =
        "#!/bin/sh\n\nif test -z \"$CONFIGHOME\"; then\n\tCONFIGHOME=/usr/data\n\texport CONFIGHOME\nfi\n\n"
        "chmod -R 775 $CONFIGHOME/userconfig\n\n";
    formats::ZipArchive archive;
    auto add = [&](const std::string& name, std::string_view content) {
        archive.entries.push_back(formats::makeEntry(name, remoteMode(name), bytesOf(content), now));
    };
    add("userconfig/", {});
    add(".version", "0x04 0x00 0x00 0x00\r\n");
    add(".preinstall", kPreinstall);
    add(".postinstall", kPostinstall);
    add("META-INF/", {});
    add("META-INF/MANIFEST.MF", "\r\n");
    return archive;
}

/// Ставит новую запись после последней записи папки @p folder («userconfig/»); нет таких — в конец.
void insertIntoFolder(formats::ZipArchive& archive, std::string_view folder, formats::ZipEntry entry) {
    auto position = archive.entries.end();
    for (auto it = archive.entries.begin(); it != archive.entries.end(); ++it) {
        if (it->name.starts_with(folder)) {
            position = std::next(it);
        }
    }
    archive.entries.insert(position, std::move(entry));
}

/// Добавляет запись самой папки @p folder в конец архива, если её нет.
void ensureFolder(formats::ZipArchive& archive, std::string_view folder, std::int64_t now) {
    const std::string name(folder);
    if (archive.find(name) == nullptr) {
        archive.entries.push_back(formats::makeEntry(name, remoteMode(name), {}, now));
    }
}

} // namespace

Result<ConfigDocument> ConfigDocument::fromEzHex(std::span<const std::uint8_t> ezhex, std::string name) {
    auto container = formats::parseEzHex(ezhex);
    if (!container) {
        return std::unexpected(std::move(container.error()));
    }
    auto archive = formats::readZip(container->payload);
    if (!archive) {
        return std::unexpected(std::move(archive.error()));
    }

    Files files;
    for (const PackedFile& file : packedFiles()) {
        const formats::ZipEntry* entry = archive->find(std::string(file.path));
        if (entry == nullptr) {
            continue;
        }
        auto content = formats::entryContent(*entry);
        if (!content) {
            return std::unexpected(std::move(content.error()));
        }
        if (file.text != nullptr) {
            files.*file.text = std::string(content->begin(), content->end());
        } else {
            files.*file.binary = std::move(*content);
        }
    }
    if (files.userConfiguration.empty()) {
        return makeError(ErrorCode::Format, "the .EZHex has no userconfig/UserConfiguration.xml");
    }

    auto document = fromFiles(files, std::move(name));
    if (!document) {
        return document;
    }
    document->impl_->package = std::make_shared<const detail::Package>(detail::Package{
        .header = container->header, .identity = container->identity, .archive = std::move(*archive)});
    return document;
}

Result<std::vector<std::uint8_t>> ConfigDocument::toEzHex(bool forRemote) const {
    const auto* target = remoteProfile(targetRemote());
    if (forRemote && (!target || !target->writable || identity().architecture != 15 || identity().skin != 61)) {
        return makeError(ErrorCode::Unsupported, "writing configurations for target remote %1 is not implemented; this is an experimental project profile", {targetRemote()});
    }
    // На пульт — без помеченного «не отправлять».
    if (forRemote && hasNotSent()) {
        return detail::remoteEzHex(*this);
    }
    if (forRemote) {
        for (const auto& device : model().devices) {
            for (const auto& command : device.commands) {
                const auto details = commandDetails(device.id, command.name);
                if (!details) {
                    return wrapError(details.error(), "cannot write command %1 of device %2",
                                     {command.name, device.displayName()});
                }
                if (details->signal.durations.empty()) {
                    return makeError(ErrorCode::Format, "the command produces an empty IR signal");
                }
            }
        }
    }
    auto files = detail::currentFiles(*impl_);
    if (!files) {
        return std::unexpected(std::move(files.error()));
    }
    const std::int64_t now = std::time(nullptr);
    formats::ZipArchive archive = impl_->package ? impl_->package->archive : skeleton(now);

    // Файлы конфигурации: изменившиеся пересжимаются, остальные остаются байт в байт.
    for (const PackedFile& file : packedFiles()) {
        const Bytes content = file.text != nullptr ? bytesOf((*files).*file.text) : (*files).*file.binary;
        const std::string path(file.path);
        if (content.empty() && file.text == nullptr) {
            // Таблицы нет — нет и в архиве: прежняя (например, после replaceContent()) не подходит к XML.
            std::erase_if(archive.entries, [&](const formats::ZipEntry& entry) { return entry.name == path; });
            continue;
        }
        if (formats::ZipEntry* entry = archive.find(path)) {
            if (auto replaced = formats::setContent(*entry, content); !replaced) {
                return std::unexpected(std::move(replaced.error()));
            }
        } else {
            insertIntoFolder(archive, kUserFolder, formats::makeEntry(path, remoteMode(path), content, now));
        }
    }
    if (forRemote) {
        for (formats::ZipEntry& entry : archive.entries) {
            entry.setUnixMode(remoteMode(entry.name));
        }
    }

    formats::EzHex container;
    container.header = impl_->package ? impl_->package->header : std::string();
    container.identity = identity();
    container.payload = formats::writeZip(archive);
    return formats::writeEzHex(container);
}

Result<void> ConfigDocument::saveAsEzHex(const std::filesystem::path& file) {
    auto bytes = toEzHex(false);
    if (!bytes) {
        return std::unexpected(std::move(bytes.error()));
    }
    std::error_code error;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }
    if (auto written = detail::writeFileAtomic(
            file, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        !written) {
        return written;
    }
    // Записанное становится основой: следующее сохранение сравнивает с ним.
    auto saved = fromEzHex(*bytes, impl_->name);
    if (saved && saved->impl_->package) {
        impl_->package = std::move(saved->impl_->package);
        impl_->actionLists = std::move(saved->impl_->actionLists);
    }
    impl_->ezhexFile = file;
    impl_->folder.clear();
    const std::u8string stem = file.stem().u8string();
    impl_->name.assign(stem.begin(), stem.end());
    return {};
}

const std::filesystem::path& ConfigDocument::ezhexFile() const {
    return impl_->ezhexFile;
}

const std::filesystem::path& ConfigDocument::location() const {
    return impl_->ezhexFile.empty() ? impl_->folder : impl_->ezhexFile;
}

formats::RemoteIdentity ConfigDocument::identity() const {
    return impl_->package ? impl_->package->identity : formats::RemoteIdentity{};
}

// --- RF-бластеры ---------------------------------------------------------------------

namespace {

/// Файлы настроек RF-бластеров: карта «устройство → излучатель», её копии и контрольные суммы.
bool isRfFile(const std::string& name) {
    return name.starts_with("platformconfig/XmlUserRfSetting");
}

} // namespace

Result<std::vector<std::uint8_t>> ConfigDocument::toEzHexKeepingRemoteRf(const ConfigDocument& remote) const {
    auto own = toEzHex(true);
    if (!own || !remote.impl_->package) {
        return own;
    }
    auto container = formats::parseEzHex(*own);
    if (!container) {
        return std::unexpected(std::move(container.error()));
    }
    auto archive = formats::readZip(container->payload);
    if (!archive) {
        return std::unexpected(std::move(archive.error()));
    }
    // Настройки RF — как на пульте сейчас: их меняют на самом пульте (привязка бластера,
    // назначение устройств), и старая копия вернула бы прежнее состояние.
    std::erase_if(archive->entries, [](const formats::ZipEntry& entry) { return isRfFile(entry.name); });
    const std::int64_t now = std::time(nullptr);
    for (const formats::ZipEntry& entry : remote.impl_->package->archive.entries) {
        if (!isRfFile(entry.name)) {
            continue;
        }
        ensureFolder(*archive, kPlatformFolder, now);
        formats::ZipEntry copy = entry;
        copy.setUnixMode(remoteMode(copy.name));
        insertIntoFolder(*archive, kPlatformFolder, std::move(copy));
    }
    container->payload = formats::writeZip(*archive);
    return formats::writeEzHex(*container);
}

std::vector<std::string> ConfigDocument::devicesOutsideRf() const {
    std::vector<std::string> outside;
    if (!impl_->package) {
        return outside;
    }
    const formats::ZipEntry* entry = impl_->package->archive.find("platformconfig/XmlUserRfSetting.xml");
    if (entry == nullptr) {
        return outside;
    }
    auto content = formats::entryContent(*entry);
    pugi::xml_document rf;
    if (!content || !rf.load_buffer(content->data(), content->size())) {
        return outside;
    }
    const pugi::xml_node info = rf.child("RemoteInfo");
    // Бластер — любой излучатель, кроме переднего ИК пульта (Guid «0»).
    bool blaster = false;
    for (pugi::xml_node controller : info.child("Controllers").children("Controller")) {
        if (std::string_view(controller.child("Guid").text().get()) != "0") {
            blaster = true;
        }
    }
    if (!blaster) {
        return outside;
    }
    std::set<std::string> mapped;
    for (pugi::xml_node controller : info.child("Controller2UserDeviceMap").children("Controller")) {
        for (pugi::xml_node device : controller.children("Device")) {
            mapped.insert(device.child("UserDeviceId").text().get());
        }
    }
    for (const auto& device : impl_->model.devices) {
        if (!device.notSent && !mapped.contains(device.id)) {
            outside.push_back(device.id);
        }
    }
    return outside;
}

// --- Настройки пульта -------------------------------------------------------------

std::optional<std::string> ConfigDocument::remotePreference(std::string_view key) const {
    const RemotePreference* preference = findRemotePreference(key);
    if (preference == nullptr || !impl_->package) {
        return std::nullopt;
    }
    const formats::ZipEntry* entry = impl_->package->archive.find(preference->archivePath());
    if (entry == nullptr) {
        return std::nullopt;
    }
    auto content = formats::entryContent(*entry);
    if (!content) {
        return std::nullopt;
    }
    std::string value(content->begin(), content->end());
    // Пульт пишет значение одной строкой; концы строк и пробелы по краям не значат ничего.
    const auto first = value.find_first_not_of(" \t\r\n");
    const auto last = value.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string() : value.substr(first, last - first + 1);
}

Result<void> ConfigDocument::setRemotePreference(std::string_view key, const std::string& value) {
    const RemotePreference* preference = findRemotePreference(key);
    if (preference == nullptr) {
        return makeError(ErrorCode::NotFound, "unknown remote setting %1", {std::string(key)});
    }
    if (!value.empty()) {
        if (auto checked = checkPreferenceValue(*preference, value); !checked) {
            return checked;
        }
    }
    if (remotePreference(key).value_or(std::string()) == value) {
        return {};
    }

    // Сначала — копия архива; документ меняется, только если всё получилось.
    const std::int64_t now = std::time(nullptr);
    detail::Package package = impl_->package ? *impl_->package
                                             : detail::Package{.header = {}, .identity = {}, .archive = skeleton(now)};
    formats::ZipArchive& archive = package.archive;
    const std::string path = preference->archivePath();
    if (value.empty()) {
        std::erase_if(archive.entries, [&](const formats::ZipEntry& entry) { return entry.name == path; });
    } else if (formats::ZipEntry* entry = archive.find(path)) {
        if (auto replaced = formats::setContent(*entry, bytesOf(value)); !replaced) {
            return replaced;
        }
    } else {
        // Новый файл — в конец папки platformconfig/ (если её нет — создаётся в конце архива).
        ensureFolder(archive, kPlatformFolder, now);
        insertIntoFolder(archive, kPlatformFolder,
                         formats::makeEntry(path, remoteMode(path), bytesOf(value), now));
    }

    // Формат часов хранится ещё и в XML — оба места меняются вместе.
    if (!preference->xmlProperty.empty() && !value.empty()) {
        const std::string property(preference->xmlProperty);
        auto changed = detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
            pugi::xml_node user = root.child("User");
            if (!user) {
                return {};
            }
            pugi::xml_node properties = user.child("Properties");
            if (!properties) {
                properties = user.prepend_child("Properties");
            }
            detail::setProperty(properties, property.c_str(), value);
            return {};
        });
        if (!changed) {
            return changed;
        }
    }
    impl_->package = std::make_shared<const detail::Package>(std::move(package));
    return {};
}

} // namespace harmony
