#include "harmony/transport/concordTransfer.h"
#include "harmony/transport/harmony900Link.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/core/formats/zipArchive.h"

#include <libconcord.h>
#include <pugixml.hpp>
#include <zip.h>
#include <hidapi.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>

// Small, documented bridge added to the vendored snapshot; no filename/locale conversion.
void harmony_concord_set_config(const uint8_t*, uint32_t, const uint8_t*, uint32_t);
int harmony_concord_validate_config(const uint8_t*, uint32_t);
int is_mh_remote();
int is_z_remote();
uint32_t _mh_get_config_len(uint8_t*, uint32_t);

namespace harmony::transport {
namespace {
constexpr std::size_t maxFileSize = 64 * 1024 * 1024;
std::mutex sessionMutex;

/// Переводит ошибку библиотеки в ошибку приложения, сохраняя номер.
Result<void> checked(int code, std::string_view operation) {
    if (!code) return {};
    return makeError(ErrorCode::Remote, "%1: %2 (libconcord %3)",
                     {std::string(operation), lc_strerror(code), std::to_string(code)});
}

/// XOR16 контейнера MH; диапазон задаётся дескриптором, не размером всего архива.
std::uint16_t xor16(std::span<const std::uint8_t> bytes, std::uint16_t seed) {
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2)
        seed ^= static_cast<std::uint16_t>(bytes[i] | (bytes[i + 1] << 8));
    return seed;
}

/// Читает один ограниченный ZIP-элемент в память, без распаковки путей на диск.
Result<formats::Bytes> zipEntry(zip_t* archive, const char* name) {
    zip_stat_t stat{};
    if (zip_stat(archive, name, 0, &stat) != 0 || stat.size > maxFileSize)
        return makeError(ErrorCode::Format, "Missing or oversized configuration ZIP entry: %1", {name});
    std::unique_ptr<zip_file_t, decltype(&zip_fclose)> file(zip_fopen(archive, name, 0), zip_fclose);
    if (!file) return makeError(ErrorCode::Format, "Cannot open configuration ZIP entry: %1", {name});
    formats::Bytes data(static_cast<std::size_t>(stat.size));
    if (zip_fread(file.get(), data.data(), data.size()) != static_cast<zip_int64_t>(data.size()))
        return makeError(ErrorCode::Format, "Truncated configuration ZIP entry: %1", {name});
    // Read once more to force the CRC check in libzip.
    std::uint8_t extra{};
    if (zip_fread(file.get(), &extra, 1) != 0)
        return makeError(ErrorCode::Format, "Invalid configuration ZIP checksum: %1", {name});
    return data;
}

/// Создаёт штатный MH-контейнер с идентичностью, используемый Concordance/Afterglow.
Result<formats::Bytes> mhBackup(formats::Bytes bytes, int skin) {
    if (bytes.size() < 8) return makeError(ErrorCode::Format, "MH configuration is too short");
    const auto length = _mh_get_config_len(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    if (length < 8 || length > bytes.size()) return makeError(ErrorCode::Format, "Invalid MH configuration end marker");
    bytes.resize(length);
    const auto checksum = xor16(std::span(bytes).first(bytes.size() - 6), 0x4321);
    const std::string xml = "<DATA><FILES><FILE NAME=\"Result.EzHex\" SIZE=\"" + std::to_string(bytes.size()) +
        "\" PATH=\"/cfg/usercfg\" VERSION=\"1\" FW_VERSION=\"9.5\" OPERATIONTYPE=\"userconfiguration\">"
        "<CHECKSUM SEED=\"0x4321\" OFFSET=\"0\" LENGTH=\"" + std::to_string(bytes.size() - 6) +
        "\" EXPECTEDVALUE=\"" + std::to_string(checksum) + "\" TYPE=\"XOR\"/></FILE></FILES>"
        "<INTENDED><SKIN>" + std::to_string(skin) + "</SKIN></INTENDED>"
        "<ORDER><ORDER_ELEMENT NAME=\"Result.EzHex\" RESET=\"true\"/></ORDER></DATA>";
    formats::ZipArchive archive;
    const formats::Bytes xmlBytes(xml.begin(), xml.end());
    archive.entries.push_back(formats::makeEntry("Description.xml", 0100644, xmlBytes, 0));
    archive.entries.push_back(formats::makeEntry("Result.EzHex", 0100644, bytes, 0));
    return formats::writeZip(archive);
}

/// Записывает новую резервную копию; существующие файлы не перезаписываются.
Result<void> saveBackup(const std::filesystem::path& path, const formats::Bytes& bytes) {
    if (path.empty()) return makeError(ErrorCode::Io, "A backup filename is required");
    std::error_code ec;
    if (std::filesystem::exists(path, ec) || ec)
        return makeError(ErrorCode::Io, "Backup already exists or cannot be checked");
    std::ofstream out(path, std::ios::binary | std::ios::noreplace);
    if (!out) return makeError(ErrorCode::Io, "Cannot create the backup file");
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) return makeError(ErrorCode::Io, "Cannot finish the backup file; remote was not written");
    std::ifstream verify(path, std::ios::binary);
    const formats::Bytes saved{std::istreambuf_iterator<char>(verify), {}};
    if (saved != bytes) return makeError(ErrorCode::Io, "Backup verification failed; remote was not written");
    return {};
}
} // namespace

