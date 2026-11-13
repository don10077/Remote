/**
 * @file configDocument.cpp
 * @brief Открытие, создание, сохранение конфигурации и снимки для отмены.
 *
 * Правки — в configDocumentEdit.cpp, добавление устройств и активностей —
 * в configDocumentAdd.cpp.
 */

#include "configDocumentImpl.h"

#include "actionLists.h"
#include "harmony/core/formats/userConfiguration.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iterator>
#include <system_error>
#include <thread>

namespace harmony {

namespace detail {

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}

namespace {

/// Записывает правленое дерево в документ; при ошибке разбора документ не меняется.
Result<void> commit(ConfigDocument::Impl& impl, const pugi::xml_document& document) {
    std::string updated = saveDom(document);
    auto parsed = formats::parseUserConfiguration(updated);
    if (!parsed) {
        return std::unexpected(std::move(parsed.error()));
    }
    impl.xml = std::move(updated);
    impl.model = std::move(*parsed);
    return {};
}

} // namespace

Result<void> mutate(ConfigDocument::Impl& impl, const Change& change) {
    pugi::xml_document document;
    if (auto loaded = loadDom(document, impl.xml); !loaded) {
        return loaded;
    }
    // Правим копию: при ошибке таблицы останутся прежними, а снимки отмены — общими с ними.
    IrData ir = *impl.ir;
    if (auto changed = change(document.child("Root"), ir); !changed) {
        return changed;
    }
    if (auto committed = commit(impl, document); !committed) {
        return committed;
    }
    impl.ir = std::make_shared<const IrData>(std::move(ir));
    return {};
}

Result<void> mutate(ConfigDocument::Impl& impl,
                    const std::function<Result<void>(pugi::xml_node root)>& change) {
    // Таблицы не меняются — и не копируются.
    pugi::xml_document document;
    if (auto loaded = loadDom(document, impl.xml); !loaded) {
        return loaded;
    }
    if (auto changed = change(document.child("Root")); !changed) {
        return changed;
    }
    return commit(impl, document);
}

} // namespace detail

