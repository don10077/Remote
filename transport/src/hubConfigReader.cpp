/**
 * @file hubConfigReader.cpp
 * @brief Чтение конфигурации стокового Harmony Hub: HTTP getProvisionInfo и WebSocket engine?config.
 *
 * Последовательность та же, что у клиентов Home Assistant (aioharmony) и у совместимого
 * режима harmony-ng (payload/source/hbus_server.c, tests/test_hbus.py): POST / с
 * setup.account?getProvisionInfo, затем GET /?domain=svcs.myharmony.com&hubId=<remoteId>
 * с переходом на WebSocket и запрос vnd.logitech.harmony.engine?config.
 */

#include "harmony/transport/hubConfigReader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <format>
#include <random>
#include <span>

namespace harmony::transport {

namespace {

using Json = nlohmann::json;
using namespace std::chrono_literals;

constexpr auto kConnectTimeout = 3000ms;             ///< Ожидание TCP-подключения к Hub
constexpr auto kReplyTimeout = 20000ms;              ///< Hub собирает конфигурацию несколько секунд
constexpr auto kOptionalReplyTimeout = 5000ms;       ///< Для необязательного discoveryinfo
constexpr std::size_t kMaxHeader = 16 * 1024;        ///< Предел HTTP-заголовков
constexpr std::size_t kMaxLine = 1024;               ///< Предел строки chunked-кодирования
constexpr std::size_t kMaxHttpBody = 1024 * 1024;    ///< Ответ provisioning — сотни байт
constexpr std::size_t kMaxMessage = 32 * 1024 * 1024; ///< Конфигурация Hub — обычно до нескольких МБ
constexpr std::size_t kChunk = 64 * 1024;            ///< Порция чтения большого кадра
constexpr int kMaxForeignMessages = 64;              ///< Уведомления Hub до ответа на запрос

constexpr std::string_view kConfigCommand = "vnd.logitech.harmony/vnd.logitech.harmony.engine?config";
constexpr std::string_view kDiscoveryCommand = "connect.discoveryinfo?get";
constexpr std::string_view kBackupFormat = "harmony-hub-config";
constexpr int kBackupVersion = 1;

/// Ошибка отмены пользователем.
std::unexpected<Error> cancelledError() {
    return makeError(ErrorCode::Cancelled, "reading the Hub configuration was cancelled");
}

/// @return false — пользователь отменил операцию.
bool proceed(const ProgressHandler& progress, Stage stage, std::size_t done, std::size_t total) {
    return !progress || progress(stage, done, total);
}

/// Байты строки без копирования.
std::span<const std::uint8_t> bytesOf(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

/// Сравнение ASCII-строк без учёта регистра (имена HTTP-заголовков).
bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

/// Base64 без переносов строк — для Sec-WebSocket-Key.
std::string base64(std::span<const std::uint8_t> data) {
    static constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const bool second = i + 1 < data.size();
        const bool third = i + 2 < data.size();
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (second ? static_cast<std::uint32_t>(data[i + 1]) << 8 : 0U) |
                                (third ? static_cast<std::uint32_t>(data[i + 2]) : 0U);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += second ? kAlphabet[(n >> 6) & 63] : '=';
        out += third ? kAlphabet[n & 63] : '=';
    }
    return out;
}

/// Член JSON-объекта или nullptr; не бросает исключений.
const Json* member(const Json& object, const char* key) {
    if (!object.is_object()) return nullptr;
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &*found;
}

/// Строка или целое число как текст; иначе пусто (Hub присылает id и числами, и строками).
std::string textOf(const Json* value) {
    if (value == nullptr) return {};
    if (value->is_string()) return value->get<std::string>();
    if (value->is_number_integer() || value->is_number_unsigned()) return value->dump();
    return {};
}

/// Код ответа Hub (число или строка «200»); -1 — нет кода.
int codeOf(const Json* value) {
    const std::string text = textOf(value);
    int code = -1;
    (void)std::from_chars(text.data(), text.data() + text.size(), code);
    return code;
}

/// Текущее время UTC в ISO 8601.
std::string utcNow() {
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

/// Читает строку до CRLF (без него).
Result<std::string> readLine(Channel& channel) {
    std::string line;
    while (!line.ends_with("\r\n")) {
        if (line.size() > kMaxLine) return makeError(ErrorCode::Format, "the Hub sent an oversized HTTP line");
        auto byte = channel.receive(1, kReplyTimeout);
        if (!byte) return std::unexpected(byte.error());
        line += static_cast<char>((*byte)[0]);
    }
    line.resize(line.size() - 2);
    return line;
}

/// Читает HTTP-заголовки до пустой строки включительно; лишних байт тела не забирает.
Result<std::string> readHeaders(Channel& channel) {
    std::string head;
    while (!head.ends_with("\r\n\r\n")) {
        if (head.size() >= kMaxHeader) return makeError(ErrorCode::Format, "the Hub sent an oversized HTTP header");
        auto byte = channel.receive(1, kReplyTimeout);
        if (!byte) return std::unexpected(byte.error());
        head += static_cast<char>((*byte)[0]);
    }
    return head;
}

/// Код статуса из строки «HTTP/1.1 200 OK»; -1 — не HTTP.
int statusCode(std::string_view head) {
    const auto space = head.find(' ');
    if (!head.starts_with("HTTP/") || space == std::string_view::npos) return -1;
    int code = -1;
    (void)std::from_chars(head.data() + space + 1, head.data() + head.size(), code);
    return code;
}

/// Значение заголовка (имя без учёта регистра); пусто — заголовка нет.
std::string headerValue(std::string_view head, std::string_view name) {
    std::size_t lineEnd = head.find("\r\n");
    while (lineEnd != std::string_view::npos && lineEnd + 2 < head.size()) {
        const std::size_t start = lineEnd + 2;
        lineEnd = head.find("\r\n", start);
        const std::string_view line = head.substr(start, lineEnd - start);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || !equalsIgnoreCase(line.substr(0, colon), name)) continue;
        std::string_view value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
        return std::string(value);
    }
    return {};
}

/// Читает тело HTTP-ответа: Content-Length, chunked или до закрытия соединения.
Result<std::string> readBody(Channel& channel, std::string_view head) {
    if (const std::string length = headerValue(head, "Content-Length"); !length.empty()) {
        std::size_t size = 0;
        const auto [end, error] = std::from_chars(length.data(), length.data() + length.size(), size);
        if (error != std::errc() || end != length.data() + length.size() || size > kMaxHttpBody)
            return makeError(ErrorCode::Format, "the Hub sent an invalid HTTP length: %1", {length});
        if (size == 0) return std::string();
        auto body = channel.receive(size, kReplyTimeout);
        if (!body) return std::unexpected(body.error());
        return std::string(body->begin(), body->end());
    }
    if (equalsIgnoreCase(headerValue(head, "Transfer-Encoding"), "chunked")) {
        std::string body;
        for (;;) {
            auto line = readLine(channel);
            if (!line) return std::unexpected(line.error());
            const std::string_view digits = std::string_view(*line).substr(0, line->find(';'));
            std::size_t size = 0;
            const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), size, 16);
            if (error != std::errc() || end == digits.data() || body.size() + size > kMaxHttpBody)
                return makeError(ErrorCode::Format, "the Hub sent an invalid HTTP chunk");
            if (size == 0) {
                // Необязательные trailer-заголовки до пустой строки.
                for (;;) {
                    auto trailer = readLine(channel);
                    if (!trailer) return std::unexpected(trailer.error());
                    if (trailer->empty()) return body;
                }
            }
            auto chunk = channel.receive(size + 2, kReplyTimeout); // данные и CRLF
            if (!chunk) return std::unexpected(chunk.error());
            body.append(chunk->begin(), chunk->end() - 2);
        }
    }
    // Без длины: тело заканчивается закрытием соединения (Connection: close).
    std::string body;
    while (body.size() < kMaxHttpBody) {
        auto byte = channel.receive(1, kReplyTimeout);
        if (!byte) break;
        body += static_cast<char>((*byte)[0]);
    }
    return body;
}

