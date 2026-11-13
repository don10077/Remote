#pragma once

/**
 * @file usbnet.h
 * @brief Пакеты протокола Harmony 900 по USB-сети (TCP 169.254.1.2:3074).
 *
 * Пакет:
 * @code
 *   [0] служба (4 бита, 2 — клиент) | старшие биты команды
 *   [1] команда (0x47 — READ_REGION, 0x41 — START_UPDATE, ...)
 *   [2] 0x80 — запрос; в ответе — статус (1 — успешно)
 *   [3] число параметров
 *   далее параметры: байт длины, затем данные
 * @endcode
 * Байт длины: старшие два бита — множитель (00/10 — ×1, 01 — ×4, 11 — ×512),
 * младшие шесть — значение; 0xC2 — 1024 байта. Числа внутри параметров — big endian.
 */

#include "harmony/core/error.h"
#include "harmony/transport/channel.h"

#include <cstdint>
#include <vector>

namespace harmony::transport::usbnet {

/// Порт протокола на пульте.
inline constexpr std::uint16_t kPort = 3074;

/// Команды, которые использует программа.
enum class Command : std::uint16_t {
    ZReset = 0x1B,
    StartUpdate = 0x41,
    WriteUpdateHeader = 0x42,
    WriteUpdateData = 0x43,
    WriteUpdateDataDone = 0x44,
    GetUpdateChecksum = 0x45,
    FinishUpdate = 0x46,
    ReadRegion = 0x47,
    ReadRegionData = 0x48,
    ReadRegionDone = 0x49,
    GetSystemInfo = 0x61,
    GetGuid = 0x67,
    UpdateTime = 0x71,
    LearnIrStart = 0xA1,
    LearnIrSingle = 0xA2,
    LearnIrDone = 0xA4,
};

/// Статус ответа LEARNIR_SINGLE: сигнала за время ожидания не было.
inline constexpr std::uint8_t kStatusTimeout = 2;

/// Область памяти с пользовательской конфигурацией.
inline constexpr std::uint8_t kUserConfigRegion = 4;
/// Размер порции данных при чтении и записи области.
inline constexpr std::size_t kChunkSize = 1024;

/// Статус успешного ответа.
inline constexpr std::uint8_t kStatusOk = 1;

/// Пакет: команда, запрос/ответ и параметры.
struct Packet {
    Command command = Command::GetSystemInfo;
    bool request = true;
    std::uint8_t status = kStatusOk; ///< Только у ответа
    std::vector<std::vector<std::uint8_t>> params;
    /// Коды длины, если нужны нестандартные (пульт ждёт их в UPDATE_TIME); пусто — по длине.
    std::vector<std::uint8_t> lengthCodes;
};

/**
 * @brief Байт длины параметра.
 * @return Код длины; ErrorCode::Format — длину нельзя закодировать.
 */
[[nodiscard]] Result<std::uint8_t> encodeLength(std::size_t length);

/// Длина параметра по её коду.
[[nodiscard]] std::size_t decodeLength(std::uint8_t code);

/// Собирает пакет в байты. ErrorCode::Format — длина параметра не кодируется.
[[nodiscard]] Result<std::vector<std::uint8_t>> encode(const Packet& packet);

/**
 * @brief Читает один пакет из канала: заголовок, затем параметры по их длинам.
 * @param channel Канал.
 * @param timeout Таймаут каждой порции.
 */
[[nodiscard]] Result<Packet> receive(Channel& channel, std::chrono::milliseconds timeout);

/// Параметр из числа: @p bytes байт, big endian.
[[nodiscard]] std::vector<std::uint8_t> number(std::uint32_t value, std::size_t bytes);

/// Число из параметра (big endian).
[[nodiscard]] std::uint32_t toNumber(const std::vector<std::uint8_t>& param);

} // namespace harmony::transport::usbnet