namespace {

using detail::pathToUtf8;

/// Имя папки в UTF-8.
std::string folderName(const std::filesystem::path& folder) {
    return pathToUtf8(folder.filename().empty() ? folder.parent_path().filename() : folder.filename());
}

/// Текущее местное время в формате свойства LastUpdated: «20240802 154354».
std::string nowStamp() {
    // Местное время через C API: часовые пояса std::chrono есть не во всех тулчейнах (MinGW).
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d %H%M%S", &local);
    return buffer;
}

/// Таблица протоколов документа в виде файла (для хеша и записи).
Result<formats::Bytes> irProtoBytes(const detail::TableFile& file) {
    if (!file.table) {
        return file.raw;
    }
    return formats::buildIrProto(*file.table);
}

/// Текущее местное время для имён файлов: «20240802-154354».
std::string fileStamp() {
    std::string stamp = nowStamp();
    std::replace(stamp.begin(), stamp.end(), ' ', '-');
    return stamp;
}

/**
 * @brief Минимальная конфигурация: свойства, пользователь, пульт, активность «выключить всё».
 *
 * Структура повторяет файлы с пульта Harmony 900 (тип пульта — «Protocol Code»).
 * Хеш — от пустой таблицы протоколов, которая сохраняется вместе с конфигурацией.
 */
std::string emptyConfigXml(const std::string& powerOffLabel, const std::string& hash) {
    using detail::appendProperty;
    using detail::appendText;
    pugi::xml_document document;
    pugi::xml_node root = document.append_child("Root");

    pugi::xml_node properties = root.append_child("Properties");
    appendProperty(properties, "version", "1.0");
    appendProperty(properties, "ProtocolCacheHash", hash);
    appendProperty(properties, "LastUpdated", nowStamp());

    pugi::xml_node user = root.append_child("User");
    appendText(user, "Id", "0");
    pugi::xml_node userProperties = user.append_child("Properties");
    appendProperty(userProperties, "TrainingWheels", "False");
    appendProperty(userProperties, "LocaleId", "rus");
    appendProperty(userProperties, "TimeDisplayFormat", "Military");
    pugi::xml_node userPresentation = user.append_child("Presentation");
    // Текстовые поля пульт пишет парой тегов даже пустыми: <FirstName></FirstName>.
    appendText(userPresentation, "FirstName", "");
    appendText(userPresentation, "LastName", "");

    pugi::xml_node controller = root.append_child("Controller");
    appendText(controller, "Id", "0");
    appendText(controller, "Type", "Protocol Code");
    appendText(controller, "Manufacturer", "Logitech");
    appendText(controller, "Model", "Harmony 1000-ish");
    controller.append_child("Properties");
    appendText(controller.append_child("Presentation"), "Label", "Harmony 1000-ish");

    pugi::xml_node powerOff = root.append_child("Activity");
    appendText(powerOff, "Id", "-1");
    appendText(powerOff, "Type", "PowerOff");
    appendText(powerOff.append_child("Presentation"), "Label", powerOffLabel);
    powerOff.append_child("EnterActions");
    powerOff.append_child("Power");

    appendText(root.append_child("Protocols"), "Hash", hash);
    return detail::saveDom(document);
}

/// Читает файл целиком; nullopt — файла нет или не читается.
std::optional<std::string> readFile(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) {
        return std::nullopt;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

/// Функция разбора двоичной таблицы.
using TableParser = Result<formats::IrTable> (*)(std::span<const std::uint8_t>);

/// Разбирает двоичную таблицу; незнакомый формат не мешает открыть конфигурацию.
detail::TableFile makeTable(const formats::Bytes& bytes, TableParser parse) {
    detail::TableFile table;
    table.present = true;
    if (auto parsed = parse(bytes)) {
        table.table = std::move(*parsed);
    } else {
        table.raw = bytes;
        table.error = parsed.error();
    }
    return table;
}

/// Читает двоичную таблицу из файла; нет файла — таблицы нет.
detail::TableFile readTable(const std::filesystem::path& file, TableParser parse) {
    const auto content = readFile(file);
    if (!content) {
        return {};
    }
    return makeTable(formats::Bytes(content->begin(), content->end()), parse);
}

} // namespace

namespace {

/**
 * @brief Записывает содержимое в новый файл и сбрасывает его на диск.
 *
 * Без сброса после сбоя питания rename() может оказаться на диске раньше данных,
 * и на месте файла останется пустой. Поток C++ сбросить на диск не умеет, поэтому —
 * через API системы.
 */
bool writeDurably(const std::filesystem::path& file, std::string_view content) {
#ifdef _WIN32
    const HANDLE handle = ::CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = true;
    for (std::size_t done = 0; ok && done < content.size();) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(content.size() - done, 1U << 30));
        DWORD written = 0;
        ok = ::WriteFile(handle, content.data() + done, chunk, &written, nullptr) != 0 && written > 0;
        done += written;
    }
    ok = ok && ::FlushFileBuffers(handle) != 0;
    return ::CloseHandle(handle) != 0 && ok;
#else
    const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
        return false;
    }
    bool ok = true;
    for (std::size_t done = 0; ok && done < content.size();) {
        const ::ssize_t written = ::write(fd, content.data() + done, content.size() - done);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        ok = written > 0;
        done += ok ? static_cast<std::size_t>(written) : 0;
    }
    ok = ok && ::fsync(fd) == 0;
    return ::close(fd) == 0 && ok;
#endif
}

/**
 * @brief rename() с повтором: на Windows файл недолго держат антивирус, индексатор
 *        и Проводник (ERROR_SHARING_VIOLATION, ERROR_ACCESS_DENIED).
 */
void renameRetrying(const std::filesystem::path& from, const std::filesystem::path& to,
                    std::error_code& error) {
    constexpr int kAttempts = 5;
    for (int attempt = 0;; ++attempt) {
        std::filesystem::rename(from, to, error);
#ifdef _WIN32
        if (error && attempt + 1 < kAttempts) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
#else
        (void)kAttempts;
        (void)attempt;
#endif
        return;
    }
}

} // namespace

/// Записывает файл через временный, чтобы при сбое не остался испорченный файл.
Result<void> detail::writeFileAtomic(const std::filesystem::path& file, std::string_view content) {
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    std::error_code error;
    if (!writeDurably(temporary, content)) {
        std::filesystem::remove(temporary, error);
        return makeError(ErrorCode::Io, "error writing the file: %1", {pathToUtf8(temporary)});
    }
    renameRetrying(temporary, file, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return makeError(ErrorCode::Io, "could not replace the file: %1", {pathToUtf8(file)});
    }
    return {};
}

namespace {

using detail::writeFileAtomic;

} // namespace

// --- Снимок -------------------------------------------------------------------

ConfigDocument::Snapshot::Snapshot() = default;
ConfigDocument::Snapshot::~Snapshot() = default;
ConfigDocument::Snapshot::Snapshot(const Snapshot&) = default;
ConfigDocument::Snapshot& ConfigDocument::Snapshot::operator=(const Snapshot&) = default;