/**
 * @brief Запрос setup.account?getProvisionInfo — так же, как его шлёт приложение Logitech.
 * @return Объект data ответа (activeRemoteId, версия прошивки и др.).
 */
Result<Json> provision(Channel& channel, const std::string& host, std::uint16_t port) {
    // Ключ "id " с пробелом — так его отправляет стоковый клиент; Hub принимает оба варианта.
    const std::string body = R"({"id ":1,"cmd":"setup.account?getProvisionInfo","timeout":90000})";
    const std::string request = std::format(
        "POST / HTTP/1.1\r\nHost: {}:{}\r\nOrigin: http://sl.dhg.myharmony.com\r\n"
        "Content-Type: application/json\r\nAccept: application/json\r\nAccept-Charset: utf-8\r\n"
        "Content-Length: {}\r\nConnection: close\r\n\r\n{}",
        host, port, body.size(), body);
    if (auto sent = channel.send(bytesOf(request)); !sent) return std::unexpected(sent.error());
    auto head = readHeaders(channel);
    if (!head) return std::unexpected(head.error());
    if (const int status = statusCode(*head); status != 200)
        return makeError(ErrorCode::Remote, "the Hub at %1 refused the provisioning request (HTTP %2)",
                         {host, std::to_string(status)});
    auto text = readBody(channel, *head);
    if (!text) return std::unexpected(text.error());
    const Json reply = Json::parse(text->begin(), text->end(), nullptr, false);
    const Json* data = member(reply, "data");
    if (reply.is_discarded() || data == nullptr || !data->is_object())
        return makeError(ErrorCode::Format, "the Hub sent an unexpected provisioning reply");
    if (const int code = codeOf(member(reply, "code")); code != 200)
        return makeError(ErrorCode::Remote, "the Hub refused the provisioning request (code %1)", {std::to_string(code)});
    return *data;
}

