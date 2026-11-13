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

/// Читает, пока сервер не закроет соединение или не замолчит на @p timeout.
std::string readAll(Channel& channel, std::chrono::milliseconds timeout) {
    std::string out;
    while (auto byte = channel.receive(1, timeout)) {
        out.push_back(static_cast<char>((*byte)[0]));
    }
    return out;
}

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
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
    const std::string response = readAll(**channel, kHttpTimeout);
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
    const std::string response = readAll(**channel, kHttpTimeout);
    const int status = httpStatus(response);
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