ConfigDocument::Snapshot ConfigDocument::snapshot() const {
    Snapshot snapshot;
    snapshot.data_ = std::make_shared<const Snapshot::Data>(
        Snapshot::Data{impl_->xml, impl_->ir, impl_->actionLists, impl_->package});
    return snapshot;
}

Result<void> ConfigDocument::restore(const Snapshot& snapshot) {
    if (!snapshot.data_) {
        return makeError(ErrorCode::NotFound, "the undo step has no saved state");
    }
    // Снимок получен из этого же документа и должен разбираться; если нет — документ не меняется.
    auto parsed = formats::parseUserConfiguration(snapshot.data_->xml);
    if (!parsed) {
        return wrapError(parsed.error(), "the saved state of the configuration could not be restored");
    }
    impl_->xml = snapshot.data_->xml;
    impl_->model = std::move(*parsed);
    impl_->ir = snapshot.data_->ir;
    impl_->actionLists = snapshot.data_->actionLists;
    impl_->package = snapshot.data_->package;
    return {};
}

Result<void> ConfigDocument::replaceContent(const ConfigDocument& source) {
    if (&source == this) {
        return {};
    }
    // Содержимое — целиком из источника; место на диске и имя остаются прежними.
    impl_->xml = source.impl_->xml;
    impl_->model = source.impl_->model;
    impl_->ir = source.impl_->ir;
    impl_->actionLists = source.impl_->actionLists;
    if (source.impl_->package) {
        impl_->package = source.impl_->package; // настройки пульта и скрипты — с пульта
    }
    return {};
}

// --- Открытие и создание ------------------------------------------------------