/**
 * @brief Минимальный клиент WebSocket (RFC 6455) поверх канала: текстовые сообщения без расширений.
 *
 * Sec-WebSocket-Accept не проверяется: соединение локальное, а ответ Hub дальше
 * проверяется как JSON с ожидаемым id запроса.
 */
class WebSocket {
public:
    explicit WebSocket(Channel& channel) : channel_(channel) {}

    /// Переводит HTTP-соединение в WebSocket; ErrorCode::Remote — Hub отказал.
    Result<void> handshake(const std::string& host, std::uint16_t port, const std::string& path) {
        std::array<std::uint8_t, 16> key{};
        for (auto& byte : key) byte = static_cast<std::uint8_t>(random_() & 0xFF);
        const std::string request = std::format(
            "GET {} HTTP/1.1\r\nHost: {}:{}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: {}\r\nSec-WebSocket-Version: 13\r\n\r\n",
            path, host, port, base64(key));
        if (auto sent = channel_.send(bytesOf(request)); !sent) return sent;
        auto head = readHeaders(channel_);
        if (!head) return std::unexpected(head.error());
        if (const int status = statusCode(*head); status != 101)
            return makeError(ErrorCode::Remote, "the Hub at %1 did not open the WebSocket (HTTP %2)",
                             {host, std::to_string(status)});
        return {};
    }

    /// Отправляет текстовое сообщение одним кадром.
    Result<void> sendText(std::string_view text) { return sendFrame(0x1, bytesOf(text)); }

