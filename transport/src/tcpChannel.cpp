/**
 * @file tcpChannel.cpp
 * @brief TCP-канал на сокетах ОС (Winsock на Windows, BSD-сокеты на Linux).
 */

#include "harmony/transport/channel.h"

#include <algorithm>
#include <chrono>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace harmony::transport {

namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalid = INVALID_SOCKET;
void closeSocket(Socket s) { closesocket(s); }
int pollSockets(pollfd* fds, unsigned long count, int timeout) { return WSAPoll(fds, count, timeout); }
/// Winsock нужно инициализировать один раз за процесс.
bool startNetwork() {
    static const bool started = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return started;
}
void setBlocking(Socket s, bool blocking) {
    u_long mode = blocking ? 0 : 1;
    ioctlsocket(s, FIONBIO, &mode);
}
bool connectInProgress() { return WSAGetLastError() == WSAEWOULDBLOCK; }
bool interrupted() { return false; } // в Winsock сигналы вызовы не прерывают
bool timedOut() { return WSAGetLastError() == WSAETIMEDOUT; }
constexpr int kSendFlags = 0;
void setSendTimeout(Socket s, std::chrono::milliseconds timeout) {
    const DWORD value = static_cast<DWORD>(timeout.count());
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&value), sizeof value);
}
void suppressSigpipe(Socket) {}
#else
using Socket = int;
constexpr Socket kInvalid = -1;
void closeSocket(Socket s) { ::close(s); }
int pollSockets(pollfd* fds, nfds_t count, int timeout) { return ::poll(fds, count, timeout); }
bool startNetwork() { return true; }
void setBlocking(Socket s, bool blocking) {
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
}
bool connectInProgress() { return errno == EINPROGRESS; }
/// Вызов прервал сигнал (например, SIGCHLD от QProcess) — его нужно просто повторить.
bool interrupted() { return errno == EINTR; }
bool timedOut() { return errno == EAGAIN || errno == EWOULDBLOCK; }
void setSendTimeout(Socket s, std::chrono::milliseconds timeout) {
    timeval value{};
    value.tv_sec = static_cast<decltype(value.tv_sec)>(timeout.count() / 1000);
    value.tv_usec = static_cast<decltype(value.tv_usec)>((timeout.count() % 1000) * 1000);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof value);
}
// Запись в сокет, который закрыл пульт, по умолчанию шлёт процессу SIGPIPE и завершает его
// молча. Linux подавляет сигнал флагом send(), macOS/BSD — опцией сокета.
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
void suppressSigpipe([[maybe_unused]] Socket s) {
#ifdef SO_NOSIGPIPE
    const int on = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
}
#endif

/// Сколько ждать, пока пульт примет отправленное, прежде чем считать его зависшим.
constexpr std::chrono::milliseconds kSendTimeout{10000};

/// Соединение TCP; закрывается при уничтожении.
class TcpChannel final : public Channel {
public:
    explicit TcpChannel(Socket socket) : socket_(socket) {}
    ~TcpChannel() override { closeSocket(socket_); }
    TcpChannel(const TcpChannel&) = delete;
    TcpChannel& operator=(const TcpChannel&) = delete;

    Result<void> send(std::span<const std::uint8_t> bytes) override {
        std::size_t sent = 0;
        while (sent < bytes.size()) {
            const auto n = ::send(socket_, reinterpret_cast<const char*>(bytes.data() + sent),
                                  static_cast<int>(bytes.size() - sent), kSendFlags);
            if (n < 0 && interrupted()) {
                continue;
            }
            if (n < 0 && timedOut()) {
                return makeError(ErrorCode::Io, "the remote did not accept data in time");
            }
            if (n <= 0) {
                return makeError(ErrorCode::Io, "connection to the remote lost (sending)");
            }
            sent += static_cast<std::size_t>(n);
        }
        return {};
    }

    Result<std::vector<std::uint8_t>> receive(std::size_t count, std::chrono::milliseconds timeout) override {
        std::vector<std::uint8_t> out(count);
        std::size_t got = 0;
        while (got < count) {
            // Таймаут — на каждую порцию (см. Channel::receive); прерванный сигналом poll
            // продолжает ждать остаток, а не начинает заново.
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            int ready = -1;
            do {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                pollfd fd{};
                fd.fd = socket_;
                fd.events = POLLIN;
                ready = pollSockets(&fd, 1, static_cast<int>(std::max<std::int64_t>(left.count(), 0)));
            } while (ready < 0 && interrupted());
            if (ready == 0) {
                return makeError(ErrorCode::Io, "the remote did not answer in time");
            }
            long long n = -1; // recv возвращает int в Winsock и ssize_t в POSIX
            if (ready > 0) {
                do {
                    n = ::recv(socket_, reinterpret_cast<char*>(out.data() + got), static_cast<int>(count - got), 0);
                } while (n < 0 && interrupted());
            }
            if (n <= 0) {
                return makeError(ErrorCode::Io, "connection to the remote lost (receiving)");
            }
            got += static_cast<std::size_t>(n);
        }
        return out;
    }

private:
    Socket socket_;
};

} // namespace

Result<std::unique_ptr<Channel>> connectTcp(const std::string& host, std::uint16_t port,
                                            std::chrono::milliseconds timeout) {
    if (!startNetwork()) {
        return makeError(ErrorCode::Io, "could not start the network subsystem");
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || found == nullptr) {
        return makeError(ErrorCode::Io, "invalid remote address: %1", {host});
    }
    const Socket s = ::socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if (s == kInvalid) {
        freeaddrinfo(found);
        return makeError(ErrorCode::Io, "could not create a socket");
    }
    // Подключение с таймаутом: неблокирующий connect и ожидание готовности.
    setBlocking(s, false);
    const int started = ::connect(s, found->ai_addr, static_cast<int>(found->ai_addrlen));
    freeaddrinfo(found);
    bool connected = started == 0;
    if (!connected && connectInProgress()) {
        pollfd fd{};
        fd.fd = s;
        fd.events = POLLOUT;
        if (pollSockets(&fd, 1, static_cast<int>(timeout.count())) > 0 && (fd.revents & POLLOUT) != 0) {
            int error = 0;
            socklen_t length = sizeof error;
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
            connected = error == 0;
        }
    }
    if (!connected) {
        closeSocket(s);
        return makeError(ErrorCode::Io, "remote not found at %1:%2", {host, std::to_string(port)});
    }
    setBlocking(s, true);
    suppressSigpipe(s);
    setSendTimeout(s, kSendTimeout);
    return std::unique_ptr<Channel>(std::make_unique<TcpChannel>(s));
}

} // namespace harmony::transport