Result<TransferFile> inspectTransferFile(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > maxFileSize)
        return makeError(ErrorCode::Format, "Empty or oversized configuration file");
    TransferFile result;
    if (bytes.size() >= 4 && bytes[0] == 'P' && bytes[1] == 'K') {
        zip_error_t error;
        zip_error_init(&error);
        zip_source_t* source = zip_source_buffer_create(bytes.data(), bytes.size(), 0, &error);
        zip_t* raw = source ? zip_open_from_source(source, ZIP_RDONLY, &error) : nullptr;
        if (!raw) {
            if (source) zip_source_free(source);
            zip_error_fini(&error);
            return makeError(ErrorCode::Format, "Invalid MH ZIP configuration");
        }
        zip_error_fini(&error);
        std::unique_ptr<zip_t, decltype(&zip_discard)> archive(raw, zip_discard);
        const char* descriptor = zip_name_locate(raw, "Description.xml", 0) >= 0 ? "Description.xml" : "Data.xml";
        if (zip_get_num_entries(raw, 0) != 2)
            return makeError(ErrorCode::Unsupported, "Expected one MH configuration and one descriptor");
        auto xmlBytes = zipEntry(raw, descriptor);
        if (!xmlBytes) return std::unexpected(xmlBytes.error());
        result.xml.assign(xmlBytes->begin(), xmlBytes->end());
        pugi::xml_document doc;
        if (!doc.load_buffer(result.xml.data(), result.xml.size()))
            return makeError(ErrorCode::Format, "Invalid MH configuration descriptor");
        const auto root = doc.child("DATA");
        const auto entry = root.child("FILES").child("FILE");
        if (!entry || entry.next_sibling("FILE") || std::string_view(entry.attribute("PATH").value()) != "/cfg/usercfg" ||
            std::string_view(entry.attribute("OPERATIONTYPE").value()) != "userconfiguration")
            return makeError(ErrorCode::Unsupported, "The file is not a user configuration");
        auto payload = zipEntry(raw, entry.attribute("NAME").value());
        if (!payload) return std::unexpected(payload.error());
        result.payload = std::move(*payload);
        result.skin = root.child("INTENDED").child("SKIN").text().as_int(-1);
        result.mh = true;
        const auto sum = entry.child("CHECKSUM");
        const std::size_t offset = sum.attribute("OFFSET").as_ullong(maxFileSize + 1);
        const std::size_t length = sum.attribute("LENGTH").as_ullong(maxFileSize + 1);
        if (result.skin < 0 || entry.attribute("SIZE").as_ullong() != result.payload.size() ||
            std::string_view(sum.attribute("TYPE").value()) != "XOR" ||
            offset > result.payload.size() || length > result.payload.size() - offset ||
            xor16(std::span(result.payload).subspan(offset, length), static_cast<std::uint16_t>(sum.attribute("SEED").as_uint())) !=
                sum.attribute("EXPECTEDVALUE").as_uint(0x10000))
            return makeError(ErrorCode::Format, "MH configuration size, identity or checksum is invalid");
        return result;
    }
    auto container = formats::parseEzHex(bytes);
    if (!container) return std::unexpected(container.error());
    pugi::xml_document doc;
    if (!doc.load_buffer(container->header.data(), container->header.size()))
        return makeError(ErrorCode::Format, "Invalid EZHex header");
    const auto intended = doc.child("INFORMATION").child("INTENDEDVERSION");
    result.protocol = intended.child("PROTOCOL").text().as_int(-1);
    result.skin = intended.child("SKIN").text().as_int(-1);
    if (result.protocol < 0 || result.skin < 0)
        return makeError(ErrorCode::Format, "EZHex must explicitly identify the intended remote");
    result.payload = std::move(container->payload);
    result.xml = std::move(container->header);
    return result;
}