    /**
     * @brief Следующее текстовое сообщение (с учётом фрагментации).
     *
     * На ping отвечает pong, pong пропускает; close и двоичные кадры — ошибка.
     * @param progress Stage::Read: принято байт текущего сообщения; false — отмена.
     * @param timeout Ожидание каждой порции данных.
     */
    Result<std::string> receiveText(const ProgressHandler& progress, std::chrono::milliseconds timeout) {
        std::string message;
        bool fragmented = false;
        for (;;) {
            auto header = channel_.receive(2, timeout);
            if (!header) return std::unexpected(header.error());
            const bool lastFrame = ((*header)[0] & 0x80) != 0;
            const std::uint8_t opcode = (*header)[0] & 0x0F;
            const bool masked = ((*header)[1] & 0x80) != 0;
            std::uint64_t length = (*header)[1] & 0x7F;
            if (length >= 126) {
                auto extended = channel_.receive(length == 126 ? 2 : 8, timeout);
                if (!extended) return std::unexpected(extended.error());
                length = 0;
                for (const std::uint8_t byte : *extended) length = (length << 8) | byte;
            }
            if ((opcode & 0x8) != 0 && length > 125)
                return makeError(ErrorCode::Format, "the Hub sent an invalid WebSocket control frame");
            if (length > kMaxMessage || message.size() + length > kMaxMessage)
                return makeError(ErrorCode::Format, "the Hub sent a message larger than %1 MB",
                                 {std::to_string(kMaxMessage / (1024 * 1024))});
            std::array<std::uint8_t, 4> mask{};
            if (masked) {
                auto key = channel_.receive(mask.size(), timeout);
                if (!key) return std::unexpected(key.error());
                std::ranges::copy(*key, mask.begin());
            }
            std::string payload;
            payload.reserve(static_cast<std::size_t>(length));
            while (payload.size() < length) {
                const auto part = std::min<std::size_t>(kChunk, static_cast<std::size_t>(length) - payload.size());
                auto bytes = channel_.receive(part, timeout);
                if (!bytes) return std::unexpected(bytes.error());
                payload.append(bytes->begin(), bytes->end());
                if (!proceed(progress, Stage::Read, message.size() + payload.size(), 0)) return cancelledError();
            }
            if (masked)
                for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);

            switch (opcode) {
            case 0x9: // ping
                if (auto sent = sendFrame(0xA, bytesOf(payload)); !sent) return std::unexpected(sent.error());
                continue;
            case 0xA: // pong
                continue;
            case 0x8: // close
                return makeError(ErrorCode::Remote, "the Hub closed the connection");
            case 0x1: // text
                if (fragmented) return makeError(ErrorCode::Format, "the Hub interrupted a fragmented message");
                message = std::move(payload);
                break;
            case 0x0: // продолжение
                if (!fragmented) return makeError(ErrorCode::Format, "the Hub sent an unexpected continuation frame");
                message += payload;
                break;
            default:
                return makeError(ErrorCode::Format, "the Hub sent an unsupported WebSocket frame (%1)",
                                 {std::to_string(opcode)});
            }
            if (lastFrame) return message;
            fragmented = true;
        }
    }

    /// Вежливо закрывает соединение; ошибки не важны — канал всё равно закрывается.
    void close() { (void)sendFrame(0x8, {}); }

private:
    /// Кадр клиента: всегда с маской (RFC 6455, 5.3).
    Result<void> sendFrame(std::uint8_t opcode, std::span<const std::uint8_t> payload) {
        const std::uint64_t size = payload.size();
        std::vector<std::uint8_t> frame;
        frame.reserve(payload.size() + 14);
        frame.push_back(static_cast<std::uint8_t>(0x80 | opcode));
        if (size < 126) {
            frame.push_back(static_cast<std::uint8_t>(0x80 | size));
        } else if (size <= 0xFFFF) {
            frame.push_back(0x80 | 126);
            frame.push_back(static_cast<std::uint8_t>(size >> 8));
            frame.push_back(static_cast<std::uint8_t>(size));
        } else {
            frame.push_back(0x80 | 127);
            for (int shift = 56; shift >= 0; shift -= 8) frame.push_back(static_cast<std::uint8_t>(size >> shift));
        }
        std::array<std::uint8_t, 4> mask{};
        for (auto& byte : mask) byte = static_cast<std::uint8_t>(random_() & 0xFF);
        frame.insert(frame.end(), mask.begin(), mask.end());
        for (std::size_t i = 0; i < payload.size(); ++i)
            frame.push_back(static_cast<std::uint8_t>(payload[i] ^ mask[i % 4]));
        return channel_.send(frame);
    }

    Channel& channel_;
    std::mt19937 random_{std::random_device{}()}; ///< Маски и ключ: защита от прокси, не секрет
};