ConfigDocument::ConfigDocument(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ConfigDocument::ConfigDocument(ConfigDocument&&) noexcept = default;
ConfigDocument& ConfigDocument::operator=(ConfigDocument&&) noexcept = default;
ConfigDocument::~ConfigDocument() = default;

Result<ConfigDocument> ConfigDocument::fromXml(std::string_view xml, std::string name) {
    auto config = formats::parseUserConfiguration(xml);
    if (!config) {
        return std::unexpected(std::move(config.error()));
    }
    auto impl = std::make_unique<Impl>();
    impl->xml = std::string(xml);
    impl->model = std::move(*config);
    impl->name = std::move(name);
    return ConfigDocument(std::move(impl));
}

namespace detail {

Result<ConfigDocument::Files> currentFiles(const ConfigDocument::Impl& impl) {
    ConfigDocument::Files files;
    files.userConfiguration = impl.xml;
    const IrData& ir = *impl.ir;
    if (ir.irProto.present) {
        auto irProto = irProtoBytes(ir.irProto);
        if (!irProto) {
            return std::unexpected(std::move(irProto.error()));
        }
        files.irProto = std::move(*irProto);
    }
    if (ir.ssIr.present) {
        files.ssIr = ir.ssIr.table ? formats::writeSsIr(*ir.ssIr.table) : ir.ssIr.raw;
    }
    // Действия кнопок выводятся из конфигурации; хеш протоколов — от текущей таблицы.
    const std::string hash = files.irProto.empty() ? std::string() : formats::protocolHash(files.irProto);
    auto actionLists = syncActionLists(impl.actionLists, impl.model, hash, nowStamp());
    if (!actionLists) {
        return std::unexpected(std::move(actionLists.error()));
    }
    files.actionLists = std::move(*actionLists);
    return files;
}

} // namespace detail

Result<ConfigDocument> ConfigDocument::fromFiles(const Files& files, std::string name) {
    auto document = fromXml(files.userConfiguration, std::move(name));
    if (!document) {
        return document;
    }
    Impl& impl = *document->impl_;
    impl.actionLists = files.actionLists;
    detail::IrData ir;
    if (!files.irProto.empty()) {
        ir.irProto = makeTable(files.irProto, &formats::parseIrProto);
    }
    if (!files.ssIr.empty()) {
        ir.ssIr = makeTable(files.ssIr, &formats::parseSsIr);
    }
    impl.ir = std::make_shared<const detail::IrData>(std::move(ir));
    return document;
}

Result<ConfigDocument> ConfigDocument::open(const std::filesystem::path& path) {
    std::error_code error;
    const bool isFolder = std::filesystem::is_directory(path, error);
    const std::filesystem::path folder = isFolder ? path : path.parent_path();
    const std::filesystem::path file =
        isFolder ? path / formats::kUserConfigurationFileName : path;

    // Только «.harmony-save»: «.harmony-save.done-…» остаётся от завершённого сохранения.
    if ((isFolder || file.extension() == ".xml") && std::filesystem::exists(folder / ".harmony-save", error)) {
        return makeError(ErrorCode::Io, "an interrupted save needs recovery from %1 before this configuration can be opened",
                         {pathToUtf8(folder / ".harmony-save")});
    }

    if (!std::filesystem::exists(file, error)) {
        return makeError(ErrorCode::NotFound, "file not found: %1", {pathToUtf8(file)});
    }
    const auto content = readFile(file);
    if (!content) {
        return makeError(ErrorCode::Io, "could not open the file: %1", {pathToUtf8(file)});
    }

    // Резервная копия с пульта: заголовок <INFORMATION>, затем ZIP.
    if (!isFolder && content->starts_with("<?xml") && content->find("<INFORMATION>") != std::string::npos &&
        content->find("<INFORMATION>") < 200) {
        const std::u8string stem = path.stem().u8string();
        auto document = fromEzHex(std::span(reinterpret_cast<const std::uint8_t*>(content->data()), content->size()),
                                  std::string(stem.begin(), stem.end()));
        if (!document) {
            return wrapError(document.error(), "%1", {pathToUtf8(file)});
        }
        document->impl_->ezhexFile = path;
        return document;
    }

    auto document = fromXml(*content, folderName(folder));
    if (!document) {
        return wrapError(document.error(), "%1", {pathToUtf8(file)});
    }
    Impl& impl = *document->impl_;
    impl.folder = folder;
    impl.actionLists = readFile(folder / detail::kActionListsFileName).value_or(std::string());
    detail::IrData ir;
    ir.irProto = readTable(folder / formats::kIrProtoFileName, &formats::parseIrProto);
    ir.ssIr = readTable(folder / formats::kSsIrFileName, &formats::parseSsIr);
    impl.ir = std::make_shared<const detail::IrData>(std::move(ir));
    return document;
}

ConfigDocument ConfigDocument::createNew(std::string name, const std::string& powerOffLabel) {
    // Пустые таблицы ИК-данных: в них лягут протоколы перенесённых устройств.
    detail::IrData ir;
    ir.irProto = {.present = true, .table = formats::IrTable{}, .raw = {}, .error = {}};
    ir.ssIr = {.present = true, .table = formats::IrTable{}, .raw = {}, .error = {}};
    const std::string hash = formats::protocolHash(formats::writeIrProto(*ir.irProto.table));

    // Шаблон собран этим же модулем, поэтому всегда разбирается.
    auto document = fromXml(emptyConfigXml(powerOffLabel, hash), std::move(name));
    document->impl_->ir = std::make_shared<const detail::IrData>(std::move(ir));
    return std::move(*document);
}

// --- Доступ -------------------------------------------------------------------

const model::RemoteConfig& ConfigDocument::model() const {
    return impl_->model;
}

const std::string& ConfigDocument::name() const {
    return impl_->name;
}

const std::filesystem::path& ConfigDocument::folder() const {
    return impl_->folder;
}

std::string ConfigDocument::toXml() const {
    return impl_->xml;
}

// --- Сохранение ---------------------------------------------------------------

namespace {

/// Папка, в которой готовится сохранение; если она есть при открытии — сохранение прервалось.
constexpr std::string_view kStagingName = ".harmony-save";

/**
 * @brief Убирает папку подготовки после успешного сохранения.
 *
 * Папку может недолго держать антивирус или Проводник, поэтому удаление повторяется.
 * Не вышло — папка переименовывается в «.harmony-save.done-<время>»: такое имя
 * открытие не считает прерванным сохранением, а следующее сохранение удалит её.
 */
Result<void> removeStaging(const std::filesystem::path& folder, const std::filesystem::path& staging) {
    std::error_code error;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::filesystem::remove_all(staging, error);
        if (!error) {
            return {};
        }
    }
    const std::string base = std::string(kStagingName) + ".done-" + fileStamp();
    std::filesystem::path done = folder / base;
    for (int n = 2; std::filesystem::exists(done, error); ++n) {
        done = folder / (base + "-" + std::to_string(n));
    }
    renameRetrying(staging, done, error);
    if (error) {
        return makeError(ErrorCode::Io,
                         "the configuration was saved, but the folder %1 could not be removed; delete it manually",
                         {pathToUtf8(staging)});
    }
    return {};
}

/// Удаляет папки, оставшиеся от прежних сохранений (см. removeStaging()); ошибки не важны.
void removeOldStaging(const std::filesystem::path& folder) {
    const std::string prefix = std::string(kStagingName) + ".done-";
    std::vector<std::filesystem::path> old;
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder, error);
         !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        if (pathToUtf8(it->path().filename()).starts_with(prefix)) {
            old.push_back(it->path());
        }
    }
    for (const auto& path : old) {
        std::filesystem::remove_all(path, error);
    }
}