Result<void> validateTransfer(const TransferFile& file, const TransferInfo& info) {
    if (!info.writable || file.mh != info.mh || file.skin != info.remote.identity.skin ||
        (!file.mh && file.protocol != info.protocol))
        return makeError(ErrorCode::Unsupported, "Configuration does not match the connected remote");
    if (file.payload.empty() || file.payload.size() > maxFileSize ||
        (!info.network && info.capacity > 1 && file.payload.size() > info.capacity))
        return makeError(ErrorCode::Format, "Configuration exceeds the remote's configuration area");
    const int arch = info.remote.identity.architecture;
    if (!info.mh && (arch == 12 || arch == 14)) {
        const auto& b = file.payload;
        const std::uint32_t base = arch == 12 ? 0x040000u : 0x030000u;
        if (b.size() < 12 || !std::equal(b.begin(), b.begin() + 4, "GSPM"))
            return makeError(ErrorCode::Format, "Invalid GSPM signature");
        const auto end = static_cast<std::uint32_t>(b[4]) | (static_cast<std::uint32_t>(b[5]) << 8) |
                         (static_cast<std::uint32_t>(b[6]) << 16);
        if (end < base + 2 || end - base != b.size() - 4 ||
            !std::equal(b.end() - 4, b.end(), "PTYY"))
            return makeError(ErrorCode::Format, "Invalid GSPM end vector");
        const std::size_t close = b.size() - 6;
        if (close % 2 != 0 || xor16(std::span(b).first(close), 0x4321) != (b[close] | (b[close + 1] << 8)))
            return makeError(ErrorCode::Format, "Invalid GSPM XOR16 checksum");
        if (arch == 12 && (b.size() >= 0x390000 || b.size() < 0x67 ||
            !std::equal(b.begin() + 0x63, b.begin() + 0x67, "LWJL")))
            return makeError(ErrorCode::Format, "Harmony One configuration overlaps protected memory or has no LWJL signature");
    }
    return {};
}

