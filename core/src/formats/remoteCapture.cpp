/**
 * @file remoteCapture.cpp
 * @brief Снятый с пульта файл `*.remote.json` и старый `*.hid.json`: см. remoteCapture.h.
 */

#include "harmony/core/formats/remoteCapture.h"

#include "harmony/core/formats/digest.h"
#include "harmony/core/version.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <optional>

namespace harmony::formats {

namespace {

using Json = nlohmann::json;

constexpr std::string_view kFormat = "harmony-remote-capture";
constexpr std::string_view kLegacyFormat = "harmony-hid-backup";
constexpr int kVersion = 1;
constexpr std::size_t kMaxData = 64 * 1024 * 1024; ///< Как у транспорта: больше пультов не бывает

/// Член объекта или nullptr; не бросает исключений.
const Json* member(const Json& object, const char* key) {
    if (!object.is_object()) return nullptr;
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &*found;
}

/// Строка; пусто, если поля нет или оно не строка.
std::string textOf(const Json& object, const char* key) {
    const Json* value = member(object, key);
    return value != nullptr && value->is_string() ? value->get<std::string>() : std::string();
}

/// Целое неотрицательное число в пределах @p limit; nullopt — нет поля или не подходит.
std::optional<std::uint64_t> unsignedOf(const Json& object, const char* key, std::uint64_t limit) {
    const Json* value = member(object, key);
    if (value == nullptr || !value->is_number_integer()) return std::nullopt;
    if (value->is_number_unsigned()) {
        const auto number = value->get<std::uint64_t>();
        return number <= limit ? std::optional(number) : std::nullopt;
    }
    const auto number = value->get<std::int64_t>();
    return number >= 0 && static_cast<std::uint64_t>(number) <= limit
               ? std::optional(static_cast<std::uint64_t>(number))
               : std::nullopt;
}

/// Целое число со знаком; @p fallback — нет поля.
int intOf(const Json& object, const char* key, int fallback) {
    const Json* value = member(object, key);
    if (value == nullptr || !value->is_number_integer()) return fallback;
    const auto number = value->get<std::int64_t>();
    return number >= -1 && number <= 0xFFFF ? static_cast<int>(number) : fallback;
}

/// Декодирует данные и сверяет их с размером и SHA-256 из файла.
Result<Bytes> checkedData(const std::string& base64, std::optional<std::uint64_t> size, const std::string& sha256) {
    if (!size || base64.size() > kMaxData / 3 * 4 + 4)
        return makeError(ErrorCode::Format, "the remote capture has no data size or the data is too large");
    auto data = base64Decode(base64);
    if (!data || data->empty()) return makeError(ErrorCode::Format, "the remote capture data is not valid Base64");
    if (data->size() != *size)
        return makeError(ErrorCode::Format, "the remote capture data size does not match: %1 bytes, expected %2",
                         {std::to_string(data->size()), std::to_string(*size)});
    if (sha256Hex(*data) != sha256)
        return makeError(ErrorCode::Format, "the remote capture data is damaged: SHA-256 does not match");
    return std::move(*data);
}

/// Старый `*.hid.json` (HarmonyHidReader): исходный образ без контейнера.
Result<RemoteCapture> parseLegacy(const Json& root) {
    if (intOf(root, "version", 0) != 1)
        return makeError(ErrorCode::Unsupported, "unsupported remote capture version %1",
                         {member(root, "version") != nullptr ? member(root, "version")->dump() : std::string("?")});
    RemoteCapture capture;
    capture.legacy = true;
    capture.container = CaptureContainer::Raw;
    capture.createdUtc = textOf(root, "createdUtc");
    capture.model = textOf(root, "model");
    capture.identity.architecture = intOf(root, "architecture", -1);
    capture.identity.skin = intOf(root, "skin", -1);
    capture.identity.flash = textOf(root, "flash");
    capture.identity.board = textOf(root, "board");
    capture.firmware = textOf(root, "firmware");
    capture.identityHex = textOf(root, "identityHex");
    capture.rawBase = static_cast<std::uint32_t>(unsignedOf(root, "configBase", 0xFFFFFFFF).value_or(0));
    auto data = checkedData(textOf(root, "configBase64"), unsignedOf(root, "configSize", kMaxData),
                            textOf(root, "configSha256"));
    if (!data) return std::unexpected(data.error());
    capture.data = std::move(*data);
    return capture;
}

} // namespace

std::string_view containerName(CaptureContainer container) {
    switch (container) {
    case CaptureContainer::EzHex: return "ezhex";
    case CaptureContainer::Mh: return "mh";
    case CaptureContainer::Raw: return "raw";
    }
    return "raw";
}

bool isRemoteCapture(std::string_view json) {
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) return false;
    const std::string format = textOf(root, "format");
    return format == kFormat || format == kLegacyFormat;
}