/**
 * @brief Отправляет команду hbus и ждёт ответ с тем же id.
 *
 * Уведомления Hub (stateDigest и др.) и промежуточные ответы с кодом 100 пропускаются.
 * @return Объект data ответа с кодом 200.
 */
Result<Json> call(WebSocket& socket, const std::string& remoteId, std::string_view command, const std::string& id,
                  const ProgressHandler& progress, std::chrono::milliseconds timeout) {
    const Json request = {{"hubId", remoteId},
                          {"timeout", 30},
                          {"hbus", {{"cmd", std::string(command)}, {"id", id}, {"params", {{"verb", "get"}, {"format", "json"}}}}}};
    if (auto sent = socket.sendText(request.dump()); !sent) return std::unexpected(sent.error());
    for (int skipped = 0; skipped < kMaxForeignMessages; ++skipped) {
        auto text = socket.receiveText(progress, timeout);
        if (!text) return std::unexpected(text.error());
        const Json reply = Json::parse(text->begin(), text->end(), nullptr, false);
        if (reply.is_discarded() || textOf(member(reply, "id")) != id) continue;
        const int code = codeOf(member(reply, "code"));
        if (code == 100) continue;
        if (code != 200)
            return makeError(ErrorCode::Remote, "the Hub answered %1 with code %2",
                             {std::string(command), std::to_string(code)});
        const Json* data = member(reply, "data");
        if (data == nullptr || !data->is_object())
            return makeError(ErrorCode::Format, "the Hub answered %1 without data", {std::string(command)});
        return *data;
    }
    return makeError(ErrorCode::Remote, "the Hub did not answer %1", {std::string(command)});
}

/// Заполняет сводку устройств и активностей из объекта конфигурации Hub.
void summarize(const Json& config, HubConfig& out) {
    if (const Json* devices = member(config, "device"); devices != nullptr && devices->is_array()) {
        for (const Json& device : *devices) {
            if (!device.is_object()) continue;
            HubDeviceSummary item;
            item.label = textOf(member(device, "label"));
            item.manufacturer = textOf(member(device, "manufacturer"));
            item.model = textOf(member(device, "model"));
            item.type = textOf(member(device, "type"));
            if (const Json* groups = member(device, "controlGroup"); groups != nullptr && groups->is_array()) {
                for (const Json& group : *groups) {
                    if (const Json* functions = member(group, "function"); functions != nullptr && functions->is_array())
                        item.commands += functions->size();
                }
            }
            out.devices.push_back(std::move(item));
        }
    }
    if (const Json* activities = member(config, "activity"); activities != nullptr && activities->is_array()) {
        for (const Json& activity : *activities) {
            if (textOf(member(activity, "id")) == "-1") continue; // PowerOff есть у каждого Hub
            out.activities.push_back(textOf(member(activity, "label")));
        }
    }
}

/// Проверяет, что ответ похож на конфигурацию Hub.
bool looksLikeConfig(const Json& config) {
    const Json* devices = member(config, "device");
    const Json* activities = member(config, "activity");
    return (devices != nullptr && devices->is_array()) || (activities != nullptr && activities->is_array());
}

/// Только безобидные поля discoveryinfo: без адресов, учётной записи и e-mail.
Json safeDiscovery(const Json& discovery) {
    Json out = Json::object();
    for (const char* key : {"friendlyName", "current_fw_version", "protocolVersion", "productId"}) {
        if (const Json* value = member(discovery, key); value != nullptr && value->is_primitive()) out[key] = *value;
    }
    return out;
}

} // namespace