struct ConcordTransfer::Impl {
    std::unique_lock<std::mutex> lock{sessionMutex, std::try_to_lock};
    std::unique_ptr<Harmony900Link> network;
    TransferInfo info;
    bool initialized = false;
    bool connected = false;
    /// Библиотека не поддерживает безопасную отмену посреди записи.
    static void callback(std::uint32_t stage, std::uint32_t, std::uint32_t current,
                         std::uint32_t total, std::uint32_t, void* context, const std::uint32_t*) {
        auto* progress = static_cast<const ProgressHandler*>(context);
        if (!progress || !*progress) return;
        const Stage mapped = stage == LC_CB_STAGE_READ_CONFIG ? Stage::Read :
            stage == LC_CB_STAGE_VERIFY_CONFIG ? Stage::Verify :
            stage == LC_CB_STAGE_RESET ? Stage::Restart :
            stage == LC_CB_STAGE_SET_TIME ? Stage::SetTime :
            stage == LC_CB_STAGE_GET_IDENTITY ? Stage::Connect : Stage::Write;
        (void)(*progress)(mapped, current, total);
    }
    ~Impl() {
        if (initialized) { delete_opfile_obj(); deinit_concord(); }
    }
};

ConcordTransfer::ConcordTransfer() : impl_(std::make_unique<Impl>()) {}
ConcordTransfer::~ConcordTransfer() = default;

Result<TransferInfo> ConcordTransfer::connect(std::string_view modelId, const Endpoint& endpoint) {
    if (!impl_->lock.owns_lock()) return makeError(ErrorCode::Io, "Another remote transfer is running");
    if (impl_->initialized || impl_->network) return makeError(ErrorCode::Io, "This transfer session is already connected");
    const auto* model = catalogModel(modelId);
    const bool automatic = modelId == "auto";
    if (!model || (!automatic && model->skins.empty())) return makeError(ErrorCode::Unsupported, "Unknown remote identity");
    auto& info = impl_->info;
    info.network = model->network;
    int hidCount = 0;
    if (!info.network) {
        if (hid_init() != 0) return makeError(ErrorCode::Io, "Cannot initialize HID");
        const std::unique_ptr<hid_device_info, decltype(&hid_free_enumeration)> devices(hid_enumerate(0, 0), hid_free_enumeration);
        for (auto* device = devices.get(); device; device = device->next)
            if ((device->vendor_id == 0x046d && device->product_id >= 0xc110 && device->product_id <= 0xc14f) ||
                (device->vendor_id == 0x0400 && device->product_id == 0xc359)) ++hidCount;
        if (automatic && hidCount == 0) info.network = true;
    }
    if (info.network) {
        impl_->network = std::make_unique<Harmony900Link>();
        auto remote = impl_->network->connect(endpoint);
        if (!remote) return std::unexpected(remote.error());
        info.remote = *remote;
        info.protocol = remote->identity.architecture;
    } else {
        // libconcord picks the first HID device; refuse an ambiguous selection first.
        if (hidCount != 1) return makeError(ErrorCode::Io, "Connect exactly one Harmony HID remote");
        impl_->initialized = true;
        if (auto result = checked(init_concord(), "Connect"); !result) return std::unexpected(result.error());
        if (auto result = checked(get_identity(Impl::callback, nullptr), "Identify"); !result)
            return std::unexpected(result.error());
        auto& remote = info.remote;
        remote.model = get_model();
        remote.identity.architecture = get_arch();
        remote.identity.skin = get_skin();
        remote.identity.softwareType = get_fw_type();
        remote.firmwareVersion = std::to_string(get_fw_ver_maj()) + "." + std::to_string(get_fw_ver_min());
        remote.identity.board = std::to_string(get_hw_ver_maj()) + "." + std::to_string(get_hw_ver_min()) + "." + std::to_string(get_hw_ver_mic());
        char flash[20]{};
        std::snprintf(flash, sizeof(flash), "0x%02X:0x%02X", get_flash_mfg(), get_flash_id());
        remote.identity.flash = flash;
        info.protocol = get_proto();
        info.mh = is_mh_remote() != 0;
        info.capacity = static_cast<std::uint32_t>(std::max(get_config_bytes_total(), 0));
    }
    if (!automatic && std::ranges::find(model->skins, info.remote.identity.skin) == model->skins.end())
        return makeError(ErrorCode::Unsupported, "Selected %1, but connected %2 (skin %3)",
            {std::string(model->name), info.remote.model, std::to_string(info.remote.identity.skin)});
    if (automatic) {
        const auto models = remoteCatalog();
        const auto actual = std::ranges::find_if(models, [&](const RemoteModel& candidate) {
            return std::ranges::find(candidate.skins, info.remote.identity.skin) != candidate.skins.end();
        });
        if (actual == models.end()) return makeError(ErrorCode::Unsupported, "Unknown remote identity");
        model = &*actual;
    }
    info.readable = model->readable;
    info.writable = model->writable;
    impl_->connected = true;
    return info;
}

