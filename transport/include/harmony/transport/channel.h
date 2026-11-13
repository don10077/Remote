#pragma once

/**
 * @file channel.h
 * @brief Канал байтов до пульта: TCP-соединение или подставной пульт в тестах.
 */

#include "harmony/core/error.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace harmony::transport {

/// Двусторонний канал байтов с таймаутами.
class Channel {
public:
    virtual ~Channel() = default;

    /// Отправляет все байты. ErrorCode::Io — соединение потеряно.
    [[nodiscard]] virtual Result<void> send(std::span<const std::uint8_t> bytes) = 0;

    /**
     * @brief Читает ровно @p count байт.
     * @param timeout Сколько ждать каждой порции данных.
     * @return Байты; ErrorCode::Io — соединение закрыто или истёк таймаут.
     */
    [[nodiscard]] virtual Result<std::vector<std::uint8_t>> receive(std::size_t count,
                                                                    std::chrono::milliseconds timeout) = 0;
};

/// Создаёт канал (подключается). Нужен, чтобы переподключиться после перезапуска пульта.
using ChannelFactory = std::function<Result<std::unique_ptr<Channel>>()>;

/**
 * @brief Подключается по TCP.
 * @param host IP-адрес или имя.
 * @param port Порт.
 * @param timeout Сколько ждать подключения.
 * @return Канал; ErrorCode::Io — не удалось подключиться.
 */
[[nodiscard]] Result<std::unique_ptr<Channel>> connectTcp(const std::string& host, std::uint16_t port,
                                                          std::chrono::milliseconds timeout);

} // namespace harmony::transport