/**
 * @brief Записывает все файлы конфигурации в папку.
 *
 * Новые файлы сначала пишутся в папку подготовки, затем по одному ставятся на место;
 * прежние уходят туда же как «<имя>.previous» и при сбое возвращаются. Таблицы, которой
 * в документе нет, в папке тоже не остаётся: её прежний файл уходит в .previous.
 */
Result<void> writeAll(ConfigDocument::Impl& impl, const std::filesystem::path& folder) {
    auto files = detail::currentFiles(impl);
    if (!files) {
        return std::unexpected(std::move(files.error()));
    }
    removeOldStaging(folder);
    const auto staging = folder / kStagingName;
    std::error_code error;
    if (!std::filesystem::create_directory(staging, error)) {
        return makeError(ErrorCode::Io, "could not create save staging folder: %1; keep its contents for recovery",
                         {pathToUtf8(staging)});
    }
    struct Pending {
        std::string name;
        std::string content;
        bool remove = false; ///< Файла не должно быть: прежний только убирается в .previous
        bool backedUp = false;
        bool installed = false;
    };
    std::vector<Pending> pending = {
        {.name = std::string(formats::kUserConfigurationFileName), .content = files->userConfiguration},
        {.name = std::string(detail::kActionListsFileName), .content = files->actionLists}};
    for (const auto& [name, bytes] : std::array{
             std::pair{formats::kIrProtoFileName, &files->irProto},
             std::pair{formats::kSsIrFileName, &files->ssIr}}) {
        pending.push_back({.name = std::string(name),
                           .content = std::string(bytes->begin(), bytes->end()),
                           .remove = bytes->empty()});
    }
    // Prepare every new file before replacing any member of the configuration.
    for (const auto& item : pending) {
        if (item.remove) {
            continue;
        }
        auto written = writeFileAtomic(staging / item.name, item.content);
        if (!written) {
            std::filesystem::remove_all(staging, error);
            return written;
        }
    }
    for (auto& item : pending) {
        const auto target = folder / item.name;
        if (std::filesystem::exists(target, error)) {
            if (!std::filesystem::is_regular_file(target, error)) {
                error = std::make_error_code(std::errc::invalid_argument);
                break;
            }
            renameRetrying(target, staging / (item.name + ".previous"), error);
            if (error) break;
            item.backedUp = true;
        }
        if (error) break;
        if (item.remove) continue;
        renameRetrying(staging / item.name, target, error);
        if (error) break;
        item.installed = true;
    }
    if (error) {
        bool restored = true;
        for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
            std::error_code rollback;
            if (it->installed) {
                std::filesystem::remove(folder / it->name, rollback);
                if (rollback) { restored = false; continue; }
            }
            if (it->backedUp) {
                renameRetrying(staging / (it->name + ".previous"), folder / it->name, rollback);
                if (rollback) restored = false;
            }
        }
        if (restored) std::filesystem::remove_all(staging, error);
        return makeError(ErrorCode::Io, restored ? "saving failed; the previous configuration was restored: %1"
                                               : "saving failed; recovery files are kept in %1",
                         {pathToUtf8(restored ? folder : staging)});
    }
    impl.actionLists = std::move(files->actionLists);
    return removeStaging(folder, staging);
}

} // namespace

Result<void> ConfigDocument::save() {
    if (!impl_->ezhexFile.empty()) {
        return saveAsEzHex(impl_->ezhexFile);
    }
    if (impl_->folder.empty()) {
        return makeError(ErrorCode::Unsupported, "the configuration has no location on disk yet — use “Save as”");
    }
    return writeAll(*impl_, impl_->folder);
}

Result<void> ConfigDocument::saveAs(const std::filesystem::path& folder) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error) {
        return makeError(ErrorCode::Io, "could not create the folder: %1", {pathToUtf8(folder)});
    }
    if (auto written = writeAll(*impl_, folder); !written) {
        return written;
    }
    impl_->folder = folder;
    impl_->ezhexFile.clear();
    impl_->name = folderName(folder);
    return {};
}

} // namespace harmony