Result<formats::Bytes> ConcordTransfer::read(const ProgressHandler& progress) {
    if (!impl_->connected) return makeError(ErrorCode::Io, "Remote is not connected");
    if (!impl_->info.readable) return makeError(ErrorCode::Unsupported, "This model does not expose configuration readback through this transport");
    if (progress && !progress(Stage::Read, 0, 0)) return makeError(ErrorCode::Cancelled, "Reading cancelled");
    if (impl_->network) return impl_->network->readConfig(progress);
    std::uint8_t* blob = nullptr;
    std::uint32_t size = 0;
    const int code = read_config_from_remote(&blob, &size, Impl::callback, const_cast<ProgressHandler*>(&progress));
    std::unique_ptr<std::uint8_t, decltype(&delete_blob)> owner(blob, delete_blob);
    if (auto result = checked(code, "Read configuration"); !result) return std::unexpected(result.error());
    if (!blob || !size || size > maxFileSize) return makeError(ErrorCode::Format, "Invalid remote configuration size");
    formats::Bytes bytes(blob, blob + size);
    if (progress && !progress(Stage::Read, size, size)) return makeError(ErrorCode::Cancelled, "Reading cancelled");
    if (impl_->info.mh) return mhBackup(std::move(bytes), impl_->info.remote.identity.skin);
    auto identity = impl_->info.remote.identity;
    identity.architecture = impl_->info.protocol; // EZHex PROTOCOL is not always architecture on old models.
    return formats::writeEzHex({.header = {}, .payload = std::move(bytes), .identity = identity});
}

Result<void> ConcordTransfer::write(std::span<const std::uint8_t> bytes, const std::filesystem::path& backup,
                                   bool allowWithoutBackup, const ProgressHandler& progress) {
    if (!impl_->connected) return makeError(ErrorCode::Io, "Remote is not connected");
    auto file = inspectTransferFile(bytes);
    if (!file) return std::unexpected(file.error());
    if (auto valid = validateTransfer(*file, impl_->info); !valid) return valid;
    if (!impl_->network) {
        if (auto valid = checked(harmony_concord_validate_config(file->payload.data(),
            static_cast<std::uint32_t>(file->payload.size())), "Validate configuration memory map"); !valid) return valid;
    }
    if (impl_->info.readable) {
        auto before = read(progress);
        if (!before) return std::unexpected(before.error());
        if (auto saved = saveBackup(backup, *before); !saved) return saved;
    } else if (!allowWithoutBackup) {
        return makeError(ErrorCode::Unsupported, "This model cannot be backed up; explicit write-without-backup consent is required");
    }
    if (progress && !progress(Stage::Write, 0, file->payload.size())) return makeError(ErrorCode::Cancelled, "Writing cancelled before programming");
    if (impl_->network) return impl_->network->writeConfig({bytes.begin(), bytes.end()}, progress);
    harmony_concord_set_config(file->payload.data(), static_cast<std::uint32_t>(file->payload.size()),
        reinterpret_cast<const std::uint8_t*>(file->xml.data()), static_cast<std::uint32_t>(file->xml.size()));
    auto result = checked(update_configuration(Impl::callback, const_cast<ProgressHandler*>(&progress), 0), "Write configuration");
    delete_opfile_obj();
    return result;
}
} // namespace harmony::transport