Result<HubConfig> readHubConfig(const ChannelFactory& connect, const std::string& host, std::uint16_t port,
                                const ProgressHandler& progress) {
    constexpr std::size_t kSteps = 3;
    if (!proceed(progress, Stage::Connect, 0, kSteps)) return cancelledError();
    HubConfig out;
    out.host = host;
    out.port = port;

    // 1. HTTP: remoteId Hub. Соединение закрывается сразу (Connection: close).
    Json provisionData;
    {
        auto channel = connect();
        if (!channel)
            return wrapError(channel.error(), "could not connect to the Harmony Hub at %1:%2", {host, std::to_string(port)});
        auto data = provision(**channel, host, port);
        if (!data) return std::unexpected(data.error());
        provisionData = std::move(*data);
    }
    out.remoteId = textOf(member(provisionData, "activeRemoteId"));
    if (out.remoteId.empty() || out.remoteId.size() > 20 ||
        !std::ranges::all_of(out.remoteId, [](char c) { return c >= '0' && c <= '9'; }))
        return makeError(ErrorCode::Format, "the Hub did not report a valid remote id");
    if (!proceed(progress, Stage::Connect, 1, kSteps)) return cancelledError();

    // 2. WebSocket и команда config.
    auto channel = connect();
    if (!channel)
        return wrapError(channel.error(), "could not connect to the Harmony Hub at %1:%2", {host, std::to_string(port)});
    WebSocket socket(**channel);
    if (auto opened = socket.handshake(host, port, "/?domain=svcs.myharmony.com&hubId=" + out.remoteId); !opened)
        return std::unexpected(opened.error());
    if (!proceed(progress, Stage::Connect, 2, kSteps)) return cancelledError();

    auto config = call(socket, out.remoteId, kConfigCommand, "harmony-config", progress, kReplyTimeout);
    if (!config) {
        socket.close();
        return std::unexpected(config.error());
    }
    if (!looksLikeConfig(*config)) {
        socket.close();
        return makeError(ErrorCode::Format, "the Hub configuration has no devices or activities");
    }

    // 3. Имя и версия прошивки — необязательно: их отсутствие не мешает копии.
    Json discovery = Json::object();
    if (auto info = call(socket, out.remoteId, kDiscoveryCommand, "harmony-discovery", progress, kOptionalReplyTimeout); info) {
        discovery = safeDiscovery(*info);
    } else if (info.error().code == ErrorCode::Cancelled) {
        socket.close();
        return std::unexpected(info.error());
    }
    socket.close();
    if (!proceed(progress, Stage::Connect, kSteps, kSteps)) return cancelledError();

    out.hubName = textOf(member(discovery, "friendlyName"));
    if (out.hubName.empty()) out.hubName = textOf(member(provisionData, "hubName"));
    out.firmware = textOf(member(discovery, "current_fw_version"));
    if (out.firmware.empty()) out.firmware = textOf(member(provisionData, "hubSwVersion"));
    summarize(*config, out);

    // E-mail и accountId из provisioning в файл не попадают.
    const Json backup = {{"format", std::string(kBackupFormat)}, {"version", kBackupVersion}, {"createdUtc", utcNow()},
                         {"remoteId", out.remoteId}, {"hubName", out.hubName},    {"firmware", out.firmware},
                         {"discovery", discovery},   {"config", *config}};
    out.backupJson = backup.dump(2, ' ', false, Json::error_handler_t::replace);
    out.backupJson += '\n';
    return out;
}

Result<HubConfig> readHubConfig(const std::string& host, std::uint16_t port, const ProgressHandler& progress) {
    const ChannelFactory connect = [host, port] { return connectTcp(host, port, kConnectTimeout); };
    return readHubConfig(connect, host, port, progress);
}

Result<HubConfig> openHubBackup(std::string_view json) {
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || textOf(member(root, "format")) != kBackupFormat)
        return makeError(ErrorCode::Format, "not a Harmony Hub configuration file");
    if (const Json* version = member(root, "version"); version == nullptr || !version->is_number_integer() ||
                                                       version->get<long long>() != kBackupVersion)
        return makeError(ErrorCode::Unsupported, "unsupported Harmony Hub configuration file version %1",
                         {version != nullptr ? version->dump() : std::string("?")});
    const Json* config = member(root, "config");
    if (config == nullptr || !config->is_object())
        return makeError(ErrorCode::Format, "the Harmony Hub configuration file has no configuration");
    HubConfig out;
    out.remoteId = textOf(member(root, "remoteId"));
    out.hubName = textOf(member(root, "hubName"));
    out.firmware = textOf(member(root, "firmware"));
    summarize(*config, out);
    out.backupJson = std::string(json);
    return out;
}

} // namespace harmony::transport
