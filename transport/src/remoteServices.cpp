/**
 * @file remoteServices.cpp
 * @brief Служба настроек (HTTP, порт 80) и консоль (telnet, порт 23) Harmony 900.
 */

#include "harmony/transport/remoteServices.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <optional>
#include <string>

namespace harmony::transport {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::chrono::milliseconds kConnectTimeout{3000};
constexpr std::chrono::milliseconds kHttpTimeout{5000};

/// Подключение: через фабрику (тесты) или по TCP.
Result<std::unique_ptr<Channel>> open(const ChannelFactory& factory, const std::string& host, std::uint16_t port) {
    return factory ? factory() : connectTcp(host, port, kConnectTimeout);
}

Result<void> sendText(Channel& channel, std::string_view text) {
    return channel.send({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

/// Предел ответа службы настроек: настоящие ответы — сотни байт; больше — сбой, память не тратим.
constexpr std::size_t kMaxHttpResponse = 1024U * 1024U;
/// Предел заголовков ответа.
constexpr std::size_t kMaxHttpHeaders = 16U * 1024U;

/**
 * @brief Значение Content-Length из заголовков ответа.
 * @param headers Заголовки до пустой строки, без неё.
 * @return Длина тела; nullopt — заголовка нет или значение не число.
 */
std::optional<std::size_t> contentLength(std::string_view headers) {
    constexpr std::string_view kName = "content-length:";
    std::size_t lineStart = 0;
    while (lineStart < headers.size()) {
        auto lineEnd = headers.find("\r\n", lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = headers.size();
        }
        const std::string_view line = headers.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 2;
        if (line.size() < kName.size() ||
            !std::ranges::equal(line.substr(0, kName.size()), kName, [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == b;
            })) {
            continue;
        }
        const std::string value = trim(line.substr(kName.size()));
        if (value.empty() || !std::ranges::all_of(value, [](char c) { return c >= '0' && c <= '9'; }) ||
            value.size() > 12) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(std::stoull(value));
    }
    return std::nullopt;
}

/**
 * @brief Ответ HTTP целиком: заголовки, затем тело.
 *
 * Тело читается по Content-Length — ровно столько байт, без ожидания, пока сервер закроет
 * соединение. Без Content-Length (и без заголовков вообще, так тоже бывает) — до закрытия
 * соединения или паузы @p timeout. Ответ больше kMaxHttpResponse — ошибка.
 * @return Текст ответа; пустой — сервер ничего не прислал.
 */
Result<std::string> readHttpResponse(Channel& channel, std::chrono::milliseconds timeout) {
    std::string out;
    std::optional<std::size_t> bodyStart;
    while (!bodyStart) {
        auto byte = channel.receive(1, timeout);
        if (!byte) {
            return out; // закрыто до конца заголовков: разбирают то, что есть
        }
        out.push_back(static_cast<char>((*byte)[0]));
        if (out.ends_with("\r\n\r\n")) {
            bodyStart = out.size();
        } else if (out.size() > kMaxHttpHeaders) {
            return makeError(ErrorCode::Remote, "the remote sent too long a response header");
        }
    }
    if (const auto length = contentLength(std::string_view(out).substr(0, *bodyStart - 4))) {
        if (*length > kMaxHttpResponse - out.size()) {
            return makeError(ErrorCode::Remote, "the remote sent too large a response: %1 bytes",
                             {std::to_string(*length)});
        }
        if (*length > 0) {
            auto body = channel.receive(*length, timeout);
            if (!body) {
                return makeError(ErrorCode::Remote, "the remote sent an incomplete response");
            }
            out.append(body->begin(), body->end());
        }
        return out;
    }
    while (auto byte = channel.receive(1, timeout)) {
        out.push_back(static_cast<char>((*byte)[0]));
        if (out.size() > kMaxHttpResponse) {
            return makeError(ErrorCode::Remote, "the remote sent too large a response: over %1 bytes",
                             {std::to_string(kMaxHttpResponse)});
        }
    }
    return out;
}

/// Код ответа HTTP («HTTP/1.0 404 …» → 404); 0 — ответ без строки статуса (так тоже бывает).
int httpStatus(std::string_view response) {
    if (!response.starts_with("HTTP/")) {
        return 0;
    }
    const auto space = response.find(' ');
    if (space == std::string_view::npos || space + 4 > response.size()) {
        return 0;
    }
    int code = 0;
    for (std::size_t i = space + 1; i < space + 4; ++i) {
        if (response[i] < '0' || response[i] > '9') {
            return 0;
        }
        code = code * 10 + (response[i] - '0');
    }
    return code;
}

// --- канал событий (HAO) -----------------------------------------------------------

/// Что отправляет собственный интерфейс пульта перед сопряжением Z-Wave (afterglow rf_pairing.py).
constexpr std::string_view kReadyForRf = "<Event><Payload><Name>RF:ReadyForRF</Name></Payload></Event>";
constexpr std::string_view kAddReceiver =
    "<Event><Payload><Name>RF:AddReceiver</Name><Params></Params></Payload></Event>";
/// Предел одного сообщения канала событий: настоящие — сотни байт.
constexpr std::size_t kMaxEvent = 64U * 1024U;
/// Сколько ждать подтверждения готовности перед включением сопряжения.
constexpr std::chrono::milliseconds kReadyWait{500};

Result<void> sendEvent(Channel& channel, std::string_view message) {
    std::string framed(message);
    framed.push_back('\0');
    return sendText(channel, framed);
}

/// Одно сообщение канала событий до '\0'; nullopt — время вышло или канал закрыт.
std::optional<std::string> readEvent(Channel& channel, Clock::time_point deadline) {
    std::string event;
    while (Clock::now() < deadline) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        auto byte = channel.receive(1, left);
        if (!byte) {
            return std::nullopt;
        }
        if ((*byte)[0] == 0) {
            return event;
        }
        event.push_back(static_cast<char>((*byte)[0]));
        if (event.size() > kMaxEvent) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

/// Имя события из `<Name>…</Name>`; пусто — поля нет.
std::string_view eventName(std::string_view event) {
    constexpr std::string_view open = "<Name>";
    constexpr std::string_view close = "</Name>";
    const auto start = event.find(open);
    if (start == std::string_view::npos) {
        return {};
    }
    const auto end = event.find(close, start + open.size());
    if (end == std::string_view::npos) {
        return {};
    }
    return event.substr(start + open.size(), end - start - open.size());
}

// --- telnet ------------------------------------------------------------------------

constexpr std::uint8_t kIac = 255;
constexpr std::uint8_t kDont = 254;
constexpr std::uint8_t kDo = 253;
constexpr std::uint8_t kWont = 252;
constexpr std::uint8_t kWill = 251;
constexpr std::uint8_t kSb = 250;
constexpr std::uint8_t kSe = 240;

/**
 * @brief Сессия telnet: на все предложения сервера отвечаем отказом,
 *        и дальше идёт обычный построчный текст.
 */
class TelnetSession {
public:
    explicit TelnetSession(Channel& channel) : channel_(channel) {}

    /// Читает текст, пока в нём не появится одна из строк @p needles или не истечёт время.
    std::string readUntil(std::initializer_list<std::string_view> needles, Clock::time_point deadline) {
        std::string text;
        for (;;) {
            const auto byte = next(deadline);
            if (!byte) {
                return text;
            }
            text.push_back(static_cast<char>(*byte));
            for (const auto needle : needles) {
                if (text.ends_with(needle)) {
                    return text;
                }
            }
        }
    }

    Result<void> sendLine(std::string_view line) { return sendText(channel_, std::string(line) + "\r\n"); }

private:
    /// Следующий байт данных; служебные команды обрабатываются по пути.
    std::optional<std::uint8_t> next(Clock::time_point deadline) {
        for (;;) {
            const auto byte = raw(deadline);
            if (!byte || *byte != kIac) {
                return byte;
            }
            const auto verb = raw(deadline);
            if (!verb) {
                return std::nullopt;
            }
            if (*verb == kIac) {
                return kIac; // экранированный 0xFF — это данные
            }
            if (*verb == kDo || *verb == kDont || *verb == kWill || *verb == kWont) {
                const auto option = raw(deadline);
                if (!option) {
                    return std::nullopt;
                }
                if (*verb == kDo || *verb == kWill) {
                    const std::array<std::uint8_t, 3> refuse = {kIac, *verb == kDo ? kWont : kDont, *option};
                    (void)channel_.send(refuse);
                }
            } else if (*verb == kSb) {
                // Подпереговоры — пропускаем до IAC SE.
                std::uint8_t previous = 0;
                for (;;) {
                    const auto skipped = raw(deadline);
                    if (!skipped) {
                        return std::nullopt;
                    }
                    if (previous == kIac && *skipped == kSe) {
                        break;
                    }
                    previous = *skipped;
                }
            }
        }
    }

    std::optional<std::uint8_t> raw(Clock::time_point deadline) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0) {
            return std::nullopt;
        }
        auto byte = channel_.receive(1, left);
        return byte ? std::optional((*byte)[0]) : std::nullopt;
    }

    Channel& channel_;
};

/// Признак конца вывода команды. В команде он записан двумя частями: эхо строки
/// от сервера не содержит его целиком, и чтение не останавливается раньше времени.
constexpr std::string_view kMarker = "__harmony_done__";
constexpr std::string_view kMarkerCommand = "echo \"__harmony\"\"_done__\"";

} // namespace

std::string settingValue(std::string_view response) {
    const auto open = response.find("<Value>");
    const auto close = response.find("</Value>");
    if (open != std::string_view::npos && close != std::string_view::npos && close > open) {
        return trim(response.substr(open + 7, close - open - 7));
    }
    if (const auto body = response.find("\r\n\r\n"); response.starts_with("HTTP/") && body != std::string_view::npos) {
        return trim(response.substr(body + 4));
    }
    return trim(response);
}

Result<std::string> readRemoteSetting(const std::string& host, std::string_view endpoint,
                                      const ChannelFactory& factory) {
    auto channel = open(factory, host, kSettingsPort);
    if (!channel) {
        return std::unexpected(std::move(channel.error()));
    }
    if (auto sent = sendText(**channel, "GET " + std::string(endpoint) + " HTTP/1.0\r\n\r\n"); !sent) {
        return std::unexpected(std::move(sent.error()));
    }
    auto read = readHttpResponse(**channel, kHttpTimeout);
    if (!read) {
        return std::unexpected(std::move(read.error()));
    }
    const std::string& response = *read;
    if (response.empty()) {
        return makeError(ErrorCode::Remote, "the remote did not answer the settings request %1", {std::string(endpoint)});
    }
    if (const int status = httpStatus(response); status >= 400) {
        return makeError(ErrorCode::Remote, "the remote refused the setting %1 (HTTP %2)",
                         {std::string(endpoint), std::to_string(status)});
    }
    return settingValue(response);
}

Result<void> writeRemoteSetting(const std::string& host, std::string_view endpoint, std::string_view value,
                                const ChannelFactory& factory) {
    auto channel = open(factory, host, kSettingsPort);
    if (!channel) {
        return std::unexpected(std::move(channel.error()));
    }
    // Тело — голое значение: обёртку <Event> пульт сохранил бы как значение.
    const std::string request = "POST " + std::string(endpoint) +
                                " HTTP/1.0\r\nContent-Type: text/plain\r\nContent-Length: " +
                                std::to_string(value.size()) + "\r\n\r\n" + std::string(value);
    if (auto sent = sendText(**channel, request); !sent) {
        return sent;
    }
    auto read = readHttpResponse(**channel, kHttpTimeout);
    if (!read) {
        return std::unexpected(std::move(read.error()));
    }
    const int status = httpStatus(*read);
    if (status >= 400) {
        return makeError(ErrorCode::Remote, "the remote refused the setting %1 (HTTP %2)",
                         {std::string(endpoint), std::to_string(status)});
    }
    // Без строки статуса неизвестно, принял ли пульт значение: считать это успехом нельзя.
    if (status == 0) {
        return makeError(ErrorCode::Remote, "the remote did not confirm the setting %1", {std::string(endpoint)});
    }
    return {};
}

Result<bool> startExtenderPairing(const std::string& host, std::chrono::milliseconds wait,
                                  const ChannelFactory& factory) {
    auto channel = open(factory, host, kEventPort);
    if (!channel) {
        return std::unexpected(std::move(channel.error()));
    }
    if (auto sent = sendEvent(**channel, kReadyForRf); !sent) {
        return std::unexpected(std::move(sent.error()));
    }
    // Пульт отвечает на готовность событием (netmgr_app_ready), и окно сопряжения открывается
    // после него: сообщения не должны идти вплотную (afterglow rf_pairing.py). Ответ не
    // обязателен — если его нет, через kReadyWait уходит включение.
    static_cast<void>(readEvent(**channel, Clock::now() + kReadyWait));
    if (auto sent = sendEvent(**channel, kAddReceiver); !sent) {
        return std::unexpected(std::move(sent.error()));
    }

    const auto deadline = Clock::now() + wait;
    while (Clock::now() < deadline) {
        const auto event = readEvent(**channel, deadline);
        if (!event) {
            break;
        }
        if (eventName(*event) == "NewReceiversFound") {
            return true;
        }
    }
    return false;
}

Result<std::string> runRemoteShell(const std::string& host, std::string_view command,
                                   std::chrono::milliseconds timeout, const ChannelFactory& factory) {
    auto channel = open(factory, host, kTelnetPort);
    if (!channel) {
        return std::unexpected(std::move(channel.error()));
    }
    TelnetSession session(**channel);
    const auto loginDeadline = Clock::now() + std::chrono::seconds(10);
    if (session.readUntil({"ogin: ", "ogin:"}, loginDeadline).find("ogin:") == std::string::npos) {
        return makeError(ErrorCode::Remote, "the remote console did not ask for a login");
    }
    (void)session.sendLine(kTelnetUser);
    session.readUntil({"assword:", "assword: "}, loginDeadline);
    (void)session.sendLine(kTelnetPassword);
    const std::string banner = session.readUntil({"# ", "$ ", "ogin:"}, loginDeadline);
    std::string lowered = banner;
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowered.find("incorrect") != std::string::npos || banner.ends_with("ogin:") ||
        !(banner.ends_with("# ") || banner.ends_with("$ "))) {
        return makeError(ErrorCode::Remote, "the remote console refused the login");
    }

    if (auto sent = session.sendLine(std::string(command) + "; " + std::string(kMarkerCommand)); !sent) {
        return std::unexpected(std::move(sent.error()));
    }
    std::string output = session.readUntil({kMarker}, Clock::now() + timeout);
    const bool complete = output.ends_with(kMarker);
    if (complete) {
        output.resize(output.size() - kMarker.size());
    }
    // Первая строка — эхо нашей команды.
    if (const auto eol = output.find('\n'); eol != std::string::npos &&
                                            output.substr(0, eol).find(kMarkerCommand) != std::string::npos) {
        output.erase(0, eol + 1);
    }
    std::erase(output, '\r');
    while (!output.empty() && output.back() == '\n') {
        output.pop_back();
    }
    (void)session.sendLine("exit");
    // Без признака конца вывод обрезан: отдавать его как полный результат нельзя.
    if (!complete) {
        return makeError(ErrorCode::Remote, "the remote console did not finish the command in %1 s",
                         {std::to_string(std::chrono::duration_cast<std::chrono::seconds>(timeout).count())});
    }
    return output;
}

Result<std::string> readRemoteLog(const std::string& host, const ChannelFactory& factory) {
    return runRemoteShell(host, "sloginfo", std::chrono::seconds(30), factory);
}

} // namespace harmony::transport