Result<RemoteCapture> parseRemoteCapture(std::string_view json) {
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return makeError(ErrorCode::Format, "this is not a remote capture file");
    const std::string format = textOf(root, "format");
    if (format == kLegacyFormat) return parseLegacy(root);
    if (format != kFormat) return makeError(ErrorCode::Format, "this is not a remote capture file");
    if (const Json* version = member(root, "version");
        version == nullptr || !version->is_number_integer() || version->get<std::int64_t>() != kVersion)
        return makeError(ErrorCode::Unsupported, "unsupported remote capture version %1",
                         {version != nullptr ? version->dump() : std::string("?")});

    const Json* remote = member(root, "remote");
    const Json* data = member(root, "data");
    if (remote == nullptr || !remote->is_object() || data == nullptr || !data->is_object())
        return makeError(ErrorCode::Format, "the remote capture has no remote or data section");

    RemoteCapture capture;
    capture.createdUtc = textOf(root, "createdUtc");
    capture.program = textOf(root, "program");
    capture.modelId = textOf(*remote, "id");
    capture.model = textOf(*remote, "model");
    capture.identity.architecture = intOf(*remote, "architecture", -1);
    capture.identity.skin = intOf(*remote, "skin", -1);
    capture.identity.softwareType = intOf(*remote, "softwareType", 0);
    capture.identity.flash = textOf(*remote, "flash");
    capture.identity.board = textOf(*remote, "board");
    capture.protocol = intOf(*remote, "protocol", -1);
    capture.firmware = textOf(*remote, "firmware");
    if (capture.identity.skin < 0)
        return makeError(ErrorCode::Format, "the remote capture does not say which remote it was read from");

    const std::string container = textOf(*data, "container");
    if (container == containerName(CaptureContainer::EzHex)) capture.container = CaptureContainer::EzHex;
    else if (container == containerName(CaptureContainer::Mh)) capture.container = CaptureContainer::Mh;
    else if (container == containerName(CaptureContainer::Raw)) {
        capture.container = CaptureContainer::Raw;
        capture.rawBase = static_cast<std::uint32_t>(unsignedOf(*data, "base", 0xFFFFFFFF).value_or(0));
        capture.identityHex = textOf(*data, "identityHex");
    } else return makeError(ErrorCode::Unsupported, "unknown remote capture container: %1", {container});

    auto bytes = checkedData(textOf(*data, "base64"), unsignedOf(*data, "size", kMaxData), textOf(*data, "sha256"));
    if (!bytes) return std::unexpected(bytes.error());
    capture.data = std::move(*bytes);
    return capture;
}

Result<std::string> writeRemoteCapture(const RemoteCapture& capture) {
    if (capture.data.empty() || capture.data.size() > kMaxData)
        return makeError(ErrorCode::Format, "the remote capture has no data or the data is too large");
    // Порядок ключей — как в описании: человеку удобнее читать сверху вниз.
    Json remote = Json::object();
    remote["id"] = capture.modelId;
    remote["model"] = capture.model;
    remote["architecture"] = capture.identity.architecture;
    remote["skin"] = capture.identity.skin;
    remote["protocol"] = capture.protocol;
    remote["softwareType"] = capture.identity.softwareType;
    remote["firmware"] = capture.firmware;
    remote["flash"] = capture.identity.flash;
    remote["board"] = capture.identity.board;
    Json data = Json::object();
    data["container"] = std::string(containerName(capture.container));
    data["size"] = capture.data.size();
    data["sha256"] = sha256Hex(capture.data);
    if (capture.container == CaptureContainer::Raw) {
        // Образ памяти без контейнера: адрес и ответ пульта нужны, чтобы разобрать его позже.
        data["base"] = capture.rawBase;
        data["identityHex"] = capture.identityHex;
    }
    data["base64"] = base64Encode(capture.data);
    Json root = Json::object();
    root["format"] = std::string(kFormat);
    root["version"] = kVersion;
    root["createdUtc"] = capture.createdUtc;
    root["program"] = capture.program.empty() ? "Harmony " + std::string(harmony::kVersion) : capture.program;
    root["remote"] = std::move(remote);
    root["data"] = std::move(data);
    // Строки от пульта могут оказаться не UTF-8: заменяем, а не бросаем исключение.
    return root.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

std::string utcTimestamp(std::chrono::system_clock::time_point time) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(time);
    const auto days = std::chrono::floor<std::chrono::days>(seconds);
    const std::chrono::year_month_day date{days};
    const std::chrono::hh_mm_ss clock{seconds - days};
    char text[32]{};
    std::snprintf(text, sizeof(text), "%04d-%02u-%02uT%02d:%02d:%02dZ", static_cast<int>(date.year()),
                  static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                  static_cast<int>(clock.hours().count()), static_cast<int>(clock.minutes().count()),
                  static_cast<int>(clock.seconds().count()));
    return text;
}

std::string captureFileName(std::string_view modelId, int year, int month, int day, int hour, int minute) {
    std::string id;
    for (const char c : modelId) {
        const bool safe = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' ||
                          c == '_';
        id += safe ? c : '-';
    }
    if (id.empty() || id == "auto") id = "remote";
    char stamp[32]{};
    std::snprintf(stamp, sizeof(stamp), "-%04d%02d%02d-%02d%02d", year, month, day, hour, minute);
    return "Harmony-" + id + stamp + std::string(kRemoteCaptureExtension);
}

} // namespace harmony::formats
